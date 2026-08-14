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
#include <string>

#include "extrachain_global.h"

class DbConnector;
class Transaction;
class BigNumber;

class EXTRACHAIN_EXPORT DagQuarantine {
public:
    explicit DagQuarantine(const std::filesystem::path &database_path);
    ~DagQuarantine();

    bool          record(const Transaction &transaction, const std::string &reason, const std::string &source);
    bool          contains(const std::string &transaction_hash) const;
    void          resolve_range(const BigNumber &from, const BigNumber &to);
    std::uint64_t count() const;

private:
    std::unique_ptr<DbConnector> database_;
};
