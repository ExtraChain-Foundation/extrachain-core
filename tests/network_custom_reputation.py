"""Compile actual Responder/setup/Custom code with DB and decoding observers."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class CustomReputationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = shutil.which(os.environ.get('RACCOON_TEST_CXX', 'c++'))
        if not compiler:
            raise unittest.SkipTest('a C++20 compiler is required')
        root = Path(os.environ.get('RACCOON_TEST_SOURCE_ROOT', Path(__file__).resolve().parents[1]))
        source = (root / 'sources/network/network_manager.cpp').read_text(encoding='utf-8')
        header = (root / 'headers/network/network_manager.h').read_text(encoding='utf-8')
        start = source.index('void NetworkManager::message_received(')
        setup_start = source.index('    Responder responder(this);', start)
        setup = source[setup_start:source.index('#ifdef QT_DEBUG', setup_start)]
        increment_start = source.index('    if (send_type == SendMode::Broadcast', setup_start)
        increment = source[increment_start:source.index('    // try {', increment_start)]
        custom_start = source.index('    case MessageType::Custom:', increment_start)
        custom = source[custom_start:source.index('    case MessageType::ShareConnections:', custom_start)]
        responder_start = header.index('class Responder {')
        responder = header[responder_start:header.index('\n};', responder_start) + 3]
        cls.temporary = tempfile.TemporaryDirectory(prefix='custom-reputation-regression-')
        cls.addClassCleanup(cls.temporary.cleanup)
        folder = Path(cls.temporary.name)
        cpp = folder / 'fixture.cpp'
        cls.binary = folder / ('fixture.exe' if os.name == 'nt' else 'fixture')
        cpp.write_text(r'''
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>
#define emit
enum class MessageType { Custom, ShareConnections };
enum class SendMode { Broadcast, Focused };
enum class MessageStatus { NoStatus };
struct NodeId { int actor_id; std::string node_identifier; };
struct CustomMessage { std::string body; };
struct NetworkPackageStorage { std::string message_id, identifier; };
class NetworkManager;
std::string generate_message_id() { return "unused-synthetic-id"; }
namespace MessagePack {
template<class T> std::string serialize(const T&) { return {}; }
template<class T> std::optional<T> deserialize(const std::string& body) {
    if (body == "invalid") { return std::nullopt; }
    return T{body};
}
}
int warnings = 0;
template<class... T> void eWarning(const char*, T&&...) { ++warnings; }
void require(bool ok) { if (!ok) { throw std::runtime_error("dispatch contract"); } }
''' + responder + r'''
struct ReputationObserver {
    int value = 7, reads = 0, writes = 0;
    bool unavailable = false;
    std::vector<std::string> order;
    int read_luminance(const NodeId&) {
        ++reads;
        order.push_back("read");
        if (unavailable) { throw std::runtime_error("unexpected database access"); }
        return value;
    }
    void increment(const NodeId&) {
        ++writes;
        order.push_back("increment");
        if (unavailable) { throw std::runtime_error("unexpected database access"); }
    }
};
struct Node {
    bool is_custom_app_ = true;
    ReputationObserver reputation;
    ReputationObserver* luminance_manager() { return &reputation; }
};
class NetworkManager {
public:
    Node* node;
    int emitted = 0, forwarded = 0;
    std::optional<Responder> non_custom;
    explicit NetworkManager(Node* value) : node(value) {}
    void customMessageReceived(const NetworkPackageStorage& package, const CustomMessage& custom) {
        require(package.message_id == "synthetic-id" && package.identifier == "synthetic-connection");
        require(custom.body == "payload");
        ++emitted;
    }
    void send_broadcast_message_further(const NetworkPackageStorage& package) {
        require(package.message_id == "synthetic-id" && package.identifier == "synthetic-connection");
        ++forwarded;
    }
    void dispatch(MessageType, SendMode, bool, bool);
};
void NetworkManager::dispatch(MessageType type, SendMode send_type, bool is_luminance, bool valid) {
    const std::string message_id = "synthetic-id", identifier = "synthetic-connection", ip = "synthetic-peer";
    const NodeId node_id{11, "synthetic-node"};
    const std::string serialized = valid ? "payload" : "invalid";
    const NetworkPackageStorage package_data{message_id, identifier};
''' + setup + increment + '\n    switch (type) {\n' + custom + r'''
    default: non_custom = responder; break;
    }
}
int main(int argc, char** argv) {
    if (argc != 2) { return 3; }
    try {
        const std::string mode = argv[1];
        if (mode == "custom-local" || mode == "custom-forward" || mode == "custom-invalid") {
            for (bool root : {false, true}) {
                for (auto send : {SendMode::Broadcast, SendMode::Focused}) {
                    for (bool app : {false, true}) {
                        Node node;
                        node.is_custom_app_ = mode == "custom-invalid" ? app : mode == "custom-local";
                        node.reputation.unavailable = true;
                        NetworkManager manager(&node);
                        warnings = 0;
                        manager.dispatch(MessageType::Custom, send, root, mode != "custom-invalid");
                        require(node.reputation.reads == 0 && node.reputation.writes == 0);
                        require(!manager.non_custom.has_value());
                        require(manager.emitted == (mode == "custom-local" ? 1 : 0));
                        require(manager.forwarded == (mode == "custom-forward" ? 1 : 0));
                        require(warnings == (mode == "custom-invalid" ? 1 : 0));
                    }
                }
            }
        } else if (mode == "non-custom-focused" || mode == "non-custom-broadcast") {
            for (int value : {-1, 0, 1, 7}) {
                for (bool root : {false, true}) {
                    Node node;
                    node.reputation.value = value;
                    NetworkManager manager(&node);
                    const bool broadcast = mode == "non-custom-broadcast";
                    manager.dispatch(MessageType::ShareConnections,
                                     broadcast ? SendMode::Broadcast : SendMode::Focused, root, true);
                    require(manager.non_custom.has_value());
                    const auto& responder = *manager.non_custom;
                    require(responder.luminance() == (value == -1 ? 1 : value) * (root ? 10 : 1));
                    require(responder.node_id().actor_id == 11 && responder.node_id().node_identifier == "synthetic-node");
                    require(responder.message_id() == "synthetic-id" && responder.ip() == "synthetic-peer");
                    require(responder.identifiers() == std::unordered_set<std::string>{"synthetic-connection"});
                    const std::vector<std::string> expected = broadcast ? std::vector<std::string>{"read", "increment"}
                                                                        : std::vector<std::string>{"read"};
                    require(node.reputation.order == expected && node.reputation.reads == 1);
                    require(node.reputation.writes == (broadcast ? 1 : 0));
                    require(manager.emitted == 0 && manager.forwarded == 0);
                }
            }
        } else if (mode == "interleaved") {
            Node node;
            NetworkManager manager(&node);
            manager.dispatch(MessageType::ShareConnections, SendMode::Broadcast, true, true);
            require(manager.non_custom->luminance() == 70);
            const auto order = node.reputation.order;
            manager.dispatch(MessageType::Custom, SendMode::Broadcast, true, true);
            node.is_custom_app_ = false;
            manager.dispatch(MessageType::Custom, SendMode::Focused, false, true);
            require(node.reputation.order == order && node.reputation.reads == 1 && node.reputation.writes == 1);
            require(manager.emitted == 1 && manager.forwarded == 1);
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

    def test_custom_and_non_custom_dispatch(self):
        for mode in ('custom-local', 'custom-forward', 'custom-invalid', 'non-custom-focused',
                     'non-custom-broadcast', 'interleaved'):
            with self.subTest(mode=mode):
                result = subprocess.run([str(self.binary), mode], capture_output=True, text=True, timeout=10)
                self.assertEqual(result.returncode, 0, mode + ': ' + result.stderr)


if __name__ == '__main__':
    unittest.main()
