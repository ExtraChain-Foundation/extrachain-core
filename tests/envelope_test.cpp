// Envelope signature verification at the network dispatch layer.
//
// Every message carries the origin's signature over MessageBody::calculate_hash().
// NetworkService::verify_envelope checks it whenever the origin's actor is known
// locally, and lets an unknown origin through (its actor gets requested). This
// pins both halves: a known actor cannot be impersonated, and a fresh link whose
// actors have not been synced yet still gets its first messages delivered.

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "chain/actor.h"
#include "chain/actor_index.h"
#include "core/byte_array.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/message_body.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/serialization.h"

int main() {
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-envelope-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    // A custom app publishes Custom messages to custom_message_event(); that is the
    // observable end of the dispatch path.
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("envelope-profile", ActorType::User, owner);

    Actor<KeyPrivate> peer;
    peer.create(ActorType::User);
    TEST_REQUIRE(node->actor_index()->save_actor(peer.to_public()).has_value());

    std::size_t delivered  = 0;
    auto        connection = node->network()->custom_message_event().subscribe(
        [&delivered](const NetworkPackageStorage &, const CustomMessage &) { ++delivered; });

    const auto envelope = [](const Actor<KeyPrivate> &signer, const ActorId &claimed_origin, bool corrupt_signature) {
        const CustomMessage custom { .owner = claimed_origin, .data = "hello" };
        const MessageBody   body = make_init_message(MessagePack::serialize(custom),
                                                     SendMode::Focused,
                                                     MessageType::Custom,
                                                     MessageStatus::NoStatus,
                                                     claimed_origin,
                                                     std::string(),
                                                     "peer-node");
        const auto          signature = signer.key().sign(ByteArray(body.calculate_hash()).toBytes());
        TEST_REQUIRE(signature.has_value());
        std::string blob = body.serialize() + ByteArray(signature.value()).toString();
        if (corrupt_signature) {
            blob.back() = static_cast<char>(blob.back() ^ 0x01);
        }
        return blob;
    };

    // Genuine: signed by the known actor it names.
    node->network()->message_received(envelope(peer, peer.id(), false), "127.0.0.1", "peer-node");
    TEST_REQUIRE_EQ(delivered, std::size_t(1));

    // Corrupted signature from a known actor: dropped.
    node->network()->message_received(envelope(peer, peer.id(), true), "127.0.0.1", "peer-node");
    TEST_REQUIRE_EQ(delivered, std::size_t(1));

    // Impostor signing with its own key while naming the known actor: dropped.
    Actor<KeyPrivate> impostor;
    impostor.create(ActorType::User);
    node->network()->message_received(envelope(impostor, peer.id(), false), "127.0.0.1", "peer-node");
    TEST_REQUIRE_EQ(delivered, std::size_t(1));

    // Unknown origin (its actor is not in the index): let through, actor requested.
    node->network()->message_received(envelope(impostor, impostor.id(), false), "127.0.0.1", "peer-node");
    TEST_REQUIRE_EQ(delivered, std::size_t(2));

    connection.disconnect();
    node->cleanUp();
    node.reset();
    std::error_code ignored;
    std::filesystem::current_path(std::filesystem::temp_directory_path(), ignored);
    std::filesystem::remove_all(test_path, ignored);
    std::printf("envelope verification: PASS\n");
    return 0;
}
