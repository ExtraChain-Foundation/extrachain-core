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

#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <unordered_set>

#include "contracts/contract_types.h"
#include "contracts/wasm_runtime.h"

namespace ExtraChain::Contracts {

    EXTRACHAIN_EXPORT void retain_current_contract_state(ContractRecord &record);

    class EXTRACHAIN_EXPORT ContractStorage {
    public:
        virtual ~ContractStorage() = default;

        [[nodiscard]] virtual std::expected<ContractRecord, ContractFailure> load(
            std::string_view contract_id) const = 0;

        [[nodiscard]] virtual std::expected<void, ContractFailure> create(const ContractRecord &record) = 0;

        [[nodiscard]] virtual std::expected<void, ContractFailure> stage(const ContractRecord &record) = 0;

        [[nodiscard]] virtual std::expected<void, ContractFailure> replace(
            const ContractRecord &record,
            std::uint32_t         expected_version,
            std::string_view      expected_state_hash) = 0;
    };

    class EXTRACHAIN_EXPORT MemoryContractStorage final : public ContractStorage {
    public:
        [[nodiscard]] std::expected<ContractRecord, ContractFailure> load(
            std::string_view contract_id) const override;

        [[nodiscard]] std::expected<void, ContractFailure> create(const ContractRecord &record) override;

        [[nodiscard]] std::expected<void, ContractFailure> stage(const ContractRecord &record) override;

        [[nodiscard]] std::expected<void, ContractFailure> replace(const ContractRecord &record,
                                                                   std::uint32_t         expected_version,
                                                                   std::string_view expected_state_hash) override;

    private:
        mutable std::shared_mutex                       mutex_;
        std::unordered_map<std::string, ContractRecord> records_;
    };

    class EXTRACHAIN_EXPORT ContractManager {
    public:
        explicit ContractManager(
            std::unique_ptr<ContractStorage> storage = std::make_unique<MemoryContractStorage>(),
            ExecutionLimits                  limits  = {},
            RuntimeTuning                    tuning  = {});

        [[nodiscard]] std::expected<PreparedContractChange, ContractFailure> prepare_deploy(
            std::string                   contract_id,
            std::string                   owner_id,
            std::string                   kind,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> init_arguments,
            std::uint64_t                 block);

        [[nodiscard]] std::expected<PreparedContractChange, ContractFailure> prepare_call(
            std::string_view              contract_id,
            std::string_view              sender_id,
            std::string_view              method,
            std::span<const std::uint8_t> arguments,
            std::uint64_t                 block,
            const VerifiedInputs         &verified = {});

        [[nodiscard]] std::expected<PreparedContractChange, ContractFailure> prepare_upgrade(
            std::string_view              contract_id,
            std::string_view              sender_id,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> migration_arguments,
            std::uint64_t                 block);

        [[nodiscard]] std::expected<void, ContractFailure> stage(const PreparedContractChange &change);

        [[nodiscard]] std::expected<ContractReceipt, ContractFailure> commit(PreparedContractChange change,
                                                                             std::string transaction_hash = {});

        [[nodiscard]] std::expected<ContractReceipt, ContractFailure> deploy(
            std::string                   contract_id,
            std::string                   owner_id,
            std::string                   kind,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> init_arguments,
            std::uint64_t                 block);

        [[nodiscard]] std::expected<ContractReceipt, ContractFailure> call(std::string_view contract_id,
                                                                           std::string_view sender_id,
                                                                           std::string_view method,
                                                                           std::span<const std::uint8_t> arguments,
                                                                           std::uint64_t                 block);

        [[nodiscard]] std::expected<ContractReceipt, ContractFailure> query(
            std::string_view              contract_id,
            std::string_view              sender_id,
            std::string_view              method,
            std::span<const std::uint8_t> arguments,
            std::uint64_t                 block) const;

        [[nodiscard]] std::expected<ContractReceipt, ContractFailure> upgrade(
            std::string_view              contract_id,
            std::string_view              sender_id,
            std::span<const std::uint8_t> module,
            std::span<const std::uint8_t> migration_arguments,
            std::uint64_t                 block);

        [[nodiscard]] std::expected<ContractRecord, ContractFailure> inspect(std::string_view contract_id) const;

        [[nodiscard]] std::expected<ContractOutput, ContractFailure> evaluate(
            std::span<const std::uint8_t> module,
            std::string_view              sender,
            std::string_view              method,
            std::span<const std::uint8_t> arguments,
            std::span<const std::uint8_t> state,
            std::uint64_t                 block,
            std::string_view              contract_id = {},
            std::string_view              caller      = {},
            const VerifiedInputs         &verified    = {},
            std::uint32_t                 depth       = 0) const;

    private:
        [[nodiscard]] std::expected<PreparedContractChange, ContractFailure> prepare_call_unlocked(
            std::string_view                 contract_id,
            std::string_view                 sender_id,
            std::string_view                 caller_id,
            std::string_view                 method,
            std::span<const std::uint8_t>    arguments,
            std::uint64_t                    block,
            std::uint32_t                    depth,
            std::vector<std::string>        &stack,
            std::unordered_set<std::string> &touched,
            std::uint32_t                   &call_count,
            const VerifiedInputs            &verified);

        [[nodiscard]] std::expected<StateRevision, ContractFailure> revision(const ContractOutput &output,
                                                                             const StateRevision  *previous,
                                                                             std::uint64_t         block,
                                                                             std::string author_id) const;

        static ContractVersion       &active_version(ContractRecord &record);
        static const ContractVersion &active_version(const ContractRecord &record);

        std::unique_ptr<ContractStorage> storage_;
        WasmRuntime                      runtime_;
        ExecutionLimits                  limits_;
        mutable std::shared_mutex        mutex_;
    };

} // namespace ExtraChain::Contracts
