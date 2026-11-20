"""Compile actual node shutdown bodies with dependency-order observers, not a full node."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReceiveShutdownTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root / 'sources/managers/extrachain_node.cpp').read_text(encoding='utf-8')
        start = source.index('ExtraChainNode::~ExtraChainNode()')
        bodies = source[start:source.index('bool ExtraChainNode::create_new_network(', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='receive-shutdown-regression-')
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        cpp = folder / 'fixture.cpp'
        cls.binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(r'''
#include <atomic>
#include <functional>
#include <string>
#include <vector>
std::atomic_bool node_enabled{true};
std::vector<std::string> events;
template<class... T> void eLog(const char*, T&&...) {}
struct NetworkManager {
    bool stopped = false;
    void stop_receive() {
        if (node_enabled) { events.push_back("enabled-during-stop"); }
        stopped = true;
        events.push_back("joined");
    }
    void deleteLater() { events.push_back(stopped ? "delete-network" : "unsafe-network"); }
};
struct Dependency {
    NetworkManager* network;
    ~Dependency() { events.push_back(network->stopped ? "delete-dependency" : "unsafe-dependency"); }
    void deleteLater() { events.push_back(network->stopped ? "defer-dependency" : "unsafe-dependency"); }
};
struct ExtraChainNode {
    NetworkManager* network_manager_ = nullptr;
    Dependency *dag_ = nullptr, *dfs_ = nullptr, *chat_manager_ = nullptr;
    std::function<void()> cleanup_callback_;
    ~ExtraChainNode();
    void cleanUp();
};
''' + bodies + r'''
int main(int argc, char** argv) {
    if (argc != 2) { return 3; }
    const std::string mode = argv[1];
    NetworkManager manager;
    Dependency dfs{&manager};
    {
        ExtraChainNode node;
        if (mode == "partial") { return 0; }
        node.network_manager_ = &manager;
        if (mode == "destructor") {
            node.cleanup_callback_ = [&] {
                events.push_back(manager.stopped ? "callback" : "unsafe-callback");
            };
        } else if (mode == "cleanup") {
            node.dag_ = new Dependency{&manager};
            node.dfs_ = &dfs;
            node.chat_manager_ = new Dependency{&manager};
            node.cleanUp();
            node.network_manager_ = nullptr;
        } else { return 3; }
    }
    const std::vector<std::string> expected = mode == "destructor"
        ? std::vector<std::string>{"joined", "callback"}
        : std::vector<std::string>{"joined", "delete-dependency", "delete-network", "defer-dependency", "delete-dependency"};
    return events == expected ? 0 : 2;
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', str(cpp), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-O2', str(cpp), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_node_destructor_joins_before_cleanup_callback(self):
        self.check_case('destructor')

    def test_thread_cleanup_joins_before_dependency_deletion(self):
        self.check_case('cleanup')

    def test_uninitialized_node_destruction(self):
        self.check_case('partial')

    def check_case(self, mode):
        result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, mode + ': ' + result.stderr)


if __name__ == '__main__':
    unittest.main()
