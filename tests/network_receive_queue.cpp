#include "precompiled.h"
#include "chain/actor.h"
#include "network/network_receive_queue.h"
#include "utils/db_connector.h"
#include "managers/luminance_manager.h"

#include <QDir>
#include <QPointer>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest/QtTest>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>

using namespace std::chrono_literals;

namespace {
struct MetaCallCounter : QObject {
    int count = 0;

    bool eventFilter(QObject *, QEvent *event) override {
        if (event->type() == QEvent::MetaCall) {
            ++count;
        }
        return false;
    }
};

struct Gate {
    std::mutex mutex;
    std::condition_variable condition;
    std::atomic<bool> entered = false;
    bool released = false;

    void wait() {
        std::unique_lock lock(mutex);
        entered = true;
        if (!condition.wait_for(lock, 5s, [this] { return released; })) {
            throw std::runtime_error("Test gate timed out");
        }
    }
    void release() {
        std::lock_guard lock(mutex);
        released = true;
        condition.notify_one();
    }
};

NodeId identity() {
    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    return { actor.id(), "test-receive-node" };
}
}

class NetworkReceiveQueueTest : public QObject {
    Q_OBJECT
private slots:
    void copiesPacketsAndDispatchesFifo() {
        std::vector<std::string> seen;
        NetworkReceiveQueue queue(nullptr, [&](const auto &packet) { seen.push_back(packet.message); });
        std::string first = "first";
        QVERIFY(queue.enqueue(first, "test-ip", "test-peer"));
        first = "changed";
        QVERIFY(queue.enqueue("second", {}, {}));
        QCOMPARE(seen.size(), std::size_t(0));
        QTRY_COMPARE(queue.pending_count(), std::size_t(0));
        QVERIFY(seen == std::vector<std::string>({ "first", "second" }));
        QCOMPARE(queue.pending_bytes(), std::size_t(0));
    }

    void capacityIncludesActivePacketAndMetadata() {
        Gate gate;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            queue->defer([&] { gate.wait(); }, [] {});
        }, NetworkReceiveQueue::ErrorHandler {}, NetworkReceiveQueue::Limits { 1, 5 });
        QVERIFY(!queue->enqueue("12345", "x", {}));
        QCOMPARE(queue->pending_bytes(), std::size_t(0));
        QVERIFY(queue->enqueue("123", "x", "y"));
        QTRY_VERIFY(gate.entered.load());
        QVERIFY(!queue->enqueue("x", {}, {}));
        QCOMPARE(queue->pending_bytes(), std::size_t(5));
        gate.release();
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QCOMPARE(queue->pending_bytes(), std::size_t(0));
    }

    void blockedWorkLeavesOwnerEventLoopRunning() {
        Gate gate;
        int ticks = 0;
        int completed = 0;
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++ticks; });
        timer.start(1);
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            queue->defer([&] { gate.wait(); }, [&] { ++completed; });
        });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(gate.entered.load());
        QTest::qWait(30);
        QVERIFY(ticks > 0);
        QCOMPARE(completed, 0);
        gate.release();
        QTRY_COMPARE(completed, 1);
    }

    void realReputationReadCommitDispatchOrder() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        LuminanceManager storage(nullptr);
        const auto peer = identity();
        storage.write_luminance(peer, 7);
        std::vector<int> dispatches;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            if (packet.message == "custom") {
                dispatches.push_back(-1);
                return;
            }
            auto before = std::make_shared<int>(-1);
            queue->defer([&, before] { *before = storage.read_luminance(peer); }, [&, before] {
                queue->defer([&] { storage.increment(peer); }, [&, before] {
                    dispatches.push_back(*before);
                    QCOMPARE(storage.read_luminance(peer), *before + 1);
                });
            });
        });
        QVERIFY(queue->enqueue("broadcast-a", {}, {}));
        QVERIFY(queue->enqueue("custom", {}, {}));
        QVERIFY(queue->enqueue("broadcast-b", {}, {}));
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QVERIFY(dispatches == std::vector<int>({ 7, -1, 8 }));
        QCOMPARE(storage.read_luminance(peer), 9);
    }

    void nestedHandlerEventLoopCannotAdvanceActivePacket() {
        MetaCallCounter calls;
        std::vector<std::string> seen;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            if (packet.message == "second") {
                seen.push_back("second");
                return;
            }
            seen.push_back("handler-start");
            const auto before = calls.count;
            queue->defer([] {}, [&] { seen.push_back("continuation"); });
            QTRY_VERIFY(calls.count > before);
            seen.push_back("handler-end");
        });
        queue->installEventFilter(&calls);
        QVERIFY(queue->enqueue("first", {}, {}));
        QVERIFY(queue->enqueue("second", {}, {}));
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QVERIFY(seen == std::vector<std::string>({ "handler-start", "handler-end", "continuation", "second" }));
    }

    void nestedContinuationEventLoopCannotAdvanceActivePacket() {
        MetaCallCounter calls;
        std::vector<std::string> seen;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            if (packet.message == "second") {
                seen.push_back("second");
                return;
            }
            queue->defer([] {}, [&] {
                seen.push_back("stage-start");
                const auto before = calls.count;
                queue->defer([] {}, [&] { seen.push_back("final"); });
                QTRY_VERIFY(calls.count > before);
                seen.push_back("stage-end");
            });
        });
        queue->installEventFilter(&calls);
        QVERIFY(queue->enqueue("first", {}, {}));
        QVERIFY(queue->enqueue("second", {}, {}));
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QVERIFY(seen == std::vector<std::string>({ "stage-start", "stage-end", "final", "second" }));
    }

    void maintenanceIsOrderedCoalescedAndHasReservedCapacity() {
        Gate gate;
        std::vector<std::string> seen;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            if (packet.maintenance) {
                seen.push_back("maintenance");
                return;
            }
            queue->defer([&] { gate.wait(); }, [&] { seen.push_back("packet"); });
        }, NetworkReceiveQueue::ErrorHandler {}, NetworkReceiveQueue::Limits { 1, 10 });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(gate.entered.load());
        QVERIFY(queue->enqueue_maintenance());
        QVERIFY(queue->enqueue_maintenance());
        QCOMPARE(queue->pending_count(), std::size_t(2));
        QVERIFY(!queue->enqueue("next", {}, {}));
        gate.release();
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QVERIFY(seen == std::vector<std::string>({ "packet", "maintenance" }));
        QVERIFY(queue->enqueue_maintenance());
        QTRY_COMPARE(seen.size(), std::size_t(3));
    }

    void stopWaitsForAcceptedWriteWithoutDispatchingBacklog() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        LuminanceManager storage(nullptr);
        const auto peer = identity();
        storage.write_luminance(peer, 1);
        Gate gate;
        int begins = 0;
        int continuations = 0;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            ++begins;
            queue->defer([&] { gate.wait(); storage.increment(peer); }, [&] { ++continuations; });
        });
        QVERIFY(queue->enqueue("accepted", {}, {}));
        QVERIFY(queue->enqueue("not-yet-dispatched", {}, {}));
        QTRY_VERIFY(gate.entered.load());
        std::jthread releaser([&] { std::this_thread::sleep_for(30ms); gate.release(); });
        queue->stop();
        QCOMPARE(storage.read_luminance(peer), 2);
        QCoreApplication::processEvents();
        QCOMPARE(begins, 1);
        QCOMPARE(continuations, 0);
        QCOMPARE(queue->pending_count(), std::size_t(0));
        QVERIFY(!queue->enqueue("after-stop", {}, {}));
        QVERIFY(!queue->enqueue_maintenance());
        queue->stop();
    }

    void workerFailureDoesNotDispatchAndQueueRecovers() {
        int errors = 0;
        std::vector<std::string> seen;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            if (packet.message == "bad") {
                queue->defer([] { throw std::runtime_error("synthetic failure"); }, [&] { seen.push_back("wrong"); });
            } else {
                seen.push_back(packet.message);
            }
        }, [&](auto) { ++errors; });
        QVERIFY(queue->enqueue("bad", {}, {}));
        QVERIFY(queue->enqueue("good", {}, {}));
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QCOMPARE(errors, 1);
        QVERIFY(seen == std::vector<std::string>({ "good" }));
    }

    void reentrantStopRetainsBorrowedPacketAndDropsPendingCompletion() {
        MetaCallCounter calls;
        const std::string payload(8192, 'p');
        int begins = 0;
        int continuations = 0;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &packet) {
            ++begins;
            const auto before = calls.count;
            queue->defer([] {}, [&] { ++continuations; });
            QTRY_VERIFY(calls.count > before);
            queue->stop();
            QVERIFY(packet.message == payload);
            QCOMPARE(queue->pending_count(), std::size_t(0));
            QCOMPARE(queue->pending_bytes(), std::size_t(0));
        });
        queue->installEventFilter(&calls);
        QVERIFY(queue->enqueue(payload, {}, {}));
        QVERIFY(queue->enqueue("backlog", {}, {}));
        QTRY_COMPARE(begins, 1);
        QCoreApplication::processEvents();
        QCOMPARE(continuations, 0);
        QCOMPARE(queue->pending_count(), std::size_t(0));
        QCOMPARE(queue->pending_bytes(), std::size_t(0));
    }

    void exceptionAfterDeferralSuppressesContinuation() {
        int errors = 0;
        int continuations = 0;
        std::atomic<int> writes = 0;
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            queue->defer([&] { ++writes; }, [&] { ++continuations; });
            throw std::runtime_error("failure after accepting work");
        }, [&](auto) { ++errors; throw std::runtime_error("reporting failure"); });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_COMPARE(queue->pending_count(), std::size_t(0));
        QCOMPARE(writes.load(), 1);
        QCOMPARE(continuations, 0);
        QCOMPARE(errors, 1);
    }

    void handlerCanDestroyOwner() {
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) { queue.reset(); });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(!queue);
    }

    void continuationCanDestroyOwner() {
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            queue->defer([] {}, [&] { queue.reset(); });
        });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(!queue);
    }

    void errorHandlerCanDestroyOwner() {
        std::unique_ptr<NetworkReceiveQueue> queue;
        queue = std::make_unique<NetworkReceiveQueue>(nullptr, [&](const auto &) {
            queue->defer([] { throw std::runtime_error("synthetic failure"); }, [] {});
        }, [&](auto) { queue.reset(); });
        QVERIFY(queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(!queue);
    }

    void concurrentEnqueueStillHonorsOnePacketLimit() {
        int dispatched = 0;
        std::atomic<int> accepted = 0;
        NetworkReceiveQueue queue(nullptr, [&](const auto &) { ++dispatched; }, {}, { 1, 32 });
        std::vector<std::jthread> writers;
        for (int i = 0; i < 8; ++i) {
            writers.emplace_back([&] { accepted += int(queue.enqueue("packet", {}, {})); });
        }
        writers.clear();
        QCOMPARE(accepted.load(), 1);
        QCOMPARE(queue.pending_count(), std::size_t(1));
        QTRY_COMPARE(dispatched, 1);
    }

    void parentShutdownJoinsBeforeOwnedStorageDestruction() {
        struct Storage : LuminanceManager {
            Storage() : LuminanceManager(nullptr) {}
            std::function<void()> onDestroy;
            ~Storage() { onDestroy(); }
        };
        struct Owner : QObject {
            std::unique_ptr<Storage> storage;
            QPointer<NetworkReceiveQueue> queue;
            ~Owner() override {
                if (queue) {
                    queue->stop();
                }
            }
        };
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto previous = QDir::currentPath();
        const auto restore = qScopeGuard([&] { QDir::setCurrent(previous); });
        QVERIFY(QDir::setCurrent(directory.path()));
        const auto peer = identity();
        Gate gate;
        std::atomic<bool> finished = false;
        bool storageDestroyedAfterJoin = false;
        int continuations = 0;
        auto owner = std::make_unique<Owner>();
        owner->storage = std::make_unique<Storage>();
        auto *storage = owner->storage.get();
        storage->write_luminance(peer, 7);
        storage->onDestroy = [&] { storageDestroyedAfterJoin = finished.load(); };
        owner->queue = new NetworkReceiveQueue(owner.get(), [&](const auto &) {
            owner->queue->defer([&] {
                gate.wait();
                storage->increment(peer);
                finished = true;
            }, [&] { ++continuations; });
        });
        QVERIFY(owner->queue->enqueue("packet", {}, {}));
        QTRY_VERIFY(gate.entered.load());
        std::jthread release([&] { std::this_thread::sleep_for(20ms); gate.release(); });
        owner.reset();
        release.join();
        QCoreApplication::processEvents();
        QVERIFY(storageDestroyedAfterJoin);
        QCOMPARE(continuations, 0);
        LuminanceManager reopened(nullptr);
        QCOMPARE(reopened.read_luminance(peer), 8);
        auto second = std::make_unique<Owner>();
        second->queue = new NetworkReceiveQueue(second.get(), [](const auto &) {});
        delete second->queue.data();
        QVERIFY(second->queue.isNull());
        second.reset();
    }

    void immediateIdleShutdownDoesNotLoseWakeup() {
        for (int i = 0; i < 100; ++i) {
            NetworkReceiveQueue queue(nullptr, [](const auto &) {});
            queue.stop();
        }
    }
};

QTEST_GUILESS_MAIN(NetworkReceiveQueueTest)
#include "network_receive_queue.moc"
