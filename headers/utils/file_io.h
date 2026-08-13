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

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

#include "extrachain_global.h"

namespace FileIo {

    enum class Error {
        OpenFailed,
        ReadFailed,
        WriteFailed,
        FlushFailed,
        ReplaceFailed,
        DirectorySyncFailed
    };

    EXTRACHAIN_EXPORT std::expected<std::string, Error> read_all(const std::filesystem::path& path);
    EXTRACHAIN_EXPORT std::expected<void, Error> write_atomic(const std::filesystem::path& path,
                                                              std::string_view             data);

} // namespace FileIo
