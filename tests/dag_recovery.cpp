#include <filesystem>
#include <memory>

#include "chain/actor.h"
#include "chain/dag.h"
#include "chain/dag_quarantine.h"
#include "core/extrachain_node.h"
#include "network/responder.h"
#include "test_support.h"

namespace {
    class CapturingSender final : public ResponseSender {
    public:
        std::string send_response(const std::string &data_serialized,
                                  MessageType        type,
                                  SendMode,
                                  MessageStatus,
                                  const Responder &) override {
            payload      = data_serialized;
            message_type = type;
            ++responses;
            return {};
        }

        std::string payload;
        MessageType message_type = MessageType::Custom;
        std::size_t responses    = 0;
    };
} // namespace

int main() {
    const auto original_path = std::filesystem::current_path();
    const auto test_path =
        std::filesystem::temp_directory_path() / ("extrachain-dag-recovery-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(test_path);
    std::filesystem::current_path(test_path);

    Actor<KeyPrivate> actor;
    actor.create(ActorType::User);
    Transaction transaction;
    transaction.set_sender(actor.id());
    transaction.set_receiver(actor.id());
    transaction.set_token(actor.id());
    transaction.set_type(TransactionType::Reward);
    transaction.set_amount(BigNumberFloat("0.01"));
    transaction.set_section(SectionId(42));
    TEST_REQUIRE(transaction.sign(actor));

    DagQuarantine quarantine(test_path / "quarantine" / "Quarantine.db");
    TEST_REQUIRE(quarantine.record(transaction, "test-reason", "test-source"));
    TEST_REQUIRE(quarantine.contains(transaction.hash()));
    TEST_REQUIRE_EQ(quarantine.count(), std::uint64_t(1));
    quarantine.resolve_range(SectionId(40), SectionId(50));
    TEST_REQUIRE(!quarantine.contains(transaction.hash()));

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);

    node->dag()->cache().write_cached_balances({}, SectionId(123));
    TEST_REQUIRE_EQ(node->dag()->cache().section(), SectionId(123));
    node->dag()->cache().reset_db();
    TEST_REQUIRE_EQ(node->dag()->cache().section(), SectionId(-1));

    CapturingSender sender;
    Responder       responder(&sender);
    responder.add_identifier("missing-control-peer");
    node->dag()->network_request_control_section(DagControlRangeRequest { .from = SectionId(0),
                                                                          .to   = SectionId(20) },
                                                 responder);
    TEST_REQUIRE_EQ(sender.responses, std::size_t(1));
    TEST_REQUIRE_EQ(sender.message_type, MessageType::DagControlRangeResponse);

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original_path);
    std::filesystem::remove_all(test_path);
    return 0;
}
