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
#include "contracts/contract_manager.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_hash.h"
#include "contracts/contract_module.h"
#include "contracts/standard_token.h"

#include <algorithm>
#include <array>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <future>
#include <iostream>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

    using Bytes = std::vector<std::uint8_t>;

    constexpr char Alice[]          = "1111111111111111111111111111111111111111";
    constexpr char Bob[]            = "2222222222222222222222222222222222222222";
    constexpr char Carol[]          = "3333333333333333333333333333333333333333";
    constexpr char Dave[]           = "4444444444444444444444444444444444444444";
    constexpr char ClaimContract[]  = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    constexpr char TargetContract[] = "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

    void append_length(Bytes &output, std::uint8_t code8, std::uint8_t code16, std::size_t length) {
        if (length <= 0xff) {
            output.push_back(code8);
            output.push_back(static_cast<std::uint8_t>(length));
            return;
        }
        if (length <= 0xffff) {
            output.push_back(code16);
            output.push_back(static_cast<std::uint8_t>(length >> 8));
            output.push_back(static_cast<std::uint8_t>(length));
            return;
        }
        throw std::runtime_error("Test value is too large");
    }

    void append_array(Bytes &output, std::size_t length) {
        if (length > 15) {
            throw std::runtime_error("Test array is too large");
        }
        output.push_back(static_cast<std::uint8_t>(0x90 | length));
    }

    void append_string(Bytes &output, std::string_view value) {
        if (value.size() <= 31) {
            output.push_back(static_cast<std::uint8_t>(0xa0 | value.size()));
        } else {
            append_length(output, 0xd9, 0xda, value.size());
        }
        output.insert(output.end(), value.begin(), value.end());
    }

    void append_binary(Bytes &output, std::span<const std::uint8_t> value) {
        append_length(output, 0xc4, 0xc5, value.size());
        output.insert(output.end(), value.begin(), value.end());
    }

    void append_unsigned(Bytes &output, std::uint64_t value) {
        if (value <= 0x7f) {
            output.push_back(static_cast<std::uint8_t>(value));
        } else if (value <= 0xff) {
            output.push_back(0xcc);
            output.push_back(static_cast<std::uint8_t>(value));
        } else {
            output.push_back(0xcf);
            for (int shift = 56; shift >= 0; shift -= 8) {
                output.push_back(static_cast<std::uint8_t>(value >> shift));
            }
        }
    }

    Bytes string_argument(std::string_view value) {
        Bytes result;
        append_array(result, 1);
        append_string(result, value);
        return result;
    }

    Bytes unsigned_argument(std::uint64_t value) {
        Bytes result;
        append_array(result, 1);
        append_unsigned(result, value);
        return result;
    }

    Bytes empty_arguments() {
        Bytes result;
        append_array(result, 0);
        return result;
    }

    Bytes pair_argument(std::string_view address, std::uint64_t amount) {
        Bytes result;
        append_array(result, 2);
        append_string(result, address);
        append_unsigned(result, amount);
        return result;
    }

    Bytes string_pair_argument(std::string_view first, std::string_view second) {
        Bytes result;
        append_array(result, 2);
        append_string(result, first);
        append_string(result, second);
        return result;
    }

    Bytes dfs_binding_argument(std::string_view logical_key,
                               std::string_view file_id,
                               std::string_view content_hash,
                               std::string_view previous_content_hash) {
        Bytes result;
        append_array(result, 4);
        append_string(result, logical_key);
        append_string(result, file_id);
        append_string(result, content_hash);
        append_string(result, previous_content_hash);
        return result;
    }

    Bytes transfer_from_argument(std::string_view owner, std::string_view receiver, std::uint64_t amount) {
        Bytes result;
        append_array(result, 3);
        append_string(result, owner);
        append_string(result, receiver);
        append_unsigned(result, amount);
        return result;
    }

    Bytes token_init_argument() {
        Bytes result;
        append_array(result, 5);
        append_string(result, "Example Token");
        append_string(result, "EXT");
        append_unsigned(result, 0);
        append_unsigned(result, 1'000);
        append_array(result, 0);
        return result;
    }

    Bytes token_init_argument(std::string_view supply) {
        Bytes result;
        append_array(result, 5);
        append_string(result, "Example Token");
        append_string(result, "EXT");
        append_unsigned(result, 0);
        append_string(result, supply);
        append_array(result, 0);
        return result;
    }

    Bytes token_migration_argument() {
        Bytes result;
        append_array(result, 5);
        append_string(result, "Legacy Token");
        append_string(result, "LEG");
        append_unsigned(result, 0);
        append_string(result, "1000");
        append_array(result, 2);
        append_array(result, 2);
        append_string(result, Alice);
        append_string(result, "700");
        append_array(result, 2);
        append_string(result, Bob);
        append_string(result, "300");
        return result;
    }

    Bytes migration_target_argument() {
        Bytes result;
        append_array(result, 5);
        append_string(result, "Legacy Token");
        append_string(result, "LEG");
        append_unsigned(result, 0);
        append_string(result, "0");
        append_array(result, 0);
        return result;
    }

    Bytes legacy_import_argument() {
        Bytes result;
        append_array(result, 3);
        append_string(result, "5555555555555555555555555555555555555555");
        append_string(result, "1000");
        append_array(result, 2);
        append_array(result, 2);
        append_string(result, Alice);
        append_string(result, "700");
        append_array(result, 2);
        append_string(result, Bob);
        append_string(result, "300");
        return result;
    }

    Bytes request(std::string_view                       sender,
                  std::string_view                       method,
                  std::span<const std::uint8_t>          arguments,
                  std::span<const std::uint8_t>          state,
                  const ExtraChain::Contracts::DfsProof *dfs_proof = nullptr) {
        Bytes result;
        append_array(result, 6);
        append_array(result, 5);
        append_string(result, sender);
        append_string(result, sender);
        append_string(result, "test-contract");
        append_unsigned(result, 1);
        append_unsigned(result, 0);
        append_string(result, method);
        append_binary(result, arguments);
        append_binary(result, state);
        append_array(result, 2);
        append_array(result, 0);
        append_array(result, dfs_proof == nullptr ? 0 : 1);
        if (dfs_proof != nullptr) {
            append_array(result, 3);
            append_string(result, dfs_proof->file_id);
            append_string(result, dfs_proof->owner_id);
            append_string(result, dfs_proof->content_hash);
        }
        append_unsigned(result, ExtraChain::Contracts::ContractAbiVersion);
        return result;
    }

    class Reader {
    public:
        explicit Reader(std::span<const std::uint8_t> source)
            : source_(source) {
        }

        std::size_t array() {
            auto marker = byte();
            if ((marker & 0xf0) != 0x90) {
                throw std::runtime_error("Expected a MessagePack array");
            }
            return marker & 0x0f;
        }

        bool boolean() {
            auto marker = byte();
            if (marker == 0xc3) {
                return true;
            }
            if (marker == 0xc2) {
                return false;
            }
            throw std::runtime_error("Expected a MessagePack boolean");
        }

        std::uint64_t unsigned_value() {
            auto marker = byte();
            if (marker <= 0x7f) {
                return marker;
            }
            if (marker == 0xcc) {
                return byte();
            }
            if (marker == 0xcf) {
                std::uint64_t result = 0;
                for (int index = 0; index < 8; ++index) {
                    result = (result << 8) | byte();
                }
                return result;
            }
            throw std::runtime_error("Expected a MessagePack unsigned value");
        }

        Bytes binary() {
            auto marker = byte();
            auto length = length_value(marker, 0xc4, 0xc5);
            return take(length);
        }

        std::string string() {
            auto        marker = byte();
            std::size_t length = 0;
            if ((marker & 0xe0) == 0xa0) {
                length = marker & 0x1f;
            } else {
                length = length_value(marker, 0xd9, 0xda);
            }
            auto value = take(length);
            return { value.begin(), value.end() };
        }

        std::optional<std::string> optional_string() {
            if (peek() == 0xc0) {
                byte();
                return std::nullopt;
            }
            return string();
        }

        bool empty() const {
            return source_.empty();
        }

    private:
        std::uint8_t peek() const {
            if (source_.empty()) {
                throw std::runtime_error("Unexpected end of MessagePack input");
            }
            return source_.front();
        }

        std::uint8_t byte() {
            auto value = peek();
            source_    = source_.subspan(1);
            return value;
        }

        std::size_t length_value(std::uint8_t marker, std::uint8_t marker8, std::uint8_t marker16) {
            if (marker == marker8) {
                return byte();
            }
            if (marker == marker16) {
                auto high = byte();
                auto low  = byte();
                return (static_cast<std::size_t>(high) << 8) | low;
            }
            throw std::runtime_error("Invalid MessagePack length marker");
        }

        Bytes take(std::size_t length) {
            if (length > source_.size()) {
                throw std::runtime_error("Unexpected end of MessagePack value");
            }
            Bytes result(source_.begin(), source_.begin() + static_cast<std::ptrdiff_t>(length));
            source_ = source_.subspan(length);
            return result;
        }

        std::span<const std::uint8_t> source_;
    };

    struct Response {
        bool                       ok;
        Bytes                      state;
        Bytes                      data;
        std::optional<std::string> error;
    };

    Response response(std::span<const std::uint8_t> source) {
        Reader reader(source);
        if (reader.array() != 6) {
            throw std::runtime_error("Invalid contract response");
        }
        Response result { .ok = reader.boolean(), .state = reader.binary(), .data = reader.binary() };
        auto     events = reader.array();
        for (std::size_t index = 0; index < events; ++index) {
            if (reader.array() != 2) {
                throw std::runtime_error("Invalid contract event");
            }
            static_cast<void>(reader.string());
            static_cast<void>(reader.binary());
        }
        auto effects = reader.array();
        for (std::size_t index = 0; index < effects; ++index) {
            if (reader.array() != 4) {
                throw std::runtime_error("Invalid contract effect");
            }
            static_cast<void>(reader.string());
            static_cast<void>(reader.string());
            static_cast<void>(reader.string());
            static_cast<void>(reader.binary());
        }
        result.error = reader.optional_string();
        return result;
    }

    void require_receipt(const Response  &response,
                         std::string_view operation,
                         std::string_view subject,
                         std::string_view amount) {
        Reader reader(response.data);
        if (reader.array() != 3 || reader.string() != operation || reader.string() != subject
            || reader.string() != amount || !reader.empty()) {
            throw std::runtime_error("Contract operation receipt is invalid");
        }
    }

    Bytes read_file(const char *path) {
        std::ifstream stream(path, std::ios::binary | std::ios::ate);
        if (!stream) {
            throw std::runtime_error("Cannot read a contract module");
        }
        auto size = stream.tellg();
        stream.seekg(0);
        Bytes result(static_cast<std::size_t>(size));
        stream.read(reinterpret_cast<char *>(result.data()), size);
        return result;
    }

    Response invoke(ExtraChain::Contracts::WasmRuntime &runtime,
                    const Bytes                        &module,
                    std::string_view                    sender,
                    std::string_view                    method,
                    const Bytes                        &arguments,
                    const Bytes                        &state,
                    std::string_view                    module_hash = {}) {
        auto input = request(sender, method, arguments, state);
        auto result =
            module_hash.empty() ? runtime.invoke(module, input) : runtime.invoke(module, module_hash, input);
        if (!result.has_value()) {
            throw std::runtime_error(result.error().detail);
        }
        return response(result->output);
    }

    Response invoke_with_dfs(ExtraChain::Contracts::WasmRuntime    &runtime,
                             const Bytes                           &module,
                             std::string_view                       sender,
                             std::string_view                       method,
                             const Bytes                           &arguments,
                             const Bytes                           &state,
                             const ExtraChain::Contracts::DfsProof &proof) {
        auto input  = request(sender, method, arguments, state, &proof);
        auto result = runtime.invoke(module, input);
        if (!result.has_value()) {
            throw std::runtime_error(result.error().detail);
        }
        return response(result->output);
    }

    void require(bool value, std::string_view message) {
        if (!value) {
            throw std::runtime_error(std::string(message));
        }
    }

    bool contains_text(const Bytes &value, std::string_view text) {
        return std::search(value.begin(), value.end(), text.begin(), text.end()) != value.end();
    }

    class StageTrackingStorage final : public ExtraChain::Contracts::ContractStorage {
    public:
        std::expected<ExtraChain::Contracts::ContractRecord, ExtraChain::Contracts::ContractFailure> load(
            std::string_view contract_id) const override {
            const auto record = records_.find(std::string(contract_id));
            if (record == records_.end()) {
                return std::unexpected(ExtraChain::Contracts::ContractFailure {
                    .error  = ExtraChain::Contracts::ContractError::NotFound,
                    .detail = "Contract does not exist",
                });
            }
            return record->second;
        }

        std::expected<void, ExtraChain::Contracts::ContractFailure> create(
            const ExtraChain::Contracts::ContractRecord &record) override {
            auto current = record;
            ExtraChain::Contracts::retain_current_contract_state(current);
            if (!records_.emplace(current.contract_id, std::move(current)).second) {
                return std::unexpected(ExtraChain::Contracts::ContractFailure {
                    .error  = ExtraChain::Contracts::ContractError::AlreadyExists,
                    .detail = "Contract already exists",
                });
            }
            return {};
        }

        std::expected<void, ExtraChain::Contracts::ContractFailure> stage(
            const ExtraChain::Contracts::ContractRecord &) override {
            ++stage_count;
            return {};
        }

        std::expected<void, ExtraChain::Contracts::ContractFailure> replace(
            const ExtraChain::Contracts::ContractRecord &record,
            std::uint32_t,
            std::string_view) override {
            auto current = record;
            ExtraChain::Contracts::retain_current_contract_state(current);
            records_.insert_or_assign(current.contract_id, std::move(current));
            return {};
        }

        std::size_t stage_count = 0;

    private:
        std::unordered_map<std::string, ExtraChain::Contracts::ContractRecord> records_;
    };

    Bytes test_fungible(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        Bytes      state;
        auto       result     = invoke(runtime, module, Alice, "init", token_init_argument(), state);
        const auto init_error = "Token init failed: " + result.error.value_or(std::string("no error detail"));
        require(result.ok, init_error);
        require_receipt(result, "mint", Alice, "1000");
        state = result.state;

        result = invoke(runtime, module, Alice, "transfer", pair_argument(Bob, 250), state);
        require(result.ok, "Token transfer failed");
        require_receipt(result, "transfer", Bob, "250");
        state = result.state;

        result = invoke(runtime, module, Alice, "approve", pair_argument(Carol, 100), state);
        require(result.ok, "Token approval failed");
        require_receipt(result, "approval", Carol, "100");
        state = result.state;

        result = invoke(runtime, module, Carol, "transfer_from", transfer_from_argument(Alice, Dave, 75), state);
        require(result.ok, "Approved token transfer failed");
        require_receipt(result, "transfer", Dave, "75");
        state = result.state;

        result = invoke(runtime, module, Dave, "burn", unsigned_argument(25), state);
        require(result.ok, "Token burn failed");
        require_receipt(result, "burn", Dave, "25");
        state = result.state;

        result = invoke(runtime, module, Alice, "mint", pair_argument(Bob, 50), state);
        require(result.ok, "Owner mint failed");
        require_receipt(result, "mint", Bob, "50");
        state = result.state;

        result = invoke(runtime, module, Alice, "revoke_mint", empty_arguments(), state);
        require(result.ok, "Mint revoke failed");
        state = result.state;

        result = invoke(runtime, module, Alice, "mint", pair_argument(Bob, 1), state);
        require(!result.ok && result.state == state, "Mint worked after permanent revoke");
        return state;
    }

    void test_u128_boundaries(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        constexpr std::string_view Maximum = "340282366920938463463374607431768211455";
        auto maximum = invoke(runtime, module, Alice, "init", token_init_argument(Maximum), {});
        require(maximum.ok, "Token rejected the maximum u128 supply");
        auto overflow = invoke(runtime, module, Alice, "mint", pair_argument(Alice, 1), maximum.state);
        require(!overflow.ok && overflow.state == maximum.state, "Token accepted a u128 supply overflow");
        auto leading_zero = invoke(runtime, module, Alice, "init", token_init_argument("01"), {});
        require(!leading_zero.ok, "Token accepted a non-canonical amount");
        auto too_large = invoke(runtime,
                                module,
                                Alice,
                                "init",
                                token_init_argument("340282366920938463463374607431768211456"),
                                {});
        require(!too_large.ok, "Token accepted an amount above u128");
    }

    Bytes nft_init_argument() {
        Bytes result;
        append_array(result, 2);
        append_string(result, "Example Collection");
        append_string(result, "ENFT");
        return result;
    }

    Bytes nft_mint_argument(std::string_view                       id,
                            std::string_view                       receiver,
                            const ExtraChain::Contracts::DfsProof &proof) {
        Bytes result;
        append_array(result, 5);
        append_string(result, id);
        append_string(result, receiver);
        append_string(result, proof.owner_id);
        append_string(result, proof.file_id);
        append_string(result, proof.content_hash);
        return result;
    }

    Bytes nft_pair_argument(std::string_view id, std::string_view actor) {
        Bytes result;
        append_array(result, 2);
        append_string(result, id);
        append_string(result, actor);
        return result;
    }

    Bytes nft_transfer_from_argument(std::string_view id, std::string_view owner, std::string_view receiver) {
        Bytes result;
        append_array(result, 3);
        append_string(result, id);
        append_string(result, owner);
        append_string(result, receiver);
        return result;
    }

    Bytes test_nft(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        const ExtraChain::Contracts::DfsProof proof {
            .file_id      = "metadata-file",
            .owner_id     = Alice,
            .content_hash = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        };
        const std::string item_id = "18446744073709551616";
        Bytes             state;
        auto              result = invoke(runtime, module, Alice, "init", nft_init_argument(), state);
        require(result.ok, "NFT collection init failed");
        state  = result.state;
        result = invoke_with_dfs(runtime,
                                 module,
                                 Alice,
                                 "mint",
                                 nft_mint_argument(item_id, Alice, proof),
                                 state,
                                 proof);
        require(result.ok, "NFT mint with a u128 item ID failed");
        require_receipt(result, "nft_mint", Alice, item_id);
        state  = result.state;
        result = invoke(runtime, module, Alice, "owner_of", string_argument(item_id), state);
        require(result.ok && Reader(result.data).string() == Alice, "NFT owner query failed");
        result = invoke(runtime, module, Alice, "approve", nft_pair_argument(item_id, Carol), state);
        require(result.ok, "NFT approval failed");
        require_receipt(result, "nft_approval", Carol, item_id);
        state  = result.state;
        result = invoke(runtime,
                        module,
                        Carol,
                        "transfer_from",
                        nft_transfer_from_argument(item_id, Alice, Bob),
                        state);
        require(result.ok, "Approved NFT transfer failed");
        require_receipt(result, "nft_transfer", Bob, item_id);
        state  = result.state;
        result = invoke(runtime, module, Bob, "metadata_of", string_argument(item_id), state);
        require(result.ok, "NFT metadata query failed");
        result = invoke(runtime, module, Bob, "burn", string_argument(item_id), state);
        require(result.ok, "NFT burn failed");
        require_receipt(result, "nft_burn", Bob, item_id);
        state  = result.state;
        result = invoke(runtime, module, Bob, "owner_of", string_argument(item_id), state);
        require(!result.ok && result.state == state, "Burned NFT is still available");
        return state;
    }

    void test_runtime_limits(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        auto invalid = runtime.invoke({}, {});
        require(!invalid.has_value()
                    && invalid.error().error == ExtraChain::Contracts::ExecutionError::InvalidModule,
                "Runtime accepted an invalid module");

        Bytes oversized_module(2 * 1024 * 1024 + 1);
        auto  module_result = runtime.invoke(oversized_module, {});
        require(!module_result.has_value()
                    && module_result.error().error == ExtraChain::Contracts::ExecutionError::ModuleTooLarge,
                "Runtime accepted an oversized module");

        Bytes oversized_input(8 * 1024 * 1024 + 1);
        auto  input_result = runtime.invoke(module, oversized_input);
        require(!input_result.has_value()
                    && input_result.error().error == ExtraChain::Contracts::ExecutionError::InputTooLarge,
                "Runtime accepted oversized input");

        const auto module_hash    = ExtraChain::Contracts::content_hash(module);
        auto       changed_module = module;
        changed_module.back() ^= 1;
        const auto mismatched = runtime.invoke(changed_module, module_hash, {});
        require(!mismatched.has_value()
                    && mismatched.error().error == ExtraChain::Contracts::ExecutionError::InvalidModule,
                "Runtime accepted bytes that do not match the trusted module hash");

        require(runtime.invoke(module, module_hash, {}).has_value(), "Runtime could not cache a trusted module");
        const auto cached_mismatch = runtime.invoke(changed_module, module_hash, {});
        require(!cached_mismatch.has_value()
                    && cached_mismatch.error().error == ExtraChain::Contracts::ExecutionError::InvalidModule,
                "Runtime reused a cached module for changed bytes");

        auto module_with_float_bytes_in_custom_data = module;
        module_with_float_bytes_in_custom_data.insert(module_with_float_bytes_in_custom_data.end(),
                                                      { 0x00, 0x05, 0x00, 0x43, 0x44, 0x7c, 0x7d });
        require(runtime.invoke(module_with_float_bytes_in_custom_data, {}).has_value(),
                "Runtime treated custom data as contract instructions");

        const Bytes endless_module {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0b, 0x02, 0x60, 0x02, 0x7f, 0x7f, 0x01,
            0x7f, 0x60, 0x00, 0x01, 0x7f, 0x03, 0x03, 0x02, 0x00, 0x01, 0x05, 0x03, 0x01, 0x00, 0x01, 0x07,
            0x28, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x0a, 0x65, 0x78, 0x63, 0x5f,
            0x69, 0x6e, 0x76, 0x6f, 0x6b, 0x65, 0x00, 0x00, 0x0e, 0x65, 0x78, 0x63, 0x5f, 0x72, 0x65, 0x73,
            0x75, 0x6c, 0x74, 0x5f, 0x6c, 0x65, 0x6e, 0x00, 0x01, 0x0a, 0x10, 0x02, 0x09, 0x00, 0x03, 0x40,
            0x0c, 0x00, 0x0b, 0x41, 0x00, 0x0b, 0x04, 0x00, 0x41, 0x00, 0x0b,
        };
        auto endless_result = runtime.invoke(endless_module, {});
        require(!endless_result.has_value()
                    && endless_result.error().error == ExtraChain::Contracts::ExecutionError::InstructionLimit,
                "Runtime did not stop an endless contract");

        const Bytes floating_point_module {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x0e, 0x03, 0x60, 0x02, 0x7f, 0x7f, 0x01, 0x7f,
            0x60, 0x00, 0x01, 0x7f, 0x60, 0x00, 0x00, 0x03, 0x04, 0x03, 0x00, 0x01, 0x02, 0x05, 0x03, 0x01, 0x00,
            0x01, 0x07, 0x28, 0x03, 0x06, 0x6d, 0x65, 0x6d, 0x6f, 0x72, 0x79, 0x02, 0x00, 0x0a, 0x65, 0x78, 0x63,
            0x5f, 0x69, 0x6e, 0x76, 0x6f, 0x6b, 0x65, 0x00, 0x00, 0x0e, 0x65, 0x78, 0x63, 0x5f, 0x72, 0x65, 0x73,
            0x75, 0x6c, 0x74, 0x5f, 0x6c, 0x65, 0x6e, 0x00, 0x01, 0x0a, 0x14, 0x03, 0x04, 0x00, 0x41, 0x00, 0x0b,
            0x04, 0x00, 0x41, 0x00, 0x0b, 0x08, 0x00, 0x43, 0x00, 0x00, 0x00, 0x00, 0x1a, 0x0b,
        };
        const auto floating_point_result = runtime.invoke(floating_point_module, {});
        require(!floating_point_result.has_value()
                    && floating_point_result.error().error == ExtraChain::Contracts::ExecutionError::InvalidModule
                    && floating_point_result.error().detail.find("floating-point") != std::string::npos,
                "Runtime accepted a floating-point instruction");

        const Bytes floating_point_signature {
            0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00, 0x01, 0x05, 0x01, 0x60, 0x00, 0x01, 0x7d,
        };
        const auto floating_point_signature_result = runtime.invoke(floating_point_signature, {});
        require(!floating_point_signature_result.has_value()
                    && floating_point_signature_result.error().error
                           == ExtraChain::Contracts::ExecutionError::InvalidModule
                    && floating_point_signature_result.error().detail.find("floating-point") != std::string::npos,
                "Runtime accepted a floating-point type");
    }

    void test_message_claim(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        Bytes state;
        auto  result = invoke(runtime, module, Alice, "init", empty_arguments(), state);
        require(result.ok, "Message contract init failed");
        state = result.state;

        result = invoke(runtime, module, Alice, "store", string_argument("hello"), state);
        require(result.ok, "Message store failed");
        Reader token_reader(result.data);
        require(token_reader.unsigned_value() == 1, "Message token ID is invalid");
        state = result.state;

        Bytes transfer_arguments;
        append_array(transfer_arguments, 2);
        append_unsigned(transfer_arguments, 1);
        append_string(transfer_arguments, Bob);
        result = invoke(runtime, module, Alice, "transfer", transfer_arguments, state);
        require(result.ok, "Message token transfer failed");
        state = result.state;

        result = invoke(runtime, module, Alice, "redeem", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Previous owner redeemed the message token");

        result = invoke(runtime, module, Bob, "redeem", unsigned_argument(1), state);
        require(result.ok, "Current owner could not redeem the message token");
        Reader message_reader(result.data);
        require(message_reader.string() == "hello", "Redeemed message is invalid");
        state = result.state;

        result = invoke(runtime, module, Bob, "redeem", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Message token was redeemed twice");
        result = invoke(runtime, module, Bob, "owner_of", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Redeemed message token still has an owner");
    }

    void test_assemblyscript(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        const auto initialized = invoke(runtime, module, Alice, "init", empty_arguments(), {});
        require(initialized.ok && !initialized.state.empty(),
                "AssemblyScript contract did not execute ABI 4 initialization");
        const auto unknown = invoke(runtime, module, Alice, "unknown", empty_arguments(), initialized.state);
        require(!unknown.ok && unknown.state == initialized.state,
                "AssemblyScript contract did not preserve state after an unknown method");
        const auto added = invoke(runtime, module, Alice, "add", unsigned_argument(5), initialized.state);
        require(added.ok && added.state != initialized.state,
                "AssemblyScript contract did not save an owner-approved change");
        const auto denied = invoke(runtime, module, Bob, "add", unsigned_argument(1), added.state);
        require(!denied.ok && denied.state == added.state,
                "AssemblyScript ownership component accepted another caller");
    }

    void test_assemblyscript_upgrade(const Bytes &module) {
        ExtraChain::Contracts::ContractManager manager;
        auto deployed = manager.deploy("as-basic", Alice, "generic", module, empty_arguments(), 1);
        require(deployed.has_value(), "AssemblyScript router deployment failed");
        auto upgraded = manager.upgrade("as-basic", Alice, module, empty_arguments(), 2);
        if (!upgraded.has_value()) {
            throw std::runtime_error("AssemblyScript router update failed: " + upgraded.error().detail);
        }
        require(upgraded->version == 2, "AssemblyScript router update version is invalid");
    }

    void test_assemblyscript_dfs_binding(const Bytes &module) {
        ExtraChain::Contracts::ContractManager manager;
        auto deployed = manager.deploy("dfs-contract", "alice", "storage", module, empty_arguments(), 1);
        if (!deployed.has_value()) {
            throw std::runtime_error("AssemblyScript DFS contract deployment failed: " + deployed.error().detail);
        }
        const std::string hash(64, 'a');
        const auto        arguments = dfs_binding_argument("profile/avatar", "file-1", hash, "");
        require(!manager.prepare_call("dfs-contract", "alice", "bind", arguments, 2).has_value(),
                "DFS binding was accepted without a verified file");
        ExtraChain::Contracts::VerifiedInputs verified {
            .dfs = { ExtraChain::Contracts::DfsProof { .file_id      = "file-1",
                                                       .owner_id     = "dfs-contract",
                                                       .content_hash = hash } },
        };
        require(!manager
                     .prepare_call("dfs-contract",
                                   "alice",
                                   "bind",
                                   dfs_binding_argument("../avatar", "file-1", hash, ""),
                                   2,
                                   verified)
                     .has_value(),
                "DFS binding accepted an unsafe logical key");
        auto binding = manager.prepare_call("dfs-contract", "alice", "bind", arguments, 2, verified);
        require(binding.has_value() && manager.commit(std::move(binding.value()), "bind-transaction").has_value(),
                "Verified DFS binding was rejected");
        auto lookup = manager.query("dfs-contract", "alice", "binding", string_argument("profile/avatar"), 2);
        require(lookup.has_value(), "Stored DFS binding could not be read");
        Reader binding_reader(lookup->data);
        require(binding_reader.array() == 2 && binding_reader.string() == "file-1"
                    && binding_reader.string() == hash && binding_reader.empty(),
                "Stored DFS binding changed during state encoding");
        auto tombstone = manager.prepare_call("dfs-contract",
                                              "alice",
                                              "tombstone",
                                              dfs_binding_argument("profile/avatar", "", "", hash),
                                              3);
        require(tombstone.has_value()
                    && manager.commit(std::move(tombstone.value()), "tombstone-transaction").has_value(),
                "DFS binding tombstone was rejected");
        auto removed = manager.query("dfs-contract", "alice", "binding", string_argument("profile/avatar"), 3);
        require(removed.has_value() && removed->data.empty(), "DFS binding remained visible after a tombstone");
    }

    void test_worker_thread(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        auto invocation = std::async(std::launch::async, [&runtime, &module]() {
            Bytes state;
            auto  first = invoke(runtime, module, "alice", "init", empty_arguments(), state);
            if (!first.ok) {
                return first;
            }
            state.clear();
            return invoke(runtime, module, "alice", "init", empty_arguments(), state);
        });
        require(invocation.get().ok, "Repeated WAMR execution failed on a worker thread");
    }

    void test_parallel_worker_threads(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        constexpr int                  WorkerCount    = 4;
        constexpr int                  CallsPerWorker = 20;
        std::vector<std::future<bool>> workers;
        workers.reserve(WorkerCount);
        for (int worker = 0; worker < WorkerCount; ++worker) {
            workers.push_back(std::async(std::launch::async, [&runtime, &module]() {
                for (int call = 0; call < CallsPerWorker; ++call) {
                    Bytes state;
                    if (!invoke(runtime, module, Alice, "init", empty_arguments(), state).ok)
                        return false;
                }
                return true;
            }));
        }
        for (auto &worker : workers) {
            require(worker.get(), "Parallel WAMR execution failed");
        }
    }

    void test_contract_manager(const Bytes &fungible_module, const Bytes &message_module) {
        ExtraChain::Contracts::ContractManager manager;

        auto token =
            manager.deploy("token-contract", Alice, "fungible-token", fungible_module, token_init_argument(), 1);
        require(token.has_value(), "ContractManager token deploy failed");
        auto transfer = manager.call("token-contract", Alice, "transfer", pair_argument(Bob, 200), 2);
        if (!transfer.has_value()) {
            throw std::runtime_error("ContractManager token transfer failed: " + transfer.error().detail);
        }
        require(transfer.value().revision == 2, "ContractManager token transfer revision is invalid");
        auto balance = manager.query("token-contract", Alice, "balance_of", string_argument(Bob), 2);
        require(balance.has_value() && balance->revision == 2, "ContractManager read-only token query failed");
        Reader balance_reader(balance->data);
        require(balance_reader.string() == "200", "ContractManager returned an invalid token balance");
        auto upgrade = manager.upgrade("token-contract", Alice, fungible_module, empty_arguments(), 3);
        require(upgrade.has_value() && upgrade->version == 2, "ContractManager token upgrade failed");
        auto token_record = manager.inspect("token-contract");
        require(token_record.has_value() && token_record->versions.size() == 2,
                "ContractManager did not retain the immutable token version chain");
        auto frozen = manager.call("token-contract", Alice, "transfer", pair_argument(Bob, 799), 4);
        require(frozen.has_value(), "Upgraded token did not enter the one-token freeze state");
        const auto locked = manager.query("token-contract", Alice, "locked_balance_of", string_argument(Alice), 4);
        require(locked.has_value() && Reader(locked->data).string() == "1",
                "Upgraded token did not expose the locked balance");
        auto denied_frozen = manager.call("token-contract", Alice, "transfer", pair_argument(Bob, 1), 5);
        require(!denied_frozen.has_value(), "Upgraded token allowed transfer of the frozen reserve");

        auto legacy = manager.prepare_deploy("legacy-token",
                                             Alice,
                                             "fungible-token",
                                             fungible_module,
                                             token_migration_argument(),
                                             1);
        require(legacy.has_value() && legacy->output.effects.empty(),
                "Legacy token migration emitted a second balance delta");
        require(manager.stage(*legacy).has_value(), "Legacy token migration could not be staged");
        require(manager.commit(std::move(*legacy)).has_value(), "Legacy token migration could not be committed");
        auto legacy_balance = manager.query("legacy-token", Alice, "balance_of", string_argument(Bob), 1);
        require(legacy_balance.has_value(), "Migrated token balance query failed");
        Reader legacy_balance_reader(legacy_balance->data);
        require(legacy_balance_reader.string() == "300", "Migrated token balance changed");

        auto claim = manager.deploy(ClaimContract, Alice, "message-claim", message_module, empty_arguments(), 1);
        require(claim.has_value(), "ContractManager claim deploy failed");
        auto stored = manager.call(ClaimContract, Alice, "store", string_argument("decentralized"), 2);
        if (!stored.has_value()) {
            throw std::runtime_error("ContractManager message store failed: " + stored.error().detail);
        }
        auto stored_record = manager.inspect(ClaimContract);
        require(stored_record.has_value()
                    && contains_text(stored_record->versions.back().revisions.back().state, "decentralized"),
                "ContractManager did not persist the message in contract state");

        Bytes transfer_arguments;
        append_array(transfer_arguments, 2);
        append_unsigned(transfer_arguments, 1);
        append_string(transfer_arguments, Bob);
        auto transferred = manager.call(ClaimContract, Alice, "transfer", transfer_arguments, 3);
        require(transferred.has_value(), "ContractManager message token transfer failed");
        auto denied = manager.call(ClaimContract, Alice, "redeem", unsigned_argument(1), 4);
        require(!denied.has_value(), "ContractManager allowed the previous owner to redeem");
        auto redeemed = manager.call(ClaimContract, Bob, "redeem", unsigned_argument(1), 4);
        require(redeemed.has_value(), "ContractManager current owner redeem failed");
        Reader message_reader(redeemed->data);
        require(message_reader.string() == "decentralized", "ContractManager returned an invalid message");
        auto redeemed_record = manager.inspect(ClaimContract);
        require(redeemed_record.has_value()
                    && !contains_text(redeemed_record->versions.back().revisions.back().state, "decentralized"),
                "ContractManager did not remove the redeemed message from contract state");
        auto replay = manager.call(ClaimContract, Bob, "redeem", unsigned_argument(1), 5);
        require(!replay.has_value(), "ContractManager allowed a second redeem");

        auto target = manager.deploy(TargetContract, Alice, "message-claim", message_module, empty_arguments(), 6);
        require(target.has_value(), "ContractManager target deploy failed");
        auto forwarded =
            manager.call(ClaimContract, Alice, "forward_store", string_pair_argument(TargetContract, "atomic"), 7);
        if (!forwarded.has_value()) {
            throw std::runtime_error("ContractManager cross-contract call failed: " + forwarded.error().detail);
        }
        const auto root_record   = manager.inspect(ClaimContract);
        const auto target_record = manager.inspect(TargetContract);
        require(root_record.has_value() && target_record.has_value()
                    && root_record->versions.back().revisions.back().revision == 5
                    && target_record->versions.back().revisions.back().revision == 2
                    && contains_text(root_record->versions.back().revisions.back().state, "atomic")
                    && contains_text(target_record->versions.back().revisions.back().state, "atomic"),
                "ContractManager did not commit the complete call graph");
        const auto forwarded_owner = manager.query(TargetContract, Alice, "owner_of", unsigned_argument(1), 7);
        require(forwarded_owner.has_value(), "Cross-contract token owner query failed");
        Reader forwarded_owner_reader(forwarded_owner->data);
        require(forwarded_owner_reader.string() == ClaimContract,
                "Cross-contract call used the original sender as direct authority");
        auto cycle =
            manager.call(ClaimContract, Alice, "forward_store", string_pair_argument(ClaimContract, "cycle"), 8);
        require(!cycle.has_value(), "ContractManager accepted a contract call cycle");
    }

    Bytes test_legacy_import(const Bytes &module, std::string_view contract_id) {
        ExtraChain::Contracts::ContractManager manager;
        auto                                   target = manager.deploy(std::string(contract_id),
                                     Alice,
                                     "fungible-token",
                                     module,
                                     migration_target_argument(),
                                     1);
        require(target.has_value(), "Migration target deployment failed");
        const auto ready = manager.query(std::string(contract_id), Alice, "migration_ready", empty_arguments(), 1);
        require(ready.has_value() && Reader(ready->data).boolean(), "Migration target is not inactive and ready");
        const auto name   = manager.query(std::string(contract_id), Alice, "token_name", empty_arguments(), 1);
        const auto symbol = manager.query(std::string(contract_id), Alice, "token_symbol", empty_arguments(), 1);
        const auto decimals =
            manager.query(std::string(contract_id), Alice, "token_decimals", empty_arguments(), 1);
        require(name.has_value() && Reader(name->data).string() == "Legacy Token" && symbol.has_value()
                    && Reader(symbol->data).string() == "LEG" && decimals.has_value()
                    && Reader(decimals->data).unsigned_value() == 0,
                "Migration target metadata queries changed");
        require(!manager.call(std::string(contract_id), Alice, "transfer", pair_argument(Bob, 1), 2).has_value(),
                "Inactive migration target accepted a transfer");
        auto imported =
            manager.call(std::string(contract_id), Alice, "import_legacy", legacy_import_argument(), 2);
        require(imported.has_value(), "Legacy balance import failed");
        const auto source = manager.query(std::string(contract_id), Alice, "legacy_source", empty_arguments(), 2);
        require(source.has_value() && Reader(source->data).string() == "5555555555555555555555555555555555555555",
                "Imported contract did not retain the legacy token ID");
        const auto alice = manager.query(std::string(contract_id), Alice, "balance_of", string_argument(Alice), 2);
        const auto bob   = manager.query(std::string(contract_id), Alice, "balance_of", string_argument(Bob), 2);
        require(alice.has_value() && bob.has_value() && Reader(alice->data).string() == "700"
                    && Reader(bob->data).string() == "300",
                "Imported balances changed");
        require(!manager.call(std::string(contract_id), Alice, "import_legacy", legacy_import_argument(), 3)
                     .has_value(),
                "Legacy state was imported twice");
        const auto record = manager.inspect(std::string(contract_id));
        require(record.has_value(), "Imported contract record is missing");
        return record->versions.back().revisions.back().state;
    }

    void test_language_lock(const Bytes &rust_module, const Bytes &assemblyscript_module) {
        ExtraChain::Contracts::ContractManager rust_manager;
        require(rust_manager.deploy("rust-token", Alice, "fungible-token", rust_module, token_init_argument(), 1)
                    .has_value(),
                "Rust token deployment failed before the language lock test");
        const auto rust_to_as =
            rust_manager.prepare_upgrade("rust-token", Alice, assemblyscript_module, empty_arguments(), 2);
        require(!rust_to_as.has_value()
                    && rust_to_as.error().error == ExtraChain::Contracts::ContractError::UpgradeDenied,
                "A Rust token accepted an AssemblyScript upgrade");

        ExtraChain::Contracts::ContractManager assemblyscript_manager;
        require(assemblyscript_manager
                    .deploy("as-token", Alice, "fungible-token", assemblyscript_module, token_init_argument(), 1)
                    .has_value(),
                "AssemblyScript token deployment failed before the language lock test");
        const auto as_to_rust =
            assemblyscript_manager.prepare_upgrade("as-token", Alice, rust_module, empty_arguments(), 2);
        require(!as_to_rust.has_value()
                    && as_to_rust.error().error == ExtraChain::Contracts::ContractError::UpgradeDenied,
                "An AssemblyScript token accepted a Rust upgrade");
    }

    void benchmark_token_x(ExtraChain::Contracts::WasmRuntime &runtime,
                           const Bytes                        &module,
                           std::string_view                    language) {
        constexpr std::size_t      Samples     = 500;
        const auto                 module_hash = ExtraChain::Contracts::content_hash(module);
        std::vector<std::uint64_t> samples;
        samples.reserve(Samples);
        for (std::size_t index = 0; index < Samples; ++index) {
            const auto started = std::chrono::steady_clock::now();
            auto initialized   = invoke(runtime, module, Alice, "init", token_init_argument(), {}, module_hash);
            require(initialized.ok, "Token X benchmark initialization failed");
            auto migrated =
                invoke(runtime, module, Alice, "migrate", empty_arguments(), initialized.state, module_hash);
            require(migrated.ok, "Token X benchmark migration failed");
            auto frozen =
                invoke(runtime, module, Alice, "transfer", pair_argument(Bob, 999), migrated.state, module_hash);
            require(frozen.ok, "Token X benchmark freeze transfer failed");
            auto denied =
                invoke(runtime, module, Alice, "transfer", pair_argument(Bob, 1), frozen.state, module_hash);
            require(!denied.ok && denied.state == frozen.state, "Token X benchmark freeze rule failed");
            const auto elapsed =
                std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - started);
            samples.push_back(static_cast<std::uint64_t>(elapsed.count()));
        }
        std::ranges::sort(samples);
        const auto percentile = [&](std::size_t numerator) {
            const auto position = std::min(samples.size() - 1, (samples.size() * numerator + 99) / 100 - 1);
            return samples[position];
        };
        const auto total = std::accumulate(samples.begin(), samples.end(), std::uint64_t { 0 });
        std::cout << "BENCHMARK {\"language\":\"" << language << "\",\"samples\":" << Samples
                  << ",\"mean_ns\":" << total / Samples << ",\"p50_ns\":" << percentile(50)
                  << ",\"p95_ns\":" << percentile(95) << ",\"p99_ns\":" << percentile(99) << "}\n";
    }

    void benchmark_parallel_contract_calls(const Bytes &module, std::string_view language) {
        constexpr std::size_t                  Workers        = 4;
        constexpr std::size_t                  CallsPerWorker = 100;
        ExtraChain::Contracts::ContractManager manager;
        std::array<std::string, Workers>       contract_ids;
        for (std::size_t index = 0; index < Workers; ++index) {
            contract_ids[index] = "parallel-token-" + std::to_string(index);
            require(manager
                        .deploy(contract_ids[index],
                                Alice,
                                "fungible-token",
                                module,
                                token_init_argument(),
                                index + 1)
                        .has_value(),
                    "Parallel contract benchmark deployment failed");
        }

        std::atomic<std::size_t> completed = 0;
        std::atomic<bool>        failed    = false;
        const auto               started   = std::chrono::steady_clock::now();
        std::vector<std::thread> workers;
        workers.reserve(Workers);
        for (std::size_t worker = 0; worker < Workers; ++worker) {
            workers.emplace_back([&, worker] {
                for (std::size_t call = 0; call < CallsPerWorker; ++call) {
                    auto result = manager.call(contract_ids[worker],
                                               Alice,
                                               "approve",
                                               pair_argument(Bob, call + 1),
                                               call + Workers + 1);
                    if (!result.has_value()) {
                        failed = true;
                        return;
                    }
                    ++completed;
                }
            });
        }
        for (auto &worker : workers)
            worker.join();
        require(!failed.load(), "Parallel contract benchmark call failed");
        const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        std::cout << "CONTRACT_THROUGHPUT {\"language\":\"" << language << "\",\"workers\":" << Workers
                  << ",\"calls\":" << completed.load() << ",\"elapsed_s\":" << elapsed
                  << ",\"calls_per_s\":" << completed.load() / elapsed << "}\n";
    }

    void test_parallel_contract_conflict(const Bytes &module) {
        ExtraChain::Contracts::ContractManager manager;
        require(manager.deploy("parallel-conflict", Alice, "fungible-token", module, token_init_argument(), 1)
                    .has_value(),
                "Parallel conflict test deployment failed");

        std::array<std::optional<std::expected<ExtraChain::Contracts::ContractReceipt,
                                               ExtraChain::Contracts::ContractFailure>>,
                   2>
                                   results;
        std::barrier               start(2);
        std::array<std::thread, 2> workers;
        for (std::size_t index = 0; index < workers.size(); ++index) {
            workers[index] = std::thread([&, index] {
                start.arrive_and_wait();
                results[index] =
                    manager.call("parallel-conflict", Alice, "approve", pair_argument(Bob, index + 1), 2);
            });
        }
        for (auto &worker : workers)
            worker.join();

        const auto successful = std::ranges::count_if(results, [](const auto &result) {
            return result.has_value() && result->has_value();
        });
        require(successful >= 1, "Both parallel calls failed");
        for (const auto &result : results) {
            require(result.has_value()
                        && (result->has_value()
                            || result->error().error == ExtraChain::Contracts::ContractError::Conflict),
                    "Parallel calls returned an unexpected error");
        }
        const auto record = manager.inspect("parallel-conflict");
        require(record.has_value()
                    && record->versions.back().revisions.back().revision
                           == static_cast<std::uint64_t>(successful + 1),
                "Parallel conflict resolution produced an invalid revision");
    }

    void test_checkpoint_schedule(const Bytes &fungible_module) {
        std::vector<std::string> state_hashes;
        state_hashes.reserve(ExtraChain::Contracts::ContractCheckpointInterval + 1);

        for (int node_index = 0; node_index < 2; ++node_index) {
            ExtraChain::Contracts::ContractManager manager;
            auto                                   deploy = manager.prepare_deploy("checkpoint-contract",
                                                 Alice,
                                                 "fungible-token",
                                                 fungible_module,
                                                 token_init_argument(),
                                                 1);
            require(deploy.has_value() && deploy->checkpoint, "Deploy did not create a checkpoint");
            const auto deploy_hash = deploy->record.versions.back().revisions.back().state_hash;
            if (node_index == 0) {
                state_hashes.push_back(deploy_hash);
            } else {
                require(state_hashes.front() == deploy_hash, "Peer deploy result is not deterministic");
            }
            require(manager.commit(std::move(*deploy), "deploy-transaction").has_value(),
                    "Checkpoint contract deploy failed");

            for (std::uint64_t call_index = 1; call_index <= ExtraChain::Contracts::ContractCheckpointInterval;
                 ++call_index) {
                auto call = manager.prepare_call("checkpoint-contract",
                                                 Alice,
                                                 "approve",
                                                 pair_argument(Bob, call_index),
                                                 call_index + 1);
                require(call.has_value(), "Checkpoint schedule call failed");
                const auto call_hash = call->record.versions.back().revisions.back().state_hash;
                if (node_index == 0) {
                    state_hashes.push_back(call_hash);
                } else {
                    require(state_hashes.at(call_index) == call_hash, "Peer call result is not deterministic");
                }
                const bool expected = call_index == ExtraChain::Contracts::ContractCheckpointInterval;
                require(call->checkpoint == expected, "Checkpoint schedule is incorrect");
                require(manager.commit(std::move(*call), "call-" + std::to_string(call_index)).has_value(),
                        "Checkpoint schedule commit failed");
            }
            const auto record = manager.inspect("checkpoint-contract");
            require(record.has_value(), "Checkpoint contract cannot be inspected");
            const auto &head = record->versions.back().revisions.back();
            require(record->versions.back().revisions.size() == 1
                        && head.revision == ExtraChain::Contracts::ContractCheckpointInterval + 1
                        && head.checkpoint_revision == head.revision
                        && head.checkpoint_transaction_hash
                               == "call-" + std::to_string(ExtraChain::Contracts::ContractCheckpointInterval),
                    "Checkpoint head metadata is incorrect");
        }
    }

    void test_prepare_and_artifact_stage_are_separate(const Bytes &message_module) {
        auto                                   storage = std::make_unique<StageTrackingStorage>();
        auto                                  *tracker = storage.get();
        ExtraChain::Contracts::ContractManager manager(std::move(storage));

        auto deploy = manager.prepare_deploy("staged-contract",
                                             Alice,
                                             "message-claim",
                                             message_module,
                                             empty_arguments(),
                                             1);
        require(deploy.has_value(), "Contract preparation failed");
        require(tracker->stage_count == 0, "Contract preparation published artifacts");
        require(manager.stage(*deploy).has_value() && tracker->stage_count == 1, "Contract artifact stage failed");
        require(manager.commit(std::move(*deploy), "deploy-transaction").has_value(),
                "Staged contract commit failed");
    }

    void test_json_codec() {
        const auto encoded = ExtraChain::Contracts::Codec::encode_json(R"(["hello",7,true,null])");
        require(encoded.has_value(), "JSON arguments could not be encoded");
        const auto decoded = ExtraChain::Contracts::Codec::decode_json(*encoded);
        require(decoded.has_value() && *decoded == R"(["hello",7,true,null])",
                "MessagePack arguments did not round-trip through JSON");

        const auto binary = ExtraChain::Contracts::Codec::encode_json(R"({"$binary":"AQID"})");
        require(binary.has_value(), "Extended JSON binary value could not be encoded");
        const auto binary_json = ExtraChain::Contracts::Codec::decode_json(*binary);
        require(binary_json.has_value() && *binary_json == R"({"$binary":"AQID"})",
                "Extended JSON binary value did not round-trip");
        require(!ExtraChain::Contracts::Codec::encode_json(R"({"$binary":"AQ!D"})").has_value(),
                "Invalid extended JSON binary value was accepted");

        const auto oversized = ExtraChain::Contracts::Codec::encode_json(
            std::string(ExtraChain::Contracts::ExecutionLimits {}.input_bytes + 1, ' '));
        require(!oversized.has_value(), "Oversized JSON arguments were accepted");
    }

    void test_contract_hash() {
        const Bytes value { 'a', 'b', 'c' };
        require(ExtraChain::Contracts::content_hash(value)
                    == "6437b3ac38465133ffb63b75273a8db548c558465d79db03fd359c6cd5bd9d85",
                "Contract content hash is not BLAKE3");
        require(ExtraChain::Contracts::is_standard_token_module(ExtraChain::Contracts::FungibleTokenKind,
                                                                "c23f13167d23eb39f0d6def51cb80f56f3ef1dc1af8fd7466"
                                                                "9277bee48669103")
                    && ExtraChain::Contracts::is_standard_token_module(ExtraChain::Contracts::FungibleTokenKind,
                                                                       "afe321f3e5ff054243bbafd2215fadabb6a0668aef"
                                                                       "0b7899ea7cd0c11561a46b"),
                "Previous standard fungible modules are no longer recognized");
    }

    void test_effect_codec() {
        const std::vector<ExtraChain::Contracts::ContractEffect> effects {
            { .kind      = ExtraChain::Contracts::ContractEffectKind::ContractCall,
              .target    = "child",
              .operation = "store",
              .arguments = { 1, 2 } },
            { .kind      = ExtraChain::Contracts::ContractEffectKind::TokenDelta,
              .target    = "token",
              .operation = "transfer",
              .arguments = { 3, 4 } },
            { .kind      = ExtraChain::Contracts::ContractEffectKind::DfsWrite,
              .target    = "contract",
              .operation = "state.bin",
              .arguments = { 5, 6 } },
        };
        auto encoded = ExtraChain::Contracts::Codec::encode_effects(effects);
        auto decoded = ExtraChain::Contracts::Codec::decode_effects(encoded);
        require(decoded.has_value() && decoded.value().size() == effects.size(),
                "Contract effects did not round-trip");
        for (std::size_t index = 0; index < effects.size(); ++index) {
            require(decoded.value()[index].kind == effects[index].kind
                        && decoded.value()[index].target == effects[index].target
                        && decoded.value()[index].operation == effects[index].operation
                        && decoded.value()[index].arguments == effects[index].arguments,
                    "Contract effect changed during decoding");
        }
        encoded.push_back(0);
        require(!ExtraChain::Contracts::Codec::decode_effects(encoded).has_value(),
                "Contract effect decoder accepted trailing data");
    }

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 8) {
            throw std::runtime_error(
                "Expected Rust and AssemblyScript token modules, message, and AssemblyScript fixture paths");
        }
        ExtraChain::Contracts::WasmRuntime runtime;
        require(runtime.available(), "WAMR is unavailable");
        auto rust_fungible_module = read_file(argv[1]);
        auto as_fungible_module   = read_file(argv[2]);
        auto rust_nft_module      = read_file(argv[3]);
        auto as_nft_module        = read_file(argv[4]);
        auto message_module       = read_file(argv[5]);
        require(ExtraChain::Contracts::module_language(rust_fungible_module).value_or("") == "rust",
                "Rust token language marker is invalid");
        require(ExtraChain::Contracts::module_language(as_fungible_module).value_or("") == "assemblyscript",
                "AssemblyScript token language marker is invalid");
        require(ExtraChain::Contracts::module_language(rust_nft_module).value_or("") == "rust",
                "Rust NFT language marker is invalid");
        require(ExtraChain::Contracts::module_language(as_nft_module).value_or("") == "assemblyscript",
                "AssemblyScript NFT language marker is invalid");
        test_runtime_limits(runtime, rust_fungible_module);
        test_worker_thread(runtime, message_module);
        test_parallel_worker_threads(runtime, message_module);
        test_contract_hash();
        test_effect_codec();
        test_json_codec();
        const auto rust_fungible_state = test_fungible(runtime, rust_fungible_module);
        const auto as_fungible_state   = test_fungible(runtime, as_fungible_module);
        const auto fungible_difference = "Rust and AssemblyScript fungible state encodings differ: rust="
                                         + ExtraChain::Contracts::content_hash(rust_fungible_state)
                                         + " as=" + ExtraChain::Contracts::content_hash(as_fungible_state)
                                         + " sizes=" + std::to_string(rust_fungible_state.size()) + "/"
                                         + std::to_string(as_fungible_state.size());
        require(rust_fungible_state == as_fungible_state, fungible_difference);
        test_u128_boundaries(runtime, rust_fungible_module);
        test_u128_boundaries(runtime, as_fungible_module);
        const auto rust_nft_state = test_nft(runtime, rust_nft_module);
        const auto as_nft_state   = test_nft(runtime, as_nft_module);
        require(rust_nft_state == as_nft_state, "Rust and AssemblyScript NFT state encodings differ");
        test_message_claim(runtime, message_module);
        test_contract_manager(rust_fungible_module, message_module);
        test_contract_manager(as_fungible_module, message_module);
        const auto rust_migration_state = test_legacy_import(rust_fungible_module, "rust-migration-target");
        const auto as_migration_state   = test_legacy_import(as_fungible_module, "as-migration-target");
        require(rust_migration_state == as_migration_state,
                "Rust and AssemblyScript migration target states differ");
        test_language_lock(rust_fungible_module, as_fungible_module);
        benchmark_token_x(runtime, rust_fungible_module, "rust");
        benchmark_token_x(runtime, as_fungible_module, "assemblyscript");
        benchmark_parallel_contract_calls(rust_fungible_module, "rust");
        benchmark_parallel_contract_calls(as_fungible_module, "assemblyscript");
        test_parallel_contract_conflict(rust_fungible_module);
        test_checkpoint_schedule(rust_fungible_module);
        test_checkpoint_schedule(as_fungible_module);
        test_prepare_and_artifact_stage_are_separate(message_module);
        const auto assemblyscript_basic = read_file(argv[6]);
        test_assemblyscript(runtime, assemblyscript_basic);
        test_assemblyscript_upgrade(assemblyscript_basic);
        test_assemblyscript_dfs_binding(read_file(argv[7]));
        std::cout << "Contract runtime tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Contract runtime test failed: " << error.what() << '\n';
        return 1;
    }
}
