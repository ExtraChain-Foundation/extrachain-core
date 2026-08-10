/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "managers/token_manager.h"

#include "dfs/dfs_controller.h"
#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "chain/transaction.h"
#include "chain/dag.h"
#include "network/network_manager.h"
#include "network/wire_format.h"
#include "utils/exc_utils.h"
#include "contracts/contract_manager.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"
#include "contracts/standard_token.h"

#include <QFile>
#include <msgpack.hpp>

namespace {

    constexpr std::string_view TokenRegistryName = "TokensRegistry";
    constexpr std::string_view MaximumU128       = "340282366920938463463374607431768211455";

    std::expected<std::string, CreateTokenError> base_units(const BigNumberFloat &amount, std::uint32_t decimals) {
        auto value = amount.to_string();
        if (value.empty() || value.front() == '-' || decimals > 18) {
            return std::unexpected(CreateTokenError::InvalidAmount);
        }
        auto dot        = value.find('.');
        auto whole      = value.substr(0, dot);
        auto fractional = dot == std::string::npos ? std::string() : value.substr(dot + 1);
        if (fractional.size() > decimals && std::ranges::any_of(fractional.substr(decimals), [](char digit) {
                return digit != '0';
            })) {
            return std::unexpected(CreateTokenError::InvalidAmount);
        }
        fractional.resize(decimals, '0');
        auto result = whole + fractional;
        auto first  = result.find_first_not_of('0');
        result      = first == std::string::npos ? "0" : result.substr(first);
        if (result.size() > MaximumU128.size() || (result.size() == MaximumU128.size() && result > MaximumU128)) {
            return std::unexpected(CreateTokenError::InvalidAmount);
        }
        return result;
    }

    std::expected<std::vector<std::uint8_t>, CreateTokenError> token_init_arguments(const std::string    &name,
                                                                                    const std::string    &ticker,
                                                                                    std::uint8_t          decimals,
                                                                                    const BigNumberFloat &count) {
        auto supply = base_units(count, decimals);
        if (!supply.has_value()) {
            return std::unexpected(supply.error());
        }

        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(4);
        packer.pack(name);
        packer.pack(ticker);
        packer.pack(decimals);
        packer.pack(supply.value());
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return std::vector<std::uint8_t>(begin, begin + buffer.size());
    }

    std::vector<std::uint8_t> collection_init_arguments(const std::string &name, const std::string &symbol) {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(2);
        packer.pack(name);
        packer.pack(symbol);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::uint8_t decimal_places(const BigNumberFloat &amount) {
        auto value = amount.to_string();
        auto dot   = value.find('.');
        if (dot == std::string::npos) {
            return 0;
        }
        while (!value.empty() && value.back() == '0') {
            value.pop_back();
        }
        return static_cast<std::uint8_t>(std::min<std::size_t>(18, value.size() - dot - 1));
    }

    std::expected<std::vector<std::uint8_t>, CreateTokenError> token_migration_arguments(
        const TokenData                                       &token,
        const std::vector<std::pair<ActorId, BigNumberFloat>> &balances) {
        BigNumberFloat supply(0);
        for (const auto &[_, balance] : balances) {
            supply += balance;
        }
        auto supply_units = base_units(supply, token.decimals);
        if (!supply_units.has_value()) {
            return std::unexpected(supply_units.error());
        }

        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(5);
        packer.pack(token.name);
        packer.pack(Utils::str_to_upper(token.ticker));
        packer.pack(token.decimals);
        packer.pack(supply_units.value());
        packer.pack_array(static_cast<std::uint32_t>(balances.size()));
        for (const auto &[actor_id, balance] : balances) {
            auto units = base_units(balance, token.decimals);
            if (!units.has_value() || units.value() == "0") {
                return std::unexpected(CreateTokenError::InvalidAmount);
            }
            packer.pack_array(2);
            packer.pack(actor_id.to_string());
            packer.pack(units.value());
        }
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return std::vector<std::uint8_t>(begin, begin + buffer.size());
    }

    struct TokenInitData {
        std::string  name;
        std::string  ticker;
        std::uint8_t decimals = 0;
        std::string  supply;
    };

    std::optional<TokenInitData> decode_token_init(std::span<const std::uint8_t> arguments) {
        try {
            std::size_t offset = 0;
            auto        handle =
                msgpack::unpack(reinterpret_cast<const char *>(arguments.data()), arguments.size(), offset);
            const auto &root = handle.get();
            if (offset != arguments.size() || root.type != msgpack::type::ARRAY
                || (root.via.array.size != 4 && root.via.array.size != 5)) {
                return std::nullopt;
            }
            TokenInitData result;
            root.via.array.ptr[0].convert(result.name);
            root.via.array.ptr[1].convert(result.ticker);
            root.via.array.ptr[2].convert(result.decimals);
            root.via.array.ptr[3].convert(result.supply);
            return result;
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    std::optional<std::pair<std::string, std::string>> decode_collection_init(
        std::span<const std::uint8_t> arguments) {
        try {
            std::size_t offset = 0;
            auto        handle =
                msgpack::unpack(reinterpret_cast<const char *>(arguments.data()), arguments.size(), offset);
            const auto &root = handle.get();
            if (offset != arguments.size() || root.type != msgpack::type::ARRAY || root.via.array.size != 2) {
                return std::nullopt;
            }
            std::pair<std::string, std::string> result;
            root.via.array.ptr[0].convert(result.first);
            root.via.array.ptr[1].convert(result.second);
            return result;
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

} // namespace

TokenManager::TokenManager(ExtraChainNode *node)
    : node(node)
    , QObject(node) {
}

std::unordered_map<ActorId, std::string> TokenManager::read_tokens() {
    std::unordered_map<ActorId, std::string> map = { { TokenId(), "ExC" },
                                                     {
                                                         TokenId("468faf2f1be6504a9a26f7f027f7e43380b0d77d"),
                                                         "ROCC",
                                                     } };

    // TODO!: read from vector

    return map;
}

std::optional<std::string> TokenManager::registry_file_id() const {
    const auto network_id = node->network_id();
    if (network_id.is_zero()) {
        return std::nullopt;
    }
    auto file =
        node->dfs()->read_file_status(network_id, std::string(TokenRegistryName), Dfs::Basic::TEMPLATE_VECTOR);
    if (!file.has_value() || file.value().state != Dfs::FileState::Ready) {
        return std::nullopt;
    }
    return file.value().file_id;
}

bool TokenManager::registry_row_valid(const TokenData &token_data) const {
    if (token_data.token_id.is_zero()) {
        return token_data.owner_id == node->network_id() && token_data.name == "ExtraCoin"
               && Utils::str_to_upper(token_data.ticker) == "EXC" && token_data.smart.empty()
               && token_data.kind == "native-token" && token_data.language.empty() && token_data.decimals == 8;
    }
    if (token_data.smart != token_data.token_id.to_string() || !token_data.section_id.has_value()
        || !token_data.tx_hash.has_value() || token_data.tx_hash.value().empty()) {
        return false;
    }
    auto transaction = node->dag()->find_transaction(token_data.section_id.value(), token_data.tx_hash.value());
    if (!transaction.has_value() || transaction.value().type() != TransactionType::ContractDeploy
        || transaction.value().sender() != token_data.owner_id
        || transaction.value().receiver() != token_data.token_id || !transaction.value().meta().has_value()) {
        return false;
    }
    auto metadata = Json::deserialize<ContractTransactionData>(transaction.value().meta().value());
    if (!metadata.has_value() || metadata.value().schema != 4 || metadata.value().kind != token_data.kind
        || metadata.value().language != token_data.language || metadata.value().method != "init"
        || !ExtraChain::Contracts::is_system_token_kind(token_data.kind)) {
        return false;
    }
    auto arguments = Utils::from_base64<std::vector<std::uint8_t>>(metadata.value().arguments_base64);
    if (!arguments.has_value()) {
        return false;
    }
    if (token_data.kind == ExtraChain::Contracts::FungibleTokenKind) {
        auto init  = decode_token_init(arguments.value());
        auto count = base_units(token_data.count, token_data.decimals);
        return init.has_value() && count.has_value() && init.value().name == token_data.name
               && Utils::str_to_upper(init.value().ticker) == Utils::str_to_upper(token_data.ticker)
               && init.value().decimals == token_data.decimals && init.value().supply == count.value();
    }
    auto init = decode_collection_init(arguments.value());
    return token_data.kind == ExtraChain::Contracts::NonFungibleTokenKind && init.has_value()
           && init->first == token_data.name
           && Utils::str_to_upper(init->second) == Utils::str_to_upper(token_data.ticker) && token_data.count == 0
           && token_data.decimals == 0;
}

std::vector<TokenData> TokenManager::read_registry() const {
    std::vector<TokenData> result;
    auto                   file_id = registry_file_id();
    if (!file_id.has_value()) {
        return result;
    }
    auto rows = node->dfs()->read_vector_rows(node->network_id(), file_id.value());
    if (!rows.has_value()) {
        return result;
    }
    result.reserve(rows.value().size());
    WireFormat::Scope storage_format(WireFormat::Mode::Canonical);
    for (const auto &row : rows.value()) {
        auto token_data = Utils::from_dbrow<TokenData>(row);
        auto signer     = row.find("actor");
        auto status     = row.find("status");
        if (!token_data.has_value() || signer == row.end() || status == row.end() || status->second != "1"
            || signer->second != token_data.value().owner_id.to_string()
            || !registry_row_valid(token_data.value())) {
            continue;
        }
        result.push_back(std::move(token_data.value()));
    }
    std::ranges::sort(result, {}, [](const TokenData &value) {
        return std::pair { Utils::str_to_upper(value.ticker), value.token_id.to_string() };
    });
    return result;
}

std::vector<TokenData> TokenManager::list_tokens() const {
    auto result = read_registry();
    std::erase_if(result, [](const TokenData &value) {
        return value.kind == ExtraChain::Contracts::NonFungibleTokenKind;
    });
    auto append_if_missing = [&](TokenData token_data) {
        if (std::ranges::find(result, token_data.token_id, &TokenData::token_id) == result.end()) {
            result.push_back(std::move(token_data));
        }
    };
    append_if_missing(TokenData {
        .token_id = TokenId(),
        .owner_id = node->network_id(),
        .name     = "ExtraCoin",
        .ticker   = "EXC",
        .count    = BigNumberFloat(0),
        .color    = "#808080",
        .smart    = "",
        .kind     = "native-token",
        .language = "",
        .decimals = 8,
    });
    for (auto &legacy : legacy_tokens()) {
        append_if_missing(std::move(legacy));
    }
    std::ranges::sort(result, {}, [](const TokenData &value) {
        return std::pair { Utils::str_to_upper(value.ticker), value.token_id.to_string() };
    });
    return result;
}

std::vector<TokenData> TokenManager::list_nft_collections() const {
    auto result = read_registry();
    std::erase_if(result, [](const TokenData &value) {
        return value.kind != ExtraChain::Contracts::NonFungibleTokenKind;
    });
    return result;
}

std::optional<TokenData> TokenManager::token(const TokenId &token_id) const {
    auto tokens = read_registry();
    auto found  = std::ranges::find(tokens, token_id, &TokenData::token_id);
    if (found == tokens.end()) {
        return std::nullopt;
    }
    return *found;
}

bool TokenManager::is_contract_token(const TokenId &token_id) const {
    if (token_id.is_zero()) {
        return false;
    }
    auto token_data = token(token_id);
    if (!token_data.has_value() || token_data.value().smart != token_id.to_string()) {
        return false;
    }
    auto contract = node->contract_manager()->inspect(token_id.to_string());
    return contract.has_value() && contract.value().kind == ExtraChain::Contracts::FungibleTokenKind;
}

std::expected<std::vector<std::uint8_t>, CreateTokenError> TokenManager::transfer_arguments(
    const TokenId        &token_id,
    const ActorId        &receiver,
    const BigNumberFloat &amount) const {
    auto token_data = token(token_id);
    if (!token_data.has_value() || token_data->kind != ExtraChain::Contracts::FungibleTokenKind
        || receiver.is_zero() || token_data.value().decimals > 18) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto units = base_units(amount, token_data.value().decimals);
    if (!units.has_value() || units.value() == "0") {
        return std::unexpected(CreateTokenError::InvalidAmount);
    }
    msgpack::sbuffer buffer;
    msgpack::packer  packer(buffer);
    packer.pack_array(2);
    packer.pack(receiver.to_string());
    packer.pack(units.value());
    auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
    return std::vector<std::uint8_t>(begin, begin + buffer.size());
}

std::vector<TokenData> TokenManager::legacy_tokens() const {
    if (node->dag()->mode() != DagMode::Full) {
        return {};
    }

    const auto       current_section = node->dag()->current_section();
    std::scoped_lock cache_lock(legacy_cache_mutex_);
    if (legacy_cache_section_.has_value() && legacy_cache_section_.value() == current_section) {
        return legacy_cache_;
    }

    std::map<TokenId, TokenData>    tokens;
    std::map<TokenId, std::uint8_t> decimals;
    const TokenId                   rocc("468faf2f1be6504a9a26f7f027f7e43380b0d77d");
    const ActorId                   rocc_owner("46710a2d823c23db9fc2ac01e0f84212a8128373");
    for (SectionId section_id(0); section_id <= current_section; section_id = section_id + SectionId(1)) {
        auto section = node->dag()->read_section(section_id);
        if (!section.has_value()) {
            continue;
        }
        for (const auto &transaction : section.value().transactions) {
            if (!transaction.token().is_zero()) {
                decimals[transaction.token()] =
                    std::max(decimals[transaction.token()], decimal_places(transaction.amount()));
            }
            if (transaction.type() != TransactionType::InitContract || transaction.receiver().is_zero()
                || !transaction.meta().has_value()) {
                continue;
            }
            auto metadata = Json::deserialize<TokenDataShort>(transaction.meta().value());
            if (!metadata.has_value()) {
                continue;
            }
            tokens.insert_or_assign(transaction.receiver(),
                                    TokenData { .token_id   = transaction.receiver(),
                                                .owner_id   = transaction.sender(),
                                                .name       = metadata.value().name,
                                                .ticker     = metadata.value().ticker,
                                                .count      = transaction.amount(),
                                                .color      = metadata.value().color,
                                                .smart      = "",
                                                .kind       = "legacy-token",
                                                .language   = "",
                                                .decimals   = 0,
                                                .section_id = transaction.section(),
                                                .tx_hash    = transaction.hash() });
        }
    }
    if (decimals.contains(rocc) && !tokens.contains(rocc)) {
        tokens.emplace(rocc,
                       TokenData { .token_id = rocc,
                                   .owner_id = rocc_owner,
                                   .name     = "RaccoonCoin",
                                   .ticker   = "ROCC",
                                   .count    = BigNumberFloat(0),
                                   .color    = "#FA5448",
                                   .smart    = "",
                                   .kind     = "legacy-token",
                                   .language = "",
                                   .decimals = decimals[rocc] });
    }

    std::vector<TokenData> result;
    result.reserve(tokens.size());
    for (auto &[token_id, token_data] : tokens) {
        if (node->contract_manager()->inspect(token_id.to_string()).has_value()) {
            continue;
        }
        token_data.decimals = decimals[token_id];
        result.push_back(std::move(token_data));
    }
    legacy_cache_section_ = current_section;
    legacy_cache_         = result;
    return legacy_cache_;
}

std::expected<TokenData, CreateTokenError> TokenManager::migrate_legacy_token(
    const TokenId                           &token_id,
    ExtraChain::Contracts::ToolchainLanguage language) {
    if (!registry_file_id().has_value()) {
        eWarning("[TokenManager] Legacy token migration requires a ready TokensRegistry vector");
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto legacy = legacy_tokens();
    auto found  = std::ranges::find(legacy, token_id, &TokenData::token_id);
    if (found == legacy.end()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto owner = node->account_controller()->current_profile().get_actor(found->owner_id);
    if (!owner.has_value()) {
        return std::unexpected(CreateTokenError::InvalidOwnerId);
    }

    auto actor_ids = node->actor_index()->read_all_actors_ids();
    if (std::ranges::find(actor_ids, found->owner_id) == actor_ids.end()) {
        actor_ids.push_back(found->owner_id);
    }
    auto balances_map = node->dag()->calculate_actors_balance(actor_ids);
    std::vector<std::pair<ActorId, BigNumberFloat>> balances;
    BigNumberFloat                                  migrated_supply(0);
    for (const auto &[key, balance] : balances_map) {
        if (key.second == token_id && balance > 0) {
            balances.emplace_back(key.first, balance);
            migrated_supply += balance;
        }
    }
    if (balances.empty()) {
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    auto module = ExtraChain::Contracts::standard_token_module(ExtraChain::Contracts::FungibleTokenKind, language);
    auto arguments = token_migration_arguments(*found, balances);
    if (!module.has_value() || !arguments.has_value()) {
        return std::unexpected(module.has_value() ? arguments.error() : CreateTokenError::InvalidTx);
    }
    auto deployment =
        node->contract_manager()->prepare_deploy(token_id.to_string(),
                                                 found->owner_id.to_string(),
                                                 "fungible-token",
                                                 module.value(),
                                                 arguments.value(),
                                                 static_cast<std::uint64_t>(
                                                     node->dag()->current_section().to_int().value_or(0))
                                                     + 1);
    if (!deployment.has_value()) {
        eWarning("[TokenManager] Legacy token migration failed: {}", deployment.error().detail);
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto             &record   = deployment.value().record;
    const auto             &version  = record.versions.back();
    const auto             &revision = version.revisions.back();
    ContractTransactionData contract_data {
        .kind                = "fungible-token",
        .language            = record.language,
        .method              = "init",
        .arguments_base64    = Utils::to_base64(arguments.value()),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(deployment.value().output.effects),
        .effects_base64 =
            Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(deployment.value().output.effects)),
        .version             = version.version,
        .revision            = revision.revision,
        .checkpoint          = true,
        .checkpoint_revision = revision.revision,
    };
    Transaction transaction;
    transaction.set_sender(found->owner_id);
    transaction.set_receiver(token_id);
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractDeploy);
    transaction.set_meta(Json::serialize(contract_data));
    auto sent = node->send_contract_transaction(transaction, owner.value(), std::move(deployment.value()));
    if (!sent.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto migrated     = *found;
    migrated.count    = migrated_supply;
    migrated.smart    = token_id.to_string();
    migrated.kind     = std::string(ExtraChain::Contracts::FungibleTokenKind);
    migrated.language = std::string(ExtraChain::Contracts::toolchain_language_name(language));
    cache_creation_.insert_or_assign(sent.value().hash(), migrated);
    {
        std::scoped_lock cache_lock(legacy_cache_mutex_);
        legacy_cache_section_.reset();
        legacy_cache_.clear();
    }
    return migrated;
}

bool TokenManager::token_exists(const std::string &name, const std::string &ticker) {
    const auto normalized_name   = Utils::str_to_upper(name);
    const auto normalized_ticker = Utils::str_to_upper(ticker);
    const auto registered        = read_registry();
    if (std::ranges::any_of(registered, [&](const TokenData &token_data) {
            return Utils::str_to_upper(token_data.name) == normalized_name
                   || Utils::str_to_upper(token_data.ticker) == normalized_ticker;
        })) {
        return true;
    }
    return std::ranges::any_of(cache_creation_, [&](const auto &entry) {
        return Utils::str_to_upper(entry.second.name) == normalized_name
               || Utils::str_to_upper(entry.second.ticker) == normalized_ticker;
    });
}

bool TokenManager::name_exists(const std::string &name) {
    const auto normalized = Utils::str_to_upper(name);
    return std::ranges::any_of(read_registry(), [&](const TokenData &token_data) {
        return Utils::str_to_upper(token_data.name) == normalized;
    });
}

bool TokenManager::ticker_exists(const std::string &ticker) {
    const auto normalized = Utils::str_to_upper(ticker);
    return std::ranges::any_of(read_registry(), [&](const TokenData &token_data) {
        return Utils::str_to_upper(token_data.ticker) == normalized;
    });
}

std::expected<TokenData, CreateTokenError> TokenManager::create_token(
    const ActorId                           &owner_id,
    const std::string                       &token_name,
    const std::string                       &ticker,
    const BigNumberFloat                    &token_count,
    const std::string                       &color,
    const std::string                       &predefine_token_id,
    std::uint8_t                             decimals,
    ExtraChain::Contracts::ToolchainLanguage language) {
    if (!registry_file_id().has_value()) {
        eWarning("[TokenManager] Token registry is not ready");
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    if (!node->network()->is_active_connection_exists()) {
        eLog("[TokenManager] No connections");
        return std::unexpected(CreateTokenError::NoConnections);
    }

    if (token_count <= 0 || token_count >= ChainConst::MAX_TOKEN_COUNT || decimals > 18) {
        eLog(
            "[TokenManager] Error create token. Count: {} | name: {} | ticker: {} | rull address: {} | "
            "color: {}",
            token_count,
            token_name,
            ticker,
            owner_id,
            color);
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    eLog("[TokenManager] Create token... Count: {} | name: {} | ticker: {} | rull address: {} | color: {}",
         token_count,
         token_name,
         ticker,
         owner_id,
         color);

    if (!is_valid_token_name(token_name) || !id_valid_token_ticker(ticker)) {
        eLog("[TokenManager] Incorrect name: {} {}",
             is_valid_token_name(token_name),
             id_valid_token_ticker(ticker));
        eLog("[TokenManager] Incorrect name. Name: {} | ticker: {}", token_name, ticker);
        emit errorNameTokenExist("name");
        return std::unexpected(CreateTokenError::InvalidName);
    }

    auto upperTokenName = Utils::str_to_upper(token_name);
    auto tickerSymbol   = Utils::str_to_upper(ticker);
    if (upperTokenName == "EXTRACOIN" || tickerSymbol == "EXC" || token_exists(token_name, ticker)) {
        eLog("[TokenManager] Name or ticker exists");
        emit errorNameTokenExist("exists");
        return std::unexpected(CreateTokenError::ExistToken);
    }

    auto module = ExtraChain::Contracts::standard_token_module(ExtraChain::Contracts::FungibleTokenKind, language);
    auto arguments = token_init_arguments(token_name, tickerSymbol, decimals, token_count);
    if (!module.has_value() || !arguments.has_value()) {
        eWarning("[TokenManager] Standard token module or initialization arguments are invalid");
        return std::unexpected(module.has_value() ? arguments.error() : CreateTokenError::InvalidTx);
    }

    Actor<KeyPrivate> token_actor;
    if (predefine_token_id.empty()) {
        token_actor = node->account_controller()->create_service();
    } else {
        auto temp_actor = token_actor.fromJson(QByteArray::fromStdString(predefine_token_id));
        token_actor     = node->account_controller()->create_service({}, temp_actor);
    }
    auto owner_actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!owner_actor.has_value()) {
        eWarning("[TokenManager] Token owner is not available in the current profile: {}", owner_id);
        return std::unexpected(CreateTokenError::InvalidOwnerId);
    }

    auto token_data =
        TokenData { .token_id = token_actor.id(),
                    .owner_id = owner_id,
                    .name     = token_name,
                    .ticker   = ticker,
                    .count    = token_count,
                    .color    = color,
                    .smart    = token_actor.id().to_string(),
                    .kind     = std::string(ExtraChain::Contracts::FungibleTokenKind),
                    .language = std::string(ExtraChain::Contracts::toolchain_language_name(language)),
                    .decimals = decimals };
    auto deployment =
        node->contract_manager()->prepare_deploy(token_actor.id().to_string(),
                                                 owner_id.to_string(),
                                                 "fungible-token",
                                                 module.value(),
                                                 arguments.value(),
                                                 static_cast<std::uint64_t>(
                                                     node->dag()->current_section().to_int().value_or(0))
                                                     + 1);
    if (!deployment.has_value()) {
        eWarning("[TokenManager] Contract preparation failed: {}", deployment.error().detail);
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto &record   = deployment.value().record;
    const auto &version  = record.versions.back();
    const auto &revision = version.revisions.back();

    ContractTransactionData contract_data {
        .kind                = "fungible-token",
        .language            = record.language,
        .method              = "init",
        .arguments_base64    = Utils::to_base64(arguments.value()),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(deployment.value().output.effects),
        .effects_base64 =
            Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(deployment.value().output.effects)),
        .version             = version.version,
        .revision            = revision.revision,
        .checkpoint          = true,
        .checkpoint_revision = revision.revision,
    };

    Transaction tx;
    tx.set_sender(owner_id);
    tx.set_receiver(token_actor.id());
    tx.set_amount(BigNumberFloat(0));
    tx.set_token(TokenId());
    tx.set_type(TransactionType::ContractDeploy);
    tx.set_meta(Json::serialize(contract_data));

    auto tx_res = node->send_contract_transaction(tx, owner_actor.value().get(), std::move(deployment.value()));
    if (!tx_res.has_value()) {
        eWarning("[TokenManager] Contract deployment transaction failed: {}", tx_res.error());
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    cache_creation_.insert({ tx_res.value().hash(), token_data });
    return token_data;
}

std::expected<TokenData, CreateTokenError> TokenManager::create_nft_collection(
    const ActorId                           &owner_id,
    const std::string                       &collection_name,
    const std::string                       &symbol,
    const std::string                       &color,
    ExtraChain::Contracts::ToolchainLanguage language) {
    if (!registry_file_id().has_value()) {
        eWarning("[TokenManager] Token registry is not ready");
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    if (!node->network()->is_active_connection_exists()) {
        return std::unexpected(CreateTokenError::NoConnections);
    }
    if (!is_valid_token_name(collection_name) || !id_valid_token_ticker(symbol)) {
        return std::unexpected(CreateTokenError::InvalidName);
    }
    if (Utils::str_to_upper(collection_name) == "EXTRACOIN" || Utils::str_to_upper(symbol) == "EXC"
        || token_exists(collection_name, symbol)) {
        return std::unexpected(CreateTokenError::ExistToken);
    }
    auto module =
        ExtraChain::Contracts::standard_token_module(ExtraChain::Contracts::NonFungibleTokenKind, language);
    if (!module.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto owner_actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!owner_actor.has_value()) {
        return std::unexpected(CreateTokenError::InvalidOwnerId);
    }
    auto collection_actor = node->account_controller()->create_service();
    auto arguments        = collection_init_arguments(collection_name, Utils::str_to_upper(symbol));
    auto deployment =
        node->contract_manager()->prepare_deploy(collection_actor.id().to_string(),
                                                 owner_id.to_string(),
                                                 std::string(ExtraChain::Contracts::NonFungibleTokenKind),
                                                 *module,
                                                 arguments,
                                                 static_cast<std::uint64_t>(
                                                     node->dag()->current_section().to_int().value_or(0))
                                                     + 1);
    if (!deployment.has_value()) {
        eWarning("[TokenManager] NFT collection preparation failed: {}", deployment.error().detail);
        return std::unexpected(CreateTokenError::InvalidTx);
    }

    const auto             &record   = deployment->record;
    const auto             &version  = record.versions.back();
    const auto             &revision = version.revisions.back();
    ContractTransactionData contract_data {
        .kind                = record.kind,
        .language            = record.language,
        .method              = "init",
        .arguments_base64    = Utils::to_base64(arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(deployment->output.effects),
        .effects_base64 =
            Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(deployment->output.effects)),
        .version             = version.version,
        .revision            = revision.revision,
        .checkpoint          = true,
        .checkpoint_revision = revision.revision,
    };
    Transaction transaction;
    transaction.set_sender(owner_id);
    transaction.set_receiver(collection_actor.id());
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractDeploy);
    transaction.set_meta(Json::serialize(contract_data));
    auto sent = node->send_contract_transaction(transaction, *owner_actor, std::move(*deployment));
    if (!sent.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }

    TokenData collection {
        .token_id = collection_actor.id(),
        .owner_id = owner_id,
        .name     = collection_name,
        .ticker   = Utils::str_to_upper(symbol),
        .count    = BigNumberFloat(0),
        .color    = color,
        .smart    = collection_actor.id().to_string(),
        .kind     = std::string(ExtraChain::Contracts::NonFungibleTokenKind),
        .language = std::string(ExtraChain::Contracts::toolchain_language_name(language)),
        .decimals = 0,
    };
    cache_creation_.insert_or_assign(sent->hash(), collection);
    return collection;
}

void TokenManager::final_token_creation(const Transaction &transaction) {
    const auto transaction_hash = transaction.hash();
    const auto cache_entry      = cache_creation_.find(transaction_hash);
    if (cache_entry == cache_creation_.end()) {
        return;
    }

    auto &token_data      = cache_entry->second;
    token_data.section_id = transaction.section();
    token_data.tx_hash    = transaction_hash;
    auto       json       = Json::serialize(token_data);
    const auto owner_id   = token_data.owner_id;
    const auto token_id   = token_data.token_id;

    auto res = node->dfs()->store_data_as_file(transaction.receiver(),
                                               transaction.sender(),
                                               ByteArray(json).toBytes(),
                                               Dfs::Basic::TEMPLATE_CONTRACTS,
                                               "token-description.json",
                                               Dfs::DataSecurity::Public);

    if (!res.has_value()) {
        eLog("[TokenManager] Error save file to dfs");
        return;
    }

    auto              registry_id = registry_file_id();
    WireFormat::Scope storage_format(WireFormat::Mode::Canonical);
    if (!registry_id.has_value()
        || !node->dfs()->add_vector_row(node->network_id(),
                                        registry_id.value(),
                                        token_data,
                                        token_data.owner_id)) {
        eWarning("[TokenManager] Token contract is stored, but its registry row was not published");
        return;
    }

    emit added(owner_id, token_id);
    cache_creation_.erase(transaction_hash);
}

bool TokenManager::is_valid_token_name(const std::string &name) {
    if (name.size() < 3 || name.size() > 20) {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '-' || c == '_';
    });
}

bool TokenManager::id_valid_token_ticker(const std::string &ticker) {
    if (ticker.size() < 2 || ticker.size() > 5) {
        return false;
    }

    if (!std::isalpha(ticker[0])) {
        return false;
    }

    return std::all_of(ticker.begin(), ticker.end(), [](unsigned char c) {
        return std::isupper(c) || std::isdigit(c);
    });
}
