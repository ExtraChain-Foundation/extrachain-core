/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <boost/describe/class.hpp>

namespace ExtraChain::Contracts {

    inline constexpr std::uint32_t ContractAbiVersion = 1;

    struct ExecutionLimits {
        std::size_t   module_bytes        = 2 * 1024 * 1024;
        std::int32_t  instructions        = 5'000'000;
        std::uint32_t linear_memory_bytes = 32 * 1024 * 1024;
        std::uint32_t stack_bytes         = 256 * 1024;
        std::uint32_t heap_bytes          = 1024 * 1024;
        std::size_t   input_bytes         = 8 * 1024 * 1024;
        std::size_t   result_bytes        = 256 * 1024;
    };

    enum class ExecutionError {
        RuntimeUnavailable,
        ModuleTooLarge,
        InputTooLarge,
        InvalidModule,
        InstantiateFailed,
        MissingEntryPoint,
        InvalidEntryPoint,
        MemoryLimit,
        InstructionLimit,
        ExecutionFailed,
        InvalidResult,
        ResultTooLarge
    };

    struct ExecutionResult {
        std::vector<std::uint8_t> output;
    };

    struct ExecutionFailure {
        ExecutionError error;
        std::string    detail;
    };

    struct ContractEvent {
        std::string               topic;
        std::vector<std::uint8_t> data;
    };

    struct ContractOutput {
        bool                       ok = false;
        std::vector<std::uint8_t>  state;
        std::vector<std::uint8_t>  data;
        std::vector<ContractEvent> events;
        std::optional<std::string> error;
    };

    struct StateRevision {
        std::uint64_t             revision = 0;
        std::uint64_t             block    = 0;
        std::string               previous_hash;
        std::string               state_hash;
        std::string               transaction_hash;
        std::string               author_id;
        std::string               storage_id;
        std::vector<std::uint8_t> state;
    };

    struct ContractVersion {
        std::uint32_t              version = 1;
        std::string                module_hash;
        std::string                previous_module_hash;
        std::string                module_storage_id;
        std::vector<std::uint8_t>  module;
        std::vector<StateRevision> revisions;
    };

    struct ContractRecord {
        std::string                  contract_id;
        std::string                  owner_id;
        std::string                  kind;
        std::uint32_t                active_version = 1;
        std::vector<ContractVersion> versions;
    };

    struct ContractSummary {
        std::string   contract_id;
        std::string   owner_id;
        std::string   kind;
        std::uint32_t version  = 1;
        std::uint64_t revision = 1;
        std::string   module_hash;
        std::string   state_hash;
        std::string   transaction_hash;
        std::uint64_t section = 0;
        std::string   deploy_transaction_hash;
        std::uint64_t deploy_section = 0;
    };
    BOOST_DESCRIBE_STRUCT(ContractSummary,
                          (),
                          (contract_id,
                           owner_id,
                           kind,
                           version,
                           revision,
                           module_hash,
                           state_hash,
                           transaction_hash,
                           section,
                           deploy_transaction_hash,
                           deploy_section))

    struct ContractCatalogFilter {
        std::optional<std::string> owner_id;
        std::optional<std::string> kind;
        std::size_t                limit = 50;
        std::optional<std::string> cursor;
    };

    struct ContractCatalogPage {
        std::vector<ContractSummary> items;
        std::optional<std::string>   next_cursor;
    };
    BOOST_DESCRIBE_STRUCT(ContractCatalogPage, (), (items, next_cursor))

    enum class ContractError {
        NotFound,
        AlreadyExists,
        InvalidOwner,
        InvalidModule,
        InvalidArguments,
        InvalidResponse,
        ExecutionFailed,
        StateTooLarge,
        TooManyEvents,
        Conflict,
        StorageError,
        UpgradeDenied
    };

    struct ContractFailure {
        ContractError error;
        std::string   detail;
    };

    struct ContractReceipt {
        std::string                contract_id;
        std::uint32_t              version  = 1;
        std::uint64_t              revision = 0;
        std::string                previous_state_hash;
        std::string                state_hash;
        std::vector<std::uint8_t>  data;
        std::vector<ContractEvent> events;
    };

    enum class ContractChangeKind {
        Create,
        Replace,
        ReadOnly
    };

    struct PreparedContractChange {
        ContractChangeKind kind = ContractChangeKind::ReadOnly;
        ContractRecord     record;
        ContractOutput     output;
        std::uint32_t      expected_version = 0;
        std::string        expected_state_hash;
    };

} // namespace ExtraChain::Contracts
