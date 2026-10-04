"""Compile the real pool wrapper and Asio to verify task ownership and copy cost."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ThreadPoolWorkTests(unittest.TestCase):
    def compile_and_run(self, body, modes):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            self.skipTest('a C++20 compiler is required')
        checkout = Path(__file__).resolve().parents[1]
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', checkout))
        boost = Path(os.environ.get('RACCOON_TEST_BOOST_INCLUDE',
                                    checkout.parent / 'vcpkg/installed/x64-windows/include'))
        if not (boost / 'boost/asio/thread_pool.hpp').is_file():
            self.skipTest('installed Boost headers are required')
        with tempfile.TemporaryDirectory(prefix='thread-pool-work-') as temporary:
            folder = Path(temporary)
            cpp = folder / 'fixture.cpp'
            cpp.write_text(r'''
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "sources/utils/thread_pool_boost.cpp"
using namespace std::chrono_literals;
void require(bool value) { if (!value) throw std::runtime_error("pool ownership contract"); }
''' + body, encoding='utf-8')
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            includes = [folder, root, root / 'headers', boost]
            if Path(compiler).name.lower() in ('cl', 'cl.exe'):
                args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2']
                args += ['/I' + str(path) for path in includes]
                args += [str(cpp), '/Fe:' + str(binary), '/link', 'ws2_32.lib', 'mswsock.lib']
            else:
                args = [compiler, '-std=c++20', '-O2', '-pthread']
                args += ['-I' + str(path) for path in includes]
                args += [str(cpp), '-o', str(binary)]
            built = subprocess.run(args, cwd=folder, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for mode in modes:
                with self.subTest(mode=mode):
                    run = subprocess.run([str(binary), mode], capture_output=True, text=True, timeout=10)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_value_categories_and_inline_dispatch(self):
        self.compile_and_run(r'''
struct State {
    std::atomic<int> copies{0};
    std::promise<bool> done;
};
struct Task {
    std::shared_ptr<State> state;
    std::vector<int> payload;
    explicit Task(std::shared_ptr<State> value) : state(std::move(value)), payload(35000, 7) {}
    Task(const Task& other) : state(other.state), payload(other.payload) { ++state->copies; }
    Task(Task&&) = default;
    void operator()() { state->done.set_value(payload.size() == 35000 && payload.back() == 7); }
};
int main(int argc, char** argv) {
    require(argc == 2);
    auto pool = ThreadPoolBoost::instance(1);
    const std::string mode = argv[1];
    auto state = std::make_shared<State>();
    auto done = state->done.get_future();
    Task task(state);
    if (mode == "post-rvalue") pool->post(std::move(task));
    else if (mode == "dispatch-rvalue") pool->dispatch(std::move(task));
    else if (mode == "post-lvalue") pool->post(task);
    else if (mode == "dispatch-lvalue") pool->dispatch(task);
    else if (mode == "post-const-lvalue") pool->post(std::as_const(task));
    else if (mode == "dispatch-const-lvalue") pool->dispatch(std::as_const(task));
    else if (mode == "nested-inline") {
        pool->post([pool, state] {
            auto inline_call = std::make_shared<std::atomic<bool>>(false);
            pool->dispatch([inline_call] { *inline_call = true; });
            state->done.set_value(inline_call->load());
        });
    } else return 2;
    require(done.wait_for(2s) == std::future_status::ready && done.get());
    pool->join();
    if (mode.find("rvalue") != std::string::npos) {
        require(state->copies == 0 && task.payload.empty());
    } else if (mode.find("lvalue") != std::string::npos) {
        require(state->copies >= 1 && task.payload.size() == 35000);
    }
}
''', ['post-rvalue', 'dispatch-rvalue', 'post-lvalue', 'dispatch-lvalue',
      'post-const-lvalue', 'dispatch-const-lvalue', 'nested-inline'])

    def test_move_only_tasks(self):
        self.compile_and_run(r'''
int main(int argc, char** argv) {
    require(argc == 2);
    auto pool = ThreadPoolBoost::instance(1);
    std::promise<bool> done;
    auto result = done.get_future();
    auto task = [owned = std::make_unique<int>(42), &done]() {
        done.set_value(owned && *owned == 42);
    };
    if (std::string(argv[1]) == "post") pool->post(std::move(task));
    else pool->dispatch(std::move(task));
    require(result.wait_for(2s) == std::future_status::ready && result.get());
    pool->join();
}
''', ['post', 'dispatch'])


if __name__ == '__main__':
    unittest.main()
