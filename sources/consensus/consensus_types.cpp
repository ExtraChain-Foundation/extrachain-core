/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include "consensus/consensus_types.h"

#include <algorithm>
#include <tuple>

#include "core/byte_array.h"
#include "utils/exc_utils.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

namespace ExtraChain::Consensus {
    namespace {
        constexpr std::string_view ValidatorIdDomain        = "EXC_CONSENSUS_VALIDATOR_ID_V1";
        constexpr std::string_view ValidatorDomain          = "EXC_CONSENSUS_VALIDATOR_BINDING_V1";
        constexpr std::string_view ValidatorSetDomain       = "EXC_CONSENSUS_VALIDATOR_SET_V1";
        constexpr std::string_view HeaderDomain             = "EXC_CONSENSUS_HEADER_V1";
        constexpr std::string_view BatchDomain              = "EXC_SHADOW_BATCH_V1";
        constexpr std::string_view TransactionLeafDomain    = "EXC_SHADOW_TRANSACTION_LEAF_V2";
        constexpr std::string_view TransactionNodeDomain    = "EXC_SHADOW_TRANSACTION_NODE_V2";
        constexpr std::string_view TransactionEmptyDomain   = "EXC_SHADOW_TRANSACTION_EMPTY_V2";
        constexpr std::string_view DataRootDomain           = "EXC_SHADOW_DATA_ROOT_V1";
        constexpr std::string_view ProposalDomain           = "EXC_CONSENSUS_PROPOSAL_V1";
        constexpr std::string_view VoteDomain               = "EXC_CONSENSUS_VOTE_V1";
        constexpr std::string_view TimeoutVoteDomain        = "EXC_SHADOW_TIMEOUT_VOTE_V1";
        constexpr std::string_view CertificateDomain        = "EXC_CONSENSUS_CERTIFICATE_V1";
        constexpr std::string_view TimeoutCertificateDomain = "EXC_SHADOW_TIMEOUT_CERTIFICATE_V1";
        constexpr std::string_view ChallengeDomain          = "EXC_CONSENSUS_AUTH_CHALLENGE_V1";
        constexpr std::string_view AuthResponseDomain       = "EXC_CONSENSUS_AUTH_RESPONSE_V1";

        template <typename T>
        std::string domain_hash(std::string_view domain, const T& value) {
            return Utils::calculate_hash(std::string(domain) + MessagePack::serialize(value),
                                         Utils::HashAlgorithm::Blake3);
        }

        template <typename T>
        std::string domain_payload(std::string_view domain, const T& value) {
            return std::string(domain) + MessagePack::serialize(value);
        }
    } // namespace

    std::string validator_binding_payload(const ValidatorRecord& record) {
        return domain_payload(ValidatorDomain,
                              std::tuple { record.protocol_version,
                                           record.network_id,
                                           record.epoch,
                                           record.validator_id,
                                           record.actor_id,
                                           record.actor_public_key,
                                           record.consensus_public_key,
                                           record.node_identifier,
                                           record.valid_from,
                                           record.valid_until,
                                           record.status });
    }

    std::string hash_validator_record(const ValidatorRecord& record) {
        return Utils::calculate_hash(validator_binding_payload(record), Utils::HashAlgorithm::Blake3);
    }

    std::string validator_set_signing_payload(const ValidatorSet& validators) {
        std::vector<std::string> record_hashes;
        record_hashes.reserve(validators.validators.size());
        for (const auto& validator : validators.validators) {
            record_hashes.push_back(hash_validator_record(validator));
        }
        std::ranges::sort(record_hashes);
        return domain_payload(ValidatorSetDomain,
                              std::tuple { validators.protocol_version,
                                           validators.network_id,
                                           validators.epoch,
                                           record_hashes,
                                           validators.governance_public_key });
    }

    std::string hash_validator_set(const ValidatorSet& validators) {
        return Utils::calculate_hash(validator_set_signing_payload(validators), Utils::HashAlgorithm::Blake3);
    }

    std::string hash_header(const ConsensusHeader& header) {
        return domain_hash(HeaderDomain, header);
    }

    std::string hash_batch_manifest(const SectionBatchManifest& manifest) {
        return domain_hash(BatchDomain, manifest);
    }

    std::string calculate_transaction_root(const std::vector<std::string>& hashes) {
        if (hashes.empty()) {
            return Utils::calculate_hash(std::string(TransactionEmptyDomain), Utils::HashAlgorithm::Blake3);
        }
        std::vector<std::string> level;
        level.reserve(hashes.size());
        for (const auto& hash : hashes) {
            level.push_back(
                Utils::calculate_hash(std::string(TransactionLeafDomain) + hash, Utils::HashAlgorithm::Blake3));
        }
        while (level.size() > 1) {
            std::vector<std::string> next;
            next.reserve((level.size() + 1) / 2);
            for (std::size_t index = 0; index < level.size(); index += 2) {
                const auto& right = index + 1 < level.size() ? level[index + 1] : level[index];
                next.push_back(Utils::calculate_hash(std::string(TransactionNodeDomain) + level[index] + right,
                                                     Utils::HashAlgorithm::Blake3));
            }
            level = std::move(next);
        }
        return level.front();
    }

    std::string calculate_data_root(const std::vector<std::pair<std::uint64_t, std::string>>& sections) {
        std::vector<std::pair<std::uint64_t, std::string>> hashes;
        hashes.reserve(sections.size());
        for (const auto& [section, bytes] : sections) {
            hashes.emplace_back(section, Utils::calculate_hash(bytes, Utils::HashAlgorithm::Blake3));
        }
        return domain_hash(DataRootDomain, hashes);
    }

    std::string hash_certificate(const QuorumCertificate& certificate) {
        return domain_hash(CertificateDomain, certificate);
    }

    std::string hash_timeout_certificate(const TimeoutCertificate& certificate) {
        return domain_hash(TimeoutCertificateDomain, certificate);
    }

    std::string proposal_signing_payload(const Proposal& proposal) {
        return domain_payload(ProposalDomain,
                              std::tuple { hash_header(proposal.header),
                                           hash_batch_manifest(proposal.batch),
                                           hash_certificate(proposal.parent_certificate),
                                           proposal.timeout_certificate.has_value()
                                               ? hash_timeout_certificate(proposal.timeout_certificate.value())
                                               : std::string {},
                                           proposal.proposer_id });
    }

    std::string vote_signing_payload(const Vote& vote) {
        return domain_payload(VoteDomain,
                              std::tuple { vote.protocol_version,
                                           vote.network_id,
                                           vote.epoch,
                                           vote.height,
                                           vote.round,
                                           vote.phase,
                                           vote.header_hash,
                                           vote.validator_id });
    }

    std::string timeout_vote_signing_payload(const TimeoutVote& vote) {
        return domain_payload(TimeoutVoteDomain,
                              std::tuple { vote.protocol_version,
                                           vote.network_id,
                                           vote.epoch,
                                           vote.height,
                                           vote.round,
                                           vote.highest_certificate_hash,
                                           vote.validator_id });
    }

    std::string hash_authentication_challenge(const AuthenticationChallenge& challenge) {
        return domain_hash(ChallengeDomain, challenge);
    }

    std::string authentication_response_signing_payload(const AuthenticationResponse& response) {
        return domain_payload(AuthResponseDomain,
                              std::tuple { response.protocol_version,
                                           response.network_id,
                                           response.epoch,
                                           response.challenge_hash,
                                           response.validator_id,
                                           response.node_identifier });
    }

    std::string validator_id_for(const PublicKey& public_key) {
        return Utils::calculate_hash(std::string(ValidatorIdDomain) + ByteArray(public_key).toString(),
                                     Utils::HashAlgorithm::Blake3);
    }

    std::expected<std::string, ConsensusError> sign_payload(const KeyPrivate& key, const std::string& payload) {
        const auto signature = key.sign(ByteArray(payload).toBytes());
        if (!signature.has_value()) {
            return std::unexpected(ConsensusError::InvalidSignature);
        }
        return Utils::to_base64(signature.value());
    }

    bool verify_payload(const std::string& public_key, const std::string& payload, const std::string& signature) {
        const auto public_key_bytes = ByteArray::fromBase64(public_key);
        const auto signature_bytes  = ByteArray::fromBase64(signature);
        if (!public_key_bytes.has_value() || public_key_bytes.value().size() != crypto_sign_PUBLICKEYBYTES
            || !signature_bytes.has_value() || signature_bytes.value().size() != crypto_sign_BYTES) {
            return false;
        }
        const KeyPublic key(public_key_bytes.value().toArray<crypto_sign_PUBLICKEYBYTES>());
        const auto      verified =
            key.verify(ByteArray(payload).toBytes(), signature_bytes.value().toArray<crypto_sign_BYTES>());
        return verified.has_value() && verified.value();
    }

} // namespace ExtraChain::Consensus
