#include "consensus/mining_request.h"
#include "chain/transaction.h"
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/serialization.h"

using namespace ExtraChain::Consensus;

int main() {
    Actor<KeyPrivate> provider;
    provider.create(ActorType::User);
    const auto network = ActorId::create(std::string(40, 'a')).value();
    const auto reader  = [](std::uint64_t) -> std::expected<std::string, ConsensusError> {
        return "data";
    };
    const auto dataset  = commit_storage_dataset(4, reader).value();
    const auto identity = storage_dataset_id(network, dataset).value();
    const auto request  = [&](IntentOperation operation, const auto& value, std::uint64_t nonce) {
        IntentEnvelope envelope;
        envelope.metadata = Utils::to_base64(MessagePack::serialize(value));
        envelope.intent   = make_intent(TransactionIntentV2 { .network_id           = network,
                                                              .sender               = provider.id(),
                                                              .receiver             = network,
                                                              .amount               = "0",
                                                              .operation            = operation,
                                                              .account_nonce        = nonce,
                                                              .expires_after_height = 1000 },
                                        envelope.metadata,
                                        provider)
                                .value();
        return envelope;
    };
    auto       state        = create_mining_state(network, 0).value();
    const auto registration = request(IntentOperation::StorageRegister, dataset, 1);
    TEST_REQUIRE(verify_intent(registration, Utils::to_base64(provider.key().public_key())));
    TEST_REQUIRE(apply_mining_request(state, registration).has_value());
    TEST_REQUIRE(!apply_mining_request(state, registration).has_value());
    const auto transaction = materialize_intent(registration, 1, 1, { });
    TEST_REQUIRE(transaction.has_value());
    TEST_REQUIRE(transaction.value().type() == TransactionType::StorageRegister);
    TEST_REQUIRE(transaction.value().verify(provider.to_public()));
    auto modified = registration;
    modified.metadata += 'a';
    TEST_REQUIRE(!verify_intent(modified, Utils::to_base64(provider.key().public_key())));
    for (unsigned mutation = 0; mutation < 7; ++mutation) {
        modified = registration;
        if (mutation == 0)
            modified.intent.token = network;
        if (mutation == 1)
            modified.intent.amount = "1";
        if (mutation == 2)
            modified.intent.amount = "0.0";
        if (mutation == 3)
            modified.intent.receiver = provider.id();
        if (mutation == 4)
            modified.intent.sender = ActorId { };
        if (mutation == 5)
            modified.metadata = std::string(256 * 1024 + 1, 'a');
        if (mutation == 6)
            modified.metadata = Utils::to_base64(MessagePack::serialize(std::string("wrong type")));
        const auto before = mining_state_root(state);
        TEST_REQUIRE(!apply_mining_request(state, modified).has_value());
        TEST_REQUIRE_EQ(mining_state_root(state), before);
    }
    state.epochs.emplace(0, freeze_mining_epoch(network, 0, 10, { { provider.id(), dataset, 0 } }).value());
    const StorageChallenge challenge { 0, std::string(64, 'b') };
    TEST_REQUIRE(open_mining_proof_window(state.epochs.at(0), challenge, 81).has_value());
    MiningProofSubmission submission {
        0,
        identity,
        make_storage_proof(network, provider.id(), dataset, challenge, reader).value()
    };
    const auto proof = request(IntentOperation::StorageProof, submission, 2);
    TEST_REQUIRE(!apply_mining_request(state, proof).has_value());
    state.section                       = 81;
    auto damaged                        = submission;
    damaged.proof.samples.front().bytes = "fake";
    TEST_REQUIRE(!apply_mining_request(state, request(IntentOperation::StorageProof, damaged, 2)).has_value());
    TEST_REQUIRE(apply_mining_request(state, proof).has_value());
    TEST_REQUIRE(!apply_mining_request(state, proof).has_value());
    damaged.proof.samples.resize(StorageChallengeSamples + 1);
    TEST_REQUIRE(!decode_mining_request(request(IntentOperation::StorageProof, damaged, 2)).has_value());
    const auto withdrawal = request(IntentOperation::StorageUnregister, identity, 3);
    TEST_REQUIRE(apply_mining_request(state, withdrawal).has_value());
    TEST_REQUIRE(state.registrations.empty());
    TEST_REQUIRE(state.epochs.at(0).datasets.at(identity).providers.contains(provider.id().to_string()));
    TEST_REQUIRE(!apply_mining_request(state, withdrawal).has_value());
}
