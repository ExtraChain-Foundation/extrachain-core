#include <cstdio>

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
    std::printf("native transfer=%s burn=%s sender=%s receiver=%s\n",
                transferred ? "PASS" : "FAIL",
                burned ? "PASS" : "FAIL",
                balances[sender_key].to_string().c_str(),
                balances[receiver_key].to_string().c_str());
    return transferred && burned ? 0 : 1;
}
