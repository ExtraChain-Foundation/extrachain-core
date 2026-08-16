/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#ifndef _WIN32
    #include <sys/stat.h>
#endif

#include "consensus/consensus_protocol.h"
#include "consensus/shadow_consensus.h"
#include "utils/exc_utils_base64.h"
#include "utils/file_io.h"
#include "utils/serialization.h"

namespace {
    using namespace ExtraChain::Consensus;

    template <typename Document>
    std::expected<Document, std::string> read_document(const std::filesystem::path& path) {
        const auto bytes = FileIo::read_all(path);
        if (!bytes.has_value()) {
            return std::unexpected("cannot read " + path.string());
        }
        const auto document = MessagePack::deserialize<Document>(bytes.value());
        if (!document.has_value()) {
            return std::unexpected("invalid MessagePack document " + path.string());
        }
        return document.value();
    }

    template <typename Document>
    std::expected<void, std::string> write_document(const std::filesystem::path& path, const Document& document) {
        const auto written = FileIo::write_atomic(path, MessagePack::serialize(document));
        if (!written.has_value()) {
            return std::unexpected("cannot write " + path.string());
        }
        return {};
    }

    std::expected<std::uint64_t, std::string> parse_number(std::string_view text) {
        std::uint64_t value  = 0;
        const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() || value == 0) {
            return std::unexpected("expected a positive integer");
        }
        return value;
    }

    bool protect_private_file(const std::filesystem::path& path) {
#ifndef _WIN32
        std::error_code error;
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     error);
        return !error;
#else
        (void)path;
        return true;
#endif
    }

    bool private_file_is_protected(const std::filesystem::path& path) {
#ifndef _WIN32
        std::error_code error;
        const auto      permissions = std::filesystem::status(path, error).permissions();
        if (error) {
            return false;
        }
        constexpr auto exposed = std::filesystem::perms::group_read | std::filesystem::perms::group_write
                                 | std::filesystem::perms::group_exec | std::filesystem::perms::others_read
                                 | std::filesystem::perms::others_write | std::filesystem::perms::others_exec;
        return (permissions & exposed) == std::filesystem::perms::none;
#else
        (void)path;
        return true;
#endif
    }

    bool valid_hash(std::string_view value) {
        return value.size() == 64 && std::ranges::all_of(value, [](unsigned char character) {
                   return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
               });
    }

    int fail(std::string_view message) {
        std::fprintf(stderr, "shadowctl: %.*s\n", static_cast<int>(message.size()), message.data());
        return 1;
    }

    void usage(const char* executable) {
        std::printf(
            "usage:\n"
            "  %s keygen <private-key.msgpack> <public-key.txt>\n"
            "  %s make-policy <network-id> <threshold> <policy.msgpack> <public-key.txt>...\n"
            "  %s request <policy.msgpack> <sequence> <action-hash> <request.msgpack>\n"
            "  %s sign <policy.msgpack> <request.msgpack> <expected-action-hash> "
            "<private-key.msgpack> <share.msgpack>\n"
            "  %s assemble <policy.msgpack> <request.msgpack> <authorization.msgpack> "
            "<share.msgpack>...\n"
            "  %s verify-authorization <policy.msgpack> <authorization.msgpack> <minimum-sequence>\n"
            "  %s verify-config <consensus-directory> <network-id>\n",
            executable,
            executable,
            executable,
            executable,
            executable,
            executable,
            executable);
    }
} // namespace

int main(int argc, char* argv[]) {
    using namespace ExtraChain::Consensus;

#ifndef _WIN32
    ::umask(0077);
#endif

    if (argc < 2) {
        usage(argv[0]);
        return 64;
    }
    const std::string command = argv[1];

    if (command == "keygen") {
        if (argc != 4) {
            usage(argv[0]);
            return 64;
        }
        const std::filesystem::path private_path = argv[2];
        const std::filesystem::path public_path  = argv[3];
        if (std::filesystem::exists(private_path) || std::filesystem::exists(public_path)) {
            return fail("key output already exists");
        }
        KeyPrivate key;
        key.generate_random();
        const auto private_written = write_document(private_path, key);
        if (!private_written.has_value() || !protect_private_file(private_path)) {
            return fail("cannot create protected private key");
        }
        const auto public_written = FileIo::write_atomic(public_path, Utils::to_base64(key.public_key()) + "\n");
        if (!public_written.has_value()) {
            return fail("cannot create public key");
        }
        std::printf("created independent Shadow signing key\n");
        return 0;
    }

    if (command == "make-policy") {
        if (argc != static_cast<int>(GovernanceSignerCount + 5)) {
            usage(argv[0]);
            return 64;
        }
        const auto network_id = ActorId::create(argv[2]);
        const auto threshold  = parse_number(argv[3]);
        if (!network_id.has_value() || network_id.value().is_zero()) {
            return fail("network ID is invalid");
        }
        if (!threshold.has_value() || threshold.value() > GovernanceSignerCount) {
            return fail("policy threshold is invalid");
        }
        std::vector<std::string> keys;
        keys.reserve(GovernanceSignerCount);
        for (int index = 5; index < argc; ++index) {
            const auto content = FileIo::read_all(argv[index]);
            if (!content.has_value()) {
                return fail("cannot read policy public key");
            }
            auto key = content.value();
            key.erase(std::remove_if(key.begin(),
                                     key.end(),
                                     [](unsigned char value) {
                                         return std::isspace(value) != 0;
                                     }),
                      key.end());
            keys.push_back(std::move(key));
        }
        const auto policy = make_multisig_policy(network_id.value(),
                                                 static_cast<std::uint8_t>(threshold.value()),
                                                 std::move(keys));
        if (!policy.has_value()) {
            return fail("policy public keys or threshold are invalid");
        }
        const auto written = write_document(argv[4], policy.value());
        if (!written.has_value()) {
            return fail(written.error());
        }
        std::printf("created policy threshold=%u signers=%zu network=%s\n",
                    policy.value().threshold,
                    policy.value().public_keys.size(),
                    policy.value().network_id.to_string().c_str());
        return 0;
    }

    if (command == "request") {
        if (argc != 6) {
            usage(argv[0]);
            return 64;
        }
        const auto policy   = read_document<MultisigPolicy>(argv[2]);
        const auto sequence = parse_number(argv[3]);
        if (!policy.has_value()) {
            return fail(policy.error());
        }
        if (!sequence.has_value()) {
            return fail(sequence.error());
        }
        if (!valid_hash(argv[4])) {
            return fail("action hash must contain 64 lowercase hexadecimal characters");
        }
        const auto request = make_authorization(policy.value(), sequence.value(), argv[4]);
        if (!request.has_value()) {
            return fail("authorization request is invalid");
        }
        const auto written = write_document(argv[5], request.value());
        if (!written.has_value()) {
            return fail(written.error());
        }
        std::printf("request network=%s sequence=%llu action_hash=%s\n",
                    request.value().network_id.to_string().c_str(),
                    static_cast<unsigned long long>(request.value().sequence),
                    request.value().action_hash.c_str());
        return 0;
    }

    if (command == "sign") {
        if (argc != 7) {
            usage(argv[0]);
            return 64;
        }
        const auto policy  = read_document<MultisigPolicy>(argv[2]);
        const auto request = read_document<GovernanceAuthorization>(argv[3]);
        const auto key     = read_document<KeyPrivate>(argv[5]);
        if (!policy.has_value()) {
            return fail(policy.error());
        }
        if (!request.has_value()) {
            return fail(request.error());
        }
        if (!key.has_value()) {
            return fail(key.error());
        }
        if (!private_file_is_protected(argv[5])) {
            return fail("private key permissions allow access outside its owner");
        }
        if (!valid_hash(argv[4])) {
            return fail("expected action hash must contain 64 lowercase hexadecimal characters");
        }
        if (request.value().action_hash != argv[4]) {
            return fail("request action hash does not match the independently checked hash");
        }
        const auto share = sign_authorization(policy.value(), request.value(), key.value());
        if (!share.has_value()) {
            return fail("signing key is outside the policy or the request is invalid");
        }
        const auto written = write_document(argv[6], share.value());
        if (!written.has_value()) {
            return fail(written.error());
        }
        std::printf("signed sequence=%llu action_hash=%s signer_index=%u\n",
                    static_cast<unsigned long long>(request.value().sequence),
                    request.value().action_hash.c_str(),
                    share.value().signer_index);
        return 0;
    }

    if (command == "assemble") {
        if (argc < 7) {
            usage(argv[0]);
            return 64;
        }
        const auto policy  = read_document<MultisigPolicy>(argv[2]);
        const auto request = read_document<GovernanceAuthorization>(argv[3]);
        if (!policy.has_value()) {
            return fail(policy.error());
        }
        if (!request.has_value()) {
            return fail(request.error());
        }
        std::vector<IndexedSignature> shares;
        shares.reserve(static_cast<std::size_t>(argc - 5));
        for (int index = 5; index < argc; ++index) {
            const auto share = read_document<IndexedSignature>(argv[index]);
            if (!share.has_value()) {
                return fail(share.error());
            }
            shares.push_back(share.value());
        }
        const auto authorization = assemble_authorization(policy.value(), request.value(), std::move(shares));
        if (!authorization.has_value()) {
            return fail("shares do not form the required authorization threshold");
        }
        const auto written = write_document(argv[4], authorization.value());
        if (!written.has_value()) {
            return fail(written.error());
        }
        std::printf("assembled signatures=%zu sequence=%llu action_hash=%s\n",
                    authorization.value().signatures.size(),
                    static_cast<unsigned long long>(authorization.value().sequence),
                    authorization.value().action_hash.c_str());
        return 0;
    }

    if (command == "verify-authorization") {
        if (argc != 5) {
            usage(argv[0]);
            return 64;
        }
        const auto policy        = read_document<MultisigPolicy>(argv[2]);
        const auto authorization = read_document<GovernanceAuthorization>(argv[3]);
        const auto sequence      = parse_number(argv[4]);
        if (!policy.has_value()) {
            return fail(policy.error());
        }
        if (!authorization.has_value()) {
            return fail(authorization.error());
        }
        if (!sequence.has_value()) {
            return fail(sequence.error());
        }
        if (!verify_authorization(policy.value(), authorization.value(), sequence.value())) {
            return fail("authorization verification failed");
        }
        std::printf("valid signatures=%zu sequence=%llu action_hash=%s\n",
                    authorization.value().signatures.size(),
                    static_cast<unsigned long long>(authorization.value().sequence),
                    authorization.value().action_hash.c_str());
        return 0;
    }

    if (command == "verify-config") {
        if (argc != 4) {
            usage(argv[0]);
            return 64;
        }
        const auto network_id = ActorId::create(argv[3]);
        if (!network_id.has_value() || network_id.value().is_zero()) {
            return fail("network ID is invalid");
        }
        const auto consensus = ShadowConsensus::load(argv[2], network_id.value());
        if (!consensus.has_value()) {
            return fail("Shadow configuration verification failed");
        }
        const auto& configuration = consensus.value()->configuration();
        std::printf(
            "valid mode=%s epoch=%llu validators=%zu activation_height=%llu "
            "activation_section=%llu timeout_ms=%llu maximum_timeout_ms=%llu batch_bytes=%llu\n",
            configuration.mode == ShadowMode::Finality ? "finality" : "observe",
            static_cast<unsigned long long>(consensus.value()->engine().validators().document().epoch),
            consensus.value()->engine().validators().active().size(),
            static_cast<unsigned long long>(configuration.activation_height),
            static_cast<unsigned long long>(configuration.activation_dag_section),
            static_cast<unsigned long long>(configuration.proposal_timeout_ms),
            static_cast<unsigned long long>(configuration.maximum_timeout_ms),
            static_cast<unsigned long long>(configuration.maximum_batch_bytes));
        return 0;
    }

    usage(argv[0]);
    return 64;
}
