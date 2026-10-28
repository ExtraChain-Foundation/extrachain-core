#include "utils/db_connector.h"
#include "test_support.h"
#include <sqlite3.h>
#include <future>
#include <thread>

namespace {
    struct Methods {
        sqlite3_io_methods        copy;
        const sqlite3_io_methods* original;
    };
    sqlite3_vfs*                                      original_vfs;
    std::mutex                                        methods_mutex;
    std::map<sqlite3_file*, std::unique_ptr<Methods>> methods;
    std::atomic<bool>                                 armed { false };
    std::atomic<unsigned>                             exclusive_locks { 0 };
    std::promise<void>                                locked, release;
    auto                                              release_signal = release.get_future().share();

    const sqlite3_io_methods* original(sqlite3_file* file) {
        std::lock_guard lock(methods_mutex);
        return methods.at(file)->original;
    }

    int lock_file(sqlite3_file* file, int mode) {
        const auto result = original(file)->xLock(file, mode);
        if (result == SQLITE_OK && mode == SQLITE_LOCK_EXCLUSIVE)
            ++exclusive_locks;
        if (result == SQLITE_OK && mode == SQLITE_LOCK_EXCLUSIVE && armed.exchange(false)) {
            locked.set_value();
            release_signal.wait();
        }
        return result;
    }

    int close_file(sqlite3_file* file) {
        const auto prior       = original(file);
        file->pMethods         = prior;
        const auto      result = prior->xClose(file);
        std::lock_guard lock(methods_mutex);
        methods.erase(file);
        return result;
    }

    int open_file(sqlite3_vfs*, const char* name, sqlite3_file* file, int flags, int* result_flags) {
        const auto result = original_vfs->xOpen(original_vfs, name, file, flags, result_flags);
        if (result != SQLITE_OK || (flags & SQLITE_OPEN_MAIN_DB) == 0)
            return result;
        auto entry         = std::make_unique<Methods>(Methods { *file->pMethods, file->pMethods });
        entry->copy.xLock  = lock_file;
        entry->copy.xClose = close_file;
        file->pMethods     = &entry->copy;
        std::lock_guard lock(methods_mutex);
        methods.emplace(file, std::move(entry));
        return result;
    }
} // namespace

int main() {
    original_vfs   = sqlite3_vfs_find(nullptr);
    auto observed  = *original_vfs;
    observed.zName = "connection-lifecycle";
    observed.xOpen = open_file;
    TEST_REQUIRE(sqlite3_vfs_register(&observed, 1) == SQLITE_OK);
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-sqlite-lifecycle-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    const auto path = directory / "data.db";
    std::filesystem::create_directory(directory / "alias");
    const auto  alias = directory / "alias" / ".." / "data.db";
    DbConnector writer(path);
    TEST_REQUIRE(writer.open());
    TEST_REQUIRE(!writer.select("PRAGMA journal_mode=WAL").empty());
    TEST_REQUIRE(writer.query("CREATE TABLE Data(value INTEGER)"));
    TEST_REQUIRE(writer.query("INSERT INTO Data VALUES(7)"));
    DbConnector closing(std::move(writer));
    auto        held = locked.get_future();
    armed            = true;
    auto closer      = std::async(std::launch::async, [&] {
        return closing.close();
    });
    if (held.wait_for(std::chrono::seconds(5)) != std::future_status::ready) {
        release.set_value();
        TEST_REQUIRE(false);
    }
    auto       incoming = std::async(std::launch::async, [&] {
        DbConnector reader(alias);
        if (!reader.open(false) || !reader.query("BEGIN IMMEDIATE") || !reader.query("INSERT INTO Data VALUES(8)")
            || !reader.query("COMMIT"))
            return false;
        const auto rows = reader.select("SELECT value FROM Data ORDER BY value");
        return rows.size() == 2 && rows.front().at("value") == "7" && rows.back().at("value") == "8";
    });
    const bool early    = incoming.wait_for(std::chrono::seconds(6)) == std::future_status::ready;
    release.set_value();
    const bool closed = closer.get();
    const bool read   = incoming.get();

    DbConnector idle(path), pending(alias);
    TEST_REQUIRE(idle.open(false));
    TEST_REQUIRE(!idle.select("SELECT value FROM Data").empty());
    TEST_REQUIRE(pending.open(false));
    const auto before_close = exclusive_locks.load();
    TEST_REQUIRE(idle.close());
    const bool registered = exclusive_locks.load() == before_close;
    TEST_REQUIRE(!pending.select("SELECT value FROM Data").empty());
    TEST_REQUIRE(pending.close());
    std::printf("SQLite %s setlk=%d completed_while_closing=%d read=%d closed=%d registered=%d\n",
                sqlite3_libversion(),
                sqlite3_compileoption_used("ENABLE_SETLK_TIMEOUT"),
                early,
                read,
                closed,
                registered);
    std::fflush(stdout);
    sqlite3_vfs_register(original_vfs, 1);
    sqlite3_vfs_unregister(&observed);
    std::filesystem::remove_all(directory);
    TEST_REQUIRE(!early && read && closed && registered);
}
