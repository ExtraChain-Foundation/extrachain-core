/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation, Inc., either version 3 of the License,
 * or (at your option) any later version.
 */

// Shadow ceremony for a stand or a test network, one node home per validator. The generated
// governance, recovery and validator keys live only in this process unless EXC_SHADOW_KEYS_DIR
// names a new directory to keep them in. Production operators must use independent offline
// key custody and authorization shares.

#include <boost/json.hpp>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "chain/dag.h"
#include "chain/actor_index.h"
#include "consensus/consensus_protocol.h"
#include "consensus/mining_epoch.h"
#include "consensus/shadow_consensus.h"
#include "consensus/validator_set.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"
#include "utils/serialization.h"

namespace {
    using namespace ExtraChain::Consensus;

    std::expected<std::string, std::string> read_node_identifier(const std::filesystem::path& home) {
        const auto content = FileIo::read_all(home / ".settings");
        if (!content.has_value()) {
            return std::unexpected("settings file is unavailable: " + (home / ".settings").string());
        }
        boost::system::error_code error;
        const auto                parsed = boost::json::parse(content.value(), error);
        if (error || !parsed.is_object()) {
            return std::unexpected("settings file is invalid: " + (home / ".settings").string());
        }
        const auto* value = parsed.as_object().if_contains("node_identifier");
        if (value == nullptr || !value->is_string() || value->as_string().empty()) {
            return std::unexpected("node identifier is absent: " + (home / ".settings").string());
        }
        return std::string(value->as_string());
    }

    std::expected<std::uint64_t, std::string> section_number(const SectionId& section) {
        const auto    text   = section.to_string();
        std::uint64_t value  = 0;
        const auto    parsed = std::from_chars(text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
            return std::unexpected("current DAG section is outside the stand range");
        }
        return value;
    }

    std::vector<std::string> public_keys(const std::vector<KeyPrivate>& keys) {
        std::vector<std::string> result;
        result.reserve(keys.size());
        for (const auto& key : keys) {
            result.push_back(Utils::to_base64(key.public_key()));
        }
        return result;
    }

    // A login hash is the hex hash of a profile's login and password, as the console computes it.
    bool valid_login_hash(std::string_view value) {
        return value.size() == 64 && std::ranges::all_of(value, [](unsigned char character) {
                   return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
               });
    }

    std::string stand_seed_login_hash() {
        return Utils::calculate_hash("gen-login"
                                     "gen-password");
    }

    bool write_private(const std::filesystem::path& path, const std::string& bytes) {
        if (!FileIo::write_atomic(path, bytes).has_value()) {
            return false;
        }
        std::error_code error;
        std::filesystem::permissions(path,
                                     std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                     std::filesystem::perm_options::replace,
                                     error);
        return !error;
    }

    // Keys in the shadowctl format: a network can then sign a committee change or a recovery.
    bool save_ceremony_keys(const std::filesystem::path&          directory,
                            const ActorId&                        network_id,
                            std::uint64_t                         activation_section,
                            const std::vector<std::string>&       node_identifiers,
                            const std::vector<Actor<KeyPrivate>>& validator_actors,
                            const std::vector<KeyPrivate>&        consensus_keys,
                            const std::vector<KeyPrivate>&        governance_keys,
                            const std::vector<KeyPrivate>&        recovery_keys) {
        std::error_code error;
        if (std::filesystem::exists(directory, error) || !std::filesystem::create_directories(directory, error)) {
            std::fprintf(stderr, "[shadow-bundle] keys directory must be new: %s\n", directory.string().c_str());
            return false;
        }
        std::filesystem::permissions(directory,
                                     std::filesystem::perms::owner_all,
                                     std::filesystem::perm_options::replace,
                                     error);
        if (error) {
            return false;
        }
        boost::json::array validators;
        for (std::size_t index = 0; index < node_identifiers.size(); ++index) {
            const auto name = "validator-" + std::to_string(index);
            if (!write_private(directory / (name + "-actor.msgpack"),
                               MessagePack::serialize(validator_actors[index]))
                || !write_private(directory / (name + "-consensus.msgpack"),
                                  MessagePack::serialize(consensus_keys[index]))) {
                return false;
            }
            validators.push_back(boost::json::object {
                { "index", index },
                { "node_identifier", node_identifiers[index] },
                { "validator_id", validator_id_for(consensus_keys[index].public_key()) },
                { "actor_id", validator_actors[index].id().to_string() },
            });
        }
        for (const auto& [prefix, keys] : { std::pair { "governance-", &governance_keys },
                                            std::pair { "recovery-", &recovery_keys } }) {
            for (std::size_t index = 0; index < keys->size(); ++index) {
                if (!write_private(directory / (prefix + std::to_string(index) + ".msgpack"),
                                   MessagePack::serialize((*keys)[index]))) {
                    return false;
                }
            }
        }
        const boost::json::object ceremony {
            { "network_id", network_id.to_string() },
            { "activation_dag_section", activation_section },
            { "governance_public_keys", boost::json::value_from(public_keys(governance_keys)) },
            { "recovery_public_keys", boost::json::value_from(public_keys(recovery_keys)) },
            { "validators", validators },
        };
        return write_private(directory / "ceremony.json", boost::json::serialize(ceremony) + "\n");
    }

    bool write_node_bundle(const std::filesystem::path& home,
                           const ValidatorSet&          validators,
                           const IdentityDocument&      identity,
                           const ShadowConfiguration&   configuration,
                           const MultisigPolicy&        governance,
                           const MultisigPolicy&        recovery,
                           const TrustAnchorV1&         anchor,
                           const ActivationManifestV1&  activation) {
        const auto directory = home / "consensus";
        return ShadowConsensus::write_validator_set(directory, validators).has_value()
               && ShadowConsensus::write_identity(directory, identity).has_value()
               && ShadowConsensus::write_configuration(directory, configuration).has_value()
               && ShadowConsensus::write_governance_policy(directory, governance).has_value()
               && ShadowConsensus::write_recovery_policy(directory, recovery).has_value()
               && ShadowConsensus::write_trust_anchor(directory, anchor).has_value()
               && ShadowConsensus::write_activation_manifest(directory, activation, governance).has_value();
    }
} // namespace

int main(int argc, char* argv[]) {
    using namespace ExtraChain::Consensus;

    if (argc == 2 && std::string_view(argv[1]) == "--login-hash") {
        // Reads the login and the password as two lines, so neither appears in the process list.
        std::string login;
        std::string password;
        if (!std::getline(std::cin, login) || !std::getline(std::cin, password) || login.empty()) {
            std::fprintf(stderr, "usage: printf '%%s\\n%%s\\n' <login> <password> | %s --login-hash\n", argv[0]);
            return 64;
        }
        std::printf("%s\n", Utils::calculate_hash(login + password).c_str());
        return 0;
    }

    if (argc == 4 && std::string_view(argv[1]) == "--prepare") {
        const auto             home = std::filesystem::absolute(argv[2]).lexically_normal();
        const std::string_view role = argv[3];
        if (role != "seed" && role != "joiner" && !valid_login_hash(role)) {
            std::fprintf(stderr, "usage: %s --prepare <node-home> <seed|joiner|login-hash>\n", argv[0]);
            return 64;
        }
        std::error_code directory_error;
        std::filesystem::current_path(home, directory_error);
        if (directory_error) {
            std::fprintf(stderr, "[shadow-bundle] cannot use node home: %s\n", directory_error.message().c_str());
            return 73;
        }
        auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
        node->process();
        const auto login_hash = role == "seed"     ? stand_seed_login_hash()
                                : role == "joiner" ? Utils::calculate_hash(std::filesystem::current_path().string()
                                                                           + ":joiner")
                                                   : std::string(role);
        const auto login      = node->login(login_hash);
        if (!login.has_value()) {
            std::fprintf(stderr, "[shadow-bundle] node login failed: %d\n", static_cast<int>(login.error()));
            return 2;
        }
        node->dag()->set_mode(DagMode::Full);
        const auto current  = section_number(node->dag()->current_section());
        const auto prepared = node->dag()->prepare_shadow_activation();
        if (!prepared.has_value()) {
            std::fprintf(stderr,
                         "[shadow-bundle] transition preparation failed: %d\n",
                         static_cast<int>(prepared.error()));
            // Every legacy transaction opens the next section, so the network reaches a boundary
            // after this many more transactions; then stop every node and prepare again.
            if (current.has_value() && current.value() % ShadowSectionInterval != 0) {
                std::fprintf(stderr,
                             "[shadow-bundle] current section %llu is not a multiple of %llu: "
                             "about %llu more transactions reach the boundary\n",
                             static_cast<unsigned long long>(current.value()),
                             static_cast<unsigned long long>(ShadowSectionInterval),
                             static_cast<unsigned long long>(ShadowSectionInterval
                                                             - current.value() % ShadowSectionInterval));
            }
            node->cleanUp();
            return 2;
        }
        std::printf("[shadow-bundle] prepared network=%s boundary=%s\n",
                    node->network_id().to_string().c_str(),
                    prepared.value().to_string().c_str());
        node->cleanUp();
        return 0;
    }

    const auto node_count = static_cast<std::size_t>(argc > 2 ? argc - 2 : 0);
    if (!valid_committee_size(node_count)) {
        std::printf(
            "usage: %s --login-hash | %s --prepare <node-home> <seed|joiner|login-hash> | "
            "%s <seed-home> <node-home-0> ... <node-home-n> (%zu to %zu validators)\n",
            argv[0],
            argv[0],
            argv[0],
            MinimumCommitteeSize,
            MaximumCommitteeSize);
        return 64;
    }

    std::vector<std::filesystem::path> homes;
    homes.reserve(node_count);
    std::vector<std::string> node_identifiers;
    node_identifiers.reserve(node_count);
    std::set<std::string> unique_identifiers;
    for (std::size_t index = 0; index < node_count; ++index) {
        const auto home       = std::filesystem::absolute(argv[index + 2]).lexically_normal();
        const auto identifier = read_node_identifier(home);
        if (!identifier.has_value()) {
            std::fprintf(stderr, "[shadow-bundle] %s\n", identifier.error().c_str());
            return 65;
        }
        if (!unique_identifiers.insert(identifier.value()).second) {
            std::fprintf(stderr, "[shadow-bundle] duplicate node identifier\n");
            return 65;
        }
        const auto consensus_directory = home / "consensus";
        if (std::filesystem::exists(consensus_directory) && !std::filesystem::is_empty(consensus_directory)) {
            std::fprintf(stderr,
                         "[shadow-bundle] consensus directory is not empty: %s\n",
                         consensus_directory.string().c_str());
            return 73;
        }
        homes.push_back(home);
        node_identifiers.push_back(identifier.value());
    }

    const auto      seed_home = std::filesystem::absolute(argv[1]).lexically_normal();
    std::error_code directory_error;
    std::filesystem::current_path(seed_home, directory_error);
    if (directory_error) {
        std::fprintf(stderr, "[shadow-bundle] cannot use seed home: %s\n", directory_error.message().c_str());
        return 73;
    }

    // The seed profile owns the network actor, which signs the validator set.
    const char* seed_login = std::getenv("EXC_SHADOW_SEED_LOGIN_HASH");
    if (seed_login != nullptr && !valid_login_hash(seed_login)) {
        std::fprintf(stderr, "[shadow-bundle] EXC_SHADOW_SEED_LOGIN_HASH must be a 64-digit hex hash\n");
        return 64;
    }
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    const auto login = node->login(seed_login != nullptr ? std::string(seed_login) : stand_seed_login_hash());
    if (!login.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] seed login failed: %d\n", static_cast<int>(login.error()));
        return 2;
    }

    node->dag()->set_mode(DagMode::Full);
    const auto prepared      = node->dag()->prepare_shadow_activation();
    const auto network_id    = node->network_id();
    const auto network_actor = node->account_controller()->system_actor();
    const auto current_section =
        prepared.has_value()
            ? section_number(prepared.value())
            : std::expected<std::uint64_t, std::string>(std::unexpected("transition preparation failed"));
    if (!prepared.has_value() || network_id.is_zero() || network_actor.id() != network_id
        || !current_section.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] seed network state is invalid\n");
        node->cleanUp();
        return 2;
    }
    node->cleanUp();
    node.reset();

    const auto activation_section = current_section.value() + ShadowSectionInterval;

    std::vector<Actor<KeyPrivate>> validator_actors(node_count);
    std::vector<KeyPrivate>        consensus_keys(node_count);
    std::vector<ValidatorRecord>   records;
    records.reserve(node_count);
    for (std::size_t index = 0; index < node_count; ++index) {
        validator_actors[index].create(ActorType::Service);
        consensus_keys[index].generate_random();
        auto record = make_validator_record(network_id,
                                            1,
                                            validator_actors[index],
                                            consensus_keys[index],
                                            node_identifiers[index],
                                            0);
        if (!record.has_value()) {
            std::fprintf(stderr, "[shadow-bundle] validator record creation failed\n");
            return 3;
        }
        records.push_back(std::move(record.value()));
    }
    const auto validators = make_validator_set(network_id, 1, std::move(records), network_actor);

    std::vector<KeyPrivate> governance_keys(GovernanceSignerCount);
    std::vector<KeyPrivate> recovery_keys(GovernanceSignerCount);
    for (auto& key : governance_keys) {
        key.generate_random();
    }
    for (auto& key : recovery_keys) {
        key.generate_random();
    }
    const auto governance = make_multisig_policy(network_id, GovernanceThreshold, public_keys(governance_keys));
    const auto recovery   = make_multisig_policy(network_id, RecoveryThreshold, public_keys(recovery_keys));
    if (!validators.has_value() || !governance.has_value() || !recovery.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] committee policy creation failed\n");
        return 3;
    }
    const auto validator_view = ValidatorSetView::create(validators.value());
    if (!validator_view.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] validator view creation failed\n");
        return 3;
    }
    const auto& first_leader       = validator_view.value().leader(1, 0);
    std::size_t first_leader_index = node_count;
    for (std::size_t index = 0; index < node_count; ++index) {
        if (validator_id_for(consensus_keys[index].public_key()) == first_leader.validator_id) {
            first_leader_index = index;
            break;
        }
    }
    if (first_leader_index == node_count) {
        std::fprintf(stderr, "[shadow-bundle] first leader mapping failed\n");
        return 3;
    }

    TrustAnchorV1 anchor {
        .network_id         = network_id,
        .initial_validators = validators.value(),
        .governance_policy  = governance.value(),
        .recovery_policy    = recovery.value(),
    };
    const auto anchor_authorization =
        authorize_action(governance.value(),
                         1,
                         trust_anchor_action_hash(anchor),
                         { governance_keys[0], governance_keys[1], governance_keys[2] });
    if (!anchor_authorization.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] trust anchor authorization failed\n");
        return 3;
    }
    anchor.authorization = anchor_authorization.value();

    ActivationManifestV1 activation {
        .network_id             = network_id,
        .activation_height      = 1,
        .activation_dag_section = activation_section,
        .validator_set_hash     = hash_validator_set(validators.value()),
    };
    const char* mining_test_flag = std::getenv("EXC_SHADOW_MINING_TEST");
    if (mining_test_flag != nullptr && std::string_view(mining_test_flag) != "1") {
        std::fprintf(stderr, "[shadow-bundle] EXC_SHADOW_MINING_TEST must be 1 when set\n");
        return 64;
    }
    const bool mining_test = mining_test_flag != nullptr;
    const char* long_test_flag = std::getenv("EXC_SHADOW_MINING_LONG_TEST");
    if (long_test_flag != nullptr && (!mining_test || std::string_view(long_test_flag) != "1"))
        return 64;
    if (mining_test) {
        // Explicit fixtures: 32 ExC, or 1,000 ExC across the long test. Neither is a production schedule.
        const MiningEmissionSegment segment = long_test_flag == nullptr
                                                  ? MiningEmissionSegment { 32, NativeCoinUnits }
                                                  : MiningEmissionSegment { 100'000, NativeCoinUnits / 100 };
        // Policy epochs are periods of block time; the schedule starts with the current one.
        const auto now_ms = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                                           std::chrono::system_clock::now().time_since_epoch())
                                                           .count());
        activation.mining_policy = MiningEmissionPolicy { now_ms / DefaultMiningEpochMs, { segment } };
    }
    const auto activation_authorization =
        authorize_action(governance.value(),
                         1,
                         activation_action_hash(activation),
                         { governance_keys[0], governance_keys[1], governance_keys[2] });
    if (!activation_authorization.has_value()) {
        std::fprintf(stderr, "[shadow-bundle] activation authorization failed\n");
        return 3;
    }
    activation.authorization = activation_authorization.value();

    const ShadowConfiguration configuration {
        .mode                   = ShadowMode::Finality,
        .activation_height      = activation.activation_height,
        .activation_dag_section = activation.activation_dag_section,
        .proposal_timeout_ms    = 2'000,
        .maximum_timeout_ms     = 16'000,
        .maximum_batch_bytes    = (mining_test ? 16ULL : 4ULL) * 1024ULL * 1024ULL,
    };

    if (const char* keys_directory = std::getenv("EXC_SHADOW_KEYS_DIR");
        keys_directory != nullptr
        && !save_ceremony_keys(std::filesystem::absolute(keys_directory).lexically_normal(),
                               network_id,
                               activation_section,
                               node_identifiers,
                               validator_actors,
                               consensus_keys,
                               governance_keys,
                               recovery_keys)) {
        std::fprintf(stderr, "[shadow-bundle] ceremony keys were not saved\n");
        return 3;
    }

    bool written = true;
    for (std::size_t index = 0; index < node_count; ++index) {
        written = write_node_bundle(homes[index],
                                    validators.value(),
                                    IdentityDocument {
                                        .validator_id = validator_id_for(consensus_keys[index].public_key()),
                                        .key          = consensus_keys[index],
                                    },
                                    configuration,
                                    governance.value(),
                                    recovery.value(),
                                    anchor,
                                    activation)
                  && written;
    }
    if (!written) {
        std::fprintf(stderr, "[shadow-bundle] bundle write failed\n");
        return 3;
    }

    for (const auto& home : homes) {
        const auto loaded = ShadowConsensus::load(home / "consensus", network_id);
        if (!loaded.has_value()) {
            std::fprintf(stderr,
                         "[shadow-bundle] generated node configuration failed verification: %d\n",
                         static_cast<int>(loaded.error()));
            return 4;
        }
    }

    std::printf(
        "[shadow-bundle] verified %zu-node bundle network=%s activation_section=%llu "
        "leader_index=%zu\n",
        node_count,
        network_id.to_string().c_str(),
        static_cast<unsigned long long>(activation_section),
        first_leader_index);
    return 0;
}
