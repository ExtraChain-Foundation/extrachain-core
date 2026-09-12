#include "chain/actor_index.h"
#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"
#include "utils/legacy_compression.h"
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
            if (body.has_value() && body.value().message_type == MessageType::DagFileSections) {
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

int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-history-sync-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    Actor<KeyPrivate> owner;
    Actor<KeyPrivate> token;
    owner.create(ActorType::User);
    token.create(ActorType::User);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Logger::instance().set_debug(true);
    node->account_controller()->create_profile("history-sync", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(token.to_public()).has_value());
    node->actor_index()->set_network_id(owner.id());
    auto &dag = *node->dag();
    dag.set_mode(DagMode::Full);
    Transaction initial;
    initial.set_type(TransactionType::Balance);
    initial.set_sender(owner.id());
    initial.set_receiver(owner.id());
    initial.set_token(owner.id());
    initial.set_section(SectionId(1));
    initial.set_amount(BigNumberFloat(5));
    TEST_REQUIRE(initial.sign(owner));
    TEST_REQUIRE(dag.save_transaction(initial));
    TEST_REQUIRE(
        dag.cache().write_cached_balances({ { { owner.id(), owner.id() }, BigNumberFloat(5) } }, SectionId(20)));
    dag.set_current_section(SectionId(20));
    dag.set_status(DagStatus::Ready);
    auto peer = std::make_shared<Peer>(*node->network());
    node->network()->connections()->insert(peer);
    dag.start_sync();
    Responder peer_response(node->network());
    peer_response.add_identifier(peer->identifier());
    dag.network_status_sync_response({ SectionId(40), SectionId(-1), "", 0, DagStatus::Ready }, peer_response);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (peer->last_request().empty() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    TEST_REQUIRE(!peer->last_request().empty());
    peer_response.set_message_id(peer->last_request());
    const auto transaction = [&](int amount, unsigned stamp, int section = 21) {
        Transaction tx;
        tx.set_type(TransactionType::Conversion);
        tx.set_sender(owner.id());
        tx.set_receiver(owner.id());
        tx.set_token(token.id());
        tx.set_meta(owner.id().to_string());
        tx.set_section(SectionId(section));
        tx.set_amount(BigNumberFloat(amount));
        tx.set_timestamp(stamp);
        TEST_REQUIRE(tx.sign(owner));
        return tx;
    };
    const auto deliver = [&](const std::vector<Section> &sections) {
        WireFormat::Scope legacy(WireFormat::Mode::Legacy);
        FileSectionsSync  packet { .to = SectionId(40), .sections = { }, .last_section = SectionId(100) };
        for (const auto &section : sections) {
            packet.sections.push_back({ section.id, Json::serialize(section) });
        }
        auto compressed = LegacyCompression::compress(MessagePack::serialize(packet));
        TEST_REQUIRE(compressed.has_value());
        dag.network_file_sections_response(compressed.value(), peer_response);
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
    };
    const auto valid             = transaction(2, 1);
    auto       invalid_signature = valid;
    invalid_signature.set_amount(BigNumberFloat(1));
    deliver({ Section { .id = SectionId(21), .transactions = { invalid_signature } } });
    TEST_REQUIRE(!dag.read_section(SectionId(21)).has_value());
    deliver({ Section { .id = SectionId(21), .transactions = { valid } },
              Section { .id = SectionId(22), .transactions = { transaction(4, 2, 22) } } });
    TEST_REQUIRE(!dag.read_section(SectionId(21)).has_value());
    TEST_REQUIRE(!dag.read_section(SectionId(22)).has_value());
    auto altered_initial = initial;
    altered_initial.set_amount(BigNumberFloat(100));
    TEST_REQUIRE(altered_initial.sign(owner));
    deliver({ Section { .id = SectionId(1), .transactions = { altered_initial } },
              Section { .id = SectionId(21), .transactions = { valid } } });
    TEST_REQUIRE(dag.read_section(SectionId(1)).value().transactions.contains(initial));
    TEST_REQUIRE(!dag.read_section(SectionId(21)).has_value());
    Transaction forged_genesis;
    forged_genesis.set_type(TransactionType::Genesis);
    forged_genesis.set_sender(token.id());
    forged_genesis.set_receiver(token.id());
    forged_genesis.set_section(SectionId(0));
    forged_genesis.set_amount(BigNumberFloat(0));
    TEST_REQUIRE(forged_genesis.sign(token));
    TEST_REQUIRE(dag.prove_transaction(forged_genesis, { }) == TransactionProveError::InvalidSignature);
    deliver({ Section { .id = SectionId(0), .transactions = { forged_genesis } },
              Section { .id = SectionId(21), .transactions = { valid } } });
    TEST_REQUIRE(!dag.read_section(SectionId(0)).has_value());
    TEST_REQUIRE(!dag.read_section(SectionId(21)).has_value());
    deliver({ Section { .id = SectionId(22), .transactions = { valid } } });
    TEST_REQUIRE(!dag.read_section(SectionId(22)).has_value());
    deliver({ Section { .id = SectionId(21), .transactions = { valid } },
              Section { .id = SectionId(21), .transactions = { valid } } });
    TEST_REQUIRE(!dag.read_section(SectionId(21)).has_value());
    deliver({ Section { .id = SectionId(21), .transactions = { valid } } });
    const auto stored = dag.read_section(SectionId(21));
    TEST_REQUIRE(stored.has_value() && stored.value().transactions.contains(valid));
    TEST_REQUIRE(dag.read_section(SectionId(1)).value().transactions.contains(initial));
    node->cleanUp();
    peer.reset();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    std::puts("PASS");
}
