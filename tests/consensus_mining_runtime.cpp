#include "chain/actor_index.h"
#include "chain/dag.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "managers/data_mining_manager.h"
#include "managers/account_controller.h"
#include "dfs/dfs_service.h"
#include <thread>
#include "test_support.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"
#include "utils/db_connector.h"
#include "network/network_service.h"
#include "network/isocket_service.h"

#include <filesystem>
#include <memory>

namespace ExtraChain::Consensus {
    class ConsensusStateTestFixture {
    public:
        static void attach(ConsensusService&                service,
                           std::unique_ptr<ShadowConsensus> shadow,
                           std::optional<AppliedCheckpoint> applied = std::nullopt) {
            service.consensus_          = std::move(shadow);
            service.applied_checkpoint_ = applied;
            service.intent_store_       = std::make_unique<IntentStore>(service.directory_ / "intent-pool.sqlite");
            TEST_REQUIRE(service.intent_store_->open().has_value());
            service.committed_nonces_ = service.intent_store_->load_committed_nonces().value();
        }
        static auto stage(ConsensusService& service, const SectionBatchData& batch) {
            return service.consensus_->engine().stage_batch(batch);
        }
        static auto project(ConsensusService&        service,
                            const SectionBatchData&  batch,
                            const QuorumCertificate& parent) {
            return service.project_mining_state(batch, parent);
        }
        static auto parent(ConsensusService& service, const QuorumCertificate& parent) {
            return service.mining_state_for(parent);
        }
        static auto settlement(ConsensusService& service, const MiningState& parent, std::uint64_t section) {
            return service.next_mining_settlement(parent, section);
        }
        static auto persist(ConsensusService& service, const FinalityProof& proof, const SectionBatchData& batch) {
            return service.persist_mining_state(proof, batch);
        }
        static auto next_nonce(ConsensusService& service, const ActorId& sender) {
            return service.next_local_nonce(sender);
        }
        static std::uint64_t certified_nonce(ConsensusService& service, const ActorId& sender) {
            const auto frontier = service.local_nonce_frontier();
            TEST_REQUIRE(frontier.has_value());
            const auto found = frontier.value().find(sender);
            return found == frontier.value().end() ? 0 : found->second;
        }
        static void require_missing_batch_request(ConsensusService& service, const std::string& hash) {
            service.queue_next_checkpoint();
            TEST_REQUIRE(service.pending_proposals_.contains(hash));
            TEST_REQUIRE(service.ancestor_requests_.contains(hash));
            const auto first = service.ancestor_requests_.at(hash);
            service.queue_next_checkpoint();
            TEST_REQUIRE(service.ancestor_requests_.at(hash).sent == first.sent);
            service.ancestor_requests_.at(hash).sent -= std::chrono::seconds(3);
            service.queue_next_checkpoint();
            TEST_REQUIRE(service.ancestor_requests_.at(hash).peer != first.peer);
        }
        static auto highest(ConsensusService& service) {
            return service.consensus_->engine().safety_state().highest_certificate.value();
        }
        static auto certified_parent(ConsensusService& service) {
            return service.consensus_->engine()
                .proposal_for(highest(service).header_hash)
                .value()
                .parent_certificate;
        }
        static void authenticate(ConsensusService&       service,
                                 const ValidatorSetView& validators,
                                 const KeyPrivate&       key,
                                 const std::string&      peer) {
            PeerAuthenticator responder(validators, ValidatorIdentity { validator_id_for(key.public_key()), key });
            const auto        challenge = service.authenticator_->create_challenge("runtime-test", peer).value();
            const auto        response  = responder.answer_challenge(challenge, peer).value();
            TEST_REQUIRE(service.authenticator_->verify_response(response, peer).has_value());
        }
        static void reset_sync_timer(ConsensusService& service) {
            service.last_sync_request_ = { };
        }
        static void expire(ConsensusService& service, const std::string& hash) {
            TEST_REQUIRE(service.intent_store_->expire({ hash }).has_value());
            service.intent_pool_.erase({ hash });
        }
        static auto ready(ConsensusService& service) {
            return service.has_unfinalized_intents() ? std::vector<IntentEnvelope> { }
                                                     : service.ready_intents(64, 8 * 1024 * 1024);
        }
        static auto admit(ConsensusService& service, const Proposal& proposal, const SectionBatchData& batch) {
            return service.admit_batch_intents(proposal, batch);
        }
        static auto apply(ConsensusService& service, const FinalityProof& proof) {
            return service.apply_finality_proof(proof);
        }
        static void forget(ConsensusService& service) {
            service.finalized_mining_.reset();
            service.staged_mining_.clear();
        }
    };
} // namespace ExtraChain::Consensus
using namespace ExtraChain::Consensus;

class RecoverySocket final : public SocketService {
public:
    RecoverySocket(PeerContext& context, std::string identifier)
        : SocketService(context) {
        identifier_ = std::move(identifier);
        peer_meta_.capabilities.insert(std::string(SHADOW_CONSENSUS_CAPABILITY));
        activated_.store(true);
        mode_ = SocketMode::Full;
    }
    bool is_active() const override {
        return activated_.load();
    }
    std::string protocol_string() const override {
        return "recovery-test";
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
    std::atomic<unsigned>      certificates { 0 }, syncs { 0 };
    std::atomic<std::uint64_t> certified_height { 0 }, sync_height { 0 };
    void                       send_message(std::span<const std::uint8_t> bytes, Priority) override {
        TEST_REQUIRE(bytes.size() >= crypto_sign_BYTES);
        const auto body = MessagePack::deserialize<MessageBody>(
            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size() - crypto_sign_BYTES));
        TEST_REQUIRE(body.has_value());
        if (body.value().message_type == MessageType::ConsensusCertificate) {
            const auto value = MessagePack::deserialize<QuorumCertificate>(body.value().data);
            TEST_REQUIRE(value.has_value());
            certified_height.store(value.value().height);
            ++certificates;
        }
        if (body.value().message_type == MessageType::ConsensusSyncRequest) {
            const auto value = MessagePack::deserialize<ShadowSyncRequest>(body.value().data);
            TEST_REQUIRE(value.has_value());
            sync_height.store(value.value().finalized_height);
            ++syncs;
        }
    }
};

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-mining-runtime-" + Utils::generate_random_hex(12));
    TEST_REQUIRE(std::filesystem::create_directory(directory));
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    node->dag()->set_mode(DagMode::Full);
    // Fixed test identities keep the signed hash-order fixture repeatable.
    const auto fixture_actor = [](const std::string& label, ActorType type) {
        KeyPrivate key;
        key.generate_seed(MasterSeed { }, label);
        Actor<KeyPrivate> actor;
        actor.set_type(type);
        actor.set_secret_key(key.secret_key(), key.public_key());
        actor.set_id(ActorId::create(Utils::calculate_hash(ByteArray(key.public_key()).toString(),
                                                           Utils::HashAlgorithm::Blake3)
                                         .substr(0, ActorId::SIZE))
                         .value());
        return actor;
    };
    auto network  = fixture_actor("mining-runtime-network", ActorType::Service);
    auto provider = fixture_actor("mining-runtime-provider", ActorType::User);
    node->account_controller()->create_profile("mining-runtime-profile", ActorType::User, provider);
    TEST_REQUIRE(node->account_controller()->system_actor().id() == provider.id());
    TEST_REQUIRE(node->actor_index()->exists(provider.id()));
    TEST_REQUIRE(node->actor_index()->store_new_actor(network.to_public()).has_value());
    std::vector<KeyPrivate>      keys(7);
    std::vector<ValidatorRecord> records;
    for (std::size_t index = 0; index < keys.size(); ++index) {
        keys[index].generate_random();
        Actor<KeyPrivate> actor;
        actor.create(ActorType::Service);
        records.push_back(
            make_validator_record(network.id(), 1, actor, keys[index], "mining-" + std::to_string(index), 0)
                .value());
    }
    const auto               validators = make_validator_set(network.id(), 1, records, network).value();
    const auto               view       = ValidatorSetView::create(validators).value();
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
                                    .validator_set_hash     = view.hash(),
                                    .mining_policy          = MiningEmissionPolicy { 2, { { 1, 10 } } } };
    manifest.authorization =
        authorize_action(governance, 1, activation_action_hash(manifest), { keys[0], keys[1], keys[2] }).value();
    TEST_REQUIRE(ShadowConsensus::write_configuration("consensus",
                                                      { .mode                   = ShadowMode::Finality,
                                                        .activation_height      = 1,
                                                        .activation_dag_section = 20 })
                     .has_value());
    TEST_REQUIRE(ShadowConsensus::write_validator_set("consensus", validators).has_value());
    TEST_REQUIRE(ShadowConsensus::write_governance_policy("consensus", governance).has_value());
    TEST_REQUIRE(ShadowConsensus::write_recovery_policy("consensus", recovery).has_value());
    TEST_REQUIRE(ShadowConsensus::write_trust_anchor("consensus", anchor).has_value());
    TEST_REQUIRE(ShadowConsensus::write_activation_manifest("consensus", manifest, governance).has_value());
    auto shadow = ShadowConsensus::load("consensus", network.id());
    TEST_REQUIRE(shadow.has_value());
    auto* engine  = &shadow.value()->engine();
    auto& service = *node->consensus();
    ConsensusStateTestFixture::attach(service, std::move(shadow.value()));
    auto                                      parent             = engine->genesis_certificate();
    std::string                               prior_section_root = std::string(64, 'a');
    std::string                               prior_state;
    std::map<std::uint64_t, SectionBatchData> batches;
    std::optional<Transaction>                paid;
    const auto reader = [](std::uint64_t) -> std::expected<std::string, ConsensusError> {
        return "bytes";
    };
    const auto dataset    = commit_storage_dataset(5, reader).value();
    const auto dataset_id = storage_dataset_id(network.id(), dataset).value();
    const auto request    = [&](IntentOperation operation,
                                const auto&     value,
                                std::uint64_t   nonce,
                                std::uint64_t   section,
                                std::uint64_t   height) {
        IntentEnvelope envelope;
        envelope.metadata = Utils::to_base64(MessagePack::serialize(value));
        envelope.intent   = make_intent(TransactionIntentV2 { .network_id           = network.id(),
                                                              .sender               = provider.id(),
                                                              .receiver             = network.id(),
                                                              .amount               = "0",
                                                              .operation            = operation,
                                                              .account_nonce        = nonce,
                                                              .expires_after_height = 1000 },
                                        envelope.metadata,
                                        provider)
                                .value();
        return materialize_intent(envelope, section, height, { }).value();
    };
    node->data_mining_manager()->set_enabled(true);
    const auto local_file_id = Utils::calculate_hash("mining-runtime-file");
    const auto local_path    = Dfs::Path::file_path(provider.id(), local_file_id).value();
    std::filesystem::create_directories(local_path.native().parent_path());
    TEST_REQUIRE(FileIo::write_atomic(local_path.native(), "bytes").has_value());
    Dfs::DirRow file_row { .actor_id = provider.id(),
                           .owner_id = provider.id(),
                           .file_id  = local_file_id,
                           .hash     = Utils::calculate_hash("bytes"),
                           .name     = "mining-runtime.bin",
                           .size     = 5,
                           .type     = Dfs::FileType::File,
                           .state    = Dfs::FileState::Ready };
    node->dfs()->notify_stored(provider.id(), file_row);
    auto alias_row        = file_row;
    alias_row.file_id     = Utils::calculate_hash("mining-runtime-alias");
    const auto alias_path = Dfs::Path::file_path(provider.id(), alias_row.file_id).value();
    TEST_REQUIRE(FileIo::write_atomic(alias_path.native(), "bytes").has_value());
    node->dfs()->notify_stored(provider.id(), alias_row);
    {
        const auto withdrawal   = request(IntentOperation::StorageUnregister, dataset_id, 2, 21, 2);
        auto       registration = request(IntentOperation::StorageRegister, dataset, 1, 21, 2);
        // Force hash order to oppose nonce order, keeping the same section and logical time.
        for (std::uint64_t expiry = 1001; !(withdrawal < registration) && expiry < 1129; ++expiry) {
            auto envelope                        = intent_from_transaction(registration).value();
            envelope.intent.expires_after_height = expiry;
            envelope.intent = make_intent(envelope.intent, envelope.metadata, provider).value();
            registration    = materialize_intent(envelope, 21, 2, { }).value();
        }
        TEST_REQUIRE(withdrawal < registration);
        Section          value { .id = SectionId(21), .transactions = { withdrawal, registration } };
        SectionBatchData ordered;
        ordered.manifest.first_section = ordered.manifest.last_section = 21;
        ordered.sections.emplace_back(21, Json::serialize(value));
        const auto replayed =
            replay_mining_batch(configure_mining_state(network.id(), 20, manifest.mining_policy).value(),
                                manifest.mining_policy,
                                ordered,
                                { },
                                LightClientVerifier::bootstrap(anchor).value());
        TEST_REQUIRE(replayed.has_value() && replayed.value().registrations.empty());
    }
    for (std::uint64_t height = 1; height <= 13; ++height) {
        const auto               first = (height - 1) * ShadowSectionInterval + 1;
        std::vector<Transaction> transactions;
        if (height == 7) {
            node->data_mining_manager()->set_enabled(false);
            // Loss of the derived index and one alias must not withdraw a dataset with another valid copy.
            TEST_REQUIRE(std::filesystem::remove(local_path.native()));
            for (const auto& index : std::filesystem::directory_iterator("consensus/mining-index"))
                TEST_REQUIRE(std::filesystem::remove(index.path()));
            node->dfs()->notify_local_removed(provider.id(), file_row.file_id);
        }
        if (height == 8)
            node->data_mining_manager()->set_enabled(true);
        node->data_mining_manager()->consensus_progress();
        auto ready = ConsensusStateTestFixture::ready(service);
        if (height == 2 || height == 8) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (ready.empty() && std::chrono::steady_clock::now() < deadline) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                node->data_mining_manager()->consensus_progress();
                ready = ConsensusStateTestFixture::ready(service);
            }
            TEST_REQUIRE_EQ(ready.size(), std::size_t(1));
            TEST_REQUIRE(ready.front().intent.operation
                         == (height == 2 ? IntentOperation::StorageRegister : IntentOperation::StorageProof));
        }
        if (height == 8) {
            TEST_REQUIRE_EQ(ready.front().intent.valid_after_height, std::uint64_t(7));
            TEST_REQUIRE_EQ(ready.front().intent.expires_after_height, std::uint64_t(8));
            IntentPool proof_pool;
            TEST_REQUIRE(
                proof_pool.submit(ready.front(), Utils::to_base64(provider.key().public_key()), 1, 8).has_value());
            TEST_REQUIRE(proof_pool.ready({ { provider.id(), 1 } }, 9, 10, 1024 * 1024).empty());
        }
        for (const auto& envelope : ready)
            transactions.push_back(materialize_intent(envelope, first, height, { }).value());
        const auto parent_state = ConsensusStateTestFixture::parent(service, parent);
        TEST_REQUIRE(parent_state.has_value());
        const auto settlement = ConsensusStateTestFixture::settlement(service, parent_state.value(), first);
        TEST_REQUIRE(settlement.has_value());
        if (height == 11) {
            TEST_REQUIRE(settlement.value().has_value());
            paid = settlement.value();
            TEST_REQUIRE(service.verify_mining_transaction(paid.value()));
            const auto previous         = Json::serialize(Section { .id = SectionId(first - 1) });
            const auto build_settlement = [&](std::uint64_t limit, const std::vector<IntentEnvelope>& intents) {
                return node->dag()->build_shadow_intent_batch(SectionId(first),
                                                              SectionId(first + ShadowSectionInterval - 1),
                                                              height,
                                                              intents,
                                                              limit,
                                                              { },
                                                              previous,
                                                              prior_section_root,
                                                              paid);
            };
            const auto payment_batch = build_settlement(MaximumShadowBatchBytes, { });
            TEST_REQUIRE(payment_batch.has_value());
            TEST_REQUIRE_EQ(payment_batch.value().manifest.transaction_hashes.size(), std::size_t(1));
            const auto extra =
                intent_from_transaction(request(IntentOperation::StorageUnregister, dataset_id, 3, first, height));
            TEST_REQUIRE(extra.has_value());
            const auto exact_payment =
                build_settlement(payment_batch.value().manifest.payload_bytes, { extra.value() });
            TEST_REQUIRE(exact_payment.has_value());
            TEST_REQUIRE_EQ(exact_payment.value().sections, payment_batch.value().sections);
            const auto oversized_payment = build_settlement(payment_batch.value().manifest.payload_bytes - 1, { });
            TEST_REQUIRE(!oversized_payment.has_value());
            TEST_REQUIRE_EQ(oversized_payment.error(), ConsensusError::DataTooLarge);
            transactions.push_back(paid.value());
        } else {
            TEST_REQUIRE(!settlement.value().has_value());
        }
        SectionBatchData batch;
        batch.manifest.first_section         = first;
        batch.manifest.last_section          = first + ShadowSectionInterval - 1;
        batch.manifest.previous_section_root = prior_section_root;
        for (auto section = first; section <= batch.manifest.last_section; ++section) {
            Section value { .id = SectionId(section) };
            if (section == first)
                value.transactions.insert(transactions.begin(), transactions.end());
            for (const auto& transaction : value.transactions)
                batch.manifest.transaction_hashes.push_back(consensus_transaction_hash(transaction));
            auto bytes = Json::serialize(value);
            batch.manifest.payload_bytes += bytes.size();
            batch.sections.emplace_back(section, std::move(bytes));
        }
        batch.manifest.transaction_root = calculate_transaction_root(batch.manifest.transaction_hashes);
        batch.manifest.data_root        = calculate_data_root(batch.sections);
        const auto projected            = ConsensusStateTestFixture::project(service, batch, parent);
        TEST_REQUIRE(projected.has_value());
        TEST_REQUIRE_EQ(projected.value().minted_units, height < 11 ? 0 : 10);
        const auto        root = node->dag()->shadow_batch_section_root(batch).value();
        StateCommitmentV2 state { .network_id                = network.id(),
                                  .epoch                     = 1,
                                  .height                    = height,
                                  .previous_state_commitment = prior_state,
                                  .section_root              = root,
                                  .account_state_root        = std::string(64, 'b'),
                                  .contract_state_root       = std::string(64, 'c'),
                                  .token_registry_root       = std::string(64, 'd'),
                                  .mining_state_root         = mining_state_root(projected.value()),
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
        TEST_REQUIRE(node->dag()->validate_shadow_batch(proposal, batch, MaximumShadowBatchBytes).has_value());
        if (height == 2) {
            auto skipped                 = ready.front();
            skipped.intent.account_nonce = 2;
            skipped.intent               = make_intent(skipped.intent, skipped.metadata, provider).value();
            auto    invalid_batch        = batch;
            Section invalid { .id = SectionId(first) };
            invalid.transactions.insert(materialize_intent(skipped, first, height, { }).value());
            invalid_batch.sections.front().second = Json::serialize(invalid);
            const auto rejected = ConsensusStateTestFixture::admit(service, proposal, invalid_batch);
            TEST_REQUIRE(!rejected.has_value() && rejected.error() == ConsensusError::InvalidNonce);
            TEST_REQUIRE(!service.intent_receipt(hash_intent(skipped.intent)).value().has_value());
        }
        {
            WireFormat::Scope legacy(WireFormat::Mode::Legacy);
            TEST_REQUIRE(ConsensusStateTestFixture::admit(service, proposal, batch).has_value());
            if (paid.has_value()) {
                TEST_REQUIRE(service.verify_mining_transaction(paid.value()));
                TEST_REQUIRE_EQ(paid.value().calculate_hash(), paid.value().hash());
            }
        }
        TEST_REQUIRE(engine->observe_proposal(proposal).has_value());
        TEST_REQUIRE(engine->stage_batch(batch).has_value());
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
        TEST_REQUIRE(engine->accept_certificate(certificate).has_value());
        const std::uint64_t certified_nonce = height >= 8 ? 2 : (height >= 2 ? 1 : 0);
        TEST_REQUIRE_EQ(ConsensusStateTestFixture::certified_nonce(service, provider.id()), certified_nonce);
        if (height == 2) {
            // A certified nonce must remain reserved even when its pending copy is gone.
            ConsensusStateTestFixture::expire(service, hash_intent(ready.front().intent));
            TEST_REQUIRE_EQ(ConsensusStateTestFixture::next_nonce(service, provider.id()).value(), 2);
            std::vector<std::string> window_requests;
            for (std::uint64_t nonce = 2; nonce <= 64; ++nonce) {
                const auto request = make_intent(TransactionIntentV2 { .network_id    = network.id(),
                                                                       .sender        = provider.id(),
                                                                       .receiver      = provider.id(),
                                                                       .amount        = "0",
                                                                       .operation     = IntentOperation::Cancel,
                                                                       .account_nonce = nonce,
                                                                       .expires_after_height = 1000 },
                                                 "",
                                                 provider);
                TEST_REQUIRE(request.has_value());
                const auto accepted = service.submit_intent({ request.value(), "" });
                TEST_REQUIRE(accepted.has_value());
                window_requests.push_back(accepted.value());
            }
            const auto full_window = ConsensusStateTestFixture::next_nonce(service, provider.id());
            TEST_REQUIRE(!full_window.has_value() && full_window.error() == ConsensusError::PoolFull);
            const auto blocked_mining =
                service.submit_mining_request(IntentOperation::StorageUnregister,
                                              Utils::to_base64(MessagePack::serialize(dataset_id)),
                                              provider);
            TEST_REQUIRE(!blocked_mining.has_value() && blocked_mining.error() == ConsensusError::PoolFull);
            for (const auto& hash : window_requests)
                ConsensusStateTestFixture::expire(service, hash);
        }
        batches.emplace(height, batch);
        if (height == 9) {
            const auto historical = engine->proposal_for(batches.at(8).header_hash).value();
            TEST_REQUIRE(ConsensusStateTestFixture::admit(service, historical, batches.at(8)).has_value());
            auto expired_batch = batches.at(8);
            auto section       = Json::deserialize<Section>(expired_batch.sections.front().second).value();
            TEST_REQUIRE(section.transactions.size() == 1);
            auto envelope                        = intent_from_transaction(*section.transactions.begin()).value();
            envelope.intent.valid_after_height   = 0;
            envelope.intent.expires_after_height = historical.header.height - 1;
            envelope.intent      = make_intent(envelope.intent, envelope.metadata, provider).value();
            section.transactions = {
                materialize_intent(envelope, expired_batch.sections.front().first, historical.header.height, { })
                    .value()
            };
            expired_batch.sections.front().second = Json::serialize(section);
            const auto rejected = ConsensusStateTestFixture::admit(service, historical, expired_batch);
            TEST_REQUIRE(!rejected.has_value() && rejected.error() == ConsensusError::IntentExpired);
        }
        if (height >= 3) {
            const auto proof =
                engine->finality_proof_for_section((height - 2) * ShadowSectionInterval).value().value();
            TEST_REQUIRE(ConsensusStateTestFixture::persist(service, proof, batches.at(height - 2)).has_value());
            TEST_REQUIRE(ConsensusStateTestFixture::persist(service, proof, batches.at(height - 2)).has_value());
            // Restore the snapshot before the applied marker advances: interrupted commit is idempotent.
            ConsensusStateTestFixture::forget(service);
            TEST_REQUIRE(ConsensusStateTestFixture::persist(service, proof, batches.at(height - 2)).has_value());
            const auto applied = ConsensusStateTestFixture::apply(service, proof);
            if (!applied.has_value())
                std::fprintf(stderr,
                             "apply height %llu failed: %u\n",
                             static_cast<unsigned long long>(proof.finalized_proposal.header.height),
                             static_cast<unsigned>(applied.error()));
            TEST_REQUIRE(applied.has_value());
            TEST_REQUIRE(!ConsensusStateTestFixture::apply(service, proof).has_value());
            TEST_REQUIRE_EQ(ConsensusStateTestFixture::certified_nonce(service, provider.id()), certified_nonce);
        }
        parent             = certificate;
        prior_state        = proposal.header.state_commitment;
        prior_section_root = root;
    }
    TEST_REQUIRE(paid.has_value());
    Balances balances;
    node->dag()->cache().process_transaction(paid.value(), balances);
    TEST_REQUIRE(balances.at({ provider.id(), ActorId { } }) == BigNumberFloat::create("0.0000001").value());
    const auto before = ConsensusStateTestFixture::parent(service, parent).value();
    TEST_REQUIRE_EQ(before.minted_units, 10);
    const auto credited = node->dag()->calculate_actors_balance({ provider.id() }, SectionId(220));
    TEST_REQUIRE(credited.at({ provider.id(), ActorId { } }) == BigNumberFloat::create("0.0000001").value());
    const auto rewound = node->dag()->calculate_actors_balance({ provider.id() }, SectionId(200));
    const auto old     = rewound.find({ provider.id(), ActorId { } });
    TEST_REQUIRE(old == rewound.end() || old->second == 0);
    TEST_REQUIRE(std::filesystem::remove("consensus/mining-state.msgpack"));
    ConsensusStateTestFixture::forget(service);
    TEST_REQUIRE_EQ(mining_state_root(ConsensusStateTestFixture::parent(service, parent).value()),
                    mining_state_root(before));
    const auto saved = FileIo::read_all("consensus/mining-state.msgpack").value();
    TEST_REQUIRE(FileIo::write_atomic("consensus/mining-state.msgpack", "corrupt").has_value());
    ConsensusStateTestFixture::forget(service);
    TEST_REQUIRE(!ConsensusStateTestFixture::parent(service, parent).has_value());
    TEST_REQUIRE(FileIo::write_atomic("consensus/mining-state.msgpack", saved).has_value());
    TEST_REQUIRE_EQ(mining_state_root(ConsensusStateTestFixture::parent(service, parent).value()),
                    mining_state_root(before));
    const auto last_applied  = engine->finality_proof_for_section(220).value().value();
    const auto expected_root = mining_state_root(before);
    service.deactivate();
    const auto next_leader = view.leader(14, 0).validator_id;
    const auto next_key    = std::ranges::find_if(keys, [&](const auto& candidate) {
        return validator_id_for(candidate.public_key()) == next_leader;
    });
    TEST_REQUIRE(ShadowConsensus::write_identity("consensus", { .validator_id = next_leader, .key = *next_key })
                     .has_value());
    auto restarted = ShadowConsensus::load("consensus", network.id());
    TEST_REQUIRE(restarted.has_value());
    ConsensusStateTestFixture::attach(service,
                                      std::move(restarted.value()),
                                      AppliedCheckpoint { last_applied.finalized_proposal.header.height,
                                                          hash_header(last_applied.finalized_proposal.header) });
    TEST_REQUIRE_EQ(mining_state_root(ConsensusStateTestFixture::parent(service, parent).value()), expected_root);
    // A repeated registration is signed and structurally valid, but must not poison the leader's queue.
    const auto repeated          = request(IntentOperation::StorageRegister, dataset, 3, 261, 14);
    const auto repeated_envelope = intent_from_transaction(repeated).value();
    const auto submitted         = service.submit_intent(repeated_envelope);
    TEST_REQUIRE(submitted.has_value());
    const auto receipt = service.intent_receipt(submitted.value());
    TEST_REQUIRE(receipt.has_value() && receipt.value().has_value()
                 && receipt.value().value().status == IntentStatus::Rejected);
    TEST_REQUIRE(service.ready_intents(10, 1024 * 1024).empty());
    TEST_REQUIRE(std::filesystem::remove(alias_path.native()));
    node->dfs()->notify_local_removed(provider.id(), alias_row.file_id);
    node->data_mining_manager()->consensus_progress();
    const auto withdrawal = service.ready_intents(10, 1024 * 1024);
    TEST_REQUIRE(withdrawal.size() == 1
                 && withdrawal.front().intent.operation == IntentOperation::StorageUnregister);
    node->data_mining_manager()->set_enabled(false);
    // Local transfers share the account with pending proof work and must reserve distinct nonces.
    const auto local_transfer = TransactionIntentV2 { .network_id           = network.id(),
                                                      .sender               = provider.id(),
                                                      .receiver             = network.id(),
                                                      .amount               = "0.00000001",
                                                      .operation            = IntentOperation::Transfer,
                                                      .expires_after_height = 1000 };
    const auto transfer_a     = service.submit_local_intent(local_transfer, "local-a", provider);
    const auto transfer_b     = service.submit_local_intent(local_transfer, "local-b", provider);
    TEST_REQUIRE(transfer_a.has_value() && transfer_b.has_value() && transfer_a.value() != transfer_b.value());
    const auto local_ready = service.ready_intents(10, 1024 * 1024);
    TEST_REQUIRE_EQ(local_ready.size(), std::size_t(3));
    TEST_REQUIRE_EQ(local_ready[1].intent.account_nonce, local_ready[0].intent.account_nonce + 1);
    TEST_REQUIRE_EQ(local_ready[2].intent.account_nonce, local_ready[1].intent.account_nonce + 1);
    ConsensusStateTestFixture::expire(service, hash_intent(local_ready.front().intent));
    TEST_REQUIRE(service.ready_intents(10, 1024 * 1024).empty());
    TEST_REQUIRE(service.repair_local_nonce_gap(provider).value());
    TEST_REQUIRE(!service.repair_local_nonce_gap(provider).value());
    const auto repaired = service.ready_intents(10, 1024 * 1024);
    TEST_REQUIRE_EQ(repaired.size(), std::size_t(3));
    TEST_REQUIRE(repaired.front().intent.operation == IntentOperation::Cancel);
    TEST_REQUIRE_EQ(hash_intent(repaired[1].intent), transfer_a.value());
    TEST_REQUIRE_EQ(hash_intent(repaired[2].intent), transfer_b.value());
    const auto cancel   = materialize_intent(repaired.front(), 261, 14, { }).value();
    const auto frontier = SectionId(260);
    TEST_REQUIRE(node->dag()->prove_transaction(cancel, { }, nullptr, &frontier)
                 == TransactionProveError::NoError);
    const auto prior_balances = balances;
    node->dag()->cache().process_transaction(cancel, balances);
    TEST_REQUIRE(balances == prior_balances);
    auto unsigned_cancel = cancel;
    unsigned_cancel.set_consensus_intent("", { });
    unsigned_cancel.update_hash();
    TEST_REQUIRE(node->dag()->prove_transaction(unsigned_cancel, { }, nullptr, &frontier)
                 != TransactionProveError::NoError);
    TEST_REQUIRE_EQ(service.finalized_mining_state().value().minted_units, 10);
    service.deactivate();
    TEST_REQUIRE(std::filesystem::remove("consensus/identity.msgpack"));
    {
        DbConnector database("consensus/safety.sqlite");
        TEST_REQUIRE(database.open());
        TEST_REQUIRE(database.query("DELETE FROM consensus_batches WHERE height > 11"));
    }
    // An observer can persist certificates before their unfinalized batches arrive.
    const auto observer_restart = service.activate(network.id());
    TEST_REQUIRE(observer_restart.has_value() && observer_restart.value());
    TEST_REQUIRE(service.active() && !service.voting());
    TEST_REQUIRE_EQ(service.finalized_mining_state().value().minted_units, 10);
    const auto waiting_nonce = ConsensusStateTestFixture::next_nonce(service, provider.id());
    TEST_REQUIRE(!waiting_nonce.has_value() && waiting_nonce.error() == ConsensusError::DataUnavailable);
    const auto waiting_transfer = service.submit_local_intent(local_transfer, "after-batch-sync", provider);
    TEST_REQUIRE(!waiting_transfer.has_value() && waiting_transfer.error() == ConsensusError::DataUnavailable);
    TEST_REQUIRE(service.ready_intents(10, 1024 * 1024).empty());
    ConsensusStateTestFixture::require_missing_batch_request(service, batches.at(13).header_hash);
    TEST_REQUIRE(ConsensusStateTestFixture::stage(service, batches.at(13)).has_value());
    ConsensusStateTestFixture::require_missing_batch_request(service, batches.at(12).header_hash);
    TEST_REQUIRE(ConsensusStateTestFixture::stage(service, batches.at(12)).has_value());
    const auto restored_nonce = ConsensusStateTestFixture::next_nonce(service, provider.id());
    TEST_REQUIRE(restored_nonce.has_value());
    TEST_REQUIRE_EQ(restored_nonce.value(), repaired.back().intent.account_nonce + 1);
    const auto restored_pending = service.ready_intents(10, 1024 * 1024);
    TEST_REQUIRE_EQ(restored_pending.size(), repaired.size());
    for (std::size_t index = 0; index < repaired.size(); ++index)
        TEST_REQUIRE_EQ(hash_intent(restored_pending[index].intent), hash_intent(repaired[index].intent));
    const auto resumed_transfer = service.submit_local_intent(local_transfer, "after-batch-sync", provider);
    TEST_REQUIRE(resumed_transfer.has_value());
    const auto resumed_pending = service.ready_intents(10, 1024 * 1024);
    TEST_REQUIRE_EQ(resumed_pending.size(), repaired.size() + 1);
    TEST_REQUIRE_EQ(resumed_pending.back().intent.account_nonce, restored_nonce.value());
    TEST_REQUIRE_EQ(hash_intent(resumed_pending.back().intent), resumed_transfer.value());
    const auto tip     = ConsensusStateTestFixture::highest(service);
    const auto peer_id = validator_id_for(keys[0].public_key());
    const auto peer    = view.find(peer_id)->node_identifier;
    auto       socket  = std::make_shared<RecoverySocket>(*node->network(), peer);
    {
        auto connections = *node->network()->connections();
        connections->insert(socket);
    }
    ConsensusStateTestFixture::authenticate(service, view, keys[0], peer);
    TimeoutVote stale { .network_id = network.id(),
                        .epoch      = 1,
                        .height     = tip.height,
                        .highest_certificate_hash =
                            hash_certificate(ConsensusStateTestFixture::certified_parent(service)),
                        .validator_id = peer_id };
    stale.signature = sign_payload(keys[0], timeout_vote_signing_payload(stale)).value();
    service.receive_timeout_vote(stale, peer);
    TEST_REQUIRE_EQ(socket->certificates.load(), 1U);
    TEST_REQUIRE_EQ(socket->certified_height.load(), tip.height);
    TimeoutVote future              = stale;
    future.height                   = tip.height + 2;
    future.highest_certificate_hash = std::string(64, 'f');
    future.signature                = sign_payload(keys[0], timeout_vote_signing_payload(future)).value();
    auto invalid                    = future;
    invalid.signature[0]            = invalid.signature[0] == 'A' ? 'B' : 'A';
    ConsensusStateTestFixture::reset_sync_timer(service);
    service.receive_timeout_vote(invalid, peer);
    TEST_REQUIRE_EQ(socket->syncs.load(), 0U);
    service.receive_timeout_vote(future, peer);
    TEST_REQUIRE_EQ(socket->syncs.load(), 1U);
    TEST_REQUIRE_EQ(socket->sync_height.load(), tip.height - 2);
    QuorumCertificate unknown { .network_id    = network.id(),
                                .epoch         = 1,
                                .height        = tip.height + 1,
                                .header_hash   = std::string(64, 'e'),
                                .signer_bitmap = { 0x1f } };
    for (std::size_t index = 0; index < 5; ++index) {
        const auto signer = view.active()[index].validator_id;
        const auto key    = std::ranges::find_if(keys, [&](const auto& candidate) {
            return validator_id_for(candidate.public_key()) == signer;
        });
        Vote       vote { .network_id   = network.id(),
                          .epoch        = 1,
                          .height       = unknown.height,
                          .header_hash  = unknown.header_hash,
                          .validator_id = signer };
        unknown.signatures.push_back(sign_payload(*key, vote_signing_payload(vote)).value());
    }
    auto invalid_certificate                       = unknown;
    invalid_certificate.signatures.front().front() = '?';
    ConsensusStateTestFixture::reset_sync_timer(service);
    service.receive_certificate(invalid_certificate, peer);
    TEST_REQUIRE_EQ(socket->syncs.load(), 1U);
    service.receive_certificate(unknown, peer);
    TEST_REQUIRE_EQ(socket->syncs.load(), 2U);
    TEST_REQUIRE_EQ(ConsensusStateTestFixture::highest(service).header_hash, tip.header_hash);
    {
        auto connections = *node->network()->connections();
        connections->erase(socket);
    }
    service.deactivate();
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
}
