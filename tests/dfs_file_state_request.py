"""Compile the actual file-state request handler across a controlled executor boundary."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FileStateRequestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++ compiler is required')
        source = (Path(__file__).resolve().parents[1] / 'sources/dfs/dfs_controller.cpp').read_text(encoding='utf-8')
        start = source.index('void DfsController::network_request_file_state(')
        method = source[start:source.index('\nvoid DfsController::network_request_file_existance(', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='dfs state request ')
        cls.addClassCleanup(cls.temporary.cleanup)
        root = Path(cls.temporary.name)
        cls.binary = root / ('fixture.exe' if os.name == 'nt' else 'fixture')
        code = root / 'fixture.cpp'
        code.write_text(r'''
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

using ActorId = std::string;
std::atomic<bool> node_enabled{true};
int warnings = 0;
template<class... T> void eWarning(const char*, T&&...) { ++warnings; }
enum class MessageType { DfsFileState };
enum class SendMode { Focused };
enum class MessageStatus { Response };
namespace Dfs {
enum class FileState { Unknown, Known, Ready, Removed };
struct DirRow { FileState state = FileState::Ready; std::string hash = "stored"; };
namespace Packets {
struct FileState {
    ActorId owner_id;
    std::string file_id;
    Dfs::FileState state = Dfs::FileState::Unknown;
    std::string hash;
};
}
}
struct Delivery {
    Dfs::Packets::FileState data;
    std::string correlation, recipient;
    std::thread::id thread;
    MessageType type;
    SendMode mode;
    MessageStatus status;
};
struct Sink { bool throwing = false; std::vector<Delivery> replies; };
struct Responder {
    std::shared_ptr<Sink> sink;
    std::string correlation = "request", recipient = "origin";
    std::string send_response(const Dfs::Packets::FileState& data, MessageType type,
                              SendMode mode, MessageStatus status) const {
        if (sink->throwing) { throw std::runtime_error("synthetic send failure"); }
        sink->replies.push_back({data, correlation, recipient, std::this_thread::get_id(), type, mode, status});
        return "sent";
    }
};
struct Database {
    int reads = 0;
    bool missing = false, throwing = false, stop_during_read = false;
    Dfs::DirRow row;
    std::thread::id read_thread;
    std::vector<std::pair<ActorId, std::string>> inputs;
};
struct DirsManager {
    std::shared_ptr<Database> db = std::make_shared<Database>();
    auto get_db_instance() { return db; }
};
namespace Dfs::Tables::DirsFile::ActorSpace {
std::optional<Dfs::DirRow> get_dir_row(std::shared_ptr<Database> db,
                                     const ActorId& owner, const std::string& file) {
    ++db->reads;
    db->inputs.emplace_back(owner, file);
    db->read_thread = std::this_thread::get_id();
    if (db->stop_during_read) { node_enabled.store(false); }
    if (db->throwing) { throw std::runtime_error("synthetic DB failure"); }
    if (db->missing) { return {}; }
    return db->row;
}
}
struct ThreadPoolBoost {
    std::deque<std::function<void()>> tasks;
    static ThreadPoolBoost* instance_dfs() { static ThreadPoolBoost pool; return &pool; }
    template<class F> void post(F&& f) { tasks.emplace_back(std::forward<F>(f)); }
    void drain() {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front()); tasks.pop_front();
            std::exception_ptr error;
            std::thread worker([&] { try { task(); } catch (...) { error = std::current_exception(); } });
            worker.join();
            if (error) { std::rethrow_exception(error); }
        }
    }
};
struct DfsController {
    DirsManager dirs_manager_;
    void network_request_file_state(const ActorId&, const std::string&, const Responder&);
};
''' + method + r'''
void require(bool condition) { if (!condition) { throw std::runtime_error("fixture assertion"); } }
int main(int argc, char** argv) {
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        const auto caller = std::this_thread::get_id();
        DfsController controller;
        auto db = controller.dirs_manager_.db;
        auto sink = std::make_shared<Sink>();
        auto pool = ThreadPoolBoost::instance_dfs();
        if (mode == "stopped") { node_enabled.store(false); }
        if (mode == "missing" || mode == "stop-missing") { db->missing = true; }
        if (mode == "read-error") { db->throwing = true; }
        if (mode == "send-error") { sink->throwing = true; }
        if (mode == "stop-read" || mode == "stop-missing") { db->stop_during_read = true; }
        if (mode == "known") { db->row.state = Dfs::FileState::Known; }
        if (mode == "removed") { db->row.state = Dfs::FileState::Removed; }
        {
            ActorId owner = "owner";
            std::string file = "file";
            Responder responder{sink};
            controller.network_request_file_state(owner, file, responder);
            require(db->reads == 0 && sink->replies.empty());
            require(pool->tasks.size() == (mode == "stopped" ? 0u : 1u));
            owner = "changed-owner"; file = "changed-file";
            responder.correlation = "changed-request"; responder.recipient = "changed-origin";
        }
        if (mode == "pending-stop") { node_enabled.store(false); }
        if (mode == "fresh-state") { db->row = {Dfs::FileState::Removed, "newer"}; }
        if (mode == "two-requests") {
            controller.network_request_file_state("second-owner", "second-file", Responder{sink, "second-request", "second-origin"});
            require(pool->tasks.size() == 2 && db->reads == 0);
        }
        pool->drain();
        if (mode == "stopped" || mode == "pending-stop") {
            require(db->reads == 0 && sink->replies.empty() && warnings == 0);
            return 0;
        }
        require(db->read_thread != caller && db->inputs[0] == std::make_pair(std::string("owner"), std::string("file")));
        require(db->reads == (mode == "two-requests" ? 2 : 1));
        if (mode == "stop-read" || mode == "stop-missing" || mode == "read-error" || mode == "send-error") {
            require(sink->replies.empty());
            require(warnings == (mode == "read-error" || mode == "send-error" ? 1 : 0));
            return 0;
        }
        require(sink->replies.size() == (mode == "two-requests" ? 2u : 1u) && warnings == 0);
        const auto& reply = sink->replies[0];
        require(reply.data.owner_id == "owner" && reply.data.file_id == "file");
        require(reply.correlation == "request" && reply.recipient == "origin" && reply.thread != caller);
        require(reply.type == MessageType::DfsFileState && reply.mode == SendMode::Focused && reply.status == MessageStatus::Response);
        require(reply.data.state == (db->missing ? Dfs::FileState::Unknown : db->row.state));
        require(reply.data.hash == (db->missing ? std::string{} : db->row.hash));
        if (mode == "two-requests") {
            const auto& second = sink->replies[1];
            require(second.data.owner_id == "second-owner" && second.data.file_id == "second-file");
            require(second.correlation == "second-request" && second.recipient == "second-origin");
        }
        return 0;
    } catch (...) { return 2; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', str(code), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-pthread', str(code), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_request_lifetime_state_and_shutdown(self):
        for mode in ('ready', 'known', 'removed', 'missing', 'fresh-state', 'two-requests',
                     'stopped', 'pending-stop', 'stop-read', 'stop-missing', 'read-error', 'send-error'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=5)
                self.assertEqual(result.returncode, 0, mode)


if __name__ == '__main__':
    unittest.main()
