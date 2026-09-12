"""Actual Logger/file-filter regressions with synthetic paths and formatting."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LoggerTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++23 compiler is required')
        source_root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (source_root / 'headers/utils/exc_logs.h').read_text(encoding='utf-8')
        start = source.index('class Logger {')
        helper_start = source.find('namespace detail {', 0, start)
        helper = source[helper_start:source.index('enum class LogLevel', helper_start)] if helper_start >= 0 else ''
        logger = source[start:source.index('\nnamespace detail {', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='logger-regression-')
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder = Path(cls.temporary.name)
        cpp = cls.folder / 'fixture.cpp'
        cls.binary = cls.folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(r'''
#include <atomic>
#include <barrier>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include "utils/exc_logs_filter.h"
#ifdef _WIN32
#include <windows.h>
#endif
namespace fmt {
template<class... T> std::string format(std::string_view, const std::string& name, T&&...) { return name + ".log"; }
template<class... T> void println(std::string_view, T&&...) {}
}
enum class PathError { None };
bool reject_logging_helpers = false;
struct PathValue {
    std::filesystem::path path;
    bool exists() const { return std::filesystem::exists(path); }
    auto native() const { return path; }
};
struct PathResult {
    PathValue value;
    explicit operator bool() const { return true; }
    PathValue* operator->() { return &value; }
    PathError error() const { return PathError::None; }
};
struct FsPath {
    static PathResult create(const std::string& path) {
        if (reject_logging_helpers) { throw std::runtime_error("logging-capable path helper entered"); }
        return {{path}};
    }
};
''' + helper + logger + r'''
void require(bool value) { if (!value) { throw std::runtime_error("logger regression failed"); } }
std::string row(int worker, int index, int size) {
    return std::to_string(worker) + ":" + std::to_string(index) + ":" + std::string(size, char('a' + worker)) + "\n";
}
void writers(int workers, int count, int size, bool toggle) {
    Logger::start_file("synthetic");
    std::barrier start(workers + (toggle ? 1 : 0));
    std::vector<std::vector<int>> successes(workers);
    std::vector<std::thread> threads;
    for (int w = 0; w < workers; ++w) {
        threads.emplace_back([&, w] {
            start.arrive_and_wait();
            for (int i = 0; i < count; ++i) {
                if (Logger::instance().write_to_file(row(w, i, size))) { successes[w].push_back(i); }
                Logger::instance().is_file_output();
            }
        });
    }
    if (toggle) {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < 200; ++i) { Logger::stop_file(); Logger::start_file("synthetic"); }
        });
    }
    for (auto& thread : threads) { thread.join(); }
    Logger::stop_file();
    std::set<std::string> expected;
    for (int w = 0; w < workers; ++w) {
        if (!toggle) { require(successes[w].size() == static_cast<size_t>(count)); }
        for (int i : successes[w]) { auto text = row(w, i, size); text.pop_back(); expected.insert(text); }
    }
    require(!expected.empty());
    std::ifstream input("logs/synthetic.log");
    std::string line;
    while (std::getline(input, line)) { require(expected.erase(line) == 1); }
    require(expected.empty());
}
void cleanup() {
    using namespace std::chrono;
    Logger::start_file("synthetic");
    require(Logger::instance().write_to_file(std::string("first\n")));
    { std::ofstream old("logs/old.log"); old << "inactive\n"; }
    const auto aged = std::filesystem::file_time_type::clock::now() - hours(24 * 8);
    std::filesystem::last_write_time("logs/synthetic.log", aged);
    std::filesystem::last_write_time("logs/old.log", aged);
    Logger::instance().cleanup_logs();
    require(std::filesystem::exists("logs/synthetic.log"));
    require(!std::filesystem::exists("logs/old.log"));
    require(Logger::instance().write_to_file(std::string("second\n")));
    Logger::stop_file();
    std::ifstream stream("logs/synthetic.log");
    require(std::string(std::istreambuf_iterator<char>(stream), {}) == "first\nsecond\n");
}
void config() {
    std::barrier start(5);
    std::vector<std::thread> threads;
    threads.emplace_back([&] {
        start.arrive_and_wait();
        for (int i = 0; i < 2000; ++i) {
            auto& log = Logger::instance();
            log.set_debug(i % 2); log.set_compact_console(i % 2); log.enable_filter(true);
            log.setInverseMode(i % 2); log.set_active_modules(LogModule::Network);
            log.add_active_module(LogModule::Utils);
            log.addExcludePattern("ignored"); log.clearExcludePatterns();
            log.addCustomPattern("network"); log.clearCustomPatterns();
        }
    });
    for (int n = 0; n < 4; ++n) {
        threads.emplace_back([&] {
            start.arrive_and_wait();
            for (int i = 0; i < 2000; ++i) {
                auto& log = Logger::instance();
                log.is_debug(); log.is_compact_console(); log.should_log("network_manager.cpp");
            }
        });
    }
    for (auto& thread : threads) { thread.join(); }
    auto& log = Logger::instance();
    log.enable_filter(false); require(log.should_log("anything"));
    log.set_debug(true); require(log.is_debug());
    log.set_compact_console(true); require(log.is_compact_console());
}
void time_snapshots() {
    std::vector<std::time_t> times{0, 946684800, 1609459200, 1893456000};
    std::vector<std::tm> expected;
    for (auto time : times) { expected.push_back(*std::localtime(&time)); }
    std::atomic<bool> valid{true};
    std::vector<std::thread> threads;
    for (int w = 0; w < 8; ++w) {
        threads.emplace_back([&, w] {
            for (int i = 0; i < 2000; ++i) {
                const auto index = (w + i) % times.size();
                const auto copy = detail::local_time_snapshot(times[index]);
                const auto reference = expected[index];
                if (copy.tm_year != reference.tm_year || copy.tm_mon != reference.tm_mon
                    || copy.tm_mday != reference.tm_mday || copy.tm_hour != reference.tm_hour
                    || copy.tm_min != reference.tm_min || copy.tm_sec != reference.tm_sec) {
                    valid.store(false);
                }
            }
        });
    }
    for (auto& thread : threads) { thread.join(); }
    require(valid.load());
}
int main(int argc, char** argv) {
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
#endif
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        if (mode == "single") { writers(1, 4000, 80, false); }
        else if (mode == "concurrent") { writers(8, 4000, 80, false); }
        else if (mode == "long-records") { writers(8, 1000, 1024, false); }
        else if (mode == "start-stop") { writers(8, 2000, 80, true); }
        else if (mode == "cleanup") { cleanup(); }
        else if (mode == "config") { config(); }
        else if (mode == "time") { time_snapshots(); }
        else if (mode == "no-logging-dependency") {
            reject_logging_helpers = true;
            Logger::start_file("synthetic");
            require(Logger::instance().write_to_file(std::string("safe\n")));
            Logger::stop_file();
        }
        else if (mode == "directory-failure") {
            reject_logging_helpers = true;
            { std::ofstream collision("logs"); collision << "keep\n"; }
            Logger::start_file("synthetic");
            require(!Logger::instance().write_to_file(std::string("unwritten\n")));
            Logger::stop_file();
            std::ifstream collision("logs");
            require(std::string(std::istreambuf_iterator<char>(collision), {}) == "keep\n");
        }
        else if (mode == "disabled") {
            require(!Logger::instance().write_to_file(std::string("disabled\n")));
            Logger::start_file("synthetic"); Logger::stop_file();
            require(!Logger::instance().is_file_output());
            require(!Logger::instance().write_to_file(std::string("stopped\n")));
        } else { return 3; }
        return 0;
    } catch (...) { return 2; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++latest', '/EHsc', '/O2',
                       '/I' + str(source_root / 'headers'), str(cpp), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++23', '-O2', '-pthread', '-I', str(source_root / 'headers'),
                       str(cpp), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=cls.folder, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_file_lifecycle_config_and_time(self):
        for mode in ('single', 'concurrent', 'long-records', 'start-stop', 'cleanup', 'config', 'time',
                     'no-logging-dependency', 'directory-failure', 'disabled'):
            with self.subTest(mode=mode):
                work = self.folder / mode
                work.mkdir()
                result = subprocess.run([str(self.binary), mode], cwd=work, capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, mode + ': ' + result.stderr)


if __name__ == '__main__':
    unittest.main()
