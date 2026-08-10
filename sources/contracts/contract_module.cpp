/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 */

#include "contracts/contract_module.h"

#include <algorithm>
#include <array>
#include <limits>
#include <optional>

namespace ExtraChain::Contracts {
    namespace {
        class Reader final {
        public:
            explicit Reader(std::span<const std::uint8_t> input)
                : input_(input) {
            }

            [[nodiscard]] bool empty() const {
                return input_.empty();
            }

            bool byte(std::uint8_t& value) {
                if (input_.empty()) {
                    return false;
                }
                value  = input_.front();
                input_ = input_.subspan(1);
                return true;
            }

            bool unsigned_leb(std::uint32_t& value) {
                std::uint64_t result = 0;
                for (std::uint32_t index = 0; index < 5; ++index) {
                    std::uint8_t current = 0;
                    if (!byte(current)) {
                        return false;
                    }
                    result |= static_cast<std::uint64_t>(current & 0x7f) << (index * 7);
                    if ((current & 0x80) == 0) {
                        if (result > std::numeric_limits<std::uint32_t>::max()) {
                            return false;
                        }
                        value = static_cast<std::uint32_t>(result);
                        return true;
                    }
                }
                return false;
            }

            bool take(std::size_t size, Reader& output) {
                if (size > input_.size()) {
                    return false;
                }
                output = Reader(input_.first(size));
                input_ = input_.subspan(size);
                return true;
            }

            bool string(std::string& output) {
                std::uint32_t size = 0;
                Reader        value({});
                if (!unsigned_leb(size) || !take(size, value)) {
                    return false;
                }
                output.assign(reinterpret_cast<const char*>(value.input_.data()), value.input_.size());
                value.input_ = {};
                return true;
            }

            std::string remaining_string() {
                std::string result(reinterpret_cast<const char*>(input_.data()), input_.size());
                input_ = {};
                return result;
            }

        private:
            std::span<const std::uint8_t> input_;
        };
    } // namespace

    std::expected<std::string, std::string> module_language(std::span<const std::uint8_t> module) {
        static constexpr std::array<std::uint8_t, 8> Header { 0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00 };
        if (module.size() < Header.size() || !std::equal(Header.begin(), Header.end(), module.begin())) {
            return std::unexpected("Contract module is not valid WebAssembly");
        }

        Reader                     input(module.subspan(Header.size()));
        std::optional<std::string> found;
        while (!input.empty()) {
            std::uint8_t  section_id   = 0;
            std::uint32_t section_size = 0;
            Reader        section({});
            if (!input.byte(section_id) || !input.unsigned_leb(section_size)
                || !input.take(section_size, section)) {
                return std::unexpected("Contract module has an invalid section");
            }
            if (section_id != 0) {
                continue;
            }
            std::string name;
            if (!section.string(name)) {
                return std::unexpected("Contract module has an invalid custom section");
            }
            if (name != ContractLanguageSection) {
                continue;
            }
            if (found.has_value()) {
                return std::unexpected("Contract module has more than one language section");
            }
            auto language = section.remaining_string();
            if (language != "rust" && language != "assemblyscript") {
                return std::unexpected("Contract module declares an unsupported language");
            }
            found = std::move(language);
        }
        if (!found.has_value()) {
            return std::unexpected("Contract module does not declare its language");
        }
        return found.value();
    }

} // namespace ExtraChain::Contracts
