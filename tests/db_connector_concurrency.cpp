#include "precompiled.h"
#include "utils/db_connector.h"
#include "sqlite3.h"

#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>

using namespace std::chrono_literals;

namespace {

struct QueryGate
{
    std::mutex mutex;
    std::condition_variable condition;
    bool entered = false;
    bool released = false;

    static void wait(sqlite3_context *context, int, sqlite3_value **)
    {
        auto &gate = *static_cast<QueryGate *>(sqlite3_user_data(context));
        std::unique_lock lock(gate.mutex);
        gate.entered = true;
        gate.condition.notify_all();
        if (!gate.condition.wait_for(lock, 10s, [&gate] { return gate.released; })) {
            sqlite3_result_error(context, "test gate deadline", -1);
            return;
        }
        sqlite3_result_int(context, 1);
    }

    bool awaitEntry()
    {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, 5s, [this] { return entered; });
    }

    void release()
    {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_all();
    }
};

}

class DbConnectorConcurrencyTest : public QObject
{
    Q_OBJECT

private slots:
    void independentConnectionsProgress()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector busy(directory.filePath("busy.sqlite").toStdString());
        DbConnector other(directory.filePath("other.sqlite").toStdString(), DbConnectorType::Regular,
                          DbConnectorLockScope::Connection);
        QVERIFY(busy.open());
        QVERIFY(other.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(busy.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return busy.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto independent = std::async(std::launch::async, [&] { return other.select("SELECT 42 AS value"); });
        const bool progressed = independent.wait_for(500ms) == std::future_status::ready;
        gate.release();
        const auto busyRows = blocked.get();
        const auto otherRows = independent.get();
        QVERIFY(entered);
        QVERIFY2(progressed, "an unrelated connection waited for the busy database");
        QCOMPARE(busyRows.size(), std::size_t(1));
        QCOMPARE(otherRows.size(), std::size_t(1));
        QCOMPARE(otherRows.front().at("value"), std::string("42"));
    }

    void sameConnectionStillSerializes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector database(directory.filePath("same.sqlite").toStdString(), DbConnectorType::Regular,
                             DbConnectorLockScope::Connection);
        QVERIFY(database.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(database.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return database.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto follower = std::async(std::launch::async, [&] { return database.select("SELECT 7 AS value"); });
        const bool serialized = follower.wait_for(150ms) == std::future_status::timeout;
        gate.release();
        const auto first = blocked.get();
        const auto second = follower.get();
        QVERIFY(entered);
        QVERIFY(serialized);
        QCOMPARE(first.size(), std::size_t(1));
        QCOMPARE(second.size(), std::size_t(1));
        QCOMPARE(second.front().at("value"), std::string("7"));
    }

    void defaultConnectionsRetainSharedLock()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector first(directory.filePath("first.sqlite").toStdString());
        DbConnector second(directory.filePath("second.sqlite").toStdString());
        QVERIFY(first.open());
        QVERIFY(second.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(first.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return first.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto follower = std::async(std::launch::async, [&] { return second.select("SELECT 7 AS value"); });
        const bool serialized = follower.wait_for(150ms) == std::future_status::timeout;
        gate.release();
        const auto firstRows = blocked.get();
        const auto secondRows = follower.get();
        QVERIFY(entered);
        QVERIFY2(serialized, "default locking semantics changed");
        QCOMPARE(firstRows.size(), std::size_t(1));
        QCOMPARE(secondRows.size(), std::size_t(1));
    }

    void recursiveBindings()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector database(directory.filePath("bindings.sqlite").toStdString(), DbConnectorType::Regular,
                             DbConnectorLockScope::Connection);
        QVERIFY(database.open());
        QVERIFY(database.create_table("CREATE TABLE values_table (value INT)"));
        QVERIFY(database.insert("values_table", {{"value", "3"}}));
        auto rows = database.select("SELECT * FROM values_table WHERE value = ?", "values_table", {{"value", "3"}});
        QCOMPARE(rows.size(), std::size_t(1));
        QVERIFY(database.delete_row("values_table", {{"value", "3"}}));
        QCOMPARE(database.count("values_table"), std::uint64_t(0));
    }

    void movedConnectorRemainsUsable()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector original(directory.filePath("move.sqlite").toStdString(), DbConnectorType::Regular,
                             DbConnectorLockScope::Connection);
        QVERIFY(original.open());
        QVERIFY(original.create_table("CREATE TABLE values_table (value INT)"));
        DbConnector moved(std::move(original));
        QVERIFY(moved.insert("values_table", {{"value", "5"}}));
        QCOMPARE(moved.count("values_table"), std::uint64_t(1));
    }
};

QTEST_GUILESS_MAIN(DbConnectorConcurrencyTest)
#include "db_connector_concurrency.moc"
