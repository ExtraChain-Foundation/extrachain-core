#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "test_support.h"
#include "utils/exc_utils.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace ExtraChain::Consensus {
    class ConsensusStateTestFixture {
    public:
        static void attach(ConsensusService& service, std::unique_ptr<ShadowConsensus> shadow) {
            service.consensus_ = std::move(shadow);
        }
        static auto build(ConsensusService&        service,
                          const SectionBatchData&  batch,
                          std::uint64_t            height,
                          const QuorumCertificate& parent) {
            return service.build_state_commitment(batch, "test-section-root", height, parent);
        }
    };
} // namespace ExtraChain::Consensus
using namespace ExtraChain::Consensus;

SectionBatchData batch(std::uint64_t                   first,
                       const std::vector<Transaction>& transactions,
                       std::string                     previous = "test-section-root") {
    SectionBatchData result;
    result.manifest.first_section         = first;
    result.manifest.last_section          = first + ShadowSectionInterval - 1;
    result.manifest.previous_section_root = std::move(previous);
    for (auto section = first; section <= result.manifest.last_section; ++section) {
        Section value { .id = SectionId(section) };
        for (const auto& transaction : transactions) {
            if (transaction.section() == value.id) {
                value.transactions.insert(transaction);
                result.manifest.transaction_hashes.push_back(consensus_transaction_hash(transaction));
            }
        }
        auto bytes = Json::serialize(value);
        result.manifest.payload_bytes += bytes.size();
        result.sections.emplace_back(section, std::move(bytes));
    }
    result.manifest.transaction_root = calculate_transaction_root(result.manifest.transaction_hashes);
    result.manifest.data_root        = calculate_data_root(result.sections);
    return result;
}

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-state-replay-" + Utils::generate_random_hex(12));
    TEST_REQUIRE(std::filesystem::create_directory(directory));
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);
    Actor<KeyPrivate> network;
    network.create(ActorType::Service);
    std::vector<KeyPrivate>      keys(7);
    std::vector<ValidatorRecord> records;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index].generate_random();
        Actor<KeyPrivate> actor;
        actor.create(ActorType::Service);
        records.push_back(
            make_validator_record(network.id(), 1, actor, keys[index], "replay-" + std::to_string(index), 0)
                .value());
    }
    const auto validators = make_validator_set(network.id(), 1, std::move(records), network).value();
    const auto view       = ValidatorSetView::create(validators).value();
    const auto leader     = view.leader(1, 0).validator_id;
    const auto key        = std::ranges::find_if(keys, [&](const auto& candidate) {
        return validator_id_for(candidate.public_key()) == leader;
    });
    TEST_REQUIRE(key != keys.end());
    TEST_REQUIRE(ShadowConsensus::write_validator_set(directory / "state-consensus", validators).has_value());
    TEST_REQUIRE(ShadowConsensus::write_identity(directory / "state-consensus",
                                                 IdentityDocument { .validator_id = leader, .key = *key })
                     .has_value());
    auto shadow = ShadowConsensus::load(directory / "state-consensus", network.id());
    TEST_REQUIRE(shadow.has_value());
    auto&            engine  = shadow.value()->engine();
    const auto       genesis = engine.genesis_certificate();
    ConsensusService service(*node, directory / "state-consensus");
    ConsensusStateTestFixture::attach(service, std::move(shadow.value()));
    const auto empty = ConsensusStateTestFixture::build(service, batch(1, { }), 1, genesis);
    TEST_REQUIRE(empty.has_value());

    ContractTransactionData metadata { .kind        = "fungible-token",
                                       .language    = "wasm",
                                       .module_hash = std::string(64, 'a'),
                                       .state_hash  = std::string(64, 'b') };
    Transaction             deployment;
    deployment.set_sender(network.id());
    deployment.set_receiver(ActorId::create(std::string(40, 'c')).value());
    deployment.set_type(TransactionType::ContractDeploy);
    deployment.set_section(SectionId(1));
    deployment.set_timestamp(1);
    deployment.set_meta(Json::serialize(metadata));
    TEST_REQUIRE(deployment.sign(network));
    auto       first_batch = batch(1, { deployment });
    const auto first       = ConsensusStateTestFixture::build(service, first_batch, 1, genesis);
    TEST_REQUIRE(first.has_value());
    TEST_REQUIRE(first.value().contract_state_root != empty.value().contract_state_root);
    TEST_REQUIRE(first.value().token_registry_root != empty.value().token_registry_root);
    const auto proposal = engine.make_proposal(first_batch.manifest, first.value());
    TEST_REQUIRE(proposal.has_value());
    first_batch.header_hash = hash_header(proposal.value().header);
    TEST_REQUIRE(engine.stage_batch(first_batch).has_value());
    std::optional<QuorumCertificate> certificate;
    for (const auto& signer : keys) {
        Vote vote { .network_id   = network.id(),
                    .epoch        = 1,
                    .height       = 1,
                    .header_hash  = first_batch.header_hash,
                    .validator_id = validator_id_for(signer.public_key()) };
        vote.signature = sign_payload(signer, vote_signing_payload(vote)).value();
        auto accepted  = engine.accept_vote(vote);
        TEST_REQUIRE(accepted.has_value());
        if (accepted.value().certificate.has_value())
            certificate = accepted.value().certificate;
    }
    TEST_REQUIRE(certificate.has_value());
    TEST_REQUIRE(engine.accept_certificate(certificate.value()).has_value());
    TEST_REQUIRE_EQ(engine.safety_state().finalized_height, 0);
    const auto second = ConsensusStateTestFixture::build(service, batch(21, { }), 2, certificate.value());
    TEST_REQUIRE(second.has_value());
    TEST_REQUIRE_EQ(second.value().contract_state_root, first.value().contract_state_root);
    TEST_REQUIRE_EQ(second.value().token_registry_root, first.value().token_registry_root);
    auto call = deployment;
    call.set_type(TransactionType::ContractCall);
    call.set_section(SectionId(21));
    call.set_timestamp(21);
    metadata.revision   = 2;
    metadata.state_hash = std::string(64, 'd');
    call.set_meta(Json::serialize(metadata));
    TEST_REQUIRE(call.sign(network));
    const auto called = ConsensusStateTestFixture::build(service, batch(21, { call }), 2, certificate.value());
    TEST_REQUIRE(called.has_value());
    TEST_REQUIRE(called.value().contract_state_root != first.value().contract_state_root);
    TEST_REQUIRE_EQ(called.value().token_registry_root, first.value().token_registry_root);
    service.deactivate();
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
