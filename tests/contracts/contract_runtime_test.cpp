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

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

    using Bytes = std::vector<std::uint8_t>;

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
        append_string(result, value);
        return result;
    }

    Bytes unsigned_argument(std::uint64_t value) {
        Bytes result;
        append_unsigned(result, value);
        return result;
    }

    Bytes pair_argument(std::string_view address, std::uint64_t amount) {
        Bytes result;
        append_array(result, 2);
        append_string(result, address);
        append_unsigned(result, amount);
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
        append_array(result, 4);
        append_string(result, "Example Token");
        append_string(result, "EXT");
        append_unsigned(result, 8);
        append_unsigned(result, 1'000);
        return result;
    }

    Bytes request(std::string_view              sender,
                  std::string_view              method,
                  std::span<const std::uint8_t> arguments,
                  std::span<const std::uint8_t> state) {
        Bytes result;
        append_array(result, 6);
        append_string(result, sender);
        append_string(result, method);
        append_binary(result, arguments);
        append_binary(result, state);
        append_unsigned(result, 1);
        append_unsigned(result, 1);
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

        void optional_string() {
            if (peek() == 0xc0) {
                byte();
            } else {
                static_cast<void>(string());
            }
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
        bool  ok;
        Bytes state;
        Bytes data;
    };

    Response response(std::span<const std::uint8_t> source) {
        Reader reader(source);
        if (reader.array() != 5) {
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
        reader.optional_string();
        return result;
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
                    const Bytes                        &state) {
        auto input  = request(sender, method, arguments, state);
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

    void test_fungible(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        Bytes state;
        auto  result = invoke(runtime, module, "alice", "init", token_init_argument(), state);
        require(result.ok, "Token init failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "transfer", pair_argument("bob", 250), state);
        require(result.ok, "Token transfer failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "approve", pair_argument("carol", 100), state);
        require(result.ok, "Token approval failed");
        state = result.state;

        result =
            invoke(runtime, module, "carol", "transfer_from", transfer_from_argument("alice", "dave", 75), state);
        require(result.ok, "Approved token transfer failed");
        state = result.state;

        result = invoke(runtime, module, "dave", "burn", unsigned_argument(25), state);
        require(result.ok, "Token burn failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "mint", pair_argument("bob", 50), state);
        require(result.ok, "Owner mint failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "revoke_mint", {}, state);
        require(result.ok, "Mint revoke failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "mint", pair_argument("bob", 1), state);
        require(!result.ok && result.state == state, "Mint worked after permanent revoke");
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
    }

    void test_message_claim(ExtraChain::Contracts::WasmRuntime &runtime, const Bytes &module) {
        Bytes state;
        auto  result = invoke(runtime, module, "alice", "init", {}, state);
        require(result.ok, "Message contract init failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "store", string_argument("hello"), state);
        require(result.ok, "Message store failed");
        Reader token_reader(result.data);
        require(token_reader.unsigned_value() == 1, "Message token ID is invalid");
        state = result.state;

        Bytes transfer_arguments;
        append_array(transfer_arguments, 2);
        append_unsigned(transfer_arguments, 1);
        append_string(transfer_arguments, "bob");
        result = invoke(runtime, module, "alice", "transfer", transfer_arguments, state);
        require(result.ok, "Message token transfer failed");
        state = result.state;

        result = invoke(runtime, module, "alice", "redeem", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Previous owner redeemed the message token");

        result = invoke(runtime, module, "bob", "redeem", unsigned_argument(1), state);
        require(result.ok, "Current owner could not redeem the message token");
        Reader message_reader(result.data);
        require(message_reader.string() == "hello", "Redeemed message is invalid");
        state = result.state;

        result = invoke(runtime, module, "bob", "redeem", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Message token was redeemed twice");
        result = invoke(runtime, module, "bob", "owner_of", unsigned_argument(1), state);
        require(!result.ok && result.state == state, "Redeemed message token still has an owner");
    }

    void test_contract_manager(const Bytes &fungible_module, const Bytes &message_module) {
        ExtraChain::Contracts::ContractManager manager;

        auto token =
            manager.deploy("token-contract", "alice", "fungible", fungible_module, token_init_argument(), 1);
        require(token.has_value(), "ContractManager token deploy failed");
        auto transfer = manager.call("token-contract", "alice", "transfer", pair_argument("bob", 200), 2);
        require(transfer.has_value() && transfer->revision == 2, "ContractManager token transfer failed");
        auto balance = manager.query("token-contract", "alice", "balance_of", string_argument("bob"), 2);
        require(balance.has_value() && balance->revision == 2, "ContractManager read-only token query failed");
        Reader balance_reader(balance->data);
        require(balance_reader.unsigned_value() == 200, "ContractManager returned an invalid token balance");
        auto upgrade = manager.upgrade("token-contract", "alice", fungible_module, {}, 3);
        require(upgrade.has_value() && upgrade->version == 2, "ContractManager token upgrade failed");
        auto token_record = manager.inspect("token-contract");
        require(token_record.has_value() && token_record->versions.size() == 2,
                "ContractManager did not retain the immutable token version chain");

        auto claim = manager.deploy("claim-contract", "alice", "message-claim", message_module, {}, 1);
        require(claim.has_value(), "ContractManager claim deploy failed");
        auto stored = manager.call("claim-contract", "alice", "store", string_argument("decentralized"), 2);
        require(stored.has_value(), "ContractManager message store failed");
        auto stored_record = manager.inspect("claim-contract");
        require(stored_record.has_value()
                    && contains_text(stored_record->versions.back().revisions.back().state, "decentralized"),
                "ContractManager did not persist the message in contract state");

        Bytes transfer_arguments;
        append_array(transfer_arguments, 2);
        append_unsigned(transfer_arguments, 1);
        append_string(transfer_arguments, "bob");
        auto transferred = manager.call("claim-contract", "alice", "transfer", transfer_arguments, 3);
        require(transferred.has_value(), "ContractManager message token transfer failed");
        auto denied = manager.call("claim-contract", "alice", "redeem", unsigned_argument(1), 4);
        require(!denied.has_value(), "ContractManager allowed the previous owner to redeem");
        auto redeemed = manager.call("claim-contract", "bob", "redeem", unsigned_argument(1), 4);
        require(redeemed.has_value(), "ContractManager current owner redeem failed");
        Reader message_reader(redeemed->data);
        require(message_reader.string() == "decentralized", "ContractManager returned an invalid message");
        auto redeemed_record = manager.inspect("claim-contract");
        require(redeemed_record.has_value()
                    && !contains_text(redeemed_record->versions.back().revisions.back().state, "decentralized"),
                "ContractManager did not remove the redeemed message from contract state");
        auto replay = manager.call("claim-contract", "bob", "redeem", unsigned_argument(1), 5);
        require(!replay.has_value(), "ContractManager allowed a second redeem");
    }

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc != 3) {
            throw std::runtime_error("Expected fungible and message contract module paths");
        }
        ExtraChain::Contracts::WasmRuntime runtime;
        require(runtime.available(), "WAMR is unavailable");
        auto fungible_module = read_file(argv[1]);
        auto message_module  = read_file(argv[2]);
        test_runtime_limits(runtime, fungible_module);
        test_fungible(runtime, fungible_module);
        test_message_claim(runtime, message_module);
        test_contract_manager(fungible_module, message_module);
        std::cout << "Contract runtime tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "Contract runtime test failed: " << error.what() << '\n';
        return 1;
    }
}
