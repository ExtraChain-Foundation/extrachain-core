"""Compile the actual receive prefix; no sockets, crypto or full-node claims."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReceiveFrameTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        source_root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (source_root / 'sources/network/network_manager.cpp').read_text(encoding='utf-8')
        start = source.index('void NetworkManager::message_received(')
        prefix = source[start:source.index('    auto message_body_expected =', start)]
        cls.temporary = tempfile.TemporaryDirectory(prefix='receive-frame-regression-')
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        cpp = folder / 'fixture.cpp'
        cls.binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(r'''
#include <atomic>
#include <stdexcept>
#include <string>
#include <string_view>
std::atomic_bool node_enabled{true};
int warnings = 0;
template<class... T> void eWarning(const char*, T&&...) { ++warnings; }
template<class... T> void eLog(const char*, T&&...) {}
void require(bool ok) { if (!ok) { throw std::runtime_error("receive prefix regression"); } }
struct NetworkManager {
    bool accept = true;
    int cached = 0, parsed = 0;
    std::string body, signature;
    bool check_message_count(const std::string&) { ++cached; return accept; }
    void message_received(const std::string&, const std::string&, const std::string&);
};
''' + prefix + r'''
    ++parsed;
    body = msg;
    signature = sign;
}
int main(int argc, char** argv) {
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        NetworkManager manager;
        std::string signature(64, '\0');
        for (size_t i = 0; i < signature.size(); ++i) { signature[i] = char(i * 3); }
        const auto deliver = [&](const std::string& frame) {
            manager.message_received(frame, "synthetic-peer", "synthetic-connection");
        };
        if (mode == "short") {
            for (size_t size = 0; size <= 64; ++size) { deliver(std::string(size, 'x')); }
            require(manager.cached == 0 && manager.parsed == 0);
        } else if (mode == "minimum") {
            deliver(std::string(1, '\0') + signature);
            require(manager.cached == 1 && manager.parsed == 1 && warnings == 0);
            require(manager.body == std::string(1, '\0') && manager.signature == signature);
        } else if (mode == "binary") {
            const std::string body("a\0b\xff" "c", 5);
            deliver(body + signature);
            require(manager.cached == 1 && manager.parsed == 1 && warnings == 0);
            require(manager.body == body && manager.signature == signature);
        } else if (mode == "duplicate") {
            manager.accept = false;
            deliver("body" + signature);
            require(manager.cached == 1 && manager.parsed == 0 && warnings == 0);
        } else if (mode == "disabled") {
            node_enabled = false;
            deliver("body" + signature);
            deliver("short");
            require(manager.cached == 0 && manager.parsed == 0 && warnings == 0);
        } else if (mode == "subsequent") {
            deliver("short");
            deliver("body" + signature);
            require(manager.cached == 1 && manager.parsed == 1);
            require(manager.body == "body" && manager.signature == signature);
        } else { return 3; }
        return 0;
    } catch (...) { return 2; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', str(cpp), '/Fe:' + str(cls.binary)]
        else:
            command = [compiler, '-std=c++20', '-O2', str(cpp), '-o', str(cls.binary)]
        result = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def test_receive_boundaries(self):
        for mode in ('short', 'minimum', 'binary', 'duplicate', 'disabled', 'subsequent'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, mode + ': ' + result.stderr)


if __name__ == '__main__':
    unittest.main()
