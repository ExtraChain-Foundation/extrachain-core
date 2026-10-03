#include <atomic>
#include <functional>
#include <filesystem>

#include "core/extrachain_node.h"
#include "chain/actor_index.h"
#include "network/network_service.h"
#include "managers/account_controller.h"
#include "utils/exc_utils.h"
#include <string>

#include "network/isocket_service.h"
#include "network/peer_identity.h"
#include "network/peer_access.h"
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
        std::atomic<unsigned> forbidden_sent { 0 };
        std::atomic<unsigned> fragments_sent { 0 };
        void                  restrict_for_dispatch(std::string identifier) {
            identifier_                = std::move(identifier);
            peer_meta_.update_required = true;
            peer_meta_.capabilities.insert(std::string(SHADOW_CONSENSUS_CAPABILITY));
            activated_.store(true);
            mode_ = SocketMode::Full;
        }
        void send_message(std::span<const std::uint8_t> bytes, Priority) override {
            if (bytes.size() < crypto_sign_BYTES)
                return;
            const auto body = MessagePack::deserialize<MessageBody>(
                std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size() - crypto_sign_BYTES));
            TEST_REQUIRE(body.has_value());
            const auto type = body.value().message_type;
            if (type == MessageType::Custom || type == MessageType::CoinReward
                || (std::to_underlying(type) >= 120 && std::to_underlying(type) <= 135))
                ++forbidden_sent;
            if (type == MessageType::DfsFileFragment)
                ++fragments_sent;
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
    TEST_REQUIRE(!incoming->peer_meta().update_required && incoming->peer_meta().authenticated);
    {
        Context current, old;
        auto    current_socket = std::make_shared<Socket>(current);
        auto    old_socket     = std::make_shared<Socket>(old);
        pair(*current_socket, *old_socket);
        auto legacy         = Json::deserialize<SocketService::HandshakeMessage>(
                                  ByteArray(old_socket->generate_first_message()).toString())
                                  .value();
        legacy.system_actor = { };
        legacy.node_nonce.clear();
        legacy.session_key      = { };
        legacy.peer_session_key = { };
        legacy.signature        = { };
        legacy.identifier =
            current.local_node_identifier(); // Cannot reserve the local or another signed identity.
        legacy.network_id = old.actor.id().to_string();
        for (int field = 0; field < 6; ++field) {
            auto partial = legacy;
            if (field == 0)
                partial.system_actor = old.actor.to_public();
            if (field == 1)
                partial.node_nonce = old.nonce;
            if (field == 2)
                partial.session_key = old_socket->session_key();
            if (field == 3)
                partial.peer_session_key = current_socket->session_key();
            if (field == 4)
                partial.signature[0] = 1;
            if (field == 5)
                partial.system_actor.set_id(old.actor.id());
            TEST_REQUIRE(!current_socket->check_first_message(partial));
            TEST_REQUIRE(current_socket->identifier().empty());
        }
        unsigned shared                      = 0;
        current_socket->on_share_connections = [&](auto, auto) {
            ++shared;
        };
        TEST_REQUIRE(current_socket->check_first_message(legacy));
        TEST_REQUIRE(current_socket->is_active());
        TEST_REQUIRE(current_socket->identifier().size() == 64);
        TEST_REQUIRE(current_socket->identifier() != legacy.identifier);
        TEST_REQUIRE(current_socket->peer_meta().update_required);
        TEST_REQUIRE(!current_socket->peer_meta().authenticated);
        TEST_REQUIRE(!current_socket->peer_meta().supports_shadow_consensus());
        TEST_REQUIRE(current_socket->peer_meta().capabilities.empty());
        TEST_REQUIRE(current.network.is_zero() && current.activations == 0 && shared == 0);
        // A fresh signed session after an update restores protocol access.
        auto updated      = std::make_shared<Socket>(current);
        auto updated_peer = std::make_shared<Socket>(old);
        pair(*updated, *updated_peer);
        const auto full = Json::deserialize<SocketService::HandshakeMessage>(
                              ByteArray(updated_peer->generate_first_message()).toString())
                              .value();
        TEST_REQUIRE(updated->check_first_message(full));
        TEST_REQUIRE(!updated->peer_meta().update_required && updated->peer_meta().authenticated);
        TEST_REQUIRE(updated->peer_meta().supports_shadow_consensus());
        TEST_REQUIRE(updated->identifier() == old.local_node_identifier());
        // The old v5 gate must permit its own outgoing file requests to an updated peer.
        TEST_REQUIRE(full.capabilities.value().contains("shadow_consensus_v5"));
        TEST_REQUIRE(full.capabilities.value().contains("shadow_consensus_v6"));
        // Signed old protocol versions get the same restricted data access.
        for (const auto version : { "shadow_consensus_v4", "shadow_consensus_v5" }) {
            auto signed_old = full;
            signed_old.capabilities = std::set<std::string> { version, "shadow_relay_v1" };
            signed_old.signature = { };
            signed_old.signature =
                old.actor.key()
                    .sign(ByteArray("extrachain-peer-handshake-v1:" + Json::serialize(signed_old)).toBytes())
                    .value();
            TEST_REQUIRE(updated->check_first_message(signed_old));
            TEST_REQUIRE(updated->peer_meta().update_required && updated->peer_meta().authenticated);
            TEST_REQUIRE(!updated->peer_meta().supports_shadow_consensus());
            TEST_REQUIRE(updated->peer_meta().capabilities.empty());
            signed_old.capabilities = full.capabilities;
            TEST_REQUIRE(!updated->check_first_message(signed_old));
        }
        TEST_REQUIRE(updated->check_first_message(full));
        TEST_REQUIRE(updated->peer_meta().supports_shadow_consensus());
    }
    for (int type = 120; type <= 135; ++type) {
        for (const auto status : { MessageStatus::NoStatus, MessageStatus::Request, MessageStatus::Response }) {
            TEST_REQUIRE(!Network::restricted_peer_message_allowed(static_cast<MessageType>(type), status, true));
            TEST_REQUIRE(!Network::restricted_peer_message_allowed(static_cast<MessageType>(type), status, false));
        }
    }
    for (const auto type : { MessageType::CoinReward,
                             MessageType::DagTransaction,
                             MessageType::DagTransactionBatch,
                             MessageType::DfsStoreFile,
                             MessageType::DfsFileRemove,
                             MessageType::DfsVectorAdd,
                             MessageType::DfsDictionaryAdd,
                             MessageType::DfsCollectionRowChange,
                             MessageType::TokenMigrationReadiness,
                             MessageType::ShareConnections }) {
        for (const auto status : { MessageStatus::NoStatus, MessageStatus::Request, MessageStatus::Response }) {
            TEST_REQUIRE(!Network::restricted_peer_message_allowed(type, status, true));
            TEST_REQUIRE(!Network::restricted_peer_message_allowed(type, status, false));
        }
    }
    TEST_REQUIRE(
        Network::restricted_peer_message_allowed(MessageType::DfsFileRequest, MessageStatus::NoStatus, true));
    TEST_REQUIRE(
        Network::restricted_peer_message_allowed(MessageType::DfsFileFragment, MessageStatus::NoStatus, false));
    TEST_REQUIRE(
        !Network::restricted_peer_message_allowed(MessageType::DfsFileFragment, MessageStatus::NoStatus, true));
    TEST_REQUIRE(
        Network::restricted_peer_message_allowed(MessageType::DagFileSections, MessageStatus::Request, true));
    TEST_REQUIRE(
        Network::restricted_peer_message_allowed(MessageType::DagFileSections, MessageStatus::Response, false));
    TEST_REQUIRE(
        !Network::restricted_peer_message_allowed(MessageType::DagFileSections, MessageStatus::Response, true));
    TEST_REQUIRE(
        Network::restricted_peer_message_allowed(MessageType::DfsTempSyncAll, MessageStatus::Response, true));
    TEST_REQUIRE(
        !Network::restricted_peer_message_allowed(MessageType::DfsTempSyncAll, MessageStatus::Response, false));
    TEST_REQUIRE(
        !Network::restricted_peer_message_allowed(MessageType::DfsSyncDirRows, MessageStatus::Response, true));
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-peer-id-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("stable-identity", ActorType::User);
    {
        auto restricted = std::make_shared<Socket>(*node->network());
        restricted->restrict_for_dispatch(std::string(64, 'c'));
        {
            auto connections = *node->network()->connections();
            connections->insert(restricted);
        }
        TEST_REQUIRE(node->network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY).empty());
        Responder target(node->network());
        target.add_identifier(restricted->identifier());
        for (const auto mode : { SendMode::Focused,
                                 SendMode::Neighbours,
                                 SendMode::NeighboursRandom,
                                 SendMode::OneNeighbourRandom,
                                 SendMode::Broadcast,
                                 SendMode::Except }) {
            for (const auto type : { MessageType::Custom,
                                     MessageType::CoinReward,
                                     MessageType::ConsensusIntent,
                                     MessageType::ConsensusRelay,
                                     MessageType::ConsensusVote }) {
                node->network()
                    ->send_message_send("blocked", "blocked", type, mode, MessageStatus::NoStatus, target);
            }
        }
        TEST_REQUIRE_EQ(restricted->forbidden_sent.load(), 0U);
        node->network()->send_message_send("update-fragment",
                                           "update-fragment",
                                           MessageType::DfsFileFragment,
                                           SendMode::Focused,
                                           MessageStatus::NoStatus,
                                           target);
        TEST_REQUIRE_EQ(restricted->fragments_sent.load(), 1U);
        Actor<KeyPrivate> source;
        source.create(ActorType::User);
        TEST_REQUIRE(node->actor_index()->save_actor(source.to_public()).has_value());
        unsigned delivered    = 0;
        auto     subscription = node->network()->custom_message_event().subscribe(
            [&](const NetworkPackageStorage&, const CustomMessage&) {
                ++delivered;
            });
        const auto packet = [&]() {
            const auto body      = make_init_message(MessagePack::serialize(CustomMessage { source.id(), "test" }),
                                                     SendMode::Focused,
                                                     MessageType::Custom,
                                                     MessageStatus::NoStatus,
                                                     source.id(),
                                                     "",
                                                     restricted->identifier());
            const auto signature = source.key().sign(ByteArray(body.calculate_hash()).toBytes()).value();
            return body.serialize() + ByteArray(signature).toString();
        };
        node->network()->message_received(packet(), "127.0.0.1", "unrestricted-test-origin");
        TEST_REQUIRE_EQ(delivered, 1U);
        node->network()->message_received(packet(), "127.0.0.1", restricted->identifier());
        TEST_REQUIRE_EQ(delivered, 1U);
        {
            auto connections = *node->network()->connections();
            connections->erase(restricted);
        }
    }
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
