"""Compile actual actor-response readiness and batch-dispatch wiring."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class ActorPersistenceWiringTests(unittest.TestCase):
    def test_response_commit_and_readiness_boundaries(self):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            self.skipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root / 'sources/chain/actor_index.cpp').read_text(encoding='utf-8')
        response = source[source.index('void ActorIndex::network_actors_response('):
                          source.index('void ActorIndex::send_system_actor(')]
        start = source.index('void ActorIndex::network_actors_hash_request(')
        readiness = source[start:source.index('    auto r = responder;', start)] + '}\n'
        with tempfile.TemporaryDirectory(prefix='actor-persistence-wiring-') as temporary:
            folder = Path(temporary)
            cpp = folder / 'fixture.cpp'
            cpp.write_text(r'''
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#define emit
struct ActorId {
    std::string value;
    std::string to_string() const { return value; }
};
struct KeyPublic {};
template<class T> struct Actor {
    ActorId value;
    ActorId id() const { return value; }
};
struct Responder {};
struct Synchronizer {
    std::vector<ActorId> process_sync_request(const std::vector<uint8_t>&) { return {}; }
};
struct Accounts {
    int generated = 0;
    void dogenerate() { ++generated; }
};
struct Node {
    Accounts accounts;
    Accounts* account_controller() { return &accounts; }
};
template<class... T> void eLog(const char*, T&&...) {}
template<class... T> void eWarning(const char*, T&&...) {}
std::atomic<bool> node_enabled{true};
void require(bool ok) { if (!ok) { throw std::runtime_error("actor persistence wiring"); } }
class ActorIndex {
public:
    Node storage;
    Node* node = &storage;
    std::size_t records_ = 0;
    std::uint64_t synch_count_ = 0;
    bool sync_first_done_ = false, fail = false;
    std::map<std::string, Actor<KeyPublic>> actors_todo_map_;
    Synchronizer synch_;
    int batchCalls = 0, singleCalls = 0, initialCalls = 0, ended = 0;
    std::optional<int> save_actors() {
        ++initialCalls;
        if (fail) { return std::nullopt; }
        records_ += actors_todo_map_.size();
        actors_todo_map_.clear();
        return 0;
    }
    std::optional<int> save_actors(const std::vector<Actor<KeyPublic>>&) {
        ++batchCalls;
        return fail ? std::nullopt : std::optional<int>{0};
    }
    std::optional<int> save_actor(const Actor<KeyPublic>&) { ++singleCalls; return 0; }
    void firstSyncEnded() { ++ended; }
    void network_actors_response(const std::vector<Actor<KeyPublic>>& actors);
    void network_actors_hash_request(std::uint64_t, const std::vector<uint8_t>&, const Responder&);
};
''' + response + readiness + r'''
int main() {
    const std::vector<Actor<KeyPublic>> actors{{{"first"}}, {{"second"}}};
    ActorIndex initial;
    initial.fail = true;
    initial.network_actors_response(actors);
    require(!initial.sync_first_done_ && initial.ended == 0);
    require(initial.storage.accounts.generated == 0 && initial.actors_todo_map_.size() == 2);
    initial.network_actors_hash_request(0, {}, {});
    require(!initial.sync_first_done_ && initial.storage.accounts.generated == 0);
    initial.fail = false;
    initial.network_actors_response(actors);
    require(initial.sync_first_done_ && initial.ended == 1 && initial.initialCalls == 2);
    require(initial.actors_todo_map_.empty() && initial.storage.accounts.generated == 1);
    ActorIndex live;
    live.sync_first_done_ = true;
    live.network_actors_response(actors);
    require(live.batchCalls == 1 && live.singleCalls == 0);
    live.fail = true;
    live.network_actors_response(actors);
    require(live.batchCalls == 2 && live.singleCalls == 0 && live.ended == 0);
    node_enabled = false;
    live.network_actors_response(actors);
    require(live.batchCalls == 2);
    ActorIndex alreadyComplete;
    alreadyComplete.network_actors_hash_request(0, {}, {});
    require(alreadyComplete.sync_first_done_ && alreadyComplete.ended == 1);
}
''', encoding='utf-8')
            binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
            if Path(compiler).name.lower() in ('cl', 'cl.exe'):
                args = [compiler, '/nologo', '/std:c++20', '/EHsc', str(cpp), '/Fe:' + str(binary)]
            else:
                args = [compiler, '-std=c++20', str(cpp), '-o', str(binary)]
            built = subprocess.run(args, cwd=folder, capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == '__main__':
    unittest.main()
