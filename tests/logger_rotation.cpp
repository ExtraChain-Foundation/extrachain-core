#include "utils/exc_logs.h"
#include "test_support.h"

#include <filesystem>
#include <fstream>
#include <random>
#include <regex>
#include <string>
#include <thread>
#include <vector>

int main() {
    const auto original  = std::filesystem::current_path();
    const auto directory = std::filesystem::temp_directory_path()
                           / ("extrachain-logger-rotation-" + std::to_string(std::random_device {}()));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);

    constexpr std::uintmax_t PieceBytes  = 64 * 1024;
    constexpr std::uintmax_t BudgetBytes = 256 * 1024;
    constexpr int            Threads     = 8;
    constexpr int            Lines       = 2000;
    Logger::instance().set_rotation_limits(PieceBytes, BudgetBytes);
    Logger::start_file("rotation");
    {
        std::vector<std::jthread> writers;
        for (int writer = 0; writer < Threads; ++writer) {
            writers.emplace_back([writer] {
                const std::string payload(200, static_cast<char>('a' + writer));
                for (int line = 0; line < Lines; ++line) {
                    Logger::instance().write_to_file(fmt::format("writer {} line {} {} end\n", writer, line, payload));
                }
            });
        }
    }
    Logger::stop_file();

    // Every kept line is whole: one writer's payload, never another writer's bytes in between.
    const std::regex whole(R"(writer ([0-9]) line [0-9]+ ([a-h])\2{199} end)");
    std::uintmax_t   total  = 0;
    std::size_t      pieces = 0;
    std::size_t      kept   = 0;
    for (const auto& entry : std::filesystem::directory_iterator("logs")) {
        if (entry.path().extension() != ".log")
            continue;
        ++pieces;
        const auto size = entry.file_size();
        total += size;
        TEST_REQUIRE(size < PieceBytes + 1024);
        std::ifstream file(entry.path());
        for (std::string line; std::getline(file, line); ++kept) {
            std::smatch match;
            TEST_REQUIRE(std::regex_match(line, match, whole));
            TEST_REQUIRE_EQ(match[2].str().front(), static_cast<char>('a' + std::stoi(match[1].str())));
        }
    }
    TEST_REQUIRE(pieces >= 3);
    TEST_REQUIRE(total <= BudgetBytes + PieceBytes + 1024);
    TEST_REQUIRE(kept > 0);

    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
