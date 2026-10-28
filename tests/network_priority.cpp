#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <vector>

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"

// Relay envelopes carry every consensus message type. Storage-proof intents are sent at
// normal priority so that they do not share the lane of votes, proposals and pack chunks.
namespace {
    class Recorder final : public SocketService {
    public:
        explicit Recorder(ExtraChain::Core::ExtraChainNode& node)
            : SocketService(*node.network()) {
            identifier_ = std::string(64, 'e');
            activated_  = true;
        }

        std::mutex             mutex;
        std::vector<Priority> priorities;

        std::string protocol_string() const override {
            return "test";
        }
        Network::Protocol protocol() const override {
            return Network::Protocol::WebSocket;
        }
        bool is_active() const override {
            return activated_.load();
        }
        std::uint16_t port() const override {
            return 0;
        }
        std::uint16_t server_port() const override {
            return 0;
        }
        void flush() override {
        }
        void send_message(std::span<const std::uint8_t>, Priority priority) override {
            std::lock_guard lock(mutex);
            priorities.push_back(priority);
        }
    };
} // namespace

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-network-priority-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("network-priority", ActorType::User, owner);
    auto recorder = std::make_shared<Recorder>(*node);
    node->network()->connections()->insert(recorder);

    const auto send = [&] {
        Responder responder(node->network());
        responder.add_identifier(recorder->identifier());
        node->network()->send_message(std::string("pack-chunk-request"),
                                      MessageType::DagPackRequest,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder);
    };
    send();
    {
        NetworkService::PriorityScope bulk(SocketService::Priority::Normal);
        send();
    }
    send();

    std::vector<SocketService::Priority> seen;
    {
        std::lock_guard lock(recorder->mutex);
        seen = recorder->priorities;
    }
    node->network()->connections()->erase(recorder);
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    TEST_REQUIRE_EQ(seen.size(), std::size_t(3));
    TEST_REQUIRE(seen[0] == SocketService::Priority::High);
    TEST_REQUIRE(seen[1] == SocketService::Priority::Normal);
    TEST_REQUIRE(seen[2] == SocketService::Priority::High);
    std::puts("PASS");
}
