"""Compile actual activation wiring and DFS size senders against a routing observer."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class DfsSizeFanoutTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        dfs = (root / 'sources/dfs/dfs_controller.cpp').read_text(encoding='utf-8')
        start = dfs.index('void DfsController::sendSizeRequestMsg(')
        senders = dfs[start:dfs.index('void DfsController::sendSizeReponseMsg(', start)]
        source = (root / 'sources/managers/extrachain_node.cpp').read_text(encoding='utf-8')
        call = source.index('dfs_->sendSizeRequestMsg(')
        begin = source.rfind('    connect(', 0, call)
        end = source.index('\n', source.index('});', call))
        wiring = source[begin:end]
        cls.folder = tempfile.TemporaryDirectory(prefix='dfs-size-fanout-')
        cls.addClassCleanup(cls.folder.cleanup)
        folder = Path(cls.folder.name)
        cpp = folder / 'fixture.cpp'
        cls.binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(r'''
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
void require(bool ok) { if (!ok) throw std::runtime_error("DFS size routing contract"); }
using ActorId = int;
namespace DfsP { struct RequestDfsSize { ActorId actorId; }; }
enum class MessageType { RequestDfsSize };
enum class SendMode { Neighbours, Focused };
enum class MessageStatus { Request };
class NetworkManager;
struct Responder {
    std::unordered_set<std::string> identifiers;
    explicit Responder(NetworkManager* = nullptr) {}
    bool add_identifier(const std::string& id) { return !id.empty() && identifiers.insert(id).second; }
};
class NetworkManager {
public:
    std::vector<std::function<void()>> unscoped;
    std::vector<std::function<void(std::string, std::string)>> scoped;
    std::unordered_set<std::string> peers;
    std::vector<std::string> recipients;
    bool ownerContext = false;
    void newSocketActivated() { for (const auto& callback : unscoped) callback(); }
    void newSocketActivatedWithParams(std::string ip, std::string identifier) {
        for (const auto& callback : scoped) callback(ip, identifier);
    }
    void activate(const std::string& id) {
        peers.insert(id);
        newSocketActivatedWithParams("synthetic-ip", id);
        newSocketActivated();
    }
    void send_message(const DfsP::RequestDfsSize& msg, MessageType type, SendMode mode,
                      MessageStatus status, const Responder& responder = Responder()) {
        require(msg.actorId == 7 && type == MessageType::RequestDfsSize && status == MessageStatus::Request);
        for (const auto& peer : peers) {
            if (mode == SendMode::Neighbours || responder.identifiers.contains(peer)) recipients.push_back(peer);
        }
    }
};
template<class... Args, class Callback>
void connect(NetworkManager* manager, void(NetworkManager::*)(Args...), Callback callback) {
    if constexpr (sizeof...(Args) == 0) manager->unscoped.push_back(callback);
    else manager->scoped.push_back(callback);
}
template<class... Args, class Context, class Callback>
void connect(NetworkManager* manager, void(NetworkManager::*signal)(Args...), Context*, Callback callback) {
    manager->ownerContext = true;
    connect(manager, signal, callback);
}
struct AccountController {
    struct Actor { ActorId id() const { return 7; } };
    Actor system_actor() const { return {}; }
};
class ExtraChainNode;
class DfsController {
public:
    ExtraChainNode* node;
    explicit DfsController(ExtraChainNode* value) : node(value) {}
    void sendSizeRequestMsg(const ActorId&) const;
    void sendSizeRequestMsg(const ActorId&, const std::string&) const;
};
class ExtraChainNode {
public:
    NetworkManager manager;
    AccountController accounts;
    DfsController dfs{this};
    NetworkManager* network_manager_ = &manager;
    AccountController* account_controller_ = &accounts;
    DfsController* dfs_ = &dfs;
    NetworkManager* network() { return &manager; }
    void wire() {
''' + wiring + r'''
    }
};
''' + senders + r'''
int main(int argc, char** argv) {
    if (argc != 2) return 3;
    try {
        ExtraChainNode node;
        auto& network = node.manager;
        network.peers = {"baseline-a", "baseline-b", "baseline-c"};
        node.wire();
        const std::string mode = argv[1];
        if (mode == "one-peer") {
            network.activate("new-peer");
            require(network.recipients == std::vector<std::string>{"new-peer"});
        } else if (mode == "hundred-peers") {
            for (int i = 0; i < 100; ++i) {
                const auto id = "new-peer-" + std::to_string(i);
                network.activate(id);
            }
            if (network.recipients.size() != 100) {
                throw std::runtime_error("packet-count=" + std::to_string(network.recipients.size()));
            }
            for (int i = 0; i < 100; ++i) {
                require(network.recipients[i] == "new-peer-" + std::to_string(i));
            }
        } else if (mode == "empty-peer") {
            network.newSocketActivatedWithParams("synthetic-ip", "");
            network.newSocketActivated();
            require(network.recipients.empty());
        } else if (mode == "explicit-all") {
            node.dfs.sendSizeRequestMsg(7);
            require(network.recipients.size() == network.peers.size());
        } else if (mode == "reconnect") {
            network.activate("new-peer");
            network.activate("new-peer");
            require(network.recipients == std::vector<std::string>{"new-peer", "new-peer"});
        } else if (mode == "context") {
            require(network.ownerContext);
        } else return 3;
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what(); return 2; }
}
''', encoding='utf-8')
        if Path(compiler).name.lower() in ('cl', 'cl.exe'):
            args = [compiler, '/nologo', '/std:c++20', '/EHsc', '/O2', str(cpp), '/Fe:' + str(cls.binary)]
        else:
            args = [compiler, '-std=c++20', '-O2', str(cpp), '-o', str(cls.binary)]
        built = subprocess.run(args, cwd=folder, capture_output=True, text=True, timeout=60)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def test_routing_contract(self):
        for mode in ('one-peer', 'hundred-peers', 'empty-peer', 'explicit-all', 'reconnect', 'context'):
            with self.subTest(mode=mode):
                run = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
                self.assertEqual(run.returncode, 0, mode + ': ' + run.stderr)


if __name__ == '__main__':
    unittest.main()
