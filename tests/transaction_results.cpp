#include "chain/actor_index.h"
#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "test_support.h"

namespace {
    class Peer final : public SocketService {
    public:
        explicit Peer(PeerContext& context)
            : SocketService(context) {
            identifier_ = std::string(64, 'a');
            activated_  = true;
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
                std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size() - crypto_sign_BYTES));
            if (body.has_value() && body.value().message_type == MessageType::DagTransaction) {
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
    const auto home     = std::filesystem::temp_directory_path()
                          / ("extrachain-transaction-results-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    Actor<KeyPrivate> owner;
    Actor<KeyPrivate> token;
    owner.create(ActorType::User);
    token.create(ActorType::User);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    node->account_controller()->create_profile("transaction-results", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(token.to_public()).has_value());
    auto& dag = *node->dag();
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
    const auto transaction = [&](int amount, std::uint64_t stamp) {
        Transaction tx;
        tx.set_type(TransactionType::Conversion);
        tx.set_sender(owner.id());
        tx.set_receiver(owner.id());
        tx.set_token(token.id());
        tx.set_meta(owner.id().to_string());
        tx.set_section(SectionId(21));
        tx.set_amount(BigNumberFloat(amount));
        tx.set_timestamp(stamp);
        TEST_REQUIRE(tx.sign(owner));
        return tx;
    };
    const auto stored = [&](const Transaction& tx) {
        const auto section = dag.read_section(tx.section());
        return section.has_value() && section.value().transactions.contains(tx);
    };
    unsigned approvals  = 0;
    unsigned rejections = 0;
    auto     approved   = dag.transaction_approved_event().subscribe([&](const SectionId&, const std::string&) {
        ++approvals;
    });
    auto     rejected   = dag.transaction_rejected_event().subscribe([&](const SectionId&, const std::string&) {
        ++rejections;
    });

    const auto valid   = transaction(2, 1);
    const auto request = Responder(nullptr).with_new_message_id();
    dag.add_transaction_sended(valid, request);
    auto response = request;
    response.add_identifier(peer->identifier());
    TransactionResult result { valid.section(), valid.hash(), TransactionProveError::NoError };
    auto              wrong_peer = request;
    wrong_peer.add_identifier(std::string(64, 'b'));
    dag.network_transaction_result(result, wrong_peer);
    dag.network_transaction_result(result, response.with_new_message_id());
    auto wrong_section = result;
    wrong_section.section_id += SectionId(1);
    dag.network_transaction_result(wrong_section, response);
    TEST_REQUIRE(!stored(valid) && approvals == 0);

    auto rejection   = result;
    rejection.result = TransactionProveError::Unknown;
    dag.network_transaction_result(rejection, response);
    TEST_REQUIRE(dag.sended_transactions().contains(valid.hash()));
    TEST_REQUIRE(dag.failed_transactions().empty() && rejections == 0);

    const auto deliver = [&](MessageStatus status) {
        WireFormat::Scope scope(WireFormat::wire());
        const auto        body      = make_init_message(MessagePack::serialize(result),
                                                        SendMode::Focused,
                                                        MessageType::DagTransactionResult,
                                                        status,
                                                        token.id(),
                                                        request.message_id(),
                                                        peer->identifier());
        const auto        signature = token.key().sign(ByteArray(body.calculate_hash()).toBytes());
        TEST_REQUIRE(signature.has_value());
        node->network()->message_received(body.serialize() + ByteArray(signature.value()).toString(),
                                          "127.0.0.2",
                                          peer->identifier());
    };
    deliver(MessageStatus::Request);
    deliver(MessageStatus::NoStatus);
    TEST_REQUIRE(!stored(valid) && approvals == 0);
    deliver(MessageStatus::Response);
    TEST_REQUIRE(stored(valid));
    TEST_REQUIRE(!dag.sended_transactions().contains(valid.hash()));
    TEST_REQUIRE(approvals == 1);
    dag.network_transaction_result(result, response);
    TEST_REQUIRE(approvals == 1);

    const auto overspend      = transaction(4, 2);
    const auto second_request = Responder(nullptr).with_new_message_id();
    dag.add_transaction_sended(overspend, second_request);
    auto second_response = second_request;
    second_response.add_identifier(peer->identifier());
    dag.network_transaction_result({ overspend.section(), overspend.hash(), TransactionProveError::NoError },
                                   second_response);
    TEST_REQUIRE(!stored(overspend));
    TEST_REQUIRE(!dag.sended_transactions().contains(overspend.hash()));
    TEST_REQUIRE(dag.failed_transactions().contains(overspend.hash()));
    TEST_REQUIRE(approvals == 1 && rejections == 1);

    const auto sent = dag.send_transaction(transaction(1, 3), owner);
    TEST_REQUIRE(sent.has_value());
    const auto message_id = peer->last_request();
    TEST_REQUIRE(message_id.size() == 15);
    Responder sent_response;
    sent_response.add_identifier(peer->identifier());
    sent_response.set_message_id(message_id);
    dag.network_transaction_result({ sent.value().section(), sent.value().hash(), TransactionProveError::NoError },
                                   sent_response);
    TEST_REQUIRE(stored(sent.value()) && approvals == 2);
    node->cleanUp();
    peer.reset();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    std::puts("PASS");
}
