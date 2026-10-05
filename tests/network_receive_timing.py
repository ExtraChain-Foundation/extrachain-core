"""Compile the production receive timer with a controlled clock and log sink."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ReceiveTimingTests(unittest.TestCase):
    def test_scoped_diagnostics(self):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            self.skipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root / 'sources/network/network_manager.cpp').read_text(encoding='utf-8')
        start = source.index('class ReceiveStageTimer {')
        implementation = source[start:source.index('\n};', start) + 3]
        fixture = r'''
#include <chrono>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
struct ReceiveClock {
    using time_point = std::chrono::steady_clock::time_point;
    static inline std::int64_t milliseconds = 0;
    static time_point now() { return time_point(std::chrono::milliseconds(milliseconds)); }
};
int messages = 0;
int lastType = -1;
std::int64_t lastElapsed = 0;
std::string lastStage;
bool logThrows = false;
void require(bool ok) { if (!ok) { throw std::runtime_error("receive timing regression"); } }
void eWarning(const char* format, const char* stage, int type, std::int64_t elapsed) {
    require(std::string(format) == "[NetworkManager] Slow receive stage: stage={} type={} elapsed_ms={}");
    ++messages;
    lastStage = stage;
    lastType = type;
    lastElapsed = elapsed;
    if (logThrows) { throw std::runtime_error("synthetic log failure"); }
}
''' + implementation + r'''
void earlyReturn() {
    ReceiveStageTimer timer("prepare");
    ReceiveClock::milliseconds += 101;
    return;
}
int main() {
    static_assert(std::is_nothrow_destructible_v<ReceiveStageTimer>);
    static_assert(!std::is_copy_constructible_v<ReceiveStageTimer>);
    try {
        { ReceiveStageTimer timer("prepare"); ReceiveClock::milliseconds += 99; }
        require(messages == 0);
        { ReceiveStageTimer timer("dispatch", 15); ReceiveClock::milliseconds += 100; }
        require(messages == 1 && lastType == 15 && lastElapsed == 100 && lastStage == "dispatch");
        { ReceiveStageTimer timer("prepare"); timer.set_type(55); ReceiveClock::milliseconds += 103; }
        require(messages == 2 && lastType == 55 && lastElapsed == 103);
        earlyReturn();
        require(messages == 3 && lastType == -1 && lastElapsed == 101);
        bool originalException = false;
        logThrows = true;
        try {
            ReceiveStageTimer timer("reputation-read", 39);
            ReceiveClock::milliseconds += 500;
            throw std::logic_error("original work failure");
        } catch (const std::logic_error&) { originalException = true; }
        require(originalException && messages == 4 && lastType == 39 && lastElapsed == 500);
        { ReceiveStageTimer timer("reputation-expire"); ReceiveClock::milliseconds += 100; }
        require(messages == 5 && lastType == -1);
        return 0;
    } catch (...) { return 2; }
}
'''
        with tempfile.TemporaryDirectory(prefix='receive-timing-regression-') as temporary:
            folder = Path(temporary)
            cpp = folder / 'fixture.cpp'
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            cpp.write_text(fixture, encoding='utf-8')
            if Path(compiler).name.lower() in ('cl', 'cl.exe'):
                command = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', str(cpp), '/Fe:' + str(binary)]
            else:
                command = [compiler, '-std=c++20', '-O2', str(cpp), '-o', str(binary)]
            compiled = subprocess.run(command, cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
