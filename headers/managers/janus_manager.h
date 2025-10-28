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

#include "chain/actor_id.h"

#include <string>
#include <vector>
#include <cstdint>
#include <optional>
#include <boost/describe.hpp>

struct FileLink;

enum class ContainerType {
    CppCompiler,
    PythonRuntime,
    JavaJdk,
    NodeJs,
    GoCompiler,
    RustCompiler,
    DockerCustom
};

BOOST_DESCRIBE_ENUM(ContainerType,
                    CppCompiler,
                    PythonRuntime,
                    JavaJdk,
                    NodeJs,
                    GoCompiler,
                    RustCompiler,
                    DockerCustom)

struct JanusTask {
    std::string title;
    std::string description;

    std::optional<uint32_t> min_gpu_count;
    uint32_t                min_cpu_cores;
    uint64_t                min_ram_mb;
    std::optional<uint64_t> min_vram_mb;
    bool                    ssd_required = false;

    ContainerType              container_type;
    std::optional<std::string> container_version;

    uint64_t created_at;
    uint32_t max_execution_time; // in secs

    std::vector<FileLink> files;

    // std::optional<std::string> entry_point;
    // std::vector<std::string>   dependencies;
};

BOOST_DESCRIBE_STRUCT(JanusTask,
                      (),
                      (title,
                       description,
                       min_gpu_count,
                       min_cpu_cores,
                       min_ram_mb,
                       min_vram_mb,
                       ssd_required,
                       container_type,
                       container_version,
                       created_at,
                       max_execution_time,
                       files /*,
                       entry_point,
                       dependencies */))
