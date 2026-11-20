"""Compile actual force insertion and download-admission preamble with controlled peers."""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


class ForcedDownloadTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        repo = Path(__file__).resolve().parents[1]
        header = (repo / 'headers/dfs/dfs_controller.h').read_text(encoding='utf-8')
        declaration = re.search(r'^\s*(?:std::set<Dfs::FileLink>|SafePtr<std::set<Dfs::FileLink>>) forces_files_;', header, re.M)
        if not declaration:
            raise AssertionError('review the changed force-set declaration')
        controller = (repo / 'sources/dfs/dfs_controller.cpp').read_text(encoding='utf-8')
        start = controller.index('void DfsController::request_file(')
        stop = controller.index('\nstd::expected<Dfs::DirRow, Dfs::DfsError> DfsController::read_file_status_self(', start)
        insertion = controller[start:stop]
        loader = (repo / 'sources/dfs/load_manager.cpp').read_text(encoding='utf-8')
        start = loader.index('void LoadManager::add_to_queue(const ActorId&')
        start = loader.index('{', start) + 1
        stop = loader.index('\n    if (is_priority) {', start)
        preamble = loader[start:stop]
        cls.temporary = tempfile.TemporaryDirectory(prefix='dfs forced download ')
        cls.addClassCleanup(cls.temporary.cleanup)
        directory = Path(cls.temporary.name)
        cls.binary = directory / ('fixture.exe' if os.name == 'nt' else 'fixture')
        source = directory / 'fixture.cpp'
        source.write_text(r'''
#include <atomic>
#include <barrier>
#include <compare>
#include <set>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>
#include "utils/safeptr.h"

using ActorId = int;
std::atomic<bool> node_enabled{true};
template<class... T> void eLog(const char*, T&&...) {}
enum class DfsMode { Light, Full };
enum class MessageType { DfsFileState };
enum class SendMode { Neighbours };
enum class MessageStatus { Request };
namespace Dfs {
enum class FileType { File, Folder };
struct FileLink {
    ActorId owner_id;
    std::string file_id;
    auto operator<=>(const FileLink&) const = default;
};
struct DirRow { std::string file_id = "file"; FileType type = FileType::File; };
}
struct DfsController;
struct Network {
    std::atomic<int> requests{0};
    template<class... T> void send_message(T&&...) { ++requests; }
};
struct Node {
    DfsController* controller = nullptr;
    Network peer;
    auto dfs() { return controller; }
    auto network() { return &peer; }
};
struct DfsController {
    Node* node;
''' + declaration.group(0) + r'''
    bool is_priority(const Dfs::FileLink&) const { return false; }
    DfsMode mode() const { return DfsMode::Light; }
    void request_file(const ActorId&, const std::string&);
};
static_assert(std::is_same_v<decltype(std::declval<DfsController>().forces_files_),
                             SafePtr<std::set<Dfs::FileLink>>>,
              "force-set accesses require the existing lock-owning wrapper");
''' + insertion + r'''
struct LoadManager {
    Node* node;
    bool consumed = false;
    int admitted = 0;
    void probe(const ActorId& owner_id, const Dfs::DirRow& dir_row) {
''' + preamble + r'''
        consumed = is_forced;
        ++admitted;
    }
};
int exercise(const std::string& mode) {
    Node node;
    DfsController controller{&node};
    node.controller = &controller;
    Dfs::DirRow row;
    if (mode == "concurrent") {
        for (int round = 0; round < 3; ++round) {
            controller.request_file(1, "file");
            std::barrier start(16);
            std::atomic<int> consumers{0};
            std::vector<std::thread> workers;
            for (int index = 0; index < 16; ++index) {
                workers.emplace_back([&] {
                    LoadManager loader{&node};
                    start.arrive_and_wait();
                    loader.probe(1, row);
                    if (loader.consumed) { ++consumers; }
                });
            }
            for (auto& worker : workers) { worker.join(); }
            if (consumers != 1) { return 1; }
        }
        return node.peer.requests == 3 ? 0 : 2;
    }
    if (mode == "producers") {
        std::atomic<int> consumers{0};
        std::vector<std::thread> workers;
        for (int owner = 1; owner <= 8; ++owner) {
            workers.emplace_back([&, owner] {
                for (int file = 0; file < 100; ++file) {
                    Dfs::DirRow current{std::to_string(file)};
                    controller.request_file(owner, current.file_id);
                    LoadManager loader{&node};
                    loader.probe(owner, current);
                    if (loader.consumed) { ++consumers; }
                }
            });
        }
        for (auto& worker : workers) { worker.join(); }
        return consumers == 800 && node.peer.requests == 800 ? 0 : 3;
    }
    controller.request_file(1, "file");
    if (mode == "stopped") { node_enabled.store(false); }
    if (mode == "folder") { row.type = Dfs::FileType::Folder; }
    if (mode == "different-file") { row.file_id = "other"; }
    LoadManager first{&node};
    first.probe(1, row);
    if (mode == "one-shot") {
        if (!first.consumed || first.admitted != 1) { return 4; }
    } else {
        if (first.consumed) { return 5; }
        if ((mode == "stopped" || mode == "folder") && first.admitted != 0) { return 6; }
        node_enabled.store(true);
        row = Dfs::DirRow{};
    }
    LoadManager second{&node};
    second.probe(1, row);
    return second.admitted == 1 && second.consumed == (mode != "one-shot") ? 0 : 7;
}
int main(int argc, char** argv) { return argc == 2 ? exercise(argv[1]) : 8; }
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/I' + str(repo / 'headers'),
                       str(source), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-pthread', '-I', str(repo / 'headers'),
                       str(source), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def check(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, 'force-admission preamble failed ' + mode)

    def test_consumes_hint_once(self):
        self.check('one-shot')

    def test_does_not_consume_another_file_hint(self):
        self.check('different-file')

    def test_stopped_node_preserves_hint(self):
        self.check('stopped')

    def test_folder_preserves_hint(self):
        self.check('folder')

    def test_only_one_concurrent_worker_consumes_each_hint(self):
        self.check('concurrent')

    def test_concurrent_producers_and_consumers(self):
        self.check('producers')


if __name__ == '__main__':
    unittest.main()
