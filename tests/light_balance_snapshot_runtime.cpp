#include "chain/actor_index.h"
#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"
#include "consensus/balance_snapshot.h"
#include "consensus/consensus_service.h"
#include "utils/file_io.h"
#include <chrono>
#include <thread>

namespace {
    class Peer final : public SocketService {
    public:
        explicit Peer(PeerContext &context)
            : SocketService(context) {
            identifier_ = std::string(64, 'a');
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
        void send_message(std::span<const std::uint8_t> bytes, Priority) override {
            if (bytes.size() <= crypto_sign_BYTES)
                return;
            const auto body = MessagePack::deserialize<MessageBody>(
                std::string_view(reinterpret_cast<const char *>(bytes.data()), bytes.size() - crypto_sign_BYTES));
            if (body.has_value() && body.value().message_type == MessageType::DagLightData) {
                std::lock_guard lock(mutex_);
                last_request_ = body.value().message_id;
            }
        }
        std::string last_request() {
            std::lock_guard lock(mutex_);
            return last_request_;
        }

    private:
        std::mutex  mutex_;
        std::string last_request_;
    };
} // namespace

bool test_balance_snapshot_runtime(const ExtraChain::Consensus::BalanceSnapshotV1 &snapshot,
                                   const ExtraChain::Consensus::BalanceSnapshotV1 &newer,
                                   const ExtraChain::Consensus::ValidatorSet      &validators) {
    using namespace ExtraChain::Consensus;
    struct Home {
        std::filesystem::path original  = std::filesystem::current_path();
        std::filesystem::path directory = std::filesystem::temp_directory_path()
                                          / ("extrachain-light-snapshot-" + Utils::generate_random_hex(8));
        Home() {
            std::filesystem::create_directories(directory);
            std::filesystem::current_path(directory);
        }
        ~Home() {
            std::filesystem::current_path(original);
            std::filesystem::remove_all(directory);
        }
    } home;
    try {
        Actor<KeyPrivate> account;
        account.create(ActorType::User);
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
        node->process();
        node->account_controller()->create_profile("light-snapshot", ActorType::User, account);
        node->actor_index()->set_network_id(validators.network_id);
        std::filesystem::create_directories("consensus");
        TEST_REQUIRE(FileIo::write_atomic("consensus/validator-set.msgpack", MessagePack::serialize(validators))
                         .has_value());
        auto &dag = *node->dag();
        dag.set_mode(DagMode::Full);
        const Balances initial { { { account.id(), ActorId { } }, BigNumberFloat(7) } };
        TEST_REQUIRE(dag.cache().write_cached_balances(initial, SectionId(20)));
        dag.set_current_section(SectionId(20));
        dag.set_status(DagStatus::Ready);
        auto peer = std::make_shared<Peer>(*node->network());
        node->network()->connections()->insert(peer);
        Responder response(node->network());
        response.add_identifier(peer->identifier());
        response = response.with_new_message_id();
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        const auto        payload   = MessagePack::serialize(snapshot);
        const auto        unchanged = [&] {
            return dag.cache().section() == SectionId(20)
                   && dag.cache().read_cached_balance(account.id(), ActorId { }) == BigNumberFloat(7);
        };
        dag.network_response_light(payload, response);
        TEST_REQUIRE(unchanged());
        dag.set_mode(DagMode::Light);
        TEST_REQUIRE(!dag.state_projection_ready());
        dag.network_response_light(payload, response);
        TEST_REQUIRE(unchanged());
        dag.request_light(response);
        const auto request_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (peer->last_request().empty() && std::chrono::steady_clock::now() < request_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        TEST_REQUIRE(!peer->last_request().empty());
        response.set_message_id(peer->last_request());
        dag.network_response_light(payload, response.with_new_message_id());
        Responder foreign = response;
        foreign.remove_identifier(peer->identifier());
        foreign.add_identifier(std::string(64, 'b'));
        dag.network_response_light(payload, foreign);
        TEST_REQUIRE(unchanged());
        dag.network_response_light("invalid", response);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        TEST_REQUIRE(unchanged());
        auto forged = snapshot;
        forged.balances.begin()->second += BigNumberFloat(1);
        dag.network_response_light(MessagePack::serialize(forged), response);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        TEST_REQUIRE(unchanged() && !dag.state_projection_ready());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (!dag.state_projection_ready() && std::chrono::steady_clock::now() < deadline) {
            dag.network_response_light(payload, response);
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        TEST_REQUIRE(dag.state_projection_ready());
        TEST_REQUIRE(dag.cache().section() == SectionId(snapshot.proof.finalized_proposal.header.dag_section));
        TEST_REQUIRE(dag.cache().read_cached_balances().second == snapshot.balances);
        TEST_REQUIRE(node->network_id() == validators.network_id);
        TEST_REQUIRE(!dag.read_section(SectionId(0)).has_value());
        dag.network_response_light(MessagePack::serialize(forged), response);
        TEST_REQUIRE(dag.cache().read_cached_balances().second == snapshot.balances);
        const auto request_again = [&] {
            const auto previous = peer->last_request();
            dag.request_light(response);
            const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (peer->last_request() == previous && std::chrono::steady_clock::now() < until) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            TEST_REQUIRE(peer->last_request() != previous);
            response.set_message_id(peer->last_request());
            return true;
        };
        TEST_REQUIRE(request_again());
        dag.network_response_light(MessagePack::serialize(newer), response);
        const auto newer_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        const auto newer_section  = SectionId(newer.proof.finalized_proposal.header.dag_section);
        while (dag.cache().section() != newer_section && std::chrono::steady_clock::now() < newer_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
        TEST_REQUIRE(dag.cache().section() == newer_section);
        TEST_REQUIRE(request_again());
        dag.network_response_light(payload, response);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        TEST_REQUIRE(dag.cache().section() == newer_section);
        TEST_REQUIRE(dag.cache().read_cached_balances().second == newer.balances);
        const auto restored = LightClientVerifier::load("consensus/light-client.msgpack");
        TEST_REQUIRE(restored.has_value()
                     && restored.value().trusted_height() == newer.proof.finalized_proposal.header.height);
        node->cleanUp();
        peer.reset();
        node.reset();
        node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
        node->process();
        node->actor_index()->set_network_id(validators.network_id);
        TEST_REQUIRE(node->dag()->mode() == DagMode::Light && !node->dag()->state_projection_ready());
        TEST_REQUIRE(!node->consensus()->accept_balance_snapshot(snapshot));
        TEST_REQUIRE(node->consensus()->accept_balance_snapshot(newer));
        // Verifying a proof alone must not expose the cache before the atomic install.
        TEST_REQUIRE(!node->dag()->state_projection_ready());
        node->cleanUp();
        node.reset();
        return true;
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Light snapshot runtime: %s\n", error.what());
        return false;
    }
}
