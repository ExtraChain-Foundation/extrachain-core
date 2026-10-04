"""Check work avoided by the actual synchronizer header, independent of wall time."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ActorCacheWorkTests(unittest.TestCase):
    def test_unchanged_buckets_do_not_rescan_actor_strings(self):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            self.skipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        with tempfile.TemporaryDirectory(prefix='actor-cache-work-') as temporary:
            folder = Path(temporary)
            (folder / 'chain').mkdir()
            (folder / 'chain/actor_id.h').write_text(r'''
#pragma once
#include <string>
#include <utility>
inline unsigned actor_string_reads = 0;
inline unsigned actor_copies = 0, actor_moves = 0, actor_assignments = 0;
struct ActorId {
    std::string value;
    explicit ActorId(std::string text) : value(std::move(text)) {}
    ActorId(const ActorId& other) : value(other.value) { ++actor_copies; }
    ActorId(ActorId&& other) noexcept : value(std::move(other.value)) { ++actor_moves; }
    ActorId& operator=(const ActorId& other) {
        value = other.value; ++actor_assignments; return *this;
    }
    ActorId& operator=(ActorId&& other) noexcept {
        value = std::move(other.value); ++actor_assignments; return *this;
    }
    const std::string& to_string() const { ++actor_string_reads; return value; }
    bool operator<(const ActorId& other) const { return value < other.value; }
};
''', encoding='utf-8')
            cpp = folder / 'fixture.cpp'
            cpp.write_text(r'''
#include "chain/actor_filter.h"
#include <stdexcept>
void require(bool condition) { if (!condition) throw std::runtime_error("cache work contract"); }
int main() {
    std::vector<ActorId> actors;
    for (int i = 0; i < 35000; ++i) actors.emplace_back(std::to_string(i));
    ActorSynchronizer synchronizer;
    synchronizer.set_actors(actors);
    const auto first = synchronizer.create_sync_request();
    actor_string_reads = 0;
    for (int i = 0; i < 100; ++i) {
        require(synchronizer.create_sync_request() == first);
        require(synchronizer.process_sync_request(first).empty());
    }
    require(actor_string_reads == 0);
    ActorSynchronizer empty;
    const auto empty_request = empty.create_sync_request();
    for (int i = 0; i < 100; ++i) {
        actor_copies = actor_moves = actor_assignments = 0;
        const auto missing = synchronizer.process_sync_request(empty_request);
        require(missing.size() == actors.size());
        require(std::is_sorted(missing.begin(), missing.end()));
        require(actor_copies == actors.size());
        require(actor_moves == 0 && actor_assignments == 0);
    }
    require(actor_string_reads == 0);
    const ActorId added("35000");
    synchronizer.apply_received_ids({added});
    const auto changed = synchronizer.create_sync_request();
    require(changed != first);
    require(actor_string_reads > 1 && actor_string_reads < 1000);
    synchronizer.apply_received_ids({added});
    actor_string_reads = 0;
    require(synchronizer.create_sync_request() == changed);
    require(!synchronizer.process_sync_request(first).empty());
    require(actor_string_reads == 0);
}
''', encoding='utf-8')
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            if Path(compiler).name.lower() in ('cl', 'cl.exe'):
                args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', '/I' + str(folder),
                        '/I' + str(root / 'headers'), str(cpp), '/Fe:' + str(binary)]
            else:
                args = [compiler, '-std=c++20', '-O2', '-I' + str(folder),
                        '-I' + str(root / 'headers'), str(cpp), '-o', str(binary)]
            built = subprocess.run(args, cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
