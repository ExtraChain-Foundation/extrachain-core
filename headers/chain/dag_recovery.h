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

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "extrachain_global.h"
#include "utils/bignumber.h"

class DbConnector;

enum class DagRecoveryStage {
    Detected,
    ProofRequested,
    Repairing,
    Replaying,
    Resolved,
    Failed
};

enum class StateProjectionStatus {
    Ready,
    RepairPending,
    Replaying,
    Failed
};

struct StateProjectionSnapshot {
    StateProjectionStatus status           = StateProjectionStatus::Ready;
    SectionId             verified_section = SectionId(-1);
    std::string           reason;
};

struct DagRecoveryIncident {
    std::string      id;
    SectionId        first_section;
    SectionId        last_section;
    std::string      reason;
    std::string      source;
    std::string      observed_root;
    std::string      expected_root;
    std::string      proof_hash;
    std::uint64_t    first_seen_ms = 0;
    std::uint64_t    last_seen_ms  = 0;
    std::uint64_t    attempts      = 0;
    DagRecoveryStage stage         = DagRecoveryStage::Detected;
};

class EXTRACHAIN_EXPORT DagRecoveryJournal {
public:
    explicit DagRecoveryJournal(const std::filesystem::path &database_path);
    ~DagRecoveryJournal();

    bool                             available() const;
    std::optional<std::string>       record(const SectionId   &first,
                                            const SectionId   &last,
                                            const std::string &reason,
                                            const std::string &source,
                                            const std::string &observed_root = {});
    bool                             advance(const std::string &incident_id,
                                             DagRecoveryStage   stage,
                                             const std::string &expected_root = {},
                                             const std::string &proof_hash    = {});
    std::vector<DagRecoveryIncident> pending() const;
    std::vector<DagRecoveryIncident> pending_in_range(const SectionId &first, const SectionId &last) const;
    std::uint64_t                    pending_count() const;

private:
    std::vector<DagRecoveryIncident> select_pending() const;

    mutable std::mutex           mutex_;
    std::unique_ptr<DbConnector> database_;
};
