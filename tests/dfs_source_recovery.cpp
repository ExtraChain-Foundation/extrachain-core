#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <thread>

#include <boost/asio/post.hpp>

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"

namespace {
    using namespace std::chrono_literals;

    class Source final : public SocketService {
    public:
        explicit Source(ExtraChain::Core::ExtraChainNode& node)
            : SocketService(*node.network())
            , node_(node) {
            identifier_ = std::string(64, 'f');
            activated_  = true;
        }

        std::atomic_uint requests { 0 };
        std::atomic_uint notifications { 0 };
        std::atomic_bool available { false };

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
            constexpr std::size_t signature_bytes = 64;
            if (data.size() <= signature_bytes) {
                return;
            }
            const auto message = MessagePack::deserialize<MessageBody>(
                std::string(reinterpret_cast<const char*>(data.data()), data.size() - signature_bytes));
            if (!message.has_value()) {
                return;
            }
            if (message.value().message_type == MessageType::DfsFileExistNotification) {
                const auto state = MessagePack::deserialize<Dfs::Packets::FileState>(message.value().data);
                TEST_REQUIRE(state.has_value());
                if (state.value().file_id == std::string(64, 'b')
                    && state.value().state == Dfs::FileState::Ready) {
                    notifications.fetch_add(1);
                }
                return;
            }
            if (message.value().message_type != MessageType::DfsFileRequest) {
                return;
            }
            const auto request = MessagePack::deserialize<Dfs::FileLinkFragment>(message.value().data);
            TEST_REQUIRE(request.has_value());
            requests.fetch_add(1);
            boost::asio::post(node_.serial_executor(),
                              [node   = &node_,
                               link   = request.value().file_link,
                               source = identifier_,
                               ready  = available.load()] {
                                  if (ready) {
                                      node->dfs()
                                          ->download_manager()
                                          .file_fragment_achieved({ .owner_id        = link.owner_id,
                                                                    .file_id         = link.file_id,
                                                                    .data            = std::string(1024, 'x'),
                                                                    .offset          = 0,
                                                                    .current_size    = 1024,
                                                                    .fragment_number = 1,
                                                                    .full_amount_fragments = 1 },
                                                                  source);
                                  } else {
                                      Responder responder;
                                      responder.add_identifier(source);
                                      node->dfs()->network_response_file_state({ .owner_id = link.owner_id,
                                                                                 .file_id  = link.file_id,
                                                                                 .state = Dfs::FileState::Known },
                                                                               responder);
                                  }
                              });
        }

    private:
        ExtraChain::Core::ExtraChainNode& node_;
    };

    template <typename Predicate>
    bool wait_for(Predicate predicate) {
        const auto deadline = std::chrono::steady_clock::now() + 3s;
        while (!predicate() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        return predicate();
    }
} // namespace

int main(int argc, char** argv) {
    TEST_REQUIRE(argc == 2);
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-source-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("dfs-source", ActorType::User, owner);
    node->dfs()->set_mode(DfsMode::Full);
    const auto  db = node->dfs()->get_db_instance();
    Dfs::DirRow row;
    row.owner_id      = owner.id();
    row.actor_id      = owner.id();
    row.file_id       = std::string(64, 'b');
    row.name          = "recovered-file";
    row.size          = 1024;
    row.last_modified = 500;
    row.state         = Dfs::FileState::Known;
    row.type          = Dfs::FileType::File;
    const auto path   = Dfs::Path::file_path(owner.id(), row.file_id);
    TEST_REQUIRE(path.has_value());
    std::filesystem::create_directories(
        std::filesystem::path(Dfs::Path::filePath(owner.id(), row.file_id)).parent_path());
    {
        std::ofstream file(Dfs::Path::filePath(owner.id(), row.file_id), std::ios::binary);
        file << std::string(1024, 'x');
    }
    row.hash    = Utils::calculate_hash_file(path.value()).value();
    auto stored = Utils::to_dbrow(row);
    stored.erase("prev_file_id");
    using namespace Dfs::Tables::DirsFile;
    TEST_REQUIRE(db->insert(TableNameActorsFiles, stored));
    node->dfs()->dirs_manager().update_dirs(owner.id(), row.last_modified);
    const auto state = [&] {
        const auto current = ActorSpace::get_dir_row(db, owner.id(), row.file_id);
        TEST_REQUIRE(current.has_value());
        return current.value().state;
    };

    if (std::string_view(argv[1]) == "metadata") {
        node->dfs()->download_manager().add_to_queue(owner.id(), row, "");
        TEST_REQUIRE_EQ(state(), Dfs::FileState::Ready);
        ActorSpace::update_file_state(db, owner.id(), row.file_id, Dfs::FileState::Known);
        node->dfs()->sync("");
        TEST_REQUIRE(wait_for([&] {
            return state() == Dfs::FileState::Ready;
        }));
        ActorSpace::update_file_state(db, owner.id(), row.file_id, Dfs::FileState::Removed);
        node->dfs()->completeDownloadedFile(owner.id(), row);
        TEST_REQUIRE_EQ(state(), Dfs::FileState::Removed);
        ActorSpace::update_file_state(db, owner.id(), row.file_id, Dfs::FileState::Known);
        auto obsolete = row;
        obsolete.hash = std::string(64, '0');
        node->dfs()->completeDownloadedFile(owner.id(), obsolete);
        TEST_REQUIRE_EQ(state(), Dfs::FileState::Known);
    } else {
        TEST_REQUIRE(std::string_view(argv[1]) == "backoff");
        std::filesystem::remove(Dfs::Path::filePath(owner.id(), row.file_id));
        auto source = std::make_shared<Source>(*node);
        node->network()->connections()->insert(source);
        node->dfs()->download_manager().add_to_queue(owner.id(), row, source->identifier());
        TEST_REQUIRE(wait_for([&] {
            return source->requests.load() > 0;
        }));
        std::this_thread::sleep_for(250ms);
        std::printf("DFS refused-source requests in 250ms: %u\n", source->requests.load());
        std::fflush(stdout);
        TEST_REQUIRE(source->requests.load() <= 3);
        source->available = true;
        Responder responder;
        responder.add_identifier(source->identifier());
        node->dfs()->network_response_file_state({ .owner_id          = owner.id(),
                                                   .file_id           = row.file_id,
                                                   .state             = Dfs::FileState::Ready,
                                                   .hash              = row.hash,
                                                   .notify_neighbours = true },
                                                 responder);
        TEST_REQUIRE(wait_for([&] {
            return state() == Dfs::FileState::Ready;
        }));
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner.id(), row.file_id, row.hash));
        TEST_REQUIRE(wait_for([&] {
            return source->notifications.load() == 1;
        }));
        source->close_connection();
        node->network()->connections()->erase(source);
    }
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
