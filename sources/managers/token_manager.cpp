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
#include "network/network_manager.h"
#include "utils/exc_utils.h"
#include "contracts/contract_manager.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"

#include <charconv>

#include <QFile>
#include <msgpack.hpp>

namespace {

    std::expected<std::vector<std::uint8_t>, CreateTokenError> token_init_arguments(const std::string    &name,
                                                                                    const std::string    &ticker,
                                                                                    std::uint8_t          decimals,
                                                                                    const BigNumberFloat &count) {
        auto          count_text = count.to_string(NumeralBase::Dec);
        std::uint64_t supply     = 0;
        auto [position, error] = std::from_chars(count_text.data(), count_text.data() + count_text.size(), supply);
        if (error != std::errc() || position != count_text.data() + count_text.size()) {
            return std::unexpected(CreateTokenError::InvalidAmount);
        }

        msgpack::sbuffer buffer;
        msgpack::packer  packer(buffer);
        packer.pack_array(4);
        packer.pack(name);
        packer.pack(ticker);
        packer.pack(decimals);
        packer.pack(supply);
        auto *begin = reinterpret_cast<const std::uint8_t *>(buffer.data());
        return std::vector<std::uint8_t>(begin, begin + buffer.size());
    }

    std::expected<std::vector<std::uint8_t>, CreateTokenError> standard_token_module() {
        QFile module_file(":/contracts/fungible_token.wasm");
        if (!module_file.open(QIODevice::ReadOnly)) {
            return std::unexpected(CreateTokenError::InvalidTx);
        }
        auto module = module_file.readAll();
        return std::vector<std::uint8_t>(module.begin(), module.end());
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

bool TokenManager::token_exists(const std::string &name, const std::string &ticker) {
    // TODO!: Need to check vector
    return false;
}

std::expected<TokenData, CreateTokenError> TokenManager::create_token(const ActorId        &owner_id,
                                                                      const std::string    &token_name,
                                                                      const std::string    &ticker,
                                                                      const BigNumberFloat &token_count,
                                                                      const std::string    &color,
                                                                      const std::string    &predefine_token_id,
                                                                      std::uint8_t          decimals) {
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

    auto owner_actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!owner_actor.has_value()) {
        return std::unexpected(CreateTokenError::InvalidOwnerId);
    }

    auto module    = standard_token_module();
    auto arguments = token_init_arguments(token_name, tickerSymbol, decimals, token_count);
    if (!module.has_value() || !arguments.has_value()) {
        return std::unexpected(module.has_value() ? arguments.error() : module.error());
    }

    Actor<KeyPrivate> token_actor;
    if (predefine_token_id.empty()) {
        token_actor = node->account_controller()->create_service();
    } else {
        auto temp_actor = token_actor.fromJson(QByteArray::fromStdString(predefine_token_id));
        token_actor     = node->account_controller()->create_service({}, temp_actor);
    }

    auto token_data = TokenData { .token_id = token_actor.id(),
                                  .owner_id = owner_id,
                                  .name     = token_name,
                                  .ticker   = ticker,
                                  .count    = token_count,
                                  .color    = color };
    auto deployment =
        node->contract_manager()->prepare_deploy(token_actor.id().to_string(),
                                                 owner_id.to_string(),
                                                 "fungible-token",
                                                 *module,
                                                 *arguments,
                                                 static_cast<std::uint64_t>(
                                                     node->dag()->current_section().to_int().value_or(0))
                                                     + 1);
    if (!deployment.has_value()) {
        eWarning("[TokenManager] Contract preparation failed: {}", deployment.error().detail);
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    const auto &record   = deployment->record;
    const auto &version  = record.versions.back();
    const auto &revision = version.revisions.back();

    ContractTransactionData contract_data {
        .kind                = "fungible-token",
        .method              = "init",
        .arguments_base64    = Utils::to_base64(*arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(deployment->output.effects),
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

    auto tx_res = node->send_contract_transaction(tx, owner_actor.value(), std::move(*deployment));
    if (!tx_res.has_value()) {
        return std::unexpected(CreateTokenError::InvalidTx);
    }
    cache_creation_.insert({ tx_res->hash(), token_data });
    return token_data;
}

void TokenManager::final_token_creation(const Transaction &transaction) {
    if (cache_creation_.find(transaction.hash()) == cache_creation_.end()) {
        return;
    }

    auto token_data       = cache_creation_.at(transaction.hash());
    token_data.section_id = transaction.section();
    token_data.tx_hash    = transaction.hash();
    auto json             = Json::serialize(token_data);

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

    emit added(token_data.owner_id, token_data.token_id);
    cache_creation_.erase(transaction.hash());

    // TODO!: write to vector
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
