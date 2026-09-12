#include <functional>
#include <filesystem>

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "utils/exc_utils.h"
#include <string>

#include "network/isocket_service.h"
#include "network/peer_identity.h"
#include "test_support.h"
#include "utils/serialization.h"

namespace {
    class Context final : public PeerContext {
    public:
        Context() {
            actor.create(ActorType::User);
            nonce = Utils::generate_random_hex(64);
        }
        Actor<KeyPrivate> actor;
        std::string       nonce;
        ActorId           network;
        unsigned          activations      = 0;
        unsigned          duplicate_checks = 0;
        ActorId           local_network_id() const override {
            return network;
        }
        void adopt_network_id(const ActorId& value) override {
            network = value;
        }
        std::string local_node_identifier() const override {
            return Network::peer_identifier(actor.key().public_key(), nonce).value();
        }
        std::string local_node_nonce() const override {
            return nonce;
        }
        std::optional<Actor<KeyPublic>> local_system_actor() const override {
            return actor.to_public();
        }
        std::expected<Signature, Cryptography::CryptoError> sign_handshake(const Bytes& bytes) const override {
            return actor.key().sign(bytes);
        }
        DfsMode local_dfs_mode() const override {
            return DfsMode::Full;
        }
        bool has_active_duplicate(std::string_view, const SocketService*) override {
            ++duplicate_checks;
            return false;
        }
        int active_peer_count() const override {
            return 0;
        }
        int peer_limit() const override {
            return 10;
        }
        std::set<PeerConnection> shareable_peers(std::string_view) const override {
            std::set<PeerConnection> peers;
            for (unsigned i = 0; i < 16; ++i) {
                peers.insert({ std::string(253, static_cast<char>('a' + i)), std::string(64, 'a') });
            }
            return peers;
        }
        void peer_authenticated(std::string_view, std::string_view) override {
            ++activations;
        }
        std::uint16_t local_server_port() const override {
            return 0;
        }
        bool peer_processing_enabled() const override {
            return true;
        }
    };
    class Socket final : public SocketService {
    public:
        explicit Socket(PeerContext& context)
            : SocketService(context) {
        }
        using SocketService::check_first_message;
        using SocketService::generate_first_message;
        using SocketService::set_peer_key;
        const PublicKey& session_key() const {
            return private_key_.public_key();
        }
        bool is_active() const override {
            return activated_.load();
        }
        std::string protocol_string() const override {
            return "identity-test";
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
    };
    void pair(Socket& left, Socket& right) {
        TEST_REQUIRE(left.set_peer_key(right.session_key()));
        TEST_REQUIRE(right.set_peer_key(left.session_key()));
    }
} // namespace

int main() {
    TEST_REQUIRE(sodium_init() >= 0);
    Context alice, bob, impostor;
    auto    incoming = std::make_shared<Socket>(alice);
    auto    outgoing = std::make_shared<Socket>(bob);
    pair(*incoming, *outgoing);
    const auto data = outgoing->generate_first_message();
    TEST_REQUIRE(((data.size() + 40 + 2) / 3) * 4 <= 8192);
    const auto good = Json::deserialize<SocketService::HandshakeMessage>(ByteArray(data).toString()).value();
    TEST_REQUIRE_EQ(good.identifier, bob.local_node_identifier());
    const std::vector<std::function<void(SocketService::HandshakeMessage&)>> mutations {
        [&](auto& value) {
            value.identifier = alice.local_node_identifier();
        },
        [&](auto& value) {
            value.node_nonce = alice.nonce;
        },
        [&](auto& value) {
            value.system_actor = impostor.actor.to_public();
        },
        [&](auto& value) {
            value.system_actor.set_id(impostor.actor.id());
        },
        [&](auto& value) {
            value.session_key = impostor.actor.key().public_key();
        },
        [&](auto& value) {
            value.peer_session_key = impostor.actor.key().public_key();
        },
        [&](auto& value) {
            value.network_id = impostor.actor.id().to_string();
        },
        [&](auto& value) {
            value.dfs_mode = DfsMode::Light;
        },
        [&](auto& value) {
            value.signature[0] ^= 1;
        }
    };
    for (const auto& mutate : mutations) {
        auto forged = good;
        mutate(forged);
        TEST_REQUIRE(!incoming->check_first_message(forged));
        TEST_REQUIRE(!incoming->is_active());
        TEST_REQUIRE(incoming->identifier().empty());
        TEST_REQUIRE_EQ(alice.duplicate_checks, 0U);
        TEST_REQUIRE_EQ(alice.activations, 0U);
        TEST_REQUIRE(alice.network.is_zero());
    }
    auto forged         = good;
    forged.system_actor = impostor.actor.to_public();
    forged.signature    = { };
    forged.signature    = impostor.actor.key()
                              .sign(ByteArray("extrachain-peer-handshake-v1:" + Json::serialize(forged)).toBytes())
                              .value();
    TEST_REQUIRE(!incoming->check_first_message(forged));

    auto new_connection = std::make_shared<Socket>(alice);
    TEST_REQUIRE(new_connection->set_peer_key(outgoing->session_key()));
    TEST_REQUIRE(!new_connection->check_first_message(good));
    TEST_REQUIRE(incoming->check_first_message(good));
    TEST_REQUIRE_EQ(alice.activations, 1U);
    TEST_REQUIRE_EQ(incoming->identifier(), bob.local_node_identifier());
    TEST_REQUIRE(outgoing->check_first_message(Json::deserialize<SocketService::HandshakeMessage>(
                                                   ByteArray(incoming->generate_first_message()).toString())
                                                   .value()));
    TEST_REQUIRE_EQ(bob.activations, 1U);
    TEST_REQUIRE(!Network::peer_identifier(bob.actor.key().public_key(), std::string(65, 'a')).has_value());
    TEST_REQUIRE(!Network::peer_identifier(bob.actor.key().public_key(), std::string(64, 'z')).has_value());
    TEST_REQUIRE(!Network::peer_identifier(PublicKey { }, bob.nonce).has_value());
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-peer-id-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("stable-identity", ActorType::User);
    const auto identifier = node->node_identifier();
    const auto nonce      = node->node_nonce();
    TEST_REQUIRE_EQ(Utils::read_settings().node_identifier.value(), identifier);
    node.reset();
    node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    TEST_REQUIRE(node->login("stable-identity").has_value());
    TEST_REQUIRE_EQ(node->node_identifier(), identifier);
    TEST_REQUIRE_EQ(node->node_nonce(), nonce);
    const auto renewed = node->generate_node_identifier();
    TEST_REQUIRE(renewed != identifier);
    TEST_REQUIRE(node->node_nonce() != nonce);
    TEST_REQUIRE_EQ(Utils::read_settings().node_identifier.value(), renewed);
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
