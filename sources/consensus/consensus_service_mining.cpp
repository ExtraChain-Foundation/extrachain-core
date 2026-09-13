#include "consensus/consensus_service.h"

#include <fstream>

#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "utils/file_io.h"
#include "utils/msgpack_limits.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::size_t      MaximumMiningSnapshotBytes = 16 * 1024 * 1024;
        constexpr std::size_t      MaximumCachedMiningStates  = 8;
        constexpr std::string_view MiningSnapshotFile         = "mining-state.msgpack";

        bool has_mining_records(const SectionBatchData& batch) {
            for (const auto& [_, bytes] : batch.sections) {
                const auto section = Json::deserialize<Section>(bytes);
                if (!section.has_value())
                    return true;
                for (const auto& transaction : section.value().transactions)
                    if (is_mining_request(transaction.type())
                        || transaction.type() == TransactionType::MiningSettlement)
                        return true;
            }
            return false;
        }
    } // namespace

    std::expected<const LightClientVerifier*, ConsensusError> ConsensusService::mining_verifier() const {
        std::unique_ptr<ShadowConsensus> restored;
        const auto*                      source = consensus_.get();
        if (source == nullptr) {
            if (mining_verifier_.has_value())
                return &mining_verifier_.value();
            auto loaded = ShadowConsensus::load(directory_, node_.network_id());
            if (!loaded.has_value())
                return std::unexpected(loaded.error());
            restored = std::move(loaded.value());
            source   = restored.get();
        }
        const auto& validators = source->engine().validators();
        if (mining_verifier_.has_value()
            && mining_verifier_.value().active_validators().hash() == validators.hash())
            return &mining_verifier_.value();
        auto verifier = source->trust_anchor().has_value()
                            ? LightClientVerifier::bootstrap(source->trust_anchor().value())
                            : LightClientVerifier::create(validators.document());
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        const auto history = source->epoch_starts();
        if (!history.empty() && !source->trust_anchor().has_value())
            return std::unexpected(ConsensusError::InvalidGovernance);
        for (const auto& start : history) {
            const BootstrapHistoryPageV1 page {
                .network_id        = validators.document().network_id,
                .trust_anchor_hash = hash_trust_anchor(source->trust_anchor().value()),
                .after_epoch       = verifier.value().active_validators().document().epoch,
                .entries           = { start },
            };
            const auto applied = verifier.value().apply_history_page(page);
            if (!applied.has_value())
                return std::unexpected(applied.error());
        }
        if (verifier.value().active_validators().hash() != validators.hash())
            return std::unexpected(ConsensusError::InvalidEpoch);
        mining_verifier_ = std::move(verifier.value());
        return &mining_verifier_.value();
    }

    bool ConsensusService::native_mining_enabled() const {
        std::lock_guard lock(mutex_);
        return consensus_ && consensus_->configuration().mode == ShadowMode::Finality
               && consensus_->mining_policy().has_value();
    }

    std::expected<MiningState, ConsensusError> ConsensusService::finalized_mining_state() const {
        std::lock_guard lock(mutex_);
        if (!native_mining_enabled())
            return std::unexpected(ConsensusError::NotReady);
        const auto initialized = initialize_mining_state();
        if (!initialized.has_value())
            return std::unexpected(initialized.error());
        return finalized_mining_.value().state;
    }

    std::expected<MiningState, ConsensusError> ConsensusService::mining_work_state() const {
        std::lock_guard lock(mutex_);
        if (!consensus_ || consensus_->configuration().mode != ShadowMode::Finality
            || !consensus_->mining_policy().has_value()
            || !consensus_->engine().safety_state().highest_certificate.has_value()
            || consensus_->engine().safety_state().highest_certificate.value().phase == Phase::Genesis)
            return std::unexpected(ConsensusError::NotReady);
        auto       state    = mining_state_for(consensus_->engine().safety_state().highest_certificate.value());
        const auto verifier = mining_verifier();
        if (!state.has_value() || !verifier.has_value())
            return std::unexpected(state.has_value() ? verifier.error() : state.error());
        if (state.value().section == UINT64_MAX)
            return std::unexpected(ConsensusError::InvalidHeight);
        const auto section = state.value().section + 1;
        const auto budget =
            mining_policy_budget(consensus_->mining_policy().value(), (section - 1) / ShadowSectionInterval);
        if (!budget.has_value())
            return std::unexpected(budget.error());
        const auto advanced = advance_mining_state(
            state.value(),
            section,
            budget.value(),
            [this](auto target) {
                return mining_finality(target);
            },
            *verifier.value());
        if (!advanced.has_value())
            return std::unexpected(advanced.error());
        return state;
    }

    std::expected<std::string, ConsensusError> ConsensusService::submit_mining_request(
        IntentOperation          operation,
        std::string              metadata,
        const Actor<KeyPrivate>& provider) {
        std::lock_guard lock(mutex_);
        auto            work = mining_work_state();
        if (!work.has_value())
            return std::unexpected(work.error());
        const auto nonce    = next_local_nonce(provider.id());
        const auto height   = intent_height();
        const auto duration = operation == IntentOperation::StorageProof ? 1ULL : 64ULL;
        if (!nonce.has_value() || height > UINT64_MAX - duration)
            return std::unexpected(ConsensusError::InvalidNonce);
        const auto intent = make_intent(TransactionIntentV2 { .network_id           = work.value().network,
                                                              .sender               = provider.id(),
                                                              .receiver             = work.value().network,
                                                              .amount               = "0",
                                                              .operation            = operation,
                                                              .account_nonce        = nonce.value(),
                                                              .valid_after_height   = height,
                                                              .expires_after_height = height + duration },
                                        metadata,
                                        provider);
        if (!intent.has_value())
            return std::unexpected(intent.error());
        IntentEnvelope envelope { intent.value(), std::move(metadata) };
        const auto     applicable = apply_mining_request(work.value(), envelope);
        if (!applicable.has_value())
            return std::unexpected(applicable.error());
        return accept_intent(envelope, true);
    }

    bool ConsensusService::verify_mining_transaction(const Transaction& transaction) const {
        std::lock_guard lock(mutex_);
        const auto      network =
            consensus_ ? consensus_->engine().validators().document().network_id : node_.network_id();
        if (transaction.type() == TransactionType::MiningSettlement) {
            const auto verifier = mining_verifier();
            return verifier.has_value()
                   && verify_mining_settlement_transaction(transaction, network, *verifier.value()).has_value();
        }
        if (!is_mining_request(transaction.type()))
            return false;
        const auto intent = intent_from_transaction(transaction);
        return intent.has_value() && intent.value().intent.network_id == network
               && decode_mining_request(intent.value()).has_value();
    }

    std::expected<FinalityProof, ConsensusError> ConsensusService::mining_finality(std::uint64_t section) const {
        const auto verifier = mining_verifier();
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        auto proof = [&]() -> std::expected<std::optional<FinalityProof>, ConsensusError> {
            const auto& bootstrap = consensus_->engine().epoch_bootstrap();
            if (!bootstrap.has_value() || section >= bootstrap.value().first_dag_section)
                return consensus_->engine().finality_proof_for_section(section);
            auto path = directory_ / "safety.sqlite";
            for (const auto& start : consensus_->epoch_starts()) {
                if (start.bootstrap.first_dag_section > section)
                    break;
                path = directory_ / fmt::format("safety-epoch-{}.sqlite", start.validators.epoch);
            }
            std::error_code error;
            if (!std::filesystem::is_regular_file(path, error) || error)
                return std::unexpected(ConsensusError::StorageUnavailable);
            SafetyStore store(path);
            const auto  opened = store.open();
            if (!opened.has_value())
                return std::unexpected(opened.error());
            return store.load_finality_proof_for_section(section);
        }();
        if (!proof.has_value())
            return std::unexpected(proof.error());
        if (!proof.value().has_value())
            return std::unexpected(ConsensusError::DataUnavailable);
        if (proof.value().value().finalized_proposal.header.dag_section != section
            || !verifier.value()->verify_finality_proof(proof.value().value()))
            return std::unexpected(ConsensusError::InvalidProof);
        return std::move(proof.value().value());
    }

    std::expected<void, ConsensusError> ConsensusService::initialize_mining_state() const {
        if (finalized_mining_.has_value())
            return { };
        if (!consensus_)
            return std::unexpected(ConsensusError::InvalidEpoch);
        const auto verifier = mining_verifier();
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        const auto& network  = consensus_->engine().validators().document().network_id;
        const auto  boundary = consensus_->configuration().activation_dag_section;
        const auto  initial  = configure_mining_state(network, boundary, consensus_->mining_policy());
        if (!initial.has_value())
            return std::unexpected(initial.error());
        MiningSnapshot  snapshot { { }, initial.value() };
        std::uint64_t   snapshot_height = 0;
        const auto      path            = directory_ / MiningSnapshotFile;
        std::error_code error;
        const bool      exists = std::filesystem::exists(path, error);
        if (error)
            return std::unexpected(ConsensusError::StorageFailure);
        if (exists) {
            // Read at most the fixed budget, including when the file changes during the read.
            std::ifstream input(path, std::ios::binary);
            if (!input)
                return std::unexpected(ConsensusError::StorageUnavailable);
            std::string bytes(MaximumMiningSnapshotBytes + 1, '\0');
            input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
            bytes.resize(static_cast<std::size_t>(input.gcount()));
            if (input.bad() || bytes.size() > MaximumMiningSnapshotBytes
                || !MessagePack::has_bounded_structure(bytes, 1048576, MaximumMiningRegistrations, 32))
                return std::unexpected(ConsensusError::StorageFailure);
            const auto decoded = MessagePack::deserialize<MiningSnapshot>(bytes);
            if (!decoded.has_value() || MessagePack::serialize(decoded.value()) != bytes)
                return std::unexpected(ConsensusError::StorageFailure);
            snapshot        = decoded.value();
            const auto root = mining_state_root(snapshot.state);
            if (snapshot.state.network != network || snapshot.state.section < boundary || root.empty()
                || snapshot.state.emission_policy_hash != mining_policy_hash(consensus_->mining_policy()))
                return std::unexpected(ConsensusError::InvalidRoot);
            const auto proof = mining_finality(snapshot.state.section);
            if (!proof.has_value() || snapshot.header_hash != hash_header(proof.value().finalized_proposal.header)
                || proof.value().finalized_proposal.state.mining_state_root != root)
                return std::unexpected(ConsensusError::InvalidProof);
            const auto height = proof.value().finalized_proposal.header.height;
            snapshot_height   = height;
            const auto applied =
                applied_checkpoint_.has_value()
                    ? applied_checkpoint_.value().height
                    : std::max<std::uint64_t>(1, consensus_->configuration().activation_height) - 1;
            if (height > applied && height - applied > 1)
                return std::unexpected(ConsensusError::InvalidHeight);
            if (applied_checkpoint_.has_value() && height == applied
                && snapshot.header_hash != applied_checkpoint_.value().header_hash)
                return std::unexpected(ConsensusError::InvalidProof);
        }
        // Recover a missing or older derived snapshot once from authenticated installed history.
        // This never resets counters when an applied checkpoint already exists.
        while (applied_checkpoint_.has_value() && snapshot_height < applied_checkpoint_.value().height) {
            auto section = snapshot.state.section;
            if (!snapshot.header_hash.empty()) {
                if (section > UINT64_MAX - ShadowSectionInterval)
                    return std::unexpected(ConsensusError::InvalidHeight);
                section += ShadowSectionInterval;
            }
            const auto proof = mining_finality(section);
            if (!proof.has_value())
                return std::unexpected(proof.error());
            const auto& proposal = proof.value().finalized_proposal;
            if (proposal.header.height > applied_checkpoint_.value().height)
                break;
            if (section != boundary || !snapshot.header_hash.empty()) {
                const auto batch = node_.dag()->build_shadow_batch(SectionId(proposal.batch.first_section),
                                                                   SectionId(proposal.batch.last_section),
                                                                   hash_header(proposal.header));
                if (!batch.has_value()
                    || hash_batch_manifest(batch.value().manifest) != proposal.header.batch_root)
                    return std::unexpected(ConsensusError::DataUnavailable);
                const auto replayed = replay_mining_batch(
                    snapshot.state,
                    consensus_->mining_policy(),
                    batch.value(),
                    [this](auto target) {
                        return mining_finality(target);
                    },
                    *verifier.value());
                if (!replayed.has_value())
                    return std::unexpected(replayed.error());
                snapshot.state = replayed.value();
            }
            if (proposal.state.mining_state_root != mining_state_root(snapshot.state))
                return std::unexpected(ConsensusError::InvalidRoot);
            snapshot.header_hash = hash_header(proposal.header);
            snapshot_height      = proposal.header.height;
            if (proposal.header.height == applied_checkpoint_.value().height) {
                if (snapshot.header_hash != applied_checkpoint_.value().header_hash)
                    return std::unexpected(ConsensusError::InvalidProof);
                break;
            }
        }
        if (!snapshot.header_hash.empty()) {
            const auto bytes = MessagePack::serialize(snapshot);
            if (bytes.size() > MaximumMiningSnapshotBytes || !FileIo::write_atomic(path, bytes).has_value())
                return std::unexpected(ConsensusError::StorageFailure);
        }
        finalized_mining_ = std::move(snapshot);
        return { };
    }

    std::expected<MiningState, ConsensusError> ConsensusService::mining_state_for(const QuorumCertificate& parent,
                                                                                  std::size_t depth) const {
        const auto initialized = initialize_mining_state();
        if (!initialized.has_value())
            return std::unexpected(initialized.error());
        if (parent.phase == Phase::Genesis) {
            const auto& bootstrap = consensus_->engine().epoch_bootstrap();
            const auto  boundary  = bootstrap.has_value() ? bootstrap.value().first_dag_section - 1
                                                          : consensus_->configuration().activation_dag_section;
            if (finalized_mining_.value().state.section != boundary)
                return std::unexpected(ConsensusError::InvalidParent);
            return finalized_mining_.value().state;
        }
        if (parent.header_hash == finalized_mining_.value().header_hash)
            return finalized_mining_.value().state;
        const auto cached = staged_mining_.find(parent.header_hash);
        if (cached != staged_mining_.end())
            return cached->second;
        if (depth >= MaximumStagedAncestors)
            return std::unexpected(ConsensusError::InvalidParent);
        const auto proposal = consensus_->engine().proposal_for(parent.header_hash);
        const auto batch    = consensus_->engine().batch_for(parent.header_hash);
        if (!proposal.has_value() || !batch.has_value())
            return std::unexpected(ConsensusError::DataUnavailable);
        if (proposal.value().header.height != parent.height
            || batch.value().manifest.last_section < finalized_mining_.value().state.section)
            return std::unexpected(ConsensusError::InvalidParent);
        auto state = mining_state_for(proposal.value().parent_certificate, depth + 1);
        if (!state.has_value())
            return std::unexpected(state.error());
        const auto verifier = mining_verifier();
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        const bool activation =
            proposal.value().parent_certificate.phase == Phase::Genesis
            && !consensus_->engine().epoch_bootstrap().has_value()
            && batch.value().manifest.last_section == consensus_->configuration().activation_dag_section;
        if (activation && has_mining_records(batch.value()))
            return std::unexpected(ConsensusError::InvalidIntent);
        auto projected = activation ? std::move(state)
                                    : replay_mining_batch(
                                          std::move(state.value()),
                                          consensus_->mining_policy(),
                                          batch.value(),
                                          [this](auto section) {
                                              return mining_finality(section);
                                          },
                                          *verifier.value());
        if (!projected.has_value())
            return std::unexpected(projected.error());
        if (mining_state_root(projected.value()) != proposal.value().state.mining_state_root)
            return std::unexpected(ConsensusError::InvalidRoot);
        if (staged_mining_.size() >= MaximumCachedMiningStates)
            staged_mining_.erase(staged_mining_.begin());
        staged_mining_.insert_or_assign(parent.header_hash, projected.value());
        return projected;
    }

    std::expected<MiningState, ConsensusError> ConsensusService::project_mining_state(
        const SectionBatchData&  batch,
        const QuorumCertificate& parent) const {
        auto state = mining_state_for(parent);
        if (!state.has_value())
            return std::unexpected(state.error());
        if (parent.phase == Phase::Genesis && !consensus_->engine().epoch_bootstrap().has_value()
            && batch.manifest.last_section == consensus_->configuration().activation_dag_section) {
            if (has_mining_records(batch))
                return std::unexpected(ConsensusError::InvalidIntent);
            return state;
        }
        const auto verifier = mining_verifier();
        if (!verifier.has_value())
            return std::unexpected(verifier.error());
        return replay_mining_batch(
            std::move(state.value()),
            consensus_->mining_policy(),
            batch,
            [this](auto section) {
                return mining_finality(section);
            },
            *verifier.value());
    }

    std::expected<std::optional<Transaction>, ConsensusError> ConsensusService::next_mining_settlement(
        const MiningState& parent,
        std::uint64_t      first_section) const {
        for (const auto& [epoch, frozen] : parent.epochs) {
            const auto schedule = mining_epoch_schedule(epoch);
            if (!schedule.has_value())
                return std::unexpected(schedule.error());
            if (schedule.value().settlement_first_section != first_section)
                continue;
            const auto verifier = mining_verifier();
            const auto closure  = mining_finality(schedule.value().proof_last_section);
            if (!verifier.has_value() || !closure.has_value())
                return std::unexpected(ConsensusError::DataUnavailable);
            auto       settled = frozen;
            const auto valid   = settle_mining_epoch(settled, closure.value(), *verifier.value());
            if (!valid.has_value())
                return std::unexpected(valid.error());
            if (settled.rewards.empty())
                return std::optional<Transaction> { };
            if (!finalized_mining_.has_value()
                || finalized_mining_.value().state.section != schedule.value().proof_last_section)
                return std::unexpected(ConsensusError::DataUnavailable);
            const auto witness = make_mining_epoch_witness(finalized_mining_.value().state, epoch);
            if (!witness.has_value())
                return std::unexpected(witness.error());
            const auto transaction = make_mining_settlement_transaction({ witness.value(), closure.value() });
            if (!transaction.has_value())
                return std::unexpected(transaction.error());
            return std::optional<Transaction> { transaction.value() };
        }
        return std::optional<Transaction> { };
    }

    std::expected<void, ConsensusError> ConsensusService::persist_mining_state(const FinalityProof&    proof,
                                                                               const SectionBatchData& batch) {
        const auto initialized = initialize_mining_state();
        if (!initialized.has_value())
            return std::unexpected(initialized.error());
        const auto& proposal = proof.finalized_proposal;
        const auto  header   = hash_header(proposal.header);
        if (finalized_mining_.value().header_hash == header)
            return { };
        auto state = project_mining_state(batch, proposal.parent_certificate);
        if (!state.has_value())
            return std::unexpected(state.error());
        if (mining_state_root(state.value()) != proposal.state.mining_state_root)
            return std::unexpected(ConsensusError::InvalidRoot);
        MiningSnapshot snapshot { header, std::move(state.value()) };
        const auto     bytes = MessagePack::serialize(snapshot);
        if (bytes.size() > MaximumMiningSnapshotBytes
            || !FileIo::write_atomic(directory_ / MiningSnapshotFile, bytes).has_value())
            return std::unexpected(ConsensusError::StorageFailure);
        finalized_mining_ = std::move(snapshot);
        staged_mining_.clear();
        return { };
    }
} // namespace ExtraChain::Consensus
