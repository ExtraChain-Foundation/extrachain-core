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

#include "dfs/dfs_service.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "chain/transaction.h"
#include "chain/dag.h"
#include "network/network_service.h"
#include "network/wire_format.h"
#include "utils/exc_utils.h"
#include "contracts/contract_manager.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"
#include "contracts/contract_hash.h"
#include "contracts/standard_token.h"
#include "network/responder.h"

#include <msgpack.hpp>

namespace {

    constexpr std::string_view TokenRegistryName = "TokensRegistry";
    constexpr std::string_view MaximumU128       = "340282366920938463463374607431768211455";
    constexpr std::size_t      MigrationValidatorCount = 3;
    constexpr std::uint64_t    MigrationLeadSections   = 2 * CONTROL_INTERVAL_MOD;
    constexpr std::uint64_t    MigrationWindowSections = 10 * CONTROL_INTERVAL_MOD;
    constexpr std::uint64_t    ReadinessRetryMs        = 5'000;

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
        packer.pack_array(5);
        packer.pack(name);
        packer.pack(ticker);
        packer.pack(decimals);
        packer.pack(supply.value());
        packer.pack_array(0);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return std::vector<std::uint8_t>(begin, begin + buffer.size());
    }

    std::vector<std::uint8_t> migration_target_arguments(const TokenData &token) {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(5);
        packer.pack(token.name);
        packer.pack(Utils::str_to_upper(token.ticker));
        packer.pack(token.decimals);
        packer.pack(std::string("0"));
        packer.pack_array(0);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    std::vector<std::uint8_t> no_arguments() {
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(0);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return { begin, begin + buffer.size() };
    }

    bool decode_boolean(std::span<const std::uint8_t> value) {
        try {
            std::size_t offset = 0;
            auto        data = msgpack::unpack(reinterpret_cast<const char *>(value.data()), value.size(), offset);
            return offset == value.size() && data.get().as<bool>();
        } catch (const std::exception &) {
            return false;
        }
    }

    std::optional<std::string> decode_string(std::span<const std::uint8_t> value) {
        try {
            std::size_t offset = 0;
            auto        data = msgpack::unpack(reinterpret_cast<const char *>(value.data()), value.size(), offset);
            if (offset != value.size()) {
                return std::nullopt;
            }
            return data.get().as<std::string>();
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    std::optional<std::uint64_t> decode_unsigned(std::span<const std::uint8_t> value) {
        try {
            std::size_t offset = 0;
            auto        data = msgpack::unpack(reinterpret_cast<const char *>(value.data()), value.size(), offset);
            if (offset != value.size()) {
                return std::nullopt;
            }
            return data.get().as<std::uint64_t>();
        } catch (const std::exception &) {
            return std::nullopt;
        }
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

    std::expected<std::vector<std::uint8_t>, CreateTokenError> legacy_import_arguments(
        const TokenId                                         &token_id,
        const TokenData                                       &token,
        const std::vector<std::pair<ActorId, BigNumberFloat>> &balances) {
        BigNumberFloat supply(0);
        for (const auto &[_, balance] : balances) {
            supply += balance;
        }
        auto supply_units = base_units(supply, token.decimals);
        if (!supply_units.has_value() || supply_units.value() == "0") {
            return std::unexpected(CreateTokenError::InvalidAmount);
        }
        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(3);
        packer.pack(token_id.to_string());
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

    SectionId migration_cutoff(const SectionId &plan_section) {
        const auto minimum = plan_section + SectionId(MigrationLeadSections);
        const auto value   = minimum.to_int().value_or(0);
        const auto aligned = ((value + CONTROL_INTERVAL_MOD - 1) / CONTROL_INTERVAL_MOD) * CONTROL_INTERVAL_MOD;
        return SectionId(aligned);
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

TokenManager::TokenManager(ExtraChain::Core::ExtraChainNode *node)
    : node(node) {
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

bool TokenManager::registry_row_valid(TokenData &token_data) const {
    if (token_data.token_id.is_zero()) {
        token_data.kind = "native-token";
        token_data.language.clear();
        return token_data.owner_id == node->network_id() && token_data.name == "ExtraCoin"
               && Utils::str_to_upper(token_data.ticker) == "EXC" && token_data.smart.empty()
               && token_data.decimals == 8;
    }
    if (token_data.smart.empty() || !token_data.section_id.has_value() || !token_data.tx_hash.has_value()
        || token_data.tx_hash.value().empty()) {
        return false;
    }
    auto transaction = node->dag()->find_transaction(token_data.section_id.value(), token_data.tx_hash.value());
    if (!transaction.has_value() || transaction->sender() != token_data.owner_id
        || !transaction->meta().has_value()) {
        return false;
    }
    auto metadata = Json::deserialize<ContractTransactionData>(*transaction->meta());
    if (!metadata.has_value() || metadata->schema != 4
        || !ExtraChain::Contracts::is_system_token_kind(metadata->kind)) {
        return false;
    }
    token_data.kind     = metadata->kind;
    token_data.language = metadata->language;
    auto arguments      = Utils::from_base64<std::vector<std::uint8_t>>(metadata->arguments_base64);
    if (!arguments.has_value()) {
        return false;
    }

    if (token_data.smart != token_data.token_id.to_string()) {
        const auto target_id = ActorId::create(token_data.smart);
        if (!target_id.has_value() || transaction->type() != TransactionType::ContractCall
            || transaction->receiver() != *target_id || metadata->method != "import_legacy"
            || token_data.kind != ExtraChain::Contracts::FungibleTokenKind
            || !metadata->legacy_migration.has_value()) {
            return false;
        }
        const auto &manifest = *metadata->legacy_migration;
        const auto  plan     = migration_plan_by_hash(manifest.plan_transaction_hash);
        const auto  contract = node->contract_manager()->inspect(token_data.smart);
        if (!plan.has_value() || !contract.has_value() || contract->owner_id != token_data.owner_id.to_string()
            || contract->kind != ExtraChain::Contracts::FungibleTokenKind
            || manifest.legacy_token_id != token_data.token_id.to_string()
            || manifest.target_contract_id != token_data.smart || plan->plan.legacy_token_id != token_data.token_id
            || plan->plan.target_contract_id != *target_id || plan->plan.owner_id != token_data.owner_id
            || manifest.supply != plan->plan.expected_supply) {
            return false;
        }
        const auto block = static_cast<std::uint64_t>(node->dag()->current_section().to_int().value_or(0));
        const auto query = [&](std::string_view method) {
            return node->contract_manager()->query(token_data.smart,
                                                   token_data.owner_id.to_string(),
                                                   method,
                                                   no_arguments(),
                                                   block);
        };
        const auto source         = query("legacy_source");
        const auto name           = query("token_name");
        const auto symbol         = query("token_symbol");
        const auto decimals       = query("token_decimals");
        const auto registry_units = base_units(token_data.count, token_data.decimals);
        const auto linked_source  = source.has_value() ? decode_string(source->data) : std::nullopt;
        return linked_source.has_value() && *linked_source == token_data.token_id.to_string() && name.has_value()
               && decode_string(name->data) == token_data.name && symbol.has_value()
               && decode_string(symbol->data) == Utils::str_to_upper(token_data.ticker) && decimals.has_value()
               && decode_unsigned(decimals->data) == token_data.decimals && registry_units.has_value()
               && *registry_units == manifest.supply;
    }

    if (transaction->type() != TransactionType::ContractDeploy || transaction->receiver() != token_data.token_id
        || metadata->method != "init" || metadata->legacy_migration.has_value()) {
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
        auto storage_row = row;
        storage_row.insert_or_assign("kind", "");
        storage_row.insert_or_assign("language", "");
        auto token_data = Utils::from_dbrow<TokenData>(storage_row);
        auto signer     = row.find("actor");
        auto status     = row.find("status");
        if (!token_data.has_value() || signer == row.end() || status == row.end() || status->second != "1") {
            continue;
        }
        if (signer->second != token_data->owner_id.to_string() || !registry_row_valid(*token_data)) {
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
    if (!token_data.has_value()) {
        return false;
    }
    auto contract = node->contract_manager()->inspect(token_data->smart);
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

    const auto current_section = node->dag()->current_section();
    {
        std::scoped_lock cache_lock(legacy_cache_mutex_);
        if (legacy_cache_section_.has_value() && legacy_cache_section_.value() == current_section) {
            return legacy_cache_;
        }
    }

    const auto                      registered = read_registry();
    std::set<TokenId>               registered_token_ids;
    std::map<TokenId, TokenData>    tokens;
    std::map<TokenId, std::uint8_t> decimals;
    for (const auto &token_data : registered) {
        if (!token_data.token_id.is_zero()) {
            registered_token_ids.insert(token_data.token_id);
        }
    }
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
        if (registered_token_ids.contains(token_id)
            || node->contract_manager()->inspect(token_id.to_string()).has_value()) {
            continue;
        }
        token_data.decimals = decimals[token_id];
        result.push_back(std::move(token_data));
    }
    {
        std::scoped_lock cache_lock(legacy_cache_mutex_);
        legacy_cache_section_ = current_section;
        legacy_cache_         = result;
    }
    return result;
}

std::expected<TokenData, CreateTokenError> TokenManager::submit_legacy_import(const TokenId &token_id) {
    const auto record = migration_plan(token_id);
    if (!record.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto snapshot = migration_snapshot(record.value());
    if (!snapshot.has_value()) {
        return std::unexpected(snapshot.error());
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
    auto arguments = legacy_import_arguments(token_id, *found, snapshot.value().balances);
    if (!arguments.has_value()) {
        return std::unexpected(arguments.error());
    }
    LegacyTokenMigrationData manifest {
        .plan_transaction_hash   = record.value().transaction_hash,
        .legacy_token_id         = token_id.to_string(),
        .target_contract_id      = record.value().plan.target_contract_id.to_string(),
        .source_transaction_hash = record.value().plan.source_transaction_hash,
        .source_section      = static_cast<std::uint64_t>(record.value().plan.source_section.to_int().value_or(0)),
        .cutoff_section      = static_cast<std::uint64_t>(record.value().plan.cutoff_section.to_int().value_or(0)),
        .cutoff_section_hash = snapshot.value().readiness.cutoff_section_hash,
        .cutoff_control_hash = snapshot.value().readiness.cutoff_control_hash,
        .balances_hash       = snapshot.value().readiness.balances_hash,
        .supply              = snapshot.value().readiness.supply,
    };
    auto sent = node->submit_legacy_token_import(owner.value(),
                                                 record.value().plan.target_contract_id,
                                                 arguments.value(),
                                                 manifest);
    if (!sent.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto migrated  = *found;
    migrated.count = BigNumberFloat(0);
    for (const auto &[_, balance] : snapshot->balances) {
        migrated.count += balance;
    }
    migrated.smart    = record.value().plan.target_contract_id.to_string();
    migrated.kind     = std::string(ExtraChain::Contracts::FungibleTokenKind);
    migrated.language = record.value().plan.language;
    track_token_creation(sent.value(), migrated);
    return migrated;
}

std::expected<Transaction, CreateTokenError> TokenManager::publish_legacy_token_target(
    const TokenId                           &token_id,
    ExtraChain::Contracts::ToolchainLanguage language) {
    if (!registry_file_id().has_value() || is_contract_token(token_id)) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    if (node->dag()->status() != DagStatus::Ready || !node->network()->is_active_connection_exists()) {
        return std::unexpected(CreateTokenError::NoConnections);
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
    auto module = ExtraChain::Contracts::standard_token_module(ExtraChain::Contracts::FungibleTokenKind, language);
    if (!module.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto target    = node->account_controller()->create_service();
    auto arguments = migration_target_arguments(*found);
    auto block     = static_cast<std::uint64_t>(node->dag()->current_section().to_int().value_or(0)) + 1;
    auto deployment =
        node->contract_manager()->prepare_deploy(target.id().to_string(),
                                                 found->owner_id.to_string(),
                                                 std::string(ExtraChain::Contracts::FungibleTokenKind),
                                                 module.value(),
                                                 arguments,
                                                 block);
    if (!deployment.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto             &record   = deployment.value().record;
    const auto             &version  = record.versions.back();
    const auto             &revision = version.revisions.back();
    ContractTransactionData metadata {
        .kind                = record.kind,
        .language            = record.language,
        .method              = "init",
        .arguments_base64    = Utils::to_base64(arguments),
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
    transaction.set_receiver(target.id());
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractDeploy);
    transaction.set_meta(Json::serialize(metadata));
    auto sent = node->send_contract_transaction(transaction, owner.value(), std::move(deployment.value()));
    if (!sent.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    return sent.value();
}

std::expected<Transaction, CreateTokenError> TokenManager::link_legacy_token(const TokenId &token_id,
                                                                             const ActorId &target_contract_id) {
    const auto full_peers    = node->network()->active_full_peer_identifiers();
    const auto capable_peers = node->network()->active_full_peers_with_capability(TOKEN_MIGRATION_CAPABILITY);
    if (node->runtime_profile() != RuntimeProfile::FullNode || node->dag()->status() != DagStatus::Ready
        || capable_peers.size() + 1 < MigrationValidatorCount || capable_peers.size() != full_peers.size()) {
        return std::unexpected(CreateTokenError::NoConnections);
    }
    if (is_contract_token(token_id)) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto legacy = legacy_tokens();
    auto found  = std::ranges::find(legacy, token_id, &TokenData::token_id);
    if (found == legacy.end() || !migration_target_ready(target_contract_id, *found)) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto owner = node->account_controller()->current_profile().get_actor(found->owner_id);
    if (!owner.has_value()) {
        return std::unexpected(CreateTokenError::InvalidOwnerId);
    }
    auto contract = node->contract_manager()->inspect(target_contract_id.to_string());
    if (!contract.has_value() || contract.value().versions.empty()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto           actor_ids = node->actor_index()->read_all_actors_ids();
    auto           balances  = node->dag()->calculate_actors_balance(actor_ids);
    BigNumberFloat supply(0);
    for (const auto &[key, balance] : balances) {
        if (key.second == token_id && balance > 0) {
            supply += balance;
        }
    }
    auto supply_units = base_units(supply, found->decimals);
    if (!supply_units.has_value() || supply_units.value() == "0") {
        return std::unexpected(CreateTokenError::InvalidAmount);
    }
    const auto               plan_section = node->dag()->current_section() + SectionId(1);
    const auto               cutoff       = migration_cutoff(plan_section);
    const auto              &version      = contract.value().versions.at(contract.value().active_version - 1);
    LegacyTokenMigrationPlan plan {
        .legacy_token_id         = token_id,
        .target_contract_id      = target_contract_id,
        .owner_id                = found->owner_id,
        .source_section          = found->section_id.value_or(SectionId(0)),
        .source_transaction_hash = found->tx_hash.value_or(std::string()),
        .language                = contract.value().language,
        .module_hash             = version.module_hash,
        .expected_supply         = supply_units.value(),
        .cutoff_section          = cutoff,
        .expires_section         = cutoff + SectionId(MigrationWindowSections),
    };
    Transaction transaction;
    transaction.set_sender(found->owner_id);
    transaction.set_receiver(target_contract_id);
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(token_id);
    transaction.set_type(TransactionType::TokenMigration);
    transaction.set_meta(Json::serialize(plan));
    auto sent = node->dag()->send_transaction(transaction, owner.value());
    if (!sent.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    publish_migration_status(LegacyTokenMigrationStatus { .legacy_token_id    = token_id,
                                                          .target_contract_id = target_contract_id,
                                                          .state = LegacyTokenMigrationState::Scheduled,
                                                          .plan_transaction_hash = sent.value().hash() });
    return sent.value();
}

std::vector<TokenManager::MigrationPlanRecord> TokenManager::migration_plans() const {
    const auto                       current = node->dag()->current_section();
    SectionId                        first(0);
    std::vector<MigrationPlanRecord> result;
    {
        std::scoped_lock lock(migration_mutex_);
        if (migration_plan_cache_section_.has_value() && *migration_plan_cache_section_ == current) {
            return migration_plan_cache_;
        }
        if (migration_plan_cache_section_.has_value() && *migration_plan_cache_section_ < current) {
            first  = *migration_plan_cache_section_ + SectionId(1);
            result = migration_plan_cache_;
        }
    }

    for (SectionId section_id = first; section_id <= current; section_id += SectionId(1)) {
        const auto section = node->dag()->read_section(section_id);
        if (!section.has_value()) {
            continue;
        }
        for (const auto &transaction : section->transactions) {
            if (transaction.type() != TransactionType::TokenMigration || !transaction.meta().has_value()) {
                continue;
            }
            const auto plan = Json::deserialize<LegacyTokenMigrationPlan>(*transaction.meta());
            if (!plan.has_value()) {
                continue;
            }
            result.push_back(MigrationPlanRecord {
                .plan             = *plan,
                .section          = transaction.section(),
                .transaction_hash = transaction.hash(),
            });
        }
    }
    std::ranges::sort(result, {}, [](const MigrationPlanRecord &record) {
        return std::pair { record.section, record.transaction_hash };
    });
    {
        std::scoped_lock lock(migration_mutex_);
        migration_plan_cache_section_ = current;
        migration_plan_cache_         = result;
    }
    return result;
}

std::optional<TokenManager::MigrationPlanRecord> TokenManager::migration_plan(const TokenId &token_id) const {
    const auto plans = migration_plans();
    for (auto iterator = plans.rbegin(); iterator != plans.rend(); ++iterator) {
        if (iterator->plan.legacy_token_id == token_id) {
            return *iterator;
        }
    }
    return std::nullopt;
}

std::optional<TokenManager::MigrationPlanRecord> TokenManager::migration_plan_by_hash(
    std::string_view transaction_hash) const {
    const auto plans = migration_plans();
    const auto found = std::ranges::find(plans, transaction_hash, &MigrationPlanRecord::transaction_hash);
    return found == plans.end() ? std::nullopt : std::optional<MigrationPlanRecord>(*found);
}

bool TokenManager::migration_target_ready(const ActorId &target_contract_id, const TokenData &legacy_token) const {
    const auto contract = node->contract_manager()->inspect(target_contract_id.to_string());
    if (!contract.has_value() || contract->owner_id != legacy_token.owner_id.to_string()
        || contract->kind != ExtraChain::Contracts::FungibleTokenKind || contract->versions.empty()
        || contract->active_version == 0 || contract->active_version > contract->versions.size()) {
        return false;
    }
    const auto &version = contract->versions.at(contract->active_version - 1);
    if (!ExtraChain::Contracts::is_standard_token_module(contract->kind, version.module_hash)) {
        return false;
    }
    if (std::ranges::any_of(read_registry(), [&](const TokenData &token_data) {
            return token_data.smart == target_contract_id.to_string();
        })) {
        return false;
    }
    const auto query = [&](std::string_view method) {
        return node->contract_manager()->query(target_contract_id.to_string(),
                                               legacy_token.owner_id.to_string(),
                                               method,
                                               no_arguments(),
                                               static_cast<std::uint64_t>(
                                                   node->dag()->current_section().to_int().value_or(0)));
    };
    const auto ready    = query("migration_ready");
    const auto name     = query("token_name");
    const auto symbol   = query("token_symbol");
    const auto decimals = query("token_decimals");
    return ready.has_value() && decode_boolean(ready->data) && name.has_value()
           && decode_string(name->data) == legacy_token.name && symbol.has_value()
           && decode_string(symbol->data) == Utils::str_to_upper(legacy_token.ticker) && decimals.has_value()
           && decode_unsigned(decimals->data) == legacy_token.decimals;
}

TransactionProveError TokenManager::validate_migration_plan(const Transaction &transaction) const {
    if (transaction.type() != TransactionType::TokenMigration || !transaction.meta().has_value()) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    const auto plan = Json::deserialize<LegacyTokenMigrationPlan>(*transaction.meta());
    if (!plan.has_value() || plan->schema != 1 || plan->legacy_token_id != transaction.token()
        || plan->target_contract_id != transaction.receiver() || plan->owner_id != transaction.sender()
        || plan->owner_id.is_zero() || plan->legacy_token_id.is_zero() || plan->target_contract_id.is_zero()
        || plan->expected_supply.empty() || plan->module_hash.size() != 64
        || plan->cutoff_section % CONTROL_INTERVAL_MOD != 0
        || plan->cutoff_section < transaction.section() + SectionId(MigrationLeadSections)
        || plan->expires_section != plan->cutoff_section + SectionId(MigrationWindowSections)) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    if (is_contract_token(plan->legacy_token_id)) {
        return TransactionProveError::TokenMigrationInvalid;
    }

    auto       legacy = legacy_tokens();
    const auto token  = std::ranges::find(legacy, plan->legacy_token_id, &TokenData::token_id);
    if (token == legacy.end() || token->owner_id != plan->owner_id
        || token->section_id.value_or(SectionId(0)) != plan->source_section
        || token->tx_hash.value_or(std::string()) != plan->source_transaction_hash
        || !migration_target_ready(plan->target_contract_id, *token)) {
        return TransactionProveError::TokenMigrationInvalid;
    }

    const auto contract = node->contract_manager()->inspect(plan->target_contract_id.to_string());
    if (!contract.has_value() || contract->language != plan->language || contract->versions.empty()) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    const auto &version = contract->versions.at(contract->active_version - 1);
    if (version.module_hash != plan->module_hash) {
        return TransactionProveError::TokenMigrationInvalid;
    }

    const auto prior_plans = migration_plans();
    if (std::ranges::any_of(prior_plans, [&](const MigrationPlanRecord &prior) {
            return prior.section < transaction.section() && prior.plan.expires_section >= transaction.section()
                   && (prior.plan.legacy_token_id == plan->legacy_token_id
                       || prior.plan.target_contract_id == plan->target_contract_id);
        })) {
        return TransactionProveError::TokenMigrationInvalid;
    }

    const auto     actors   = node->actor_index()->read_all_actors_ids();
    const auto     balances = node->dag()->calculate_actors_balance(actors, transaction.section() - SectionId(1));
    BigNumberFloat supply(0);
    for (const auto &[key, balance] : balances) {
        if (key.second == plan->legacy_token_id && balance > 0) {
            supply += balance;
        }
    }
    const auto supply_units = base_units(supply, token->decimals);
    return supply_units.has_value() && *supply_units == plan->expected_supply
               ? TransactionProveError::NoError
               : TransactionProveError::TokenMigrationInvalid;
}

std::string TokenManager::canonical_balances_hash(const TokenId   &token_id,
                                                  const SectionId &cutoff_section,
                                                  std::string_view cutoff_section_hash,
                                                  std::string_view cutoff_control_hash,
                                                  const std::vector<std::pair<ActorId, BigNumberFloat>> &balances,
                                                  std::uint32_t decimals) {
    std::string canonical = token_id.to_string() + "\n" + cutoff_section.to_string() + "\n"
                            + std::string(cutoff_section_hash) + "\n" + std::string(cutoff_control_hash) + "\n";
    for (const auto &[actor_id, balance] : balances) {
        const auto units = base_units(balance, decimals);
        if (!units.has_value()) {
            return {};
        }
        canonical += actor_id.to_string() + ":" + *units + "\n";
    }
    return ExtraChain::Contracts::content_hash(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t *>(canonical.data()), canonical.size()));
}

std::expected<TokenManager::MigrationSnapshot, CreateTokenError> TokenManager::migration_snapshot(
    const MigrationPlanRecord &record) const {
    const auto current = node->dag()->current_section();
    if (current < record.plan.cutoff_section + SectionId(CACHE_LAG_SECTIONS)
        || current > record.plan.expires_section) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto cutoff  = node->dag()->read_section(record.plan.cutoff_section);
    const auto control = node->dag()->read_control(record.plan.cutoff_section);
    if (!cutoff.has_value() || !control.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    auto       legacy = legacy_tokens();
    const auto token  = std::ranges::find(legacy, record.plan.legacy_token_id, &TokenData::token_id);
    if (token == legacy.end()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto actors = node->actor_index()->read_all_actors_ids();
    const auto values = node->dag()->calculate_actors_balance(actors, record.plan.cutoff_section);
    std::vector<std::pair<ActorId, BigNumberFloat>> balances;
    BigNumberFloat                                  supply(0);
    for (const auto &[key, balance] : values) {
        if (key.second == record.plan.legacy_token_id && balance > 0) {
            balances.emplace_back(key.first, balance);
            supply += balance;
        }
    }
    std::ranges::sort(balances, {}, [](const auto &entry) {
        return entry.first.to_string();
    });
    const auto supply_units  = base_units(supply, token->decimals);
    const auto cutoff_hash   = cutoff->calculate_hash();
    const auto balances_hash = canonical_balances_hash(record.plan.legacy_token_id,
                                                       record.plan.cutoff_section,
                                                       cutoff_hash,
                                                       control->control,
                                                       balances,
                                                       token->decimals);
    if (!supply_units.has_value() || *supply_units != record.plan.expected_supply || balances.empty()
        || balances_hash.empty()) {
        return std::unexpected(CreateTokenError::InvalidAmount);
    }
    return MigrationSnapshot {
        .readiness =
            TokenMigrationReadinessResponse {
                .plan_transaction_hash = record.transaction_hash,
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .cutoff_section        = record.plan.cutoff_section,
                .cutoff_section_hash   = cutoff_hash,
                .cutoff_control_hash   = control->control,
                .balances_hash         = balances_hash,
                .supply                = *supply_units,
                .module_hash           = record.plan.module_hash,
                .ready                 = true,
            },
        .balances = std::move(balances),
    };
}

TransactionProveError TokenManager::validate_legacy_import(const Transaction             &transaction,
                                                           const ContractTransactionData &metadata,
                                                           std::span<const std::uint8_t>  arguments) const {
    if (!metadata.legacy_migration.has_value()) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    const auto &manifest = *metadata.legacy_migration;
    const auto  record   = migration_plan_by_hash(manifest.plan_transaction_hash);
    if (!record.has_value() || manifest.schema != 1 || transaction.sender() != record->plan.owner_id
        || transaction.receiver() != record->plan.target_contract_id
        || manifest.legacy_token_id != record->plan.legacy_token_id.to_string()
        || manifest.target_contract_id != record->plan.target_contract_id.to_string()
        || manifest.source_transaction_hash != record->plan.source_transaction_hash
        || manifest.source_section != static_cast<std::uint64_t>(record->plan.source_section.to_int().value_or(0))
        || manifest.cutoff_section != static_cast<std::uint64_t>(record->plan.cutoff_section.to_int().value_or(0))
        || metadata.method != "import_legacy" || metadata.module_hash != record->plan.module_hash) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    const auto snapshot = migration_snapshot(*record);
    if (!snapshot.has_value() || manifest.cutoff_section_hash != snapshot->readiness.cutoff_section_hash
        || manifest.cutoff_control_hash != snapshot->readiness.cutoff_control_hash
        || manifest.balances_hash != snapshot->readiness.balances_hash
        || manifest.supply != snapshot->readiness.supply) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    auto       legacy = legacy_tokens();
    const auto token  = std::ranges::find(legacy, record->plan.legacy_token_id, &TokenData::token_id);
    if (token == legacy.end()) {
        return TransactionProveError::TokenMigrationInvalid;
    }
    const auto expected = legacy_import_arguments(record->plan.legacy_token_id, *token, snapshot->balances);
    return expected.has_value() && std::ranges::equal(*expected, arguments)
               ? TransactionProveError::NoError
               : TransactionProveError::TokenMigrationInvalid;
}

bool TokenManager::legacy_transaction_allowed(const Transaction &transaction) const {
    if (transaction.type() != TransactionType::Regular && transaction.type() != TransactionType::Burn
        && transaction.type() != TransactionType::Minting && transaction.type() != TransactionType::Conversion) {
        return true;
    }
    std::vector<TokenId> affected { transaction.token() };
    if (transaction.type() == TransactionType::Conversion && transaction.meta().has_value()) {
        const auto source = TokenId::create(*transaction.meta());
        if (source.has_value()) {
            affected.push_back(*source);
        }
    }
    if (transaction.type() != TransactionType::Regular
        && std::ranges::any_of(affected, [this](const TokenId &token_id) {
               return is_contract_token(token_id);
           })) {
        return false;
    }
    const auto plans = migration_plans();
    return std::ranges::none_of(plans, [&](const MigrationPlanRecord &record) {
        if (std::ranges::find(affected, record.plan.legacy_token_id) == affected.end()
            || transaction.section() <= record.section || transaction.section() > record.plan.expires_section) {
            return false;
        }
        return transaction.type() != TransactionType::Regular
               || transaction.section() > record.plan.cutoff_section;
    });
}

void TokenManager::handle_migration_readiness_request(const TokenMigrationReadinessRequest &request,
                                                      const Responder                      &responder) {
    TokenMigrationReadinessResponse response { .plan_transaction_hash = request.plan_transaction_hash };
    const auto                      record = migration_plan_by_hash(request.plan_transaction_hash);
    if (record.has_value()) {
        const auto snapshot = migration_snapshot(*record);
        if (snapshot.has_value()) {
            response = snapshot->readiness;
        }
    }
    responder.send_response(response,
                            MessageType::TokenMigrationReadiness,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void TokenManager::handle_migration_readiness_response(const TokenMigrationReadinessResponse &response,
                                                       const Responder                       &responder) {
    if (responder.identifiers().empty()) {
        return;
    }
    const auto peers = node->network()->active_full_peers_with_capability(TOKEN_MIGRATION_CAPABILITY);
    const auto peer  = *responder.identifiers().begin();
    if (std::ranges::find(peers, peer) == peers.end()) {
        return;
    }
    std::scoped_lock lock(migration_mutex_);
    migration_readiness_[response.plan_transaction_hash].insert_or_assign(peer, response);
}

void TokenManager::publish_migration_status(LegacyTokenMigrationStatus status) {
    {
        std::scoped_lock lock(migration_mutex_);
        const auto       current = migration_statuses_.find(status.legacy_token_id);
        if (current != migration_statuses_.end() && current->second.target_contract_id == status.target_contract_id
            && current->second.state == status.state
            && current->second.plan_transaction_hash == status.plan_transaction_hash
            && current->second.detail == status.detail) {
            return;
        }
        migration_statuses_.insert_or_assign(status.legacy_token_id, status);
    }
    migration_status_event_.publish(status);
}

std::vector<LegacyTokenMigrationStatus> TokenManager::migration_statuses() const {
    std::scoped_lock                        lock(migration_mutex_);
    std::vector<LegacyTokenMigrationStatus> result;
    result.reserve(migration_statuses_.size());
    for (const auto &[_, status] : migration_statuses_) {
        result.push_back(status);
    }
    return result;
}

void TokenManager::process_legacy_migrations() {
    if (node->runtime_profile() != RuntimeProfile::FullNode || node->dag()->status() != DagStatus::Ready) {
        return;
    }
    const auto current       = node->dag()->current_section();
    const auto full_peers    = node->network()->active_full_peer_identifiers();
    const auto capable_peers = node->network()->active_full_peers_with_capability(TOKEN_MIGRATION_CAPABILITY);
    if (capable_peers.size() + 1 < MigrationValidatorCount || capable_peers.size() != full_peers.size()) {
        for (const auto &record : migration_plans()) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::WaitingForPeers,
                .plan_transaction_hash = record.transaction_hash,
                .detail                = "Every active full validator must support the migration protocol.",
            });
        }
        return;
    }
    for (const auto &record : migration_plans()) {
        if (record.plan.expires_section < current) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::RetryWait,
                .plan_transaction_hash = record.transaction_hash,
                .detail = "The migration window expired. The owner must send a new link transaction.",
            });
            continue;
        }
        if (current < record.plan.cutoff_section) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::Scheduled,
                .plan_transaction_hash = record.transaction_hash,
            });
            continue;
        }
        const auto registered = token(record.plan.legacy_token_id);
        if (registered.has_value() && registered->smart == record.plan.target_contract_id.to_string()) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::Completed,
                .plan_transaction_hash = record.transaction_hash,
            });
            continue;
        }
        const auto snapshot = migration_snapshot(record);
        if (!snapshot.has_value()) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::Frozen,
                .plan_transaction_hash = record.transaction_hash,
                .detail                = "The node waits for the stable cutoff section and its control hash.",
            });
            continue;
        }

        const auto owner = node->account_controller()->current_profile().get_actor(record.plan.owner_id);
        if (!owner.has_value()) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::WaitingForOwner,
                .plan_transaction_hash = record.transaction_hash,
                .detail                = "The owner key is not available on this full node.",
            });
            continue;
        }

        const auto now               = Utils::current_date_ms();
        bool       request_readiness = false;
        {
            std::scoped_lock lock(migration_mutex_);
            auto            &requested_at = readiness_requested_at_[record.transaction_hash];
            if (now - requested_at >= ReadinessRetryMs) {
                requested_at      = now;
                request_readiness = true;
            }
        }
        if (request_readiness) {
            for (const auto &peer :
                 node->network()->active_full_peers_with_capability(TOKEN_MIGRATION_CAPABILITY)) {
                Responder focused(node->network());
                focused.add_identifier(peer);
                node->network()->send_message(TokenMigrationReadinessRequest { record.transaction_hash },
                                              MessageType::TokenMigrationReadiness,
                                              SendMode::Focused,
                                              MessageStatus::Request,
                                              focused);
            }
        }

        std::size_t matching = 1;
        {
            std::scoped_lock lock(migration_mutex_);
            const auto       responses = migration_readiness_.find(record.transaction_hash);
            if (responses != migration_readiness_.end()) {
                for (const auto &[peer, response] : responses->second) {
                    if (std::ranges::find(capable_peers, peer) != capable_peers.end() && response.ready
                        && Json::serialize(response) == Json::serialize(snapshot->readiness)) {
                        ++matching;
                    }
                }
            }
        }
        if (matching != capable_peers.size() + 1) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::WaitingForPeers,
                .plan_transaction_hash = record.transaction_hash,
                .detail                = "The node waits for matching snapshots from all active full validators.",
            });
            continue;
        }
        publish_migration_status(LegacyTokenMigrationStatus {
            .legacy_token_id       = record.plan.legacy_token_id,
            .target_contract_id    = record.plan.target_contract_id,
            .state                 = LegacyTokenMigrationState::Importing,
            .plan_transaction_hash = record.transaction_hash,
        });
        const auto migrated = submit_legacy_import(record.plan.legacy_token_id);
        if (!migrated.has_value()) {
            publish_migration_status(LegacyTokenMigrationStatus {
                .legacy_token_id       = record.plan.legacy_token_id,
                .target_contract_id    = record.plan.target_contract_id,
                .state                 = LegacyTokenMigrationState::Failed,
                .plan_transaction_hash = record.transaction_hash,
                .detail                = "The contract import transaction could not be sent.",
            });
        }
    }
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
    std::scoped_lock cache_lock(cache_creation_mutex_);
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
        validation_error_event_.publish("name");
        return std::unexpected(CreateTokenError::InvalidName);
    }

    auto upperTokenName = Utils::str_to_upper(token_name);
    auto tickerSymbol   = Utils::str_to_upper(ticker);
    if (upperTokenName == "EXTRACOIN" || tickerSymbol == "EXC" || token_exists(token_name, ticker)) {
        eLog("[TokenManager] Name or ticker exists");
        validation_error_event_.publish("exists");
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
        auto temp_actor = token_actor.fromJson(predefine_token_id);
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
    track_token_creation(tx_res.value(), token_data);
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
    track_token_creation(*sent, collection);
    return collection;
}

void TokenManager::track_token_creation(const Transaction &transaction, TokenData token_data) {
    {
        std::scoped_lock cache_lock(cache_creation_mutex_);
        cache_creation_.insert_or_assign(transaction.hash(), std::move(token_data));
    }
    const auto committed = node->dag()->find_transaction(transaction.section(), transaction.hash());
    if (committed.has_value()) {
        final_token_creation(*committed);
    }
}

void TokenManager::final_token_creation(const Transaction &transaction) {
    const auto transaction_hash = transaction.hash();
    TokenData  token_data;
    {
        std::scoped_lock cache_lock(cache_creation_mutex_);
        const auto       cache_entry = cache_creation_.find(transaction_hash);
        if (cache_entry == cache_creation_.end()) {
            return;
        }
        token_data = cache_entry->second;
        cache_creation_.erase(cache_entry);
    }

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
        std::scoped_lock cache_lock(cache_creation_mutex_);
        cache_creation_.insert_or_assign(transaction_hash, std::move(token_data));
        eLog("[TokenManager] Error save file to dfs");
        return;
    }

    WireFormat::Scope storage_format(WireFormat::Mode::Canonical);
    auto              registry_row = Utils::to_dbrow(token_data);
    registry_row.erase("kind");
    registry_row.erase("language");
    auto registry_id = registry_file_id();
    if (!registry_id.has_value()
        || !node->dfs()->add_vector_row(node->network_id(),
                                        registry_id.value(),
                                        std::move(registry_row),
                                        token_data.owner_id)) {
        std::scoped_lock cache_lock(cache_creation_mutex_);
        cache_creation_.insert_or_assign(transaction_hash, std::move(token_data));
        eWarning("[TokenManager] Token contract is stored, but its registry row was not published");
        return;
    }

    added_event_.publish(owner_id, token_id);
}

ExtraChain::Core::Event<std::string> &TokenManager::validation_error_event() noexcept {
    return validation_error_event_;
}

ExtraChain::Core::Event<ActorId, TokenId> &TokenManager::added_event() noexcept {
    return added_event_;
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
