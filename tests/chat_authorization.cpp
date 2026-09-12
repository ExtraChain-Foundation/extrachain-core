#include <atomic>
#include <filesystem>
#include <memory>

#include "chain/actor_index.h"
#include "chat/chat_manager.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/dfs_vector.h"
#include "test_support.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto home     = std::filesystem::temp_directory_path()
                          / ("extrachain-chat-authorization-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    TEST_REQUIRE(node->create_new_network("channel", "authorization"));
    TEST_REQUIRE(node->create_chat_templates());
    auto &manager = *node->chat_manager();
    TEST_REQUIRE(manager.activate().has_value());
    const auto public_vector = manager.create_chat(false);
    TEST_REQUIRE(public_vector.has_value());
    const auto channel = manager.subscribe_channel(public_vector->owner_id, public_vector->file_id);
    TEST_REQUIRE(channel.has_value());
    const Chat::Message owner_message {
        .id      = "owner-message",
        .message = Chat::MessageData { .type = Chat::MessageType::Text, .data = "owner message" },
    };
    TEST_REQUIRE(
        node->dfs()->add_vector_row(channel->owner_id, channel->file_id, owner_message, channel->owner_id));
    TEST_REQUIRE(manager.read_chats().has_value());
    const auto before = manager.read_chat_messages(channel->owner_id, channel->file_id);
    TEST_REQUIRE(before.has_value() && !before->empty());
    const auto latest = manager.read_last_message(channel->owner_id, channel->file_id);
    TEST_REQUIRE(latest.has_value());

    Actor<KeyPrivate> outsider;
    outsider.create(ActorType::User);
    auto vector = DfsVector::load(node.get(), outsider, channel->owner_id, channel->file_id);
    TEST_REQUIRE(vector.has_value());
    auto rows = node->dfs()->read_vector_rows(channel->owner_id, channel->file_id);
    TEST_REQUIRE(rows.has_value() && !rows->empty());
    auto hostile         = rows->front();
    hostile["actor"]     = outsider.id().to_string();
    hostile["timestamp"] = std::to_string(Utils::current_date_ms() + 1000);
    DbConnector channel_db(Dfs::Path::file_path(channel->owner_id, channel->file_id).value());
    TEST_REQUIRE(channel_db.open(false));
    // Emulate foreign rows already stored by the old receiver. Read protection
    // must also work when a later write policy blocks new network submissions.
    for (unsigned i = 0; i < 150; ++i) {
        hostile["id"]        = "foreign-" + std::to_string(i);
        const auto signature = outsider.key().sign(vector->calculate_hash(hostile).first);
        TEST_REQUIRE(signature.has_value());
        hostile["sign"] = ByteArray(signature.value()).toString();
        TEST_REQUIRE(channel_db.insert("Vector", hostile));
    }
    const auto after = manager.read_chat_messages(channel->owner_id, channel->file_id);
    TEST_REQUIRE(after.has_value() && after->size() == before->size());
    const auto preview = manager.read_chat_messages(channel->owner_id, channel->file_id, true);
    TEST_REQUIRE(preview.has_value() && preview->size() == before->size());
    const auto last = manager.read_last_message(channel->owner_id, channel->file_id);
    TEST_REQUIRE(last.has_value() && last->id == latest->id);
    std::atomic_uint events { 0 };
    auto       connection = manager.message_added_event().subscribe([&](const auto &, const auto &, const auto &) {
        ++events;
    });
    const auto catalog    = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(),
                                                                           channel->owner_id,
                                                                           channel->file_id);
    TEST_REQUIRE(catalog.has_value());
    manager.on_vector_row_added(channel->owner_id, catalog.value(), hostile);
    TEST_REQUIRE(events.load() == 0);
    auto removal_connection =
        manager.message_removed_event().subscribe([&](const auto &, const auto &, const auto &) {
            ++events;
        });
    hostile["status"] = "0";
    manager.on_vector_row_removed(channel->owner_id, catalog.value(), hostile);
    TEST_REQUIRE(events.load() == 0);
    manager.on_vector_row_added(channel->owner_id, catalog.value(), rows->front());
    TEST_REQUIRE(events.load() == 1);
    auto owner_removal      = rows->front();
    owner_removal["status"] = "0";
    manager.on_vector_row_removed(channel->owner_id, catalog.value(), owner_removal);
    TEST_REQUIRE(events.load() == 2);

    const auto self = manager.create_myself();
    TEST_REQUIRE(self.has_value() && self->chat_key.has_value());
    TEST_REQUIRE(
        manager
            .add_new_message_text(self->owner_id, self->file_id, Chat::MessageText { .text = "readable message" })
            .has_value());
    TEST_REQUIRE(manager.read_chats().has_value());
    const auto readable = manager.read_chat_messages(self->owner_id, self->file_id);
    TEST_REQUIRE(readable.has_value() && !readable->empty());
    const auto self_latest = manager.read_last_message(self->owner_id, self->file_id);
    TEST_REQUIRE(self_latest.has_value());
    auto ciphertext = node->dfs()->read_vector_rows(self->owner_id, self->file_id);
    TEST_REQUIRE(ciphertext.has_value() && !ciphertext->empty());
    auto broken         = ciphertext->front();
    broken["timestamp"] = std::to_string(Utils::current_date_ms() + 2000);
    // One complete page of unreadable rows must not hide earlier valid messages.
    broken["message"] = "invalid ciphertext";
    DbConnector self_db(Dfs::Path::file_path(self->owner_id, self->file_id).value());
    TEST_REQUIRE(self_db.open(false));
    for (unsigned i = 0; i < 15; ++i) {
        broken["id"] = "broken-" + std::to_string(i);
        TEST_REQUIRE(self_db.insert("Vector", broken));
    }
    const auto recovered = manager.read_chat_messages(self->owner_id, self->file_id);
    TEST_REQUIRE(recovered.has_value() && recovered->size() == readable->size());
    const auto recovered_last = manager.read_last_message(self->owner_id, self->file_id);
    TEST_REQUIRE(recovered_last.has_value() && recovered_last->id == self_latest->id);
    removal_connection.disconnect();
    connection.disconnect();
    TEST_REQUIRE(channel_db.close() && self_db.close());
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    return 0;
}
