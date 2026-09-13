#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"

#include <atomic>
#include <condition_variable>
#include <filesystem>
#include <memory>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;
using HistoryPage = std::tuple<ActorId, std::string, std::vector<HistoricalCollectionRow>>;

struct HistoryPeer final : SocketService {
    ExtraChain::Core::ExtraChainNode&                  node;
    std::mutex                                         mutex;
    std::condition_variable                            ready;
    std::vector<std::pair<std::string, std::uint64_t>> requests;
    explicit HistoryPeer(ExtraChain::Core::ExtraChainNode& n, char name = 'f')
        : SocketService(*n.network())
        , node(n) {
        identifier_ = std::string(64, name);
        activated_  = true;
    }
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
    void send_message(std::span<const std::uint8_t> data, Priority) override {
        if (data.size() <= 64)
            return;
        const auto body = MessagePack::deserialize<MessageBody>(
            std::string(reinterpret_cast<const char*>(data.data()), data.size() - 64));
        if (!body.has_value() || body.value().message_type != MessageType::DfsCollectionRequest)
            return;
        const auto request =
            MessagePack::deserialize<std::tuple<ActorId, std::string, std::uint64_t>>(body.value().data);
        TEST_REQUIRE(request.has_value());
        std::lock_guard lock(mutex);
        requests.emplace_back(body.value().message_id, std::get<2>(request.value()));
        ready.notify_all();
    }
    std::pair<std::string, std::uint64_t> wait(std::size_t count) {
        std::unique_lock lock(mutex);
        TEST_REQUIRE(ready.wait_for(lock, 5s, [&] {
            return requests.size() >= count;
        }));
        return requests.at(count - 1);
    }
};

struct HistoryCapture final : ResponseSender {
    std::mutex                                       mutex;
    std::condition_variable                          ready;
    std::vector<std::pair<MessageType, std::string>> messages;
    std::string                                      send_response(const std::string& data,
                                                                   MessageType        type,
                                                                   SendMode,
                                                                   MessageStatus,
                                                                   const Responder& responder) override {
        std::lock_guard lock(mutex);
        messages.emplace_back(type, data);
        ready.notify_all();
        return responder.message_id();
    }
    std::pair<MessageType, std::string> wait(std::size_t count) {
        std::unique_lock lock(mutex);
        TEST_REQUIRE(ready.wait_for(lock, 5s, [&] {
            return messages.size() >= count;
        }));
        return messages.at(count - 1);
    }
};

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-history-protocol-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("history-protocol", ActorType::User, owner);
    auto schema = Dfs::CollectionTemplate::create("Items").value();
    schema.add_fields({ Dfs::Field::String("value") });
    auto file  = node->dfs()->store_collection(owner.id(), owner.id(), "Items", schema).value();
    const auto barrier = node->dfs()->store_collection(owner.id(), owner.id(), "Barrier", schema).value();
    auto chain = HistoricalCollection::load(node.get(), owner, owner.id(), file.file_id).value();
    for (unsigned i = 0; i < 130; ++i)
        TEST_REQUIRE(node->dfs()
                         ->add_collection_row(owner.id(), file.file_id, { { "value", std::to_string(i) } })
                         .has_value());
    const auto first  = chain.get_historical_rows(0).value();
    const auto second = chain.get_historical_rows(128).value();
    TEST_REQUIRE_EQ(first.size(), std::size_t(128));
    TEST_REQUIRE_EQ(second.size(), std::size_t(3));
    const auto wire = MessagePack::serialize(HistoryPage { owner.id(), file.file_id, first });
    TEST_REQUIRE(wire.size() < HistoricalCollection::MaxPageBytes);
    TEST_REQUIRE(MessagePack::has_bounded_structure(wire, 16384, 128, 8));
    HistoryCapture capture;
    auto           target = [&](std::string id, std::string peer = std::string(64, 'f')) {
        Responder result(&capture);
        result.add_identifier(peer);
        result.set_message_id(id);
        return result;
    };
    node->dfs()->network_request_collection(owner.id(), file.file_id, target("serve-first"), 0);
    auto served = capture.wait(1);
    TEST_REQUIRE(served.first == MessageType::DfsCollectionHistory);
    TEST_REQUIRE_EQ(served.second, wire);
    node->dfs()->network_request_collection(owner.id(), file.file_id, target("serve-next"), 128);
    TEST_REQUIRE_EQ(capture.wait(2).second,
                    MessagePack::serialize(HistoryPage { owner.id(), file.file_id, second }));
    const auto path = chain.get_file_path();
    std::filesystem::remove(path.native());
    auto peer = std::make_shared<HistoryPeer>(*node);
    node->network()->connections()->insert(peer);
    node->dfs()->request_collection({ owner.id(), file.file_id });
    const auto request = peer->wait(1);
    TEST_REQUIRE_EQ(request.second, std::uint64_t(0));
    node->dfs()->network_response_historical_collection(owner.id(), file.file_id, first, target("wrong"));
    node->dfs()->network_response_historical_collection(owner.id(),
                                                        file.file_id,
                                                        first,
                                                        target(request.first, "wrong-peer"));
    node->dfs()->network_response_historical_collection(owner.id(),
                                                        std::string(64, 'a'),
                                                        first,
                                                        target(request.first));
    node->dfs()->network_response_historical_collection(owner.id(), file.file_id, second, target(request.first));
    node->dfs()->network_response_content_collection(owner.id(), file.file_id, { { { "value", "unsigned" } } });
    node->dfs()->network_request_collection(owner.id(), barrier.file_id, target("guard"), 0);
    TEST_REQUIRE(capture.wait(3).first == MessageType::DfsCollectionHistory);
    TEST_REQUIRE(!std::filesystem::exists(path.native()));
    node->dfs()->network_response_historical_collection(owner.id(), file.file_id, first, target(request.first));
    const auto next = peer->wait(2);
    TEST_REQUIRE_EQ(next.second, std::uint64_t(128));
    TEST_REQUIRE(HistoricalCollection::load(node.get(), owner, owner.id(), file.file_id).has_value());
    node->dfs()->network_response_historical_collection(owner.id(), file.file_id, first, target(request.first));
    node->dfs()->network_response_historical_collection(owner.id(), file.file_id, second, target(next.first));
    const auto wait = [&](auto condition) {
        const auto end = std::chrono::steady_clock::now() + 5s;
        while (!condition() && std::chrono::steady_clock::now() < end)
            std::this_thread::sleep_for(5ms);
        TEST_REQUIRE(condition());
    };
    wait([&] {
        return chain.get_last_row().has_value() && chain.get_last_row().value().id == 130;
    });
    TEST_REQUIRE_EQ(chain.get_collection_rows().value().size(), std::size_t(130));
    auto live = chain.add_row({ { "value", "live" } }, Dfs::DataSecurity::Public, { }).value();
    // Restore the projection to the pre-change history, then deliver the live operation.
    std::filesystem::remove(path.native());
    TEST_REQUIRE(HistoricalCollection::accept(node.get(), owner.id(), file.file_id, first).value());
    TEST_REQUIRE(HistoricalCollection::accept(node.get(), owner.id(), file.file_id, second).value());
    auto bad = live;
    bad.data = "invalid payload";
    node->dfs()->network_change_collection(owner.id(), file.file_id, bad, target("bad-live"));
    node->dfs()->network_change_collection(owner.id(), file.file_id, live, target("live"));
    const auto relay = capture.wait(4);
    TEST_REQUIRE(relay.first == MessageType::DfsCollectionRowChange);
    TEST_REQUIRE_EQ(chain.get_last_row().value().id, live.id);
    node->dfs()->network_change_collection(owner.id(), file.file_id, live, target("duplicate-live"));
    node->dfs()->network_request_collection(owner.id(), file.file_id, target("barrier"), 131);
    TEST_REQUIRE(capture.wait(5).first == MessageType::DfsCollectionHistory);
    node->dfs()->set_mode(DfsMode::Full);
    const auto catalog_file =
        node->dfs()->store_collection(owner.id(), owner.id(), "CatalogItems", schema).value();
    TEST_REQUIRE(
        node->dfs()->add_collection_row(owner.id(), catalog_file.file_id, { { "value", "catalog" } }).has_value());
    auto catalog_chain = HistoricalCollection::load(node.get(), owner, owner.id(), catalog_file.file_id).value();
    const auto catalog_page = catalog_chain.get_historical_rows().value();
    const auto catalog_row  = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(),
                                                                             owner.id(),
                                                                             catalog_file.file_id)
                                  .value();
    std::filesystem::remove(catalog_chain.get_file_path().native());
    const auto catalog_request =
        node->dfs()->dirs_manager().request_catalog_rows({ .owners = { owner.id() } }, target("catalog"));
    TEST_REQUIRE(!catalog_request.empty());
    TEST_REQUIRE(capture.wait(6).first == MessageType::DfsSyncDirRows);
    node->dfs()->dirs_manager().network_response_dir_rows(MessagePack::serialize(
                                                              Dfs::CatalogRowsPage { .rows = { catalog_row } }),
                                                          target(catalog_request));
    const auto from_catalog = peer->wait(3);
    TEST_REQUIRE_EQ(from_catalog.second, std::uint64_t(0));
    node->dfs()->network_response_historical_collection(owner.id(),
                                                        catalog_file.file_id,
                                                        { },
                                                        target(from_catalog.first));
    node->dfs()->network_request_collection(owner.id(), barrier.file_id, target("empty-response-barrier"), 0);
    TEST_REQUIRE(capture.wait(7).first == MessageType::DfsCollectionHistory);
    auto alternative = std::make_shared<HistoryPeer>(*node, 'e');
    node->network()->connections()->insert(alternative);
    node->dfs()->request_collection({ owner.id(), catalog_file.file_id }, std::string(64, 'f'));
    const auto retried = alternative->wait(1);
    TEST_REQUIRE_EQ(retried.second, std::uint64_t(0));
    node->dfs()->network_response_historical_collection(owner.id(),
                                                        catalog_file.file_id,
                                                        catalog_page,
                                                        target(retried.first, std::string(64, 'e')));
    wait([&] {
        auto head = catalog_chain.get_last_row();
        return head.has_value() && head.value().id == 1;
    });
    node->network()->connections()->clear();
    peer.reset();
    alternative.reset();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
