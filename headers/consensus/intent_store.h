/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "consensus/consensus_protocol.h"

class DbConnector;

namespace ExtraChain::Consensus {

    class EXTRACHAIN_EXPORT IntentStore {
    public:
        explicit IntentStore(std::filesystem::path database_path);
        ~IntentStore();

        IntentStore(const IntentStore&)            = delete;
        IntentStore& operator=(const IntentStore&) = delete;

        std::expected<void, ConsensusError> open();
        std::expected<void, ConsensusError> put(const IntentEnvelope& envelope);
        std::expected<void, ConsensusError> erase(const std::vector<std::string>& intent_hashes);
        std::expected<void, ConsensusError> expire(const std::vector<std::string>& intent_hashes);
        std::expected<void, ConsensusError> reject(const std::vector<std::string>& intent_hashes,
                                                   ConsensusError                  error);
        std::expected<std::vector<IntentEnvelope>, ConsensusError>      load_pending();
        std::expected<std::map<ActorId, std::uint64_t>, ConsensusError> load_committed_nonces();
        std::expected<void, ConsensusError>                             commit_finalized(
                                        const std::vector<std::pair<IntentEnvelope, IntentReceipt>>& finalized);
        std::expected<std::optional<IntentReceipt>, ConsensusError> receipt(std::string_view intent_hash);

    private:
        std::filesystem::path        database_path_;
        std::unique_ptr<DbConnector> database_;
        std::mutex                   mutex_;
    };

} // namespace ExtraChain::Consensus
