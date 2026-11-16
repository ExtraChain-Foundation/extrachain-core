#include "core/extrachain_node.h"
#include "network/network_service.h"
#include "test_support.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <random>
#include <thread>

namespace {
    class Peer final : public SocketService {
    public:
        explicit Peer(PeerContext &context)
            : SocketService(context) {
            identifier_ = std::string(64, 'c');
            activated_  = true;
            peer_meta_  = PeerMeta { };
        }
        bool is_active() const override {
            return activated_.load();
        }
        std::string protocol_string() const override {
            return "test";
        }
        Network::Protocol protocol() const override {
            return Network::Protocol::WebSocket;
        }
        std::uint16_t port() const override {
            return 0;
        }
        std::uint16_t server_port() const override {
            return 0;
        }
        void flush() override {
        }
        void send_message(std::span<const std::uint8_t>, Priority) override {
        }
        void close_connection() override {
            closes_.fetch_add(1);
        }
        int closes() const {
            return closes_.load();
        }

    private:
        std::atomic<int> closes_ { 0 };
    };
} // namespace

int main() {
    using Status         = ExtraChain::Core::NetworkStatus::Status;
    const auto original  = std::filesystem::current_path();
    const auto directory = std::filesystem::temp_directory_path()
                           / ("extrachain-offline-debounce-" + std::to_string(std::random_device {}()));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    {
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
        node->process();
        auto peer = std::make_shared<Peer>(*node->network());
        node->network()->connections()->insert(peer);
        auto &status = node->network()->network_status();
        status.update(Status::Online);

        // A reachability blip shorter than the delay keeps every connection.
        status.update(Status::Offline);
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        status.update(Status::Online);
        std::this_thread::sleep_for(std::chrono::milliseconds(2500));
        TEST_REQUIRE_EQ(peer->closes(), 0);

        // Offline that lasts closes them.
        status.update(Status::Offline);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
        while (peer->closes() == 0 && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        TEST_REQUIRE(peer->closes() > 0);

        node->network()->connections()->erase(peer);
        node->cleanUp();
    }
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
