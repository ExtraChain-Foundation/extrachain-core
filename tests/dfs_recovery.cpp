#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <thread>
#ifdef __APPLE__
    #include <mach/mach.h>
#else
    #include <unistd.h>
#endif

#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "dfs/load_manager.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"

namespace {
    std::uint64_t resident_memory() {
#ifdef __APPLE__
        mach_task_basic_info   info { };
        mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
        TEST_REQUIRE(
            task_info(mach_task_self(), MACH_TASK_BASIC_INFO, reinterpret_cast<task_info_t>(&info), &count)
            == KERN_SUCCESS);
        return info.resident_size;
#else
        std::ifstream status("/proc/self/statm");
        std::uint64_t virtual_pages  = 0;
        std::uint64_t resident_pages = 0;
        TEST_REQUIRE(static_cast<bool>(status >> virtual_pages >> resident_pages));
        return resident_pages * static_cast<std::uint64_t>(sysconf(_SC_PAGESIZE));
#endif
    }
    class Source final : public SocketService {
    public:
        explicit Source(ExtraChain::Core::ExtraChainNode& node)
            : SocketService(*node.network())
            , node_(node) {
            identifier_ = std::string(64, 'f');
            activated_  = true;
        }

        std::mutex                           mutex;
        std::map<Dfs::FileLink, std::string> pending;

        std::string request_id(const Dfs::FileLink& link) {
            std::lock_guard lock(mutex);
            const auto      request = pending.find(link);
            return request == pending.end() ? std::string { } : request->second;
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
            constexpr std::size_t signature_bytes = 64;
            if (data.size() <= signature_bytes) {
                return;
            }
            const auto message = MessagePack::deserialize<MessageBody>(
                std::string(reinterpret_cast<const char*>(data.data()), data.size() - signature_bytes));
            if (!message.has_value()) {
                return;
            }
            if (message.value().message_type != MessageType::DfsFileRequest) {
                return;
            }
            const auto request = MessagePack::deserialize<Dfs::FileLinkFragment>(message.value().data);
            TEST_REQUIRE(request.has_value());
            std::lock_guard lock(mutex);
            pending[request.value().file_link] = message.value().message_id;
        }

    private:
        ExtraChain::Core::ExtraChainNode& node_;
    };

} // namespace

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-dfs-recovery-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    {
        const auto sparse = FsPath::create(std::string_view("sparse-fragment")).value();
#ifdef _WIN32
        constexpr std::uint64_t offset = 8 * 1024 * 1024;
#else
        constexpr std::uint64_t offset = std::uint64_t(512) * 1024 * 1024 * 1024;
#endif
        TEST_REQUIRE(Utils::write_file_chunk(sparse, "tail", offset).has_value());
        TEST_REQUIRE_EQ(sparse.file_size().value(), offset + 4);
        TEST_REQUIRE(Utils::write_file_chunk(sparse, "head", 0).has_value());
        std::ifstream       input(sparse.native(), std::ios::binary);
        std::array<char, 5> head { };
        input.read(head.data(), head.size());
        TEST_REQUIRE_EQ(std::string_view(head.data(), 4), "head");
        TEST_REQUIRE_EQ(head.back(), '\0');
        input.seekg(offset);
        std::array<char, 4> tail { };
        input.read(tail.data(), tail.size());
        TEST_REQUIRE_EQ(std::string_view(tail.data(), tail.size()), "tail");
        TEST_REQUIRE(!Utils::write_file_chunk(sparse, "x", std::numeric_limits<std::uint64_t>::max()).has_value());
        TEST_REQUIRE_EQ(sparse.file_size().value(), offset + 4);
        input.close();
        std::filesystem::remove(sparse.native());
    }
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("dfs-recovery", ActorType::User, owner);
    node->dfs()->set_mode(DfsMode::Full);
    auto source = std::make_shared<Source>(*node);
    node->network()->connections()->insert(source);
    auto&      manager = node->dfs()->dirs_manager();
    const auto db      = manager.get_db_instance();
    const auto first   = ActorId::create(std::string(40, '1')).value();
    const auto second  = ActorId::create(std::string(40, '2')).value();
    using namespace Dfs::Tables::DirsFile;
    manager.update_dirs(first, 2000);
    manager.update_dirs(second, 1000);
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2000U);
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, second).value(), 1000U);
    std::jthread newer([&] {
        for (unsigned i = 2000; i < 2100; ++i)
            manager.update_dirs(first, i);
    });
    std::jthread older([&] {
        for (unsigned i = 1000; i < 1100; ++i)
            manager.update_dirs(first, i);
    });
    newer.join();
    older.join();
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2099U);
    manager.update_dirs(first, std::numeric_limits<std::uint64_t>::max());
    TEST_REQUIRE_EQ(DirsSpace::last_modified(db, first).value(), 2099U);

    // A signed large-file advertisement must not allocate one node per fragment.
    Dfs::DirRow large;
    large.owner_id          = owner.id();
    large.actor_id          = owner.id();
    large.file_id           = std::string(64, 'e');
    large.name              = "large-advertised-file";
    large.size              = std::uint64_t(512) * 1024 * 1024 * 1024;
    large.type              = Dfs::FileType::File;
    large.state             = Dfs::FileState::Known;
    large.metadata_revision = 1;
    large.hash              = std::string(64, 'a');
    large.sign              = owner.key().sign(large.calculate_hash(owner.id())).value();
    auto large_row          = Utils::to_dbrow(large);
    large_row.erase("prev_file_id");
    TEST_REQUIRE(db->insert(TableNameActorsFiles, large_row));
    const auto memory_before = resident_memory();
    node->dfs()->download_manager().add_to_queue(owner.id(), large, source->identifier());
    TEST_REQUIRE(
        node->dfs()->download_manager().add_node_identifier({ owner.id(), large.file_id }, std::string(64, 'e')));
    const auto memory_after = resident_memory();
    const auto growth       = memory_after > memory_before ? memory_after - memory_before : 0;
    std::fprintf(stderr, "Large file queue memory growth: %llu bytes\n", static_cast<unsigned long long>(growth));
    TEST_REQUIRE(growth < 8 * 1024 * 1024);
    auto tombstone              = large;
    tombstone.state             = Dfs::FileState::Removed;
    tombstone.metadata_revision = 2;
    tombstone.sign              = owner.key().sign(tombstone.calculate_hash(owner.id())).value();
    auto tombstone_row          = Utils::to_dbrow(tombstone);
    tombstone_row.erase("prev_file_id");
    TEST_REQUIRE(db->replace(TableNameActorsFiles, tombstone_row));
    TEST_REQUIRE(ActorSpace::get_dir_row(db, owner.id(), large.file_id).value().state == Dfs::FileState::Removed);
    node->dfs()->download_manager().cancel_download({ owner.id(), large.file_id });
    node->dfs()->download_manager().add_to_queue(owner.id(), large, source->identifier());
    TEST_REQUIRE_EQ(node->dfs()->download_manager().active_downloads_size(), std::size_t(0));
    auto removed    = large;
    removed.state   = Dfs::FileState::Removed;
    removed.file_id = std::string(64, 'f');
    node->dfs()->download_manager().add_to_queue(owner.id(), removed, source->identifier());
    TEST_REQUIRE_EQ(node->dfs()->download_manager().active_downloads_size(), std::size_t(0));

    // Reproduce persisted metadata after a crash, including a peer that knows
    // the file but cannot yet serve it. A size match alone cannot prove readiness.
    for (unsigned variant = 0; variant < 4; ++variant) {
        const auto  actor = owner.id();
        Dfs::DirRow remote;
        remote.owner_id      = actor;
        remote.actor_id      = owner.id();
        remote.file_id       = std::string(64, char('a' + variant));
        remote.name          = "resume-" + std::to_string(variant);
        remote.size          = 1024;
        remote.last_modified = 500;
        remote.metadata_revision = 500;
        remote.state         = variant >= 2 ? Dfs::FileState::Known : Dfs::FileState::Ready;
        remote.type          = Dfs::FileType::File;
        const auto path      = Dfs::Path::file_path(actor, remote.file_id);
        TEST_REQUIRE(path.has_value());
        std::filesystem::create_directories(
            std::filesystem::path(Dfs::Path::filePath(actor, remote.file_id)).parent_path());
        {
            std::ofstream file(Dfs::Path::filePath(actor, remote.file_id), std::ios::binary);
            file << std::string(1024, 'x');
        }
        remote.hash = Utils::calculate_hash_file(path.value()).value();
        remote.sign = owner.key().sign(remote.calculate_hash(actor)).value();
        auto local  = remote;
        local.state = variant == 1 ? Dfs::FileState::Ready : Dfs::FileState::Known;
        auto row    = Utils::to_dbrow(local);
        row.erase("prev_file_id");
        TEST_REQUIRE(db->insert(TableNameActorsFiles, row));
        {
            std::ofstream file(Dfs::Path::filePath(actor, remote.file_id), std::ios::binary);
            file << std::string(variant == 0 ? 512 : 2048, 'y');
        }
        TEST_REQUIRE(DirsSpace::last_modified(db, actor).value() <= 500U);
        if (variant == 2) {
            std::filesystem::remove(Dfs::Path::filePath(actor, remote.file_id));
        }
        Responder responder(node->network());
        responder.add_identifier(std::string(64, 'c'));
        if (variant == 3) {
            manager.update_dirs(actor, remote.last_modified);
            node->dfs()->sync(std::string(64, 'c'));
        } else {
            responder.set_message_id(manager.request_catalog_rows({ .owners = { actor } }, responder));
            TEST_REQUIRE(!responder.message_id().empty());
            manager.network_response_dir_rows(MessagePack::serialize(Dfs::CatalogRowsPage { .rows = { remote } }),
                                              responder);
        }
        const Dfs::FileLink link { .owner_id = actor, .file_id = remote.file_id };
        const auto          deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        bool                queued   = false;
        while (!queued && std::chrono::steady_clock::now() < deadline) {
            queued = node->dfs()->download_manager().add_node_identifier(link, std::string(64, 'd'));
            if (!queued) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        TEST_REQUIRE(queued);
        TEST_REQUIRE_EQ(DirsSpace::last_modified(db, actor).value(), 500U);
        if (variant >= 2) {
            node->dfs()->network_response_file_state({ .owner_id = actor,
                                                       .file_id  = remote.file_id,
                                                       .state    = Dfs::FileState::Known },
                                                     responder);
            TEST_REQUIRE(node->dfs()->download_manager().add_node_identifier(link, std::string(64, 'e')));
        }
        const Dfs::Packets::FragmentData fragment { .owner_id              = actor,
                                                    .file_id               = remote.file_id,
                                                    .data                  = std::string(1024, 'x'),
                                                    .offset                = 0,
                                                    .current_size          = 1024,
                                                    .fragment_number       = 1,
                                                    .full_amount_fragments = 1 };
        node->dfs()->download_manager().prefer_source(link, source->identifier());
        const auto request_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (source->request_id(link).empty() && std::chrono::steady_clock::now() < request_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        const auto message_id = source->request_id(link);
        TEST_REQUIRE(!message_id.empty());
        const bool existed    = path.value().exists();
        const auto prior_hash = existed ? Utils::calculate_hash_file(path.value()).value() : std::string { };
        const auto prior_size = existed ? path.value().file_size().value() : 0;
        auto&      downloads  = node->dfs()->download_manager();
        downloads.file_fragment_achieved(fragment, std::string(64, '0'), message_id);
        downloads.file_fragment_achieved(fragment, source->identifier(), "unsolicited");
        for (unsigned field = 0; field < 8; ++field) {
            auto invalid = fragment;
            switch (field) {
            case 0:
                invalid.offset = Dfs::Basic::FRAGMENT_SIZE * 2;
                break;
            case 1:
                invalid.fragment_number = 0;
                break;
            case 2:
                invalid.fragment_number = 2;
                break;
            case 3:
                invalid.full_amount_fragments = std::numeric_limits<std::size_t>::max();
                break;
            case 4:
                invalid.current_size = 1023;
                break;
            case 5:
                invalid.data.resize(1023);
                break;
            case 6:
                invalid.data.resize(Dfs::Basic::FRAGMENT_SIZE + 1);
                break;
            case 7:
                invalid.offset = 1;
                break;
            }
            downloads.file_fragment_achieved(invalid, source->identifier(), message_id);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        TEST_REQUIRE_EQ(path.value().exists(), existed);
        if (existed) {
            TEST_REQUIRE_EQ(path.value().file_size().value(), prior_size);
            TEST_REQUIRE_EQ(Utils::calculate_hash_file(path.value()).value(), prior_hash);
        }
        downloads.file_fragment_achieved(fragment, source->identifier(), message_id);
        const auto recovery_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
        while (!node->dfs()->is_file_already_downloaded(actor, remote.file_id, remote.hash)
               && std::chrono::steady_clock::now() < recovery_deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        TEST_REQUIRE(node->dfs()->is_file_already_downloaded(actor, remote.file_id, remote.hash));
        TEST_REQUIRE_EQ(path.value().file_size().value(), remote.size);
        auto replay = fragment;
        replay.data.assign(1024, 'z');
        downloads.file_fragment_achieved(replay, source->identifier(), message_id);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        TEST_REQUIRE_EQ(Utils::calculate_hash_file(path.value()).value(), remote.hash);
    }
    // Out-of-order delivery splits and then joins the pending and completed ranges.
    auto ordered              = large;
    ordered.file_id           = std::string(64, '9');
    ordered.name              = "fragment-order";
    const std::string payload = std::string(Dfs::Basic::FRAGMENT_SIZE, 'a')
                                + std::string(Dfs::Basic::FRAGMENT_SIZE, 'b')
                                + std::string(Dfs::Basic::FRAGMENT_SIZE, 'c') + std::string(17, 'd');
    ordered.size              = payload.size();
    ordered.hash              = Utils::calculate_hash(payload);
    ordered.sign              = owner.key().sign(ordered.calculate_hash(owner.id())).value();
    auto ordered_row          = Utils::to_dbrow(ordered);
    ordered_row.erase("prev_file_id");
    TEST_REQUIRE(db->insert(TableNameActorsFiles, ordered_row));
    const Dfs::FileLink ordered_link { owner.id(), ordered.file_id };
    auto&               downloads = node->dfs()->download_manager();
    downloads.add_to_queue(owner.id(), ordered, source->identifier());
    const auto request_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (source->request_id(ordered_link).empty() && std::chrono::steady_clock::now() < request_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto message_id = source->request_id(ordered_link);
    TEST_REQUIRE(!message_id.empty());
    for (const std::size_t number : { 2, 4, 1, 3 }) {
        const auto                       offset = (number - 1) * Dfs::Basic::FRAGMENT_SIZE;
        const auto                       bytes  = payload.substr(offset, Dfs::Basic::FRAGMENT_SIZE);
        const Dfs::Packets::FragmentData fragment { .owner_id              = owner.id(),
                                                    .file_id               = ordered.file_id,
                                                    .data                  = bytes,
                                                    .offset                = offset,
                                                    .current_size          = bytes.size(),
                                                    .fragment_number       = number,
                                                    .full_amount_fragments = 4 };
        downloads.file_fragment_achieved(fragment, source->identifier(), message_id);
        downloads.file_fragment_achieved(fragment, source->identifier(), message_id);
    }
    const auto completed = [&] {
        return ActorSpace::get_dir_row(db, owner.id(), ordered.file_id).value().state == Dfs::FileState::Ready;
    };
    const auto complete_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!completed() && std::chrono::steady_clock::now() < complete_deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    TEST_REQUIRE(completed());
    TEST_REQUIRE(node->dfs()->is_file_already_downloaded(owner.id(), ordered.file_id, ordered.hash));

    source->close_connection();
    node->network()->connections()->erase(source);
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
