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

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <boost/describe/class.hpp>
#include <boost/describe/enum.hpp>

#include "chain/actor.h"
#include "chain/transaction.h"
#include "contracts/contract_transaction.h"
#include "contracts/toolchain_registry.h"
#include "runtime/event.h"
#include "utils/bignumber_float.h"

namespace ExtraChain::Core {
    class ExtraChainNode;
}
class Responder;

struct LegacyTokenMigrationPlan {
    std::uint32_t schema = 1;
    TokenId       legacy_token_id;
    ActorId       target_contract_id;
    ActorId       owner_id;
    SectionId     source_section;
    std::string   source_transaction_hash;
    std::string   language;
    std::string   module_hash;
    std::string   expected_supply;
    SectionId     cutoff_section;
    SectionId     expires_section;
};
BOOST_DESCRIBE_STRUCT(LegacyTokenMigrationPlan,
                      (),
                      (schema,
                       legacy_token_id,
                       target_contract_id,
                       owner_id,
                       source_section,
                       source_transaction_hash,
                       language,
                       module_hash,
                       expected_supply,
                       cutoff_section,
                       expires_section))

struct TokenMigrationReadinessRequest {
    std::string plan_transaction_hash;
};
BOOST_DESCRIBE_STRUCT(TokenMigrationReadinessRequest, (), (plan_transaction_hash))

struct TokenMigrationReadinessResponse {
    std::string plan_transaction_hash;
    TokenId     legacy_token_id;
    ActorId     target_contract_id;
    SectionId   cutoff_section;
    std::string cutoff_section_hash;
    std::string cutoff_control_hash;
    std::string balances_hash;
    std::string supply;
    std::string module_hash;
    bool        ready = false;
};
BOOST_DESCRIBE_STRUCT(TokenMigrationReadinessResponse,
                      (),
                      (plan_transaction_hash,
                       legacy_token_id,
                       target_contract_id,
                       cutoff_section,
                       cutoff_section_hash,
                       cutoff_control_hash,
                       balances_hash,
                       supply,
                       module_hash,
                       ready))

enum class LegacyTokenMigrationState {
    WaitingForOwner,
    WaitingForPeers,
    Scheduled,
    Frozen,
    Importing,
    Completed,
    RetryWait,
    Failed
};
BOOST_DESCRIBE_ENUM(LegacyTokenMigrationState,
                    WaitingForOwner,
                    WaitingForPeers,
                    Scheduled,
                    Frozen,
                    Importing,
                    Completed,
                    RetryWait,
                    Failed)

struct LegacyTokenMigrationStatus {
    TokenId                   legacy_token_id;
    ActorId                   target_contract_id;
    LegacyTokenMigrationState state = LegacyTokenMigrationState::WaitingForOwner;
    std::string               plan_transaction_hash;
    std::string               detail;
};
BOOST_DESCRIBE_STRUCT(LegacyTokenMigrationStatus,
                      (),
                      (legacy_token_id, target_contract_id, state, plan_transaction_hash, detail))

struct TokenData {
    TokenId                    token_id;
    ActorId                    owner_id;
    std::string                name;
    std::string                ticker;
    BigNumberFloat             count;
    std::string                color;
    std::string                smart;
    std::string                kind;
    std::string                language;
    std::uint32_t              decimals = 0;
    std::optional<SectionId>   section_id;
    std::optional<std::string> tx_hash;

    bool operator==(const TokenData &other) const {
        return token_id == other.token_id && owner_id == other.owner_id && count == other.count
               && name == other.name && ticker == other.ticker && color == other.color && smart == other.smart
               && kind == other.kind && language == other.language && decimals == other.decimals;
    }

    // Overload the inequality operator
    bool operator!=(const TokenData &other) const {
        return !(*this == other);
    }
};
BOOST_DESCRIBE_STRUCT(
    TokenData,
    (),
    (token_id, owner_id, count, name, ticker, color, smart, kind, language, decimals, section_id, tx_hash))

struct TokenDataShort { // for data in transaction
    std::string                name;
    std::string                ticker;
    std::string                color;
    std::optional<std::string> smart;
};
BOOST_DESCRIBE_STRUCT(TokenDataShort, (), (name, ticker, color, smart))

enum class CreateTokenError {
    NoConnections,
    InvalidAmount,
    InvalidName,
    ExistToken,
    InvalidTx,
    InvalidOwnerId
};

class TokenManager {
public:
    TokenManager(ExtraChain::Core::ExtraChainNode *node);
    ~TokenManager() = default;

    bool token_exists(const std::string &name, const std::string &ticker);
    bool name_exists(const std::string &name);
    bool ticker_exists(const std::string &ticker);

    static std::unordered_map<ActorId, std::string>            read_tokens();
    std::vector<TokenData>                                     read_registry() const;
    std::vector<TokenData>                                     list_tokens() const;
    std::vector<TokenData>                                     list_nft_collections() const;
    std::optional<TokenData>                                   token(const TokenId &token_id) const;
    bool                                                       is_contract_token(const TokenId &token_id) const;
    std::expected<std::vector<std::uint8_t>, CreateTokenError> transfer_arguments(
        const TokenId        &token_id,
        const ActorId        &receiver,
        const BigNumberFloat &amount) const;
    std::vector<TokenData>                       legacy_tokens() const;
    std::expected<Transaction, CreateTokenError> publish_legacy_token_target(
        const TokenId                           &token_id,
        ExtraChain::Contracts::ToolchainLanguage language =
            ExtraChain::Contracts::ToolchainLanguage::AssemblyScript);
    std::expected<Transaction, CreateTokenError> link_legacy_token(const TokenId &token_id,
                                                                   const ActorId &target_contract_id);
    void                                         process_legacy_migrations();
    TransactionProveError                        validate_migration_plan(const Transaction &transaction) const;
    TransactionProveError                        validate_legacy_import(const Transaction             &transaction,
                                                                        const ContractTransactionData &metadata,
                                                                        std::span<const std::uint8_t>  arguments) const;
    bool                                         legacy_transaction_allowed(const Transaction &transaction) const;
    void handle_migration_readiness_request(const TokenMigrationReadinessRequest &request,
                                            const Responder                      &responder);
    void handle_migration_readiness_response(const TokenMigrationReadinessResponse &response,
                                             const Responder                       &responder);
    std::vector<LegacyTokenMigrationStatus> migration_statuses() const;

    std::expected<TokenData, CreateTokenError> create_token(
        const ActorId                           &owner_id,
        const std::string                       &token_name,
        const std::string                       &symbol,
        const BigNumberFloat                    &token_count,
        const std::string                       &color,
        const std::string                       &predefine_token_id = "",
        std::uint8_t                             decimals           = 8,
        ExtraChain::Contracts::ToolchainLanguage language =
            ExtraChain::Contracts::ToolchainLanguage::AssemblyScript);

    std::expected<TokenData, CreateTokenError> create_nft_collection(
        const ActorId                           &owner_id,
        const std::string                       &collection_name,
        const std::string                       &symbol,
        const std::string                       &color,
        ExtraChain::Contracts::ToolchainLanguage language =
            ExtraChain::Contracts::ToolchainLanguage::AssemblyScript);

    void final_token_creation(const Transaction &transaction);

    static bool is_valid_token_name(const std::string &name);
    static bool id_valid_token_ticker(const std::string &ticker);

    ExtraChain::Core::Event<std::string>      &validation_error_event() noexcept;
    ExtraChain::Core::Event<ActorId, TokenId> &added_event() noexcept;
    ExtraChain::Core::Event<const LegacyTokenMigrationStatus &> &migration_status_event() noexcept;

private:
    std::optional<std::string> registry_file_id() const;
    bool                       registry_row_valid(TokenData &token_data) const;
    void                       track_token_creation(const Transaction &transaction, TokenData token_data);
    std::expected<TokenData, CreateTokenError> submit_legacy_import(const TokenId &token_id);
    struct MigrationPlanRecord {
        LegacyTokenMigrationPlan plan;
        SectionId                section;
        std::string              transaction_hash;
    };
    struct MigrationSnapshot {
        TokenMigrationReadinessResponse                 readiness;
        std::vector<std::pair<ActorId, BigNumberFloat>> balances;
    };

    std::vector<MigrationPlanRecord>   migration_plans() const;
    std::optional<MigrationPlanRecord> migration_plan(const TokenId &token_id) const;
    std::optional<MigrationPlanRecord> migration_plan_by_hash(std::string_view transaction_hash) const;
    std::expected<MigrationSnapshot, CreateTokenError> migration_snapshot(const MigrationPlanRecord &record) const;
    bool migration_target_ready(const ActorId &target_contract_id, const TokenData &legacy_token) const;
    void publish_migration_status(LegacyTokenMigrationStatus status);
    static std::string canonical_balances_hash(const TokenId   &token_id,
                                               const SectionId &cutoff_section,
                                               std::string_view cutoff_section_hash,
                                               std::string_view cutoff_control_hash,
                                               const std::vector<std::pair<ActorId, BigNumberFloat>> &balances,
                                               std::uint32_t                                          decimals);

    ExtraChain::Core::ExtraChainNode          *node;
    mutable std::mutex                         cache_creation_mutex_;
    std::unordered_map<std::string, TokenData> cache_creation_; // TODO: also save to temp file
    mutable std::mutex                         legacy_cache_mutex_;
    mutable std::optional<SectionId>           legacy_cache_section_;
    mutable std::vector<TokenData>             legacy_cache_;
    mutable std::mutex                         migration_mutex_;
    mutable std::optional<SectionId>           migration_plan_cache_section_;
    mutable std::vector<MigrationPlanRecord>   migration_plan_cache_;
    std::map<std::string, std::map<std::string, TokenMigrationReadinessResponse>> migration_readiness_;
    std::map<TokenId, LegacyTokenMigrationStatus>                                 migration_statuses_;
    std::map<std::string, std::uint64_t>                                          readiness_requested_at_;
    ExtraChain::Core::Event<std::string>       validation_error_event_;
    ExtraChain::Core::Event<ActorId, TokenId>  added_event_;
    ExtraChain::Core::Event<const LegacyTokenMigrationStatus &>                   migration_status_event_;
};
