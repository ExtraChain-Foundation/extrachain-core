#include <cstdio>
#include <filesystem>
#include <memory>

#include "chain/dag.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_transaction.h"
#include "core/extrachain_node.h"
#include "test_support.h"
#include "utils/file_io.h"

#include "chain/dag_cache.h"

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    const auto sender   = ActorId::create("1111111111111111111111111111111111111111");
    const auto receiver = ActorId::create("2222222222222222222222222222222222222222");
    if (!sender.has_value() || !receiver.has_value()) {
        return 1;
    }

    DagCache cache(nullptr, nullptr);
    Balances balances;

    Transaction reward;
    reward.set_sender(sender.value());
    reward.set_receiver(sender.value());
    reward.set_amount(BigNumberFloat("3"));
    reward.set_type(TransactionType::Reward);
    reward.set_token(TokenId());
    cache.apply_transaction_delta(reward, balances);

    Transaction transfer;
    transfer.set_sender(sender.value());
    transfer.set_receiver(receiver.value());
    transfer.set_amount(BigNumberFloat("1.25"));
    transfer.set_type(TransactionType::Regular);
    transfer.set_token(TokenId());
    cache.apply_transaction_delta(transfer, balances);

    const auto sender_key   = std::pair { sender.value(), TokenId() };
    const auto receiver_key = std::pair { receiver.value(), TokenId() };
    const bool transferred =
        balances[sender_key] == BigNumberFloat("1.75") && balances[receiver_key] == BigNumberFloat("1.25");

    Transaction burn;
    burn.set_sender(receiver.value());
    burn.set_receiver(ActorId());
    burn.set_amount(BigNumberFloat("0.25"));
    burn.set_type(TransactionType::Burn);
    burn.set_token(TokenId());
    cache.apply_transaction_delta(burn, balances);

    const bool burned = balances[receiver_key] == BigNumberFloat("1");

    // Undoing the same three transactions in reverse order must land back on zero:
    // calculate_balances rewinds a cache with exactly this walk.
    cache.apply_transaction(burn, balances, true);
    cache.apply_transaction(transfer, balances, true);
    cache.apply_transaction(reward, balances, true);
    const bool reversed =
        balances[sender_key] == BigNumberFloat("0") && balances[receiver_key] == BigNumberFloat("0");

    const auto original_path = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-contract-balances-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> signer;
    signer.create(ActorType::User);
    const auto token = ActorId::create("3333333333333333333333333333333333333333");
    TEST_REQUIRE(token.has_value());
    const auto arguments = MessagePack::serialize(
        std::vector<std::tuple<std::string, std::string>> { { receiver.value().to_string(), "5" } });
    const std::vector<ExtraChain::Contracts::ContractEffect> effects {
        { .kind      = ExtraChain::Contracts::ContractEffectKind::TokenDelta,
          .target    = token.value().to_string(),
          .operation = "mint",
          .arguments = std::vector<std::uint8_t>(arguments.begin(), arguments.end()) }
    };
    ContractTransactionData metadata;
    metadata.kind           = "fungible-token";
    metadata.effects_hash   = ExtraChain::Contracts::Codec::effect_hash(effects);
    metadata.effects_base64 = Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(effects));
    Transaction mint;
    mint.set_sender(signer.id());
    mint.set_receiver(token.value());
    mint.set_type(TransactionType::ContractCall);
    mint.set_section(SectionId(9));
    mint.set_meta(Json::serialize(metadata));
    TEST_REQUIRE(mint.sign(signer));
    const auto section = Json::serialize(Section { .id = SectionId(9), .transactions = { mint } });
    TEST_REQUIRE(
        FileIo::write_atomic(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "9", section).has_value());
    const auto balance_key = std::pair { receiver.value(), token.value() };
    TEST_REQUIRE(node->dag()->cache().write_cached_balances({}, SectionId(0)));
    const auto forward =
        node->dag()->cache().calculate_balances({ receiver.value() }, SectionId(10), SectionId(0), SectionId(9));
    TEST_REQUIRE(forward.contains(balance_key));
    TEST_REQUIRE_EQ(forward.at(balance_key), BigNumberFloat("5"));
    TEST_REQUIRE(
        node->dag()->cache().write_cached_balances({ { balance_key, BigNumberFloat("5") } }, SectionId(10)));
    const auto rewind =
        node->dag()->cache().calculate_balances({ receiver.value() }, SectionId(10), SectionId(0), SectionId(8));
    TEST_REQUIRE(rewind.contains(balance_key));
    TEST_REQUIRE_EQ(rewind.at(balance_key), BigNumberFloat("0"));
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original_path);
    std::filesystem::remove_all(directory);

    std::printf("native transfer=%s burn=%s reverse=%s sender=%s receiver=%s\n",
                transferred ? "PASS" : "FAIL",
                burned ? "PASS" : "FAIL",
                reversed ? "PASS" : "FAIL",
                balances[sender_key].to_string().c_str(),
                balances[receiver_key].to_string().c_str());
    return transferred && burned && reversed ? 0 : 1;
}
