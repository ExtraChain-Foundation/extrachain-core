/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "wasm_policy.h"

#include <algorithm>
#include <array>
#include <limits>

namespace ExtraChain::Contracts::Internal {
    namespace {

        constexpr std::uint8_t F64Type = 0x7c;
        constexpr std::uint8_t F32Type = 0x7d;

        class Reader final {
        public:
            explicit Reader(std::span<const std::uint8_t> bytes)
                : bytes_(bytes) {
            }

            [[nodiscard]] bool empty() const {
                return bytes_.empty();
            }

            bool byte(std::uint8_t &value) {
                if (bytes_.empty()) {
                    return false;
                }
                value  = bytes_.front();
                bytes_ = bytes_.subspan(1);
                return true;
            }

            bool unsigned_leb(std::uint32_t &value) {
                std::uint64_t decoded = 0;
                for (std::uint32_t index = 0; index < 5; ++index) {
                    std::uint8_t current = 0;
                    if (!byte(current)) {
                        return false;
                    }
                    decoded |= static_cast<std::uint64_t>(current & 0x7f) << (index * 7);
                    if ((current & 0x80) == 0) {
                        if (decoded > std::numeric_limits<std::uint32_t>::max()) {
                            return false;
                        }
                        value = static_cast<std::uint32_t>(decoded);
                        return true;
                    }
                }
                return false;
            }

            bool signed_leb(std::uint32_t maximum_bytes) {
                for (std::uint32_t index = 0; index < maximum_bytes; ++index) {
                    std::uint8_t current = 0;
                    if (!byte(current)) {
                        return false;
                    }
                    if ((current & 0x80) == 0) {
                        return true;
                    }
                }
                return false;
            }

            bool take(std::size_t length, Reader &result) {
                if (length > bytes_.size()) {
                    return false;
                }
                result = Reader(bytes_.first(length));
                bytes_ = bytes_.subspan(length);
                return true;
            }

            bool skip(std::size_t length) {
                Reader ignored({});
                return take(length, ignored);
            }

            bool name() {
                std::uint32_t length = 0;
                return unsigned_leb(length) && skip(length);
            }

        private:
            std::span<const std::uint8_t> bytes_;
        };

        class Validator final {
        public:
            WasmPolicyResult validate(std::span<const std::uint8_t> module) {
                static constexpr std::array<std::uint8_t, 8> Header { 0x00, 0x61, 0x73, 0x6d,
                                                                      0x01, 0x00, 0x00, 0x00 };
                if (module.size() < Header.size() || !std::equal(Header.begin(), Header.end(), module.begin())) {
                    return WasmPolicyResult::Invalid;
                }

                Reader input(module.subspan(Header.size()));
                while (!input.empty()) {
                    std::uint8_t  section_id   = 0;
                    std::uint32_t section_size = 0;
                    Reader        section({});
                    if (!input.byte(section_id) || !input.unsigned_leb(section_size)
                        || !input.take(section_size, section)) {
                        return invalid();
                    }

                    bool valid = true;
                    switch (section_id) {
                    case 0:
                        break;
                    case 1:
                        valid = types(section);
                        break;
                    case 2:
                        valid = imports(section);
                        break;
                    case 6:
                        valid = globals(section);
                        break;
                    case 10:
                        valid = code(section);
                        break;
                    default:
                        break;
                    }
                    if (!valid) {
                        return result_;
                    }
                    const bool scanned = section_id == 1 || section_id == 2 || section_id == 6 || section_id == 10;
                    if (scanned && !section.empty()) {
                        return invalid();
                    }
                }
                return result_;
            }

        private:
            bool value_type(Reader &input) {
                std::uint8_t type = 0;
                if (!input.byte(type)) {
                    return fail();
                }
                if (type == F32Type || type == F64Type) {
                    return floating_point();
                }
                return true;
            }

            bool types(Reader &input) {
                std::uint32_t count = 0;
                if (!input.unsigned_leb(count)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint8_t marker = 0;
                    if (!input.byte(marker) || marker != 0x60 || !value_type_vector(input)
                        || !value_type_vector(input)) {
                        return fail();
                    }
                }
                return true;
            }

            bool value_type_vector(Reader &input) {
                std::uint32_t count = 0;
                if (!input.unsigned_leb(count)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < count; ++index) {
                    if (!value_type(input)) {
                        return false;
                    }
                }
                return true;
            }

            bool imports(Reader &input) {
                std::uint32_t count = 0;
                if (!input.unsigned_leb(count)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint8_t  kind    = 0;
                    std::uint32_t ignored = 0;
                    if (!input.name() || !input.name() || !input.byte(kind)) {
                        return fail();
                    }
                    switch (kind) {
                    case 0:
                        if (!input.unsigned_leb(ignored)) {
                            return fail();
                        }
                        break;
                    case 1:
                        if (!input.skip(1) || !limits(input)) {
                            return fail();
                        }
                        break;
                    case 2:
                        if (!limits(input)) {
                            return fail();
                        }
                        break;
                    case 3:
                        if (!value_type(input) || !input.skip(1)) {
                            return fail();
                        }
                        break;
                    default:
                        return fail();
                    }
                }
                return true;
            }

            bool limits(Reader &input) {
                std::uint32_t flags = 0;
                std::uint32_t value = 0;
                if (!input.unsigned_leb(flags) || flags > 1 || !input.unsigned_leb(value)) {
                    return fail();
                }
                if ((flags & 1) != 0 && !input.unsigned_leb(value)) {
                    return fail();
                }
                return true;
            }

            bool globals(Reader &input) {
                std::uint32_t count = 0;
                if (!input.unsigned_leb(count)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < count; ++index) {
                    if (!value_type(input) || !input.skip(1) || !constant_expression(input)) {
                        return false;
                    }
                }
                return true;
            }

            bool constant_expression(Reader &input) {
                std::uint8_t opcode = 0;
                if (!input.byte(opcode)) {
                    return fail();
                }
                switch (opcode) {
                case 0x23:
                case 0xd2: {
                    std::uint32_t ignored = 0;
                    if (!input.unsigned_leb(ignored)) {
                        return fail();
                    }
                    break;
                }
                case 0x41:
                    if (!input.signed_leb(5)) {
                        return fail();
                    }
                    break;
                case 0x42:
                    if (!input.signed_leb(10)) {
                        return fail();
                    }
                    break;
                case 0x43:
                case 0x44:
                    return floating_point();
                case 0xd0:
                    if (!input.signed_leb(5)) {
                        return fail();
                    }
                    break;
                default:
                    return fail();
                }
                std::uint8_t end = 0;
                if (!input.byte(end) || end != 0x0b) {
                    return fail();
                }
                return true;
            }

            bool code(Reader &input) {
                std::uint32_t count = 0;
                if (!input.unsigned_leb(count)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < count; ++index) {
                    std::uint32_t body_size = 0;
                    Reader        body({});
                    if (!input.unsigned_leb(body_size) || !input.take(body_size, body) || !function(body)) {
                        return false;
                    }
                }
                return true;
            }

            bool function(Reader &input) {
                std::uint32_t local_groups = 0;
                if (!input.unsigned_leb(local_groups)) {
                    return fail();
                }
                for (std::uint32_t index = 0; index < local_groups; ++index) {
                    std::uint32_t count = 0;
                    if (!input.unsigned_leb(count) || !value_type(input)) {
                        return false;
                    }
                }

                std::uint32_t depth = 1;
                while (!input.empty()) {
                    std::uint8_t opcode = 0;
                    if (!input.byte(opcode) || !instruction(input, opcode, depth)) {
                        return false;
                    }
                    if (depth == 0) {
                        return input.empty() || fail();
                    }
                }
                return fail();
            }

            bool instruction(Reader &input, std::uint8_t opcode, std::uint32_t &depth) {
                if (opcode == 0x02 || opcode == 0x03 || opcode == 0x04) {
                    std::uint8_t block_type = 0;
                    if (!input.byte(block_type)) {
                        return fail();
                    }
                    if (block_type == F32Type || block_type == F64Type) {
                        return floating_point();
                    }
                    if ((block_type & 0x80) != 0 && !input.signed_leb(4)) {
                        return fail();
                    }
                    ++depth;
                    return true;
                }
                if (opcode == 0x0b) {
                    if (depth == 0) {
                        return fail();
                    }
                    --depth;
                    return true;
                }
                if (opcode == 0x0c || opcode == 0x0d || opcode == 0x10 || (opcode >= 0x20 && opcode <= 0x26)
                    || opcode == 0xd2) {
                    return unsigned_immediate(input);
                }
                if (opcode == 0x0e) {
                    std::uint32_t count = 0;
                    if (!input.unsigned_leb(count)) {
                        return fail();
                    }
                    for (std::uint32_t index = 0; index < count; ++index) {
                        if (!unsigned_immediate(input)) {
                            return false;
                        }
                    }
                    return unsigned_immediate(input);
                }
                if (opcode == 0x11) {
                    return unsigned_immediate(input) && unsigned_immediate(input);
                }
                if (opcode == 0x1c) {
                    return value_type_vector(input);
                }
                if (opcode >= 0x28 && opcode <= 0x3e) {
                    if (opcode == 0x2a || opcode == 0x2b || opcode == 0x38 || opcode == 0x39) {
                        return floating_point();
                    }
                    return unsigned_immediate(input) && unsigned_immediate(input);
                }
                if (opcode == 0x3f || opcode == 0x40) {
                    return unsigned_immediate(input);
                }
                if (opcode == 0x41) {
                    if (!input.signed_leb(5)) {
                        return fail();
                    }
                    return true;
                }
                if (opcode == 0x42) {
                    if (!input.signed_leb(10)) {
                        return fail();
                    }
                    return true;
                }
                if (opcode == 0x43 || opcode == 0x44 || (opcode >= 0x5b && opcode <= 0x66)
                    || (opcode >= 0x8b && opcode <= 0xa6) || (opcode >= 0xa8 && opcode <= 0xab)
                    || (opcode >= 0xae && opcode <= 0xbf)) {
                    return floating_point();
                }
                if ((opcode >= 0x45 && opcode <= 0x5a) || (opcode >= 0x67 && opcode <= 0x8a)
                    || (opcode >= 0xa7 && opcode <= 0xad) || (opcode >= 0xc0 && opcode <= 0xc4)) {
                    return true;
                }
                if (opcode == 0xd0) {
                    if (!input.signed_leb(5)) {
                        return fail();
                    }
                    return true;
                }
                if (opcode == 0xfc) {
                    std::uint32_t subopcode = 0;
                    if (!input.unsigned_leb(subopcode)) {
                        return fail();
                    }
                    return subopcode <= 7 ? floating_point() : fail();
                }
                if (opcode == 0x00 || opcode == 0x01 || opcode == 0x05 || opcode == 0x0f || opcode == 0x1a
                    || opcode == 0x1b || opcode == 0xd1) {
                    return true;
                }
                return fail();
            }

            bool unsigned_immediate(Reader &input) {
                std::uint32_t ignored = 0;
                if (!input.unsigned_leb(ignored)) {
                    return fail();
                }
                return true;
            }

            bool fail() {
                if (result_ == WasmPolicyResult::Accepted) {
                    result_ = WasmPolicyResult::Invalid;
                }
                return false;
            }

            bool floating_point() {
                result_ = WasmPolicyResult::FloatingPoint;
                return false;
            }

            WasmPolicyResult invalid() {
                fail();
                return result_;
            }

            WasmPolicyResult result_ = WasmPolicyResult::Accepted;
        };

    } // namespace

    WasmPolicyResult validate_wasm_policy(std::span<const std::uint8_t> module) {
        return Validator {}.validate(module);
    }

} // namespace ExtraChain::Contracts::Internal
