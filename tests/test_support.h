#pragma once

#include <cstdio>
#include <exception>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace TestSupport {

    class Failure final : public std::runtime_error {
    public:
        explicit Failure(std::string message)
            : std::runtime_error(std::move(message)) {
        }
    };

    inline void require(bool             condition,
                        std::string_view expression,
                        std::string_view file,
                        int              line,
                        std::string_view message = {}) {
        if (condition) {
            return;
        }

        std::string error = std::string(file) + ":" + std::to_string(line) + ": " + std::string(expression);
        if (!message.empty()) {
            error += ": ";
            error += message;
        }
        throw Failure(std::move(error));
    }

    class Runner final {
    public:
        template <typename Test>
        void run(std::string_view name, Test&& test) {
            try {
                std::forward<Test>(test)();
                ++passed_;
                std::printf("[PASS] %.*s\n", static_cast<int>(name.size()), name.data());
            } catch (const std::exception& error) {
                ++failed_;
                std::fprintf(stderr,
                             "[FAIL] %.*s: %s\n",
                             static_cast<int>(name.size()),
                             name.data(),
                             error.what());
            } catch (...) {
                ++failed_;
                std::fprintf(stderr,
                             "[FAIL] %.*s: unknown exception\n",
                             static_cast<int>(name.size()),
                             name.data());
            }
        }

        [[nodiscard]] int result() const {
            std::printf("Tests: %d passed, %d failed\n", passed_, failed_);
            return failed_ == 0 ? 0 : 1;
        }

    private:
        int passed_ = 0;
        int failed_ = 0;
    };

} // namespace TestSupport

#define TEST_REQUIRE(condition)                                                                                   \
    ::TestSupport::require(static_cast<bool>(condition), #condition, __FILE__, __LINE__)
#define TEST_REQUIRE_MESSAGE(condition, message)                                                                  \
    ::TestSupport::require(static_cast<bool>(condition), #condition, __FILE__, __LINE__, message)
#define TEST_REQUIRE_EQ(actual, expected)                                                                         \
    ::TestSupport::require(static_cast<bool>((actual) == (expected)), #actual " == " #expected, __FILE__, __LINE__)
