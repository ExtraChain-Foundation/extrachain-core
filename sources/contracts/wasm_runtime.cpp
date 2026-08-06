/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/wasm_runtime.h"

#include <array>
#include <cstring>
#include <limits>
#include <mutex>

#include <wasm_export.h>

namespace ExtraChain::Contracts {
    namespace {

        constexpr std::size_t ErrorBufferBytes = 256;

        std::mutex  RuntimeMutex;
        std::size_t RuntimeUsers = 0;
        bool        RuntimeReady = false;

        ExecutionFailure failure(ExecutionError error, const char *detail) {
            return { error, detail != nullptr ? detail : "Unknown WAMR error" };
        }

        ExecutionError execution_error(const char *detail) {
            if (detail != nullptr
                && (std::strstr(detail, "instruction limit") != nullptr
                    || std::strstr(detail, "instruction count") != nullptr)) {
                return ExecutionError::InstructionLimit;
            }
            return ExecutionError::ExecutionFailed;
        }

    } // namespace

    WasmRuntime::WasmRuntime(ExecutionLimits limits)
        : limits_(limits) {
        std::scoped_lock lock(RuntimeMutex);
        if (RuntimeUsers == 0) {
            RuntimeReady = wasm_runtime_init();
            if (RuntimeReady) {
                wasm_runtime_set_default_running_mode(Mode_Interp);
                wasm_runtime_set_log_level(WASM_LOG_LEVEL_ERROR);
            }
        }

        if (RuntimeReady) {
            ++RuntimeUsers;
            available_ = true;
        }
    }

    WasmRuntime::~WasmRuntime() {
        std::scoped_lock lock(RuntimeMutex);
        if (!available_) {
            return;
        }

        --RuntimeUsers;
        if (RuntimeUsers == 0) {
            wasm_runtime_destroy();
            RuntimeReady = false;
        }
    }

    bool WasmRuntime::available() const {
        return available_;
    }

    std::expected<ExecutionResult, ExecutionFailure> WasmRuntime::invoke(
        std::span<const std::uint8_t> module_bytes,
        std::span<const std::uint8_t> input) const {
        std::scoped_lock lock(RuntimeMutex);
        if (!available_ || !RuntimeReady) {
            return std::unexpected(failure(ExecutionError::RuntimeUnavailable, "WAMR is not initialized"));
        }
        if (module_bytes.size() > limits_.module_bytes) {
            return std::unexpected(failure(ExecutionError::ModuleTooLarge, "Contract module exceeds the limit"));
        }
        if (input.size() > limits_.input_bytes) {
            return std::unexpected(failure(ExecutionError::InputTooLarge, "Contract input exceeds the limit"));
        }
        if (module_bytes.size() > std::numeric_limits<std::uint32_t>::max()
            || input.size() > std::numeric_limits<std::uint32_t>::max()) {
            return std::unexpected(failure(ExecutionError::InputTooLarge, "Contract input cannot be addressed"));
        }

        std::vector<std::uint8_t>          module_copy(module_bytes.begin(), module_bytes.end());
        std::array<char, ErrorBufferBytes> error_buffer {};
        wasm_module_t                      module = wasm_runtime_load(module_copy.data(),
                                                 static_cast<std::uint32_t>(module_copy.size()),
                                                 error_buffer.data(),
                                                 error_buffer.size());
        if (module == nullptr) {
            return std::unexpected(failure(ExecutionError::InvalidModule, error_buffer.data()));
        }

        InstantiationArgs arguments {
            .default_stack_size     = limits_.stack_bytes,
            .host_managed_heap_size = limits_.heap_bytes,
            .max_memory_pages       = limits_.linear_memory_bytes / (64 * 1024),
        };
        wasm_module_inst_t instance =
            wasm_runtime_instantiate_ex(module, &arguments, error_buffer.data(), error_buffer.size());
        if (instance == nullptr) {
            wasm_runtime_unload(module);
            return std::unexpected(failure(ExecutionError::InstantiateFailed, error_buffer.data()));
        }
        wasm_runtime_set_bounds_checks(instance, true);

        wasm_exec_env_t environment = wasm_runtime_create_exec_env(instance, limits_.stack_bytes);
        if (environment == nullptr) {
            wasm_runtime_deinstantiate(instance);
            wasm_runtime_unload(module);
            return std::unexpected(
                failure(ExecutionError::InstantiateFailed, "Cannot create the contract execution environment"));
        }
        wasm_runtime_set_instruction_count_limit(environment, limits_.instructions);

        auto finish = [&]() {
            wasm_runtime_destroy_exec_env(environment);
            wasm_runtime_deinstantiate(instance);
            wasm_runtime_unload(module);
        };

        wasm_function_inst_t invoke_function = wasm_runtime_lookup_function(instance, "exc_invoke");
        wasm_function_inst_t length_function = wasm_runtime_lookup_function(instance, "exc_result_len");
        if (invoke_function == nullptr || length_function == nullptr) {
            finish();
            return std::unexpected(
                failure(ExecutionError::MissingEntryPoint, "Contract must export exc_invoke and exc_result_len"));
        }

        if (wasm_func_get_param_count(invoke_function, instance) != 2
            || wasm_func_get_result_count(invoke_function, instance) != 1
            || wasm_func_get_param_count(length_function, instance) != 0
            || wasm_func_get_result_count(length_function, instance) != 1) {
            finish();
            return std::unexpected(
                failure(ExecutionError::InvalidEntryPoint, "Contract entry points have an invalid signature"));
        }
        std::array<wasm_valkind_t, 2> invoke_parameter_types {};
        std::array<wasm_valkind_t, 1> invoke_result_types {};
        std::array<wasm_valkind_t, 1> length_result_types {};
        wasm_func_get_param_types(invoke_function, instance, invoke_parameter_types.data());
        wasm_func_get_result_types(invoke_function, instance, invoke_result_types.data());
        wasm_func_get_result_types(length_function, instance, length_result_types.data());
        if (invoke_parameter_types[0] != WASM_I32 || invoke_parameter_types[1] != WASM_I32
            || invoke_result_types[0] != WASM_I32 || length_result_types[0] != WASM_I32) {
            finish();
            return std::unexpected(
                failure(ExecutionError::InvalidEntryPoint, "Contract entry points must use i32 values"));
        }

        void         *native_input = nullptr;
        std::uint64_t input_offset = 0;
        if (!input.empty()) {
            input_offset = wasm_runtime_module_malloc(instance, input.size(), &native_input);
            if (input_offset == 0 || native_input == nullptr
                || input_offset > std::numeric_limits<std::int32_t>::max()) {
                finish();
                return std::unexpected(
                    failure(ExecutionError::MemoryLimit, "Cannot allocate contract input memory"));
            }
            std::memcpy(native_input, input.data(), input.size());
        }

        wasm_val_t invoke_result {};
        invoke_result.kind = WASM_I32;
        wasm_val_t invoke_arguments[2] {};
        invoke_arguments[0].kind   = WASM_I32;
        invoke_arguments[0].of.i32 = static_cast<std::int32_t>(input_offset);
        invoke_arguments[1].kind   = WASM_I32;
        invoke_arguments[1].of.i32 = static_cast<std::int32_t>(input.size());
        if (!wasm_runtime_call_wasm_a(environment, invoke_function, 1, &invoke_result, 2, invoke_arguments)) {
            const char *exception = wasm_runtime_get_exception(instance);
            if (input_offset != 0) {
                wasm_runtime_module_free(instance, input_offset);
            }
            auto error = failure(execution_error(exception), exception);
            finish();
            return std::unexpected(std::move(error));
        }

        wasm_val_t length_result {};
        length_result.kind = WASM_I32;
        if (!wasm_runtime_call_wasm_a(environment, length_function, 1, &length_result, 0, nullptr)) {
            const char *exception = wasm_runtime_get_exception(instance);
            if (input_offset != 0) {
                wasm_runtime_module_free(instance, input_offset);
            }
            auto error = failure(execution_error(exception), exception);
            finish();
            return std::unexpected(std::move(error));
        }

        if (input_offset != 0) {
            wasm_runtime_module_free(instance, input_offset);
        }

        const auto output_offset = static_cast<std::uint32_t>(invoke_result.of.i32);
        const auto output_size   = static_cast<std::uint32_t>(length_result.of.i32);
        if (output_size > limits_.result_bytes) {
            finish();
            return std::unexpected(failure(ExecutionError::ResultTooLarge, "Contract result exceeds the limit"));
        }
        if (output_size != 0 && !wasm_runtime_validate_app_addr(instance, output_offset, output_size)) {
            finish();
            return std::unexpected(
                failure(ExecutionError::InvalidResult, "Contract returned an invalid memory range"));
        }

        ExecutionResult result;
        if (output_size != 0) {
            auto *output = static_cast<std::uint8_t *>(wasm_runtime_addr_app_to_native(instance, output_offset));
            result.output.assign(output, output + output_size);
        }

        finish();
        return result;
    }

} // namespace ExtraChain::Contracts
