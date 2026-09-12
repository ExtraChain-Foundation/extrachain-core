#include <filesystem>
#include <memory>
#ifdef __APPLE__
    #include <mach/mach.h>
#else
    #include <fstream>
    #include <unistd.h>
#endif

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "managers/token_manager.h"
#include "network/network_service.h"
#include "network/wire_format.h"
#include "test_support.h"

namespace {
    class Source final : public SocketService {
    public:
        explicit Source(ExtraChain::Core::ExtraChainNode& node)
            : SocketService(*node.network())
            , node_(node) {
            identifier_ = std::string(64, 'f');
            activated_  = true;
            peer_meta_.capabilities.insert(std::string(TOKEN_MIGRATION_CAPABILITY));
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

        void send_message(std::span<const std::uint8_t>, Priority) override {
        }

    private:
        ExtraChain::Core::ExtraChainNode& node_;
    };

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

} // namespace

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-migration-bounds-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    node->account_controller()->create_profile("migration-bounds", ActorType::User, owner);
    auto source = std::make_shared<Source>(*node);
    node->network()->connections()->insert(source);
    TEST_REQUIRE_EQ(node->network()->active_full_peers_with_capability(TOKEN_MIGRATION_CAPABILITY).size(),
                    std::size_t(1));
    Responder responder;
    responder.add_identifier(source->identifier());
    responder         = responder.with_new_message_id();
    const auto before = resident_memory();
    for (unsigned i = 0; i < 100000; ++i) {
        TokenMigrationReadinessResponse response;
        response.plan_transaction_hash = fmt::format("{:064x}", i);
        response.ready                 = true;
        node->token_manager()->handle_migration_readiness_response(response, responder);
    }
    const auto after  = resident_memory();
    const auto growth = after > before ? after - before : 0;
    std::printf("Unsolicited migration response memory growth: %llu bytes\n",
                static_cast<unsigned long long>(growth));
    TEST_REQUIRE(growth < 8 * 1024 * 1024);
    source->close_connection();
    node->network()->connections()->erase(source);
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
