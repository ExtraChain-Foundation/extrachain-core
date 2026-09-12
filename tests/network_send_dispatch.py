"""Exercise the actual transport dispatcher with a real Qt event queue."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class NetworkSendDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cmake = shutil.which(os.environ.get('RACCOON_TEST_CMAKE', 'cmake'))
        if not cmake:
            raise unittest.SkipTest('CMake and a Qt Core development installation are required')
        source_root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (source_root / 'sources/network/network_manager.cpp').read_text(encoding='utf-8')
        start = source.index('void NetworkManager::send_message_connections(')
        dispatch = source[start:source.index('\nvoid NetworkManager::send_broadcast_message_further(', start)]
        start = source.index('bool NetworkManager::is_active_connection_exists(')
        active = source[start:source.index('\nint NetworkManager::active_connections_count(', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='network dispatch ')
        cls.addClassCleanup(cls.temporary.cleanup)
        root = Path(cls.temporary.name)
        (root / 'fixture.cpp').write_text(r'''
#include <QCoreApplication>
#include <QEvent>
#include <QElapsedTimer>
#include <QThread>
#include <QString>
#include <QByteArray>
#include <array>
#include <exception>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

void require(bool ok) { if (!ok) { throw std::runtime_error("fixture assertion"); } }
template<class... T> void eTemp(const char*, T&&...) {}
template<class... T> void eLog(const char*, T&&...) {}
int warnings = 0;
template<class... T> void eWarning(const char*, T&&...) { ++warnings; }
#define TIMER_START(name) QElapsedTimer name; name.start();
enum class SendMode { Focused, Except, Neighbours, Broadcast, NeighboursRandom, OneNeighbourRandom };
enum class MessageType { DfsFileState, DfsFileExistNotification, DfsFileFragment, Actors,
                         DfsSyncDirRows, Custom, NewActor, DagLightData, DagSyncLastInfo };
enum class MessageStatus { Request, Response };
enum class SocketMode { Full, Light };
struct MessageBody {
    MessageType message_type = MessageType::DfsFileState;
    std::set<std::string> nodes_identifiers_to_ignore;
};
struct Record { std::string recipient, payload; int priority; };
struct Sink {
    int touches = 0, cached = 0, bytes = 0;
    std::string cached_payload, cached_recipient;
    SendMode cached_mode = SendMode::Focused;
    bool fail_cache = false;
    std::vector<Record> records;
};
struct SocketService : QObject {
    enum class Priority { High, Normal, Low };
    std::shared_ptr<Sink> sink;
    QString ident;
    bool active = true;
    SocketMode socket_mode = SocketMode::Full;
    SocketService(std::shared_ptr<Sink> out, QString id, QObject* parent)
        : QObject(parent), sink(out), ident(id) {}
    void check() const { require(QThread::currentThread() == thread()); ++sink->touches; }
    bool is_active() const { check(); return active; }
    const QString& identifier() const { check(); return ident; }
    const QString& ip() const { check(); return ident; }
    SocketMode mode() const { check(); return socket_mode; }
    void send_message(const QByteArray& data, Priority p) {
        check(); sink->records.push_back({ident.toStdString(), data.toStdString(), int(p)});
    }
};
struct Connections {
    std::vector<SocketService*> services;
    auto operator*() { return &services; }
};
struct Traffic {
    std::shared_ptr<Sink> sink;
    QThread* owner;
    void add_bytes_sent(const std::string&, size_t size) {
        require(QThread::currentThread() == owner); sink->bytes += int(size);
    }
};
namespace Utils {
template<size_t N> std::array<int, N> random_indices(size_t size) {
    require(size > 0);
    std::array<int, N> result{};
    for (size_t i = 0; i < N; ++i) { result[i] = int(i % size); }
    return result;
}
}
struct NetworkManager : QObject {
    std::shared_ptr<Sink> sink;
    Connections connections_storage;
    Connections& connections_ = connections_storage;
    Traffic traffic;
    Traffic* calculate_traffic_ = &traffic;
    explicit NetworkManager(std::shared_ptr<Sink> out)
        : sink(out), traffic{out, thread()} {}
    void add(QString id, SocketMode mode = SocketMode::Full) {
        auto service = new SocketService(sink, id, this);
        service->socket_mode = mode;
        connections_storage.services.push_back(service);
    }
    void save_to_cache(const std::string& data, SendMode mode, const std::string& id) {
        require(QThread::currentThread() == thread());
        if (sink->fail_cache) { throw std::runtime_error("synthetic cache failure"); }
        ++sink->cached; sink->cached_payload = data; sink->cached_recipient = id; sink->cached_mode = mode;
    }
    bool is_active_connection_exists();
    void send_message_connections(const std::string&, const MessageBody&, SendMode,
                                  const std::string&, MessageType, MessageStatus);
};
''' + dispatch + active + r'''
void drain() { QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        auto sink = std::make_shared<Sink>();
        auto manager = std::make_unique<NetworkManager>(sink);
        if (mode != "empty-cache" && mode != "cache-error") {
            manager->add("target");
            manager->add("other");
            manager->add("light", SocketMode::Light);
            manager->add("fourth");
        }
        sink->fail_cache = mode == "cache-error";
        std::string payload = "signed-payload", recipient = "target";
        MessageBody body;
        body.nodes_identifiers_to_ignore = {"other"};
        SendMode routing = SendMode::Focused;
        MessageType type = MessageType::DfsFileState;
        if (mode == "broadcast") { routing = SendMode::Broadcast; }
        if (mode == "except") { routing = SendMode::Except; }
        if (mode == "neighbours") { routing = SendMode::Neighbours; }
        if (mode == "random-one") { routing = SendMode::OneNeighbourRandom; }
        if (mode == "random-three") { routing = SendMode::NeighboursRandom; }
        if (mode == "light-focused") { recipient = "light"; }
        if (mode == "low") { type = MessageType::DfsFileFragment; }
        if (mode == "high") { type = MessageType::Custom; }
        const auto send = [&] {
            manager->send_message_connections(payload, body, routing, recipient, type, MessageStatus::Response);
        };
        if (mode == "owner") {
            send(); require(sink->records.size() == 1); drain();
            require(sink->records.size() == 1); return 0;
        }
        if (mode == "different-owner") {
            QThread network_thread;
            manager->moveToThread(&network_thread);
            manager->traffic.owner = &network_thread;
            send();
            require(sink->touches == 0 && sink->records.empty());
            QMetaObject::invokeMethod(manager.get(), [&] {
                manager->moveToThread(app.thread());
                network_thread.quit();
            }, Qt::QueuedConnection);
            network_thread.start();
            require(network_thread.wait(5000));
            require(sink->records.size() == 1 && warnings == 0);
            require(sink->records[0].payload == "signed-payload"); return 0;
        }
        if (mode == "many-producers") {
            std::vector<std::thread> workers;
            std::array<std::exception_ptr, 16> errors{};
            for (int producer = 0; producer < 16; ++producer) {
                workers.emplace_back([&, producer] {
                    try {
                        for (int i = 0; i < 16; ++i) {
                            manager->send_message_connections(std::to_string(producer) + ":" + std::to_string(i),
                                body, routing, recipient, type, MessageStatus::Response);
                        }
                    } catch (...) { errors[producer] = std::current_exception(); }
                });
            }
            for (auto& worker : workers) { worker.join(); }
            for (const auto& error : errors) { if (error) { std::rethrow_exception(error); } }
            require(sink->touches == 0 && sink->records.empty()); drain();
            require(sink->records.size() == 256 && warnings == 0);
            for (int producer = 0; producer < 16; ++producer) {
                int next = 0;
                const auto prefix = std::to_string(producer) + ":";
                for (const auto& record : sink->records) {
                    if (record.payload.starts_with(prefix)) {
                        require(record.payload == prefix + std::to_string(next++));
                        require(record.recipient == "target");
                    }
                }
                require(next == 16);
            }
            return 0;
        }
        std::exception_ptr worker_error;
        std::thread worker([&] {
            try { send(); if (mode == "ordered") { payload = "second"; send(); } }
            catch (...) { worker_error = std::current_exception(); }
        });
        worker.join();
        if (worker_error) { std::rethrow_exception(worker_error); }
        require(sink->touches == 0 && sink->cached == 0 && sink->records.empty());
        payload = "mutated"; recipient = "mutated"; body.nodes_identifiers_to_ignore = {"target"};
        if (mode == "destroyed") {
            manager.reset(); drain(); require(sink->touches == 0 && sink->records.empty()); return 0;
        }
        if (mode == "closed") {
            for (auto service : manager->connections_storage.services) { service->active = false; }
        }
        if (mode == "removed") {
            auto& services = manager->connections_storage.services;
            for (auto service : services) { delete service; }
            services.clear();
        }
        drain();
        if (mode == "cache-error") {
            require(warnings == 1 && sink->cached == 0 && sink->records.empty()); return 0;
        }
        require(warnings == 0);
        if (mode == "closed" || mode == "removed" || mode == "empty-cache") {
            require(sink->records.empty() && sink->cached == 1);
            require(sink->cached_payload == "signed-payload" && sink->cached_recipient == "target");
            require(sink->cached_mode == SendMode::Focused); return 0;
        }
        size_t expected = 1;
        if (mode == "broadcast" || mode == "except" || mode == "ordered") { expected = 2; }
        if (mode == "neighbours" || mode == "random-three") { expected = 3; }
        require(sink->records.size() == expected && sink->cached == 0);
        for (size_t i = 0; i < sink->records.size(); ++i) {
            const auto& row = sink->records[i];
            require(row.payload == (mode == "ordered" && i == 1 ? "second" : "signed-payload"));
            require(row.priority == int(mode == "low" ? SocketService::Priority::Low :
                                       mode == "high" ? SocketService::Priority::High : SocketService::Priority::Normal));
            if (routing == SendMode::Focused) { require(row.recipient == (mode == "light-focused" ? "light" : "target")); }
            if (mode == "broadcast") { require(row.recipient != "other" && row.recipient != "light"); }
            if (mode == "except") { require(row.recipient != "target" && row.recipient != "light"); }
            if (mode == "neighbours") { require(row.recipient != "light"); }
        }
        drain(); require(sink->records.size() == expected);
        return 0;
    } catch (const std::exception&) { return 2; }
}
''', encoding='utf-8')
        (root / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.21)
project(network_dispatch_fixture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
find_package(Qt6 REQUIRED COMPONENTS Core)
add_executable(fixture fixture.cpp)
target_link_libraries(fixture PRIVATE Qt6::Core)
''', encoding='utf-8')
        configure = [cmake, '-S', str(root), '-B', str(root / 'build'), '-DCMAKE_BUILD_TYPE=Release']
        if os.environ.get('RACCOON_TEST_QT'):
            configure.append('-DCMAKE_PREFIX_PATH=' + os.environ['RACCOON_TEST_QT'])
        for command in (configure, [cmake, '--build', str(root / 'build'), '--config', 'Release']):
            result = subprocess.run(command, capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise RuntimeError(result.stdout + result.stderr)
        cls.binary = root / 'build' / ('fixture.exe' if os.name == 'nt' else 'fixture')
        if os.name == 'nt' and not cls.binary.exists():
            cls.binary = root / 'build/Release/fixture.exe'
        cls.environment = dict(os.environ)
        if os.name == 'nt' and os.environ.get('RACCOON_TEST_QT'):
            cls.environment['PATH'] = str(Path(os.environ['RACCOON_TEST_QT']) / 'bin') + os.pathsep + cls.environment['PATH']

    def test_dispatch_contract(self):
        for mode in ('owner', 'different-owner', 'many-producers', 'worker-copy', 'ordered', 'destroyed', 'closed', 'removed',
                     'empty-cache', 'cache-error', 'broadcast', 'except', 'neighbours', 'random-one',
                     'random-three', 'light-focused', 'low', 'high'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], env=self.environment,
                                        capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, mode + '\n' + result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
