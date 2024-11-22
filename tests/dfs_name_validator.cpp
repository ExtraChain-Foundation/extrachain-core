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

#include "dfs/name_validator.h"
#include "utils/exc_logs.h"
#include <vector>
#include <string>
#include <string_view>

struct TestCase {
    std::string_view         name;
    bool                     should_pass;
    NameValidator::ErrorCode expected_error;
    size_t                   expected_position;
};

class NameValidatorTests {
private:
    size_t passed_tests = 0;
    size_t total_tests  = 0;

    static std::string repeat(char c, size_t count) {
        return std::string(count, c);
    }

    void run_test(const TestCase& test) {
        ++total_tests;
        auto result = NameValidator::validate(test.name);

        if (test.should_pass && result.has_value()) {
            eInfo("PASS: Valid name '{}'", test.name);
            ++passed_tests;
            return;
        }

        if (!test.should_pass && !result.has_value()) {
            const auto& error            = result.error();
            bool        error_matches    = error.code == test.expected_error;
            bool        position_matches = error.position == test.expected_position;

            if (error_matches && position_matches) {
                eInfo("PASS: Invalid name '{}': error code {} at position {}",
                      test.name,
                      static_cast<int>(error.code),
                      error.position);
                ++passed_tests;
                return;
            }

            eWarning("FAIL: Name '{}': Expected error {} at position {}, got {} at position {}",
                     test.name,
                     static_cast<int>(test.expected_error),
                     test.expected_position,
                     static_cast<int>(error.code),
                     error.position);
            return;
        }

        eWarning("FAIL: Name '{}': Expected {}, but got {}",
                 test.name,
                 test.should_pass ? "success" : "failure",
                 test.should_pass ? "failure" : "success");
    }

public:
    void run_all_tests() {
        const std::string max_length_str = repeat('a', 255);
        const std::string too_long_str   = repeat('a', 256);

        const std::vector<TestCase> test_cases = {
            { "normal.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "with-dash.doc", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "with_underscore.pdf", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "with.multiple.dots.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "with numbers123.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "a", true, NameValidator::ErrorCode::EmptyName, 0 },
            { max_length_str, true, NameValidator::ErrorCode::EmptyName, 0 },

            { "", false, NameValidator::ErrorCode::EmptyName, 0 },
            { too_long_str, false, NameValidator::ErrorCode::TooLong, 255 },

            { "file<.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file>.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file:.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file\".txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file/.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file\\.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file|.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file?.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "file*.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },

            { "file\x01.txt", false, NameValidator::ErrorCode::ControlChar, 4 },
            { "file\x1F.txt", false, NameValidator::ErrorCode::ControlChar, 4 },

            { ".file.txt", false, NameValidator::ErrorCode::LeadingDotSpace, 0 },
            { " file.txt", false, NameValidator::ErrorCode::LeadingDotSpace, 0 },
            { "file.txt.", false, NameValidator::ErrorCode::TrailingDotSpace, 8 },
            { "file.txt ", false, NameValidator::ErrorCode::TrailingDotSpace, 8 },

            { "file..txt", false, NameValidator::ErrorCode::ConsecutiveDots, 4 },
            { "file...txt", false, NameValidator::ErrorCode::ConsecutiveDots, 4 },

            { "CON", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "con", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "Con", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "PRN", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "AUX", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "NUL", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "COM1", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "COM9", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "LPT1", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "LPT9", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "COM1.txt", false, NameValidator::ErrorCode::ReservedName, 0 },
            { "LPT4.doc", false, NameValidator::ErrorCode::ReservedName, 0 },

            { std::string_view { "file\0.txt", 9 }, false, NameValidator::ErrorCode::NullByte, 4 },

            { "COM", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "COM0", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "COM10", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "COMM1", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "CONMAN", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "AUXILIARY", true, NameValidator::ErrorCode::EmptyName, 0 },

            { "COM1<.txt", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { "COM1<", false, NameValidator::ErrorCode::InvalidChar, 4 },
            { ".COM1", false, NameValidator::ErrorCode::LeadingDotSpace, 0 },

            { "привет.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "您好.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "😊.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "COПrn.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "СОМ1.txt", true, NameValidator::ErrorCode::EmptyName, 0 },
            { "привет<мир.txt", false, NameValidator::ErrorCode::InvalidChar, 6 },
        };

        for (const auto& test : test_cases) {
            run_test(test);
        }

        eInfo("Test results: {}/{} passed ({}%)", passed_tests, total_tests, (passed_tests * 100) / total_tests);
    }

    [[nodiscard]] bool all_passed() const noexcept {
        return passed_tests == total_tests;
    }

    [[nodiscard]] size_t get_total() const noexcept {
        return total_tests;
    }

    [[nodiscard]] size_t get_passed() const noexcept {
        return passed_tests;
    }
};

int main() {
    Logger::instance().set_debug(true);
    NameValidatorTests tests;
    tests.run_all_tests();
    return tests.all_passed() ? 0 : 1;
}
