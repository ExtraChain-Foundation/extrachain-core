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

#include <mutex>

#include "contracts/contract_manager.h"

class DfsController;
class Dag;

namespace ExtraChain::Contracts {

    class EXTRACHAIN_EXPORT DfsContractStorage final : public ContractStorage {
    public:
        DfsContractStorage(DfsController *dfs, Dag *dag);

        [[nodiscard]] std::expected<ContractRecord, ContractFailure> load(
            std::string_view contract_id) const override;

        [[nodiscard]] std::expected<void, ContractFailure> create(const ContractRecord &record) override;

        [[nodiscard]] std::expected<void, ContractFailure> stage(const ContractRecord &record) override;

        [[nodiscard]] std::expected<void, ContractFailure> replace(const ContractRecord &record,
                                                                   std::uint32_t         expected_version,
                                                                   std::string_view expected_state_hash) override;

    private:
        [[nodiscard]] std::expected<void, ContractFailure> save(const ContractRecord &record,
                                                                bool                  write_manifest) const;

        DfsController     *dfs_ = nullptr;
        Dag               *dag_ = nullptr;
        mutable std::mutex mutex_;
    };

} // namespace ExtraChain::Contracts
