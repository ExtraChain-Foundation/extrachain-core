#include "consensus/mining_request.h"

#include "utils/exc_utils.h"
#include "utils/serialization.h"
#include "utils/msgpack_limits.h"

namespace ExtraChain::Consensus {
    namespace {
        bool dataset_identity(std::string_view value) {
            return value.size() == 64 && std::ranges::all_of(value, [](char c) {
                       return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                   });
        }
        template <class T>
        std::expected<T, ConsensusError> decode(std::string_view bytes) {
            const auto value = MessagePack::deserialize<T>(std::string(bytes));
            if (!value.has_value() || MessagePack::serialize(value.value()) != bytes)
                return std::unexpected(ConsensusError::InvalidIntent);
            return value.value();
        }
    } // namespace

    bool is_mining_operation(IntentOperation operation) {
        return operation == IntentOperation::StorageRegister || operation == IntentOperation::StorageUnregister
               || operation == IntentOperation::StorageProof;
    }

    std::expected<MiningRequest, ConsensusError> decode_mining_request(const IntentEnvelope& envelope) {
        const auto& intent = envelope.intent;
        if (!is_mining_operation(intent.operation) || intent.network_id.is_zero() || intent.sender.is_zero()
            || intent.receiver != intent.network_id || !intent.token.is_zero() || intent.amount != "0"
            || envelope.metadata.size() > 256 * 1024)
            return std::unexpected(ConsensusError::InvalidIntent);
        const auto bytes = Utils::from_base64(envelope.metadata);
        if (!bytes.has_value() || !MessagePack::has_bounded_structure(bytes.value(), 8192, 1024, 16))
            return std::unexpected(ConsensusError::InvalidIntent);
        if (intent.operation == IntentOperation::StorageRegister) {
            const auto dataset = decode<StorageDataset>(bytes.value());
            if (!dataset.has_value() || !storage_dataset_id(intent.network_id, dataset.value()).has_value())
                return std::unexpected(ConsensusError::InvalidIntent);
            return MiningRequest(dataset.value());
        }
        if (intent.operation == IntentOperation::StorageUnregister) {
            const auto identity = decode<std::string>(bytes.value());
            if (!identity.has_value() || !dataset_identity(identity.value()))
                return std::unexpected(ConsensusError::InvalidIntent);
            return MiningRequest(identity.value());
        }
        const auto submission = decode<MiningProofSubmission>(bytes.value());
        if (!submission.has_value() || !dataset_identity(submission.value().dataset_id)
            || !mining_epoch_schedule(submission.value().epoch).has_value()
            || submission.value().proof.samples.empty()
            || submission.value().proof.samples.size() > StorageChallengeSamples)
            return std::unexpected(ConsensusError::InvalidIntent);
        for (const auto& sample : submission.value().proof.samples)
            if (sample.bytes.empty() || sample.bytes.size() > StorageChunkBytes || sample.path.siblings.size() > 32
                || !std::ranges::all_of(sample.path.siblings, dataset_identity))
                return std::unexpected(ConsensusError::InvalidIntent);
        return MiningRequest(submission.value());
    }

    std::expected<void, ConsensusError> apply_mining_request(MiningState& state, const IntentEnvelope& envelope) {
        if (envelope.intent.network_id != state.network)
            return std::unexpected(ConsensusError::InvalidNetwork);
        const auto request = decode_mining_request(envelope);
        if (!request.has_value())
            return std::unexpected(request.error());
        if (const auto* dataset = std::get_if<StorageDataset>(&request.value()))
            return register_storage_provider(state, envelope.intent.sender, *dataset);
        if (const auto* identity = std::get_if<std::string>(&request.value()))
            return unregister_storage_provider(state, envelope.intent.sender, *identity);
        const auto& submission = std::get<MiningProofSubmission>(request.value());
        return submit_mining_proof(state,
                                   submission.epoch,
                                   envelope.intent.sender,
                                   submission.dataset_id,
                                   submission.proof);
    }
} // namespace ExtraChain::Consensus
