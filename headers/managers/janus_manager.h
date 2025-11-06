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

#include <boost/describe.hpp>
#include "chain/actor_id.h"

class ExtraChainNode;
class DbConnector;
struct NodeId;

namespace Dfs {
    struct FileLink;
}

enum class ContainerType {
    CppCompiler,
    PythonRuntime,
    JavaJdk,
    NodeJs,
    GoCompiler,
    RustCompiler,
    DockerCustom
};

enum class SoftwareFramework {
    TensorFlow,
    PyTorch,
    Keras,
    ScikitLearn,
    MXNet,
    Caffe,
    Caffe2,
    Theano,
    Torch,
    ONNXRuntime,
    OpenCV,
    FastAI,
    PaddlePaddle,
    MindSpore,
    JAX,
    HuggingFaceTransformers,
    DeepSpeed,
    Ray,
    OpenVINO,
    TVM
};

enum class ExtraCondition {
    Windows11Pro,
    MacOSSequoia,
    Ubuntu2404LTS,
    IOS18SDK,
    Android15,
    NVIDIAGeForceGTX1660Super,
    AMDRadeonRX6800,
    IntelArcA770,
    NVIDIACUDAToolkit124,
    AMDAdrenalin245,
    Qt68,
    Boost186,
    OpenSSL33,
    FFmpeg70,
    OpenCV50,
    Xcode16,
    AndroidStudioKoala,
    VisualStudio2022,
    CMake330,
    VcpkgPackageManager,
    DockerDesktop
};

struct JanusTask {
    std::string title;
    std::string description;
    std::string code;

    // Hardware requirements
    std::optional<std::string> gpu_model;
    std::optional<uint32_t>    min_gpu_count;
    std::optional<std::string> cpu_model;
    uint32_t                   min_cpu_cores;
    uint64_t                   min_ram_mb;
    std::optional<uint64_t>    min_vram_mb;
    bool                       ssd_required = false;

    // Software requirements
    ContainerType container_type;
    // std::optional<std::string>     container_version;
    std::vector<SoftwareFramework> software_frameworks;
    std::vector<ExtraCondition>    extra_conditions;

    // Execution parameters
    uint64_t created_at;
    uint32_t max_execution_time; // in hours
    uint64_t budget_agp;

    // Security & validation
    bool encrypt_data_and_code = false;
    bool auto_accept           = false;
    bool multiple_check        = false;

    // Files
    std::vector<Dfs::FileLink> files;
};
BOOST_DESCRIBE_STRUCT(JanusTask,
                      (),
                      (title,
                       description,
                       code,
                       gpu_model,
                       min_gpu_count,
                       cpu_model,
                       min_cpu_cores,
                       min_ram_mb,
                       min_vram_mb,
                       ssd_required,
                       container_type,
                       // container_version,
                       software_frameworks,
                       extra_conditions,
                       created_at,
                       max_execution_time,
                       budget_agp,
                       encrypt_data_and_code,
                       auto_accept,
                       multiple_check,
                       files))

struct JanusData {
    std::string   id;
    std::uint64_t timestamp = 0;
    ActorId       actor;
    ActorId       owner;
    std::string   file_id;
    int           state;
};
BOOST_DESCRIBE_STRUCT(JanusData, (), (id, timestamp, actor, owner, file_id, state))

class JanusManager {
public:
    JanusManager(ExtraChainNode* node);
    ~JanusManager() = default;

    bool create_task(JanusTask task);

    bool create_janus_template();
    bool create_janus_vector();

    bool add_janus_task(const ActorId& owner_id, const std::string& file_id);

private:
    ExtraChainNode* node;
};
