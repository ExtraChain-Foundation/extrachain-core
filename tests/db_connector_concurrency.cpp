#include "precompiled.h"
#include "utils/db_connector.h"
#include "chain/actor_index.h"
#include "dfs/dfs_utils.h"
#include "sqlite3.h"

#include <QTemporaryDir>
#include <QDir>
#include <QScopeGuard>
#include <QThread>
#include <QtTest/QtTest>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <tuple>

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

QueryGate *actorCommitGate = nullptr;
QString actorDatabasePath;

int holdActorCommit(void *opaque)
{
    auto &gate = *static_cast<QueryGate *>(opaque);
    std::unique_lock lock(gate.mutex);
    gate.entered = true;
    gate.condition.notify_all();
    return gate.condition.wait_for(lock, 10s, [&] { return gate.released; }) ? 0 : 1;
}

int installActorCommitGate(sqlite3 *database, char **, const sqlite3_api_routines *)
{
    const auto filename = QString::fromUtf8(sqlite3_db_filename(database, "main"));
    if (actorCommitGate && QDir::cleanPath(QDir::fromNativeSeparators(filename)) == actorDatabasePath) {
        sqlite3_commit_hook(database, holdActorCommit, actorCommitGate);
    }
    return SQLITE_OK;
}

Actor<KeyPublic> syntheticActor()
{
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    return actor.to_public();
}

}

class DbConnectorConcurrencyTest : public QObject
{
    Q_OBJECT

private slots:
    void lockGroupRetainsOwnershipAndSerializationAfterMove()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::weak_ptr<std::recursive_mutex> retained;
        {
            auto group = std::make_shared<std::recursive_mutex>();
            retained = group;
            DbConnector first(directory.filePath("first.sqlite").toStdString(), DbConnectorType::Regular, group);
            DbConnector second(directory.filePath("second.sqlite").toStdString(), DbConnectorType::Regular, group);
            group.reset();
            QVERIFY(!retained.expired());
            QVERIFY(first.open());
            QVERIFY(second.open());
            DbConnector moved(std::move(first));
            QueryGate gate;
            QCOMPARE(sqlite3_create_function_v2(moved.getDb(), "test_gate", 0, SQLITE_UTF8,
                                                &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
            auto blocked = std::async(std::launch::async, [&] { return moved.select("SELECT test_gate() AS value"); });
            const bool entered = gate.awaitEntry();
            auto follower = std::async(std::launch::async, [&] { return second.select("SELECT 7 AS value"); });
            const bool serialized = follower.wait_for(150ms) == std::future_status::timeout;
            gate.release();
            const auto firstRows = blocked.get();
            const auto secondRows = follower.get();
            QVERIFY(entered);
            QVERIFY(serialized);
            QCOMPARE(firstRows.size(), std::size_t(1));
            QCOMPARE(secondRows.size(), std::size_t(1));
            QCOMPARE(secondRows.front().at("value"), std::string("7"));
        }
        QVERIFY(retained.expired());
    }

    void distinctLockGroupsProgressIndependently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        DbConnector first(directory.filePath("first.sqlite").toStdString(), DbConnectorType::Regular,
                          std::make_shared<std::recursive_mutex>());
        DbConnector second(directory.filePath("second.sqlite").toStdString(), DbConnectorType::Regular,
                           std::make_shared<std::recursive_mutex>());
        QVERIFY(first.open());
        QVERIFY(second.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(first.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return first.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto follower = std::async(std::launch::async, [&] { return second.select("SELECT 7 AS value"); });
        const bool progressed = follower.wait_for(500ms) == std::future_status::ready;
        gate.release();
        const auto firstRows = blocked.get();
        const auto secondRows = follower.get();
        QVERIFY(entered);
        QVERIFY(progressed);
        QCOMPARE(firstRows.size(), std::size_t(1));
        QCOMPARE(secondRows.size(), std::size_t(1));
        QCOMPARE(secondRows.front().at("value"), std::string("7"));
    }

    void actorIndexConstructionProgressesIndependently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("actors"));
        DbConnector busy(directory.filePath("unrelated.sqlite").toStdString());
        QVERIFY(busy.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(busy.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return busy.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto constructor = std::async(std::launch::async, [&] {
            ActorIndex index(nullptr);
            return index.records();
        });
        const bool progressed = constructor.wait_for(500ms) == std::future_status::ready;
        gate.release();
        const auto busyRows = blocked.get();
        const auto records = constructor.get();
        QVERIFY(entered);
        QCOMPARE(busyRows.size(), std::size_t(1));
        QCOMPARE(records, std::size_t(0));
        QVERIFY2(progressed, "ActorIndex construction waited for unrelated SQL");
    }

    void actorIndexReadsProgressIndependently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("actors"));
        ActorIndex index(nullptr);
        DbConnector busy(directory.filePath("unrelated.sqlite").toStdString());
        QVERIFY(busy.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(busy.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return busy.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto reader = std::async(std::launch::async, [&] { return index.read_all_actors_ids(); });
        const bool progressed = reader.wait_for(500ms) == std::future_status::ready;
        gate.release();
        const auto busyRows = blocked.get();
        const auto rows = reader.get();
        QVERIFY(entered);
        QCOMPARE(busyRows.size(), std::size_t(1));
        QVERIFY(rows.empty());
        QVERIFY2(progressed, "ActorIndex read waited for unrelated SQL");
    }

    void actorIndexSaveProgressesOnOwnerThread()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("actors"));
        ActorIndex index(nullptr);
        const auto actor = syntheticActor();
        bool signaledOnOwner = false;
        int savedSignals = 0;
        connect(&index, &ActorIndex::actorSaved, &index, [&](ActorId id) {
            ++savedSignals;
            signaledOnOwner = QThread::currentThread() == index.thread() && id == actor.id();
        }, Qt::DirectConnection);
        DbConnector busy(directory.filePath("unrelated.sqlite").toStdString());
        QVERIFY(busy.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(busy.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return busy.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        std::promise<void> saved;
        auto completed = saved.get_future();
        auto releaser = std::async(std::launch::async, [&] {
            const bool progressed = completed.wait_for(700ms) == std::future_status::ready;
            gate.release();
            return progressed;
        });
        const auto result = index.network_store_new_actor(actor);
        saved.set_value();
        const bool progressed = releaser.get();
        const auto busyRows = blocked.get();
        QVERIFY(entered);
        QCOMPARE(busyRows.size(), std::size_t(1));
        QVERIFY(result.has_value());
        QVERIFY(signaledOnOwner);
        QCOMPARE(savedSignals, 1);
        QCOMPARE(index.records(), std::size_t(1));
        QVERIFY(index.read_by_id(actor.id()) == actor.toJson());
        const auto duplicate = index.network_store_new_actor(actor);
        QVERIFY(!duplicate.has_value());
        QCOMPARE(duplicate.error(), ActorSaveError::AlreadyExists);
        QCOMPARE(savedSignals, 1);
        QCOMPARE(index.records(), std::size_t(1));
        ActorIndex reopened(nullptr);
        QCOMPARE(reopened.records(), std::size_t(1));
        QVERIFY(reopened.read_all_actors_ids() == std::vector<ActorId>{actor.id()});
        QVERIFY2(progressed, "ActorIndex owner-thread save waited for unrelated SQL");
    }

    void actorIndexReadersWaitForActorCommit()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("actors"));
        ActorIndex index(nullptr);
        const auto actor = syntheticActor();
        QueryGate gate;
        actorCommitGate = &gate;
        actorDatabasePath = QDir::cleanPath(QDir().absoluteFilePath("actors/actors"));
        auto entry = reinterpret_cast<void (*)()>(installActorCommitGate);
        const auto cleanup = qScopeGuard([&] {
            sqlite3_cancel_auto_extension(entry);
            actorCommitGate = nullptr;
            actorDatabasePath.clear();
        });
        QCOMPARE(sqlite3_auto_extension(entry), SQLITE_OK);
        auto observer = std::async(std::launch::async, [&] {
            const bool entered = gate.awaitEntry();
            auto reader = std::async(std::launch::async, [&] { return index.read_all_actors_ids(); });
            const bool serialized = reader.wait_for(150ms) == std::future_status::timeout;
            gate.release();
            return std::make_tuple(entered, serialized, reader.get());
        });
        const auto result = index.save_actor(actor);
        const auto [entered, serialized, rows] = observer.get();
        QVERIFY(result.has_value());
        QVERIFY2(entered, "actual ActorIndex commit hook not reached");
        QVERIFY2(serialized, "ActorIndex read escaped actor-store serialization");
        QVERIFY(rows == std::vector<ActorId>{actor.id()});
        QCOMPARE(index.records(), std::size_t(1));
    }

    void dfsMetadataProgressesIndependently()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("dfs"));
        auto result = Dfs::Tables::DirsFile::DirsSpace::create_file();
        QVERIFY(result.has_value());
        auto metadata = result.value();
        DbConnector busy(directory.filePath("unrelated.sqlite").toStdString());
        QVERIFY(busy.open());
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(busy.getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return busy.select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto reader = std::async(std::launch::async, [&] { return metadata->select("SELECT 42 AS value"); });
        const bool progressed = reader.wait_for(500ms) == std::future_status::ready;
        gate.release();
        const auto busyRows = blocked.get();
        const auto rows = reader.get();
        QVERIFY(entered);
        QVERIFY2(progressed, "DFS metadata waited for an unrelated database operation");
        QCOMPARE(busyRows.size(), std::size_t(1));
        QCOMPARE(rows.size(), std::size_t(1));
        QCOMPARE(rows.front().at("value"), std::string("42"));
    }

    void dfsMetadataSharedOwnerStillSerializes()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        QVERIFY(QDir().mkdir("dfs"));
        auto result = Dfs::Tables::DirsFile::DirsSpace::create_file();
        QVERIFY(result.has_value());
        auto metadata = result.value();
        auto consumer = metadata;
        QueryGate gate;
        QCOMPARE(sqlite3_create_function_v2(metadata->getDb(), "test_gate", 0, SQLITE_UTF8,
                                            &gate, QueryGate::wait, nullptr, nullptr, nullptr), SQLITE_OK);
        auto blocked = std::async(std::launch::async, [&] { return metadata->select("SELECT test_gate() AS value"); });
        const bool entered = gate.awaitEntry();
        auto reader = std::async(std::launch::async, [&] { return consumer->select("SELECT 7 AS value"); });
        const bool serialized = reader.wait_for(150ms) == std::future_status::timeout;
        gate.release();
        const auto first = blocked.get();
        const auto second = reader.get();
        QVERIFY(entered);
        QVERIFY(serialized);
        QCOMPARE(first.size(), std::size_t(1));
        QCOMPARE(second.size(), std::size_t(1));
        QCOMPARE(second.front().at("value"), std::string("7"));
    }

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
