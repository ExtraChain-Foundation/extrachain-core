"""Compile the actual file-state handler with controlled executor/DB boundaries."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class FileStateDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++ compiler is required')
        source = (Path(__file__).resolve().parents[1] / 'sources/dfs/dfs_controller.cpp').read_text(encoding='utf-8')
        start = source.index('void DfsController::network_response_file_state(')
        end = source.index('\nvoid DfsController::network_file_exist_notification(', start)
        method = source[start:end]
        cls.temporary = tempfile.TemporaryDirectory(prefix='dfs state dispatch ')
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

using ActorId = std::string;
std::atomic<bool> node_enabled{true};
int warnings = 0;
template<class... T> void eWarning(const char*, T&&...) { ++warnings; }

namespace Dfs {
enum class FileState { Known, Ready, Removed };
struct DirRow { FileState state = FileState::Known; std::string hash = "stored"; int metadata = 42; };
namespace Packets {
struct FileState {
    ActorId owner_id = "owner";
    std::string file_id = "file", hash = "announced";
    Dfs::FileState state = Dfs::FileState::Ready;
    bool notify_neighbours = true;
};
}
}

struct Identifiers {
    std::string first = "origin";
    bool absent = false;
    bool empty() const { return absent; }
    struct Iterator {
        const Identifiers* value;
        const std::string& operator*() const {
            if (value->absent) { throw std::logic_error("empty synthetic origin"); }
            return value->first;
        }
    };
    Iterator begin() const { return {this}; }
};
struct Responder {
    Identifiers ids;
    const Identifiers& identifiers() const { return ids; }
};
struct Database {
    int reads = 0;
    bool missing = false, throwing = false, stop_during_read = false;
    std::thread::id read_thread;
    ActorId owner;
    std::string file;
};
struct DirsManager {
    std::shared_ptr<Database> db = std::make_shared<Database>();
    auto get_db_instance() { return db; }
};
namespace Dfs::Tables::DirsFile::ActorSpace {
std::optional<Dfs::DirRow> get_dir_row(std::shared_ptr<Database> db,
                                     const ActorId& owner, const std::string& file) {
    ++db->reads;
    db->read_thread = std::this_thread::get_id();
    db->owner = owner;
    db->file = file;
    if (db->throwing) { throw std::runtime_error("synthetic DB failure"); }
    if (db->stop_during_read) { node_enabled.store(false); }
    if (db->missing) { return {}; }
    return Dfs::DirRow{};
}
}
struct LoadManager {
    int queued = 0;
    ActorId owner;
    std::string origin;
    Dfs::DirRow row;
    bool notify = false;
    std::thread::id enqueue_thread;
    void add_to_queue(const ActorId& who, const Dfs::DirRow& value, const std::string& source, bool flag) {
        ++queued;
        owner = who;
        origin = source;
        row = value;
        notify = flag;
        enqueue_thread = std::this_thread::get_id();
    }
};
struct ThreadPoolBoost {
    std::deque<std::function<void()>> tasks;
    static ThreadPoolBoost* instance_dfs() { static ThreadPoolBoost pool; return &pool; }
    template<class F> void post(F&& f) { tasks.emplace_back(std::forward<F>(f)); }
    void drain() {
        while (!tasks.empty()) {
            auto task = std::move(tasks.front());
            tasks.pop_front();
            std::exception_ptr error;
            std::thread worker([&] { try { task(); } catch (...) { error = std::current_exception(); } });
            worker.join();
            if (error) { std::rethrow_exception(error); }
        }
    }
};
struct DfsController {
    DirsManager dirs_manager_;
    LoadManager load_manager_;
    void network_response_file_state(const Dfs::Packets::FileState&, const Responder&);
};
''' + method + r'''
int exercise(const std::string& mode) {
    DfsController controller;
    auto db = controller.dirs_manager_.db;
    auto pool = ThreadPoolBoost::instance_dfs();
    auto caller = std::this_thread::get_id();
    {
        Dfs::Packets::FileState packet;
        Responder responder;
        if (mode == "not-ready") { packet.state = Dfs::FileState::Known; }
        if (mode == "removed") { packet.state = Dfs::FileState::Removed; }
        if (mode == "empty-origin") { responder.ids.absent = true; }
        if (mode == "stopped") { node_enabled.store(false); }
        db->missing = mode == "missing";
        db->throwing = mode == "exception";
        db->stop_during_read = mode == "stop-in-read";
        controller.network_response_file_state(packet, responder);
        if (db->reads || controller.load_manager_.queued) { return 1; }
        if (mode == "not-ready" || mode == "removed" || mode == "empty-origin" || mode == "stopped") {
            return pool->tasks.empty() ? 0 : 2;
        }
        if (pool->tasks.size() != 1) { return 3; }
        packet.owner_id = "changed";
        packet.file_id = "changed";
        packet.hash = "changed";
        packet.notify_neighbours = false;
        responder.ids.first = "changed";
    }
    if (mode == "stop-before-run") { node_enabled.store(false); }
    pool->drain();
    if (mode == "stop-before-run") { return db->reads == 0 && controller.load_manager_.queued == 0 ? 0 : 4; }
    if (db->reads != 1 || db->read_thread == caller || db->owner != "owner" || db->file != "file") { return 5; }
    if (mode == "exception") { return warnings == 1 && controller.load_manager_.queued == 0 ? 0 : 6; }
    if (mode == "missing" || mode == "stop-in-read") { return controller.load_manager_.queued == 0 ? 0 : 7; }
    const auto& load = controller.load_manager_;
    return load.queued == 1 && load.owner == "owner" && load.origin == "origin" && load.notify
        && load.row.state == Dfs::FileState::Ready && load.row.hash == "announced"
        && load.row.metadata == 42 && load.enqueue_thread != caller ? 0 : 8;
}
int main(int argc, char** argv) {
    if (argc != 2) { return 9; }
    try { return exercise(argv[1]); } catch (...) { return 10; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', str(code), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-pthread', str(code), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=root, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, 'actual file-state handler failed ' + mode)

    def test_ready_work_is_queued_and_copies_input_until_execution(self):
        self.check('ready')

    def test_nonready_does_not_read_metadata_or_post(self):
        self.check('not-ready')

    def test_removed_does_not_read_metadata_or_post(self):
        self.check('removed')

    def test_empty_origin_is_rejected_before_posting(self):
        self.check('empty-origin')

    def test_stopped_node_does_not_post(self):
        self.check('stopped')

    def test_shutdown_before_execution_skips_controller_work(self):
        self.check('stop-before-run')

    def test_shutdown_during_lookup_does_not_enqueue_download(self):
        self.check('stop-in-read')

    def test_missing_metadata_does_not_enqueue_download(self):
        self.check('missing')

    def test_worker_exception_is_contained(self):
        self.check('exception')


if __name__ == '__main__':
    unittest.main()
