#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/vector_sync.h"
#include "managers/account_controller.h"
#include "test_support.h"

namespace {
    class Replies final : public ResponseSender {
    public:
        std::string send_response(const std::string& data,
                                  MessageType        type,
                                  SendMode           mode,
                                  MessageStatus      status,
                                  const Responder&   responder) override {
            TEST_REQUIRE(type == MessageType::DfsVectorSyncReply && mode == SendMode::Focused
                         && status == MessageStatus::Response);
            const auto reply = MessagePack::deserialize<Dfs::VectorSyncReply>(data);
            TEST_REQUIRE(reply.has_value());
            std::lock_guard lock(mutex_);
            replies_.emplace(responder.message_id(), reply.value());
            ready_.notify_all();
            return responder.message_id();
        }
        Dfs::VectorSyncReply wait(const std::string& id) {
            std::unique_lock lock(mutex_);
            TEST_REQUIRE(ready_.wait_for(lock, std::chrono::seconds(10), [&] {
                return replies_.contains(id);
            }));
            return replies_.at(id);
        }
        bool contains(const std::string& id) {
            std::lock_guard lock(mutex_);
            return replies_.contains(id);
        }

    private:
        std::mutex                                  mutex_;
        std::condition_variable                     ready_;
        std::map<std::string, Dfs::VectorSyncReply> replies_;
    };
} // namespace

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-limits-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("vector-limits", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("limits").value();
    schema.use_id().add_fields({ Dfs::Field::String("payload").not_null() });
    const auto stored = node->dfs()->store_template(owner.id(), schema);
    TEST_REQUIRE(stored.has_value());
    const auto vector =
        node->dfs()->store_vector(owner.id(), owner.id(), "limits", owner.id(), stored.value().file_id);
    TEST_REQUIRE(vector.has_value());
    const Dfs::FileLink link { owner.id(), vector.value().file_id };
    TEST_REQUIRE(
        node->dfs()->add_vector_row(owner.id(), link.file_id, { { "id", "row" }, { "payload", "value" } }));
    Replies    replies;
    auto&      sync   = node->dfs()->vector_sync();
    const auto target = [&](char source) {
        Responder responder(&replies);
        responder.add_identifier(std::string(64, source));
        return responder.with_new_message_id();
    };
    const auto send = [&](const Dfs::VectorSyncRequest& request, const Responder& responder) {
        const auto data     = MessagePack::serialize(request);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!sync.receive_request(data, responder)) {
            TEST_REQUIRE(std::chrono::steady_clock::now() < deadline);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    };
    const auto root = [&](char source) {
        const auto responder = target(source);
        send({ .link = link }, responder);
        const auto reply = replies.wait(responder.message_id());
        TEST_REQUIRE(Dfs::VectorIndex::valid_root(reply.root));
        TEST_REQUIRE(reply.metadata.has_value() && !reply.slice.has_value());
        return reply;
    };
    const auto first = root('a');
    const auto probe = [&] {
        const auto responder = target('a');
        send({ link, first.snapshot, first.root.tree.prefix }, responder);
        const auto reply = replies.wait(responder.message_id());
        TEST_REQUIRE(reply.slice.has_value());
        TEST_REQUIRE(Dfs::VectorSnapshot::verify(first.root.tree, "id", reply.slice.value()));
    };
    TEST_REQUIRE(!sync.receive_reply("unrequested data", target('a')));
    TEST_REQUIRE(!sync.receive_request(std::string(2049, 'x'), target('a')));
    TEST_REQUIRE(
        !sync.receive_request(MessagePack::serialize(Dfs::VectorSyncRequest { .link = link, .prefix = "z" }),
                              target('a')));
    const auto stolen = target('b');
    send({ link, first.snapshot, first.root.tree.prefix }, stolen);
    probe();
    TEST_REQUIRE(!replies.contains(stolen.message_id()));
    const auto wrong_file = target('a');
    send({ { owner.id(), std::string(64, 'e') }, first.snapshot, first.root.tree.prefix }, wrong_file);
    probe();
    TEST_REQUIRE(!replies.contains(wrong_file.message_id()));
    for (unsigned i = 1; i < 4; ++i)
        root('a');
    const auto fifth = target('a');
    send({ .link = link }, fifth);
    probe();
    TEST_REQUIRE(!replies.contains(fifth.message_id()));
    for (char source : { 'b', 'c', 'd' }) {
        for (unsigned i = 0; i < 4; ++i)
            root(source);
    }
    const auto seventeenth = target('e');
    send({ .link = link }, seventeenth);
    probe();
    TEST_REQUIRE(!replies.contains(seventeenth.message_id()));
    send({ link, first.snapshot, { }, true }, target('a'));
    const auto replacement = root('a');
    TEST_REQUIRE(replacement.snapshot != first.snapshot);
    const auto stale = target('a');
    send({ link, first.snapshot, first.root.tree.prefix }, stale);
    const auto valid = target('a');
    send({ link, replacement.snapshot, replacement.root.tree.prefix }, valid);
    TEST_REQUIRE(replies.wait(valid.message_id()).slice.has_value());
    TEST_REQUIRE(!replies.contains(stale.message_id()));
    sync.stop();
    TEST_REQUIRE(
        !sync.receive_request(MessagePack::serialize(Dfs::VectorSyncRequest { .link = link }), target('a')));
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
