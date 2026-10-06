#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "test_support.h"
#include "utils/exc_utils.h"

#include <filesystem>
#include <memory>
#include <vector>

namespace ExtraChain::Consensus {
    class ConsensusStateTestFixture {
    public:
        static void attach(ConsensusService& service, std::unique_ptr<ShadowConsensus> shadow) {
            service.consensus_    = std::move(shadow);
            service.intent_store_ = std::make_unique<IntentStore>(service.directory_ / "intent-pool.sqlite");
            TEST_REQUIRE(service.intent_store_->open().has_value());
            service.committed_nonces_ = service.intent_store_->load_committed_nonces().value();
        }
        static void apply(ConsensusService& service, std::uint64_t height) {
            service.applied_checkpoint_ = AppliedCheckpoint { .height = height, .header_hash = "applied" };
        }
        static auto next_nonce(ConsensusService& service, const ActorId& sender) {
            std::lock_guard lock(service.mutex_);
            return service.next_local_nonce(sender);
        }
    };
} // namespace ExtraChain::Consensus
using namespace ExtraChain::Consensus;

// On the stand a node proved storage at height 1278 round 1 with nonce 5319, valid up to that height.
// A round-2 batch without the proof was certified at the same height too, the node expired the
// proof by that certificate and gave nonce 5319 to a transfer. The round-1 batch was finalized, so
// the transfer was rejected as a competitor of the proof and the wave never converged.
int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-nonce-fork-" + Utils::generate_random_hex(12));
    TEST_REQUIRE(std::filesystem::create_directory(directory));
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);
    Actor<KeyPrivate> network;
    network.create(ActorType::Service);
    Actor<KeyPrivate> sender;
    sender.create(ActorType::User);
    TEST_REQUIRE(node->actor_index()->store_new_actor(sender.to_public()).has_value());
    std::vector<KeyPrivate>      keys(7);
    std::vector<ValidatorRecord> records;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index].generate_random();
        Actor<KeyPrivate> actor;
        actor.create(ActorType::Service);
        records.push_back(
            make_validator_record(network.id(), 1, actor, keys[index], "nonce-" + std::to_string(index), 0)
                .value());
    }
    const auto validators = make_validator_set(network.id(), 1, records, network).value();
    const auto consensus  = directory / "nonce-consensus";
    TEST_REQUIRE(std::filesystem::create_directory(consensus));
    TEST_REQUIRE(ShadowConsensus::write_configuration(consensus,
                                                      { .mode                   = ShadowMode::Finality,
                                                        .activation_height      = 1,
                                                        .activation_dag_section = 20 })
                     .has_value());
    TEST_REQUIRE(ShadowConsensus::write_validator_set(consensus, validators).has_value());
    const auto               view = ValidatorSetView::create(validators).value();
    std::vector<std::string> governance_keys;
    for (std::size_t index = 0; index < 5; ++index)
        governance_keys.push_back(Utils::to_base64(keys[index].public_key()));
    const auto    governance = make_multisig_policy(network.id(), GovernanceThreshold, governance_keys).value();
    const auto    recovery   = make_multisig_policy(network.id(), RecoveryThreshold, governance_keys).value();
    TrustAnchorV1 anchor { .network_id         = network.id(),
                           .initial_validators = validators,
                           .governance_policy  = governance,
                           .recovery_policy    = recovery };
    anchor.authorization =
        authorize_action(governance, 1, trust_anchor_action_hash(anchor), { keys[0], keys[1], keys[2] }).value();
    ActivationManifestV1 manifest { .network_id             = network.id(),
                                    .activation_height      = 1,
                                    .activation_dag_section = 20,
                                    .validator_set_hash     = view.hash() };
    manifest.authorization =
        authorize_action(governance, 1, activation_action_hash(manifest), { keys[0], keys[1], keys[2] }).value();
    TEST_REQUIRE(ShadowConsensus::write_governance_policy(consensus, governance).has_value());
    TEST_REQUIRE(ShadowConsensus::write_recovery_policy(consensus, recovery).has_value());
    TEST_REQUIRE(ShadowConsensus::write_trust_anchor(consensus, anchor).has_value());
    TEST_REQUIRE(ShadowConsensus::write_activation_manifest(consensus, manifest, governance).has_value());
    auto shadow = ShadowConsensus::load(consensus, network.id());
    TEST_REQUIRE(shadow.has_value());
    auto&             engine = shadow.value()->engine();
    QuorumCertificate parent = engine.genesis_certificate();
    ConsensusService  service(*node, consensus);
    ConsensusStateTestFixture::attach(service, std::move(shadow.value()));

    // Certifies an empty batch at the next height: the branch the node sees has no request in it.
    std::string prior_section_root = std::string(64, 'a');
    std::string prior_state;
    const auto  certify = [&](std::uint64_t height, bool stage = true) {
        SectionBatchData batch;
        batch.manifest.first_section         = (height - 1) * ShadowSectionInterval + 1;
        batch.manifest.last_section          = height * ShadowSectionInterval;
        batch.manifest.previous_section_root = prior_section_root;
        for (auto section = batch.manifest.first_section; section <= batch.manifest.last_section; ++section) {
            auto bytes = Json::serialize(Section { .id = SectionId(section) });
            batch.manifest.payload_bytes += bytes.size();
            batch.sections.emplace_back(section, std::move(bytes));
        }
        batch.manifest.transaction_root = calculate_transaction_root(batch.manifest.transaction_hashes);
        batch.manifest.data_root        = calculate_data_root(batch.sections);
        const auto        root          = node->dag()->shadow_batch_section_root(batch).value();
        StateCommitmentV2 state { .network_id                = network.id(),
                                   .epoch                     = 1,
                                   .height                    = height,
                                   .previous_state_commitment = prior_state,
                                   .section_root              = root,
                                   .account_state_root        = std::string(64, 'b'),
                                   .contract_state_root       = std::string(64, 'c'),
                                   .token_registry_root       = std::string(64, 'd'),
                                   .mining_state_root         = std::string(64, 'e'),
                                   .validator_set_hash        = view.hash() };
        Proposal          proposal { .header             = { .network_id              = network.id(),
                                                              .epoch                   = 1,
                                                              .height                  = height,
                                                              .dag_section             = batch.manifest.last_section,
                                                              .parent_certificate_hash = hash_certificate(parent),
                                                              .section_root            = root,
                                                              .transaction_root        = batch.manifest.transaction_root,
                                                              .batch_root              = hash_batch_manifest(batch.manifest),
                                                              .validator_set_hash      = view.hash(),
                                                              .state_commitment        = hash_state_commitment(state),
                                                              .logical_time            = height },
                                      .state              = state,
                                      .batch              = batch.manifest,
                                      .parent_certificate = parent,
                                      .proposer_id        = view.leader(height, 0).validator_id };
        const auto        signer = std::ranges::find_if(keys, [&](const auto& key) {
            return validator_id_for(key.public_key()) == proposal.proposer_id;
        });
        TEST_REQUIRE(signer != keys.end());
        proposal.signature = sign_payload(*signer, proposal_signing_payload(proposal)).value();
        batch.header_hash  = hash_header(proposal.header);
        TEST_REQUIRE(engine.observe_proposal(proposal).has_value());
        if (stage)
            TEST_REQUIRE(engine.stage_batch(batch).has_value());
        QuorumCertificate certificate { .network_id    = network.id(),
                                         .epoch         = 1,
                                         .height        = height,
                                         .header_hash   = batch.header_hash,
                                         .signer_bitmap = { 0x1f } };
        for (std::size_t index = 0; index < 5; ++index) {
            const auto validator = view.active()[index].validator_id;
            const auto key       = std::ranges::find_if(keys, [&](const auto& candidate) {
                return validator_id_for(candidate.public_key()) == validator;
            });
            Vote       vote { .network_id   = network.id(),
                               .epoch        = 1,
                               .height       = height,
                               .header_hash  = batch.header_hash,
                               .validator_id = validator };
            certificate.signatures.push_back(sign_payload(*key, vote_signing_payload(vote)).value());
        }
        TEST_REQUIRE(engine.accept_certificate(certificate).has_value());
        parent             = certificate;
        prior_state        = proposal.header.state_commitment;
        prior_section_root = root;
    };

    certify(1);
    const auto proof = service.submit_local_intent(TransactionIntentV2 { .network_id = network.id(),
                                                                         .sender     = sender.id(),
                                                                         .receiver   = sender.id(),
                                                                         .amount     = "0",
                                                                         .operation  = IntentOperation::Cancel,
                                                                         .expires_after_height = 2 },
                                                   "",
                                                   sender);
    TEST_REQUIRE(proof.has_value());
    TEST_REQUIRE_EQ(ConsensusStateTestFixture::next_nonce(service, sender.id()).value(), std::uint64_t(2));
    certify(2);
    // Another batch at height 2 may still carry the request, so its nonce stays taken.
    const auto after_certificate = ConsensusStateTestFixture::next_nonce(service, sender.id()).value();
    std::printf("next nonce after a certificate past the request's last height: %llu\n",
                static_cast<unsigned long long>(after_certificate));
    std::fflush(stdout);
    // Once height 2 is applied without it, the request can no longer be included and its nonce is free.
    ConsensusStateTestFixture::apply(service, 2);
    const auto after_apply = ConsensusStateTestFixture::next_nonce(service, sender.id()).value();
    // A certificate can arrive before its batch; a local request must not wait for that batch.
    certify(3, false);
    const auto without_batch = ConsensusStateTestFixture::next_nonce(service, sender.id());
    std::printf("next nonce before the certified batch arrives: %s\n",
                without_batch.has_value() ? std::to_string(without_batch.value()).c_str() : "unavailable");
    std::fflush(stdout);

    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    TEST_REQUIRE_EQ(after_certificate, std::uint64_t(2));
    TEST_REQUIRE_EQ(after_apply, std::uint64_t(1));
    TEST_REQUIRE(without_batch.has_value() && without_batch.value() == std::uint64_t(1));
}
