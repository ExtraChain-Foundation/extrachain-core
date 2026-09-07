#pragma once

#include <algorithm>
#include <expected>
#include <string_view>

#include "contracts/contract_transaction.h"

namespace ExtraChain::Contracts {

    inline std::expected<std::uint32_t, ContractFailure> replay_call_depth(const ContractTransactionData& metadata,
                                                                           std::string_view               root,
                                                                           std::string_view contract) {
        std::uint32_t depth = 0;
        while (contract != root) {
            const auto parent =
                std::ranges::find(metadata.transitions, contract, &ContractTransitionData::contract_id);
            if (parent == metadata.transitions.end() || ++depth > ContractMaximumCallDepth) {
                return std::unexpected(
                    ContractFailure { ContractError::StorageError, "Contract replay call graph is invalid" });
            }
            contract = parent->caller_contract_id;
        }
        return depth;
    }

} // namespace ExtraChain::Contracts
