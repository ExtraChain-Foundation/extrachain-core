/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

// Minimal real-node runner for live, multi-process synchronization tests.
//
//   extrachain-node-run serve <home> [listen-port] [dfs-payload-bytes]
//       Login to the existing profile/chain in <home> and stay up serving peers.
//
//   extrachain-node-run join  <home> <peer-ip> <target-section> [listen-port] [peer-port]
//                             [dfs-owner] [dfs-name] [dfs-payload-bytes]
//       Login to the profile in <home> (whose dag/ is empty), dial <peer-ip> and
//       sync. Exits 0 once dag current_section reaches <target-section> and the
//       optional DFS payload is ready, 1 on timeout.
//
// Node data is cwd-relative, so we chdir into <home> first. Each process is its
// own node (ExtraChainNode is a per-process singleton), so server and clients
// must run as separate processes.

#include <algorithm>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "chain/dag.h"
#include "consensus/consensus_protocol.h"
#include "consensus/consensus_service.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "network/network_service.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"

namespace {
    const std::string LOGIN    = "gen-login";
    const std::string PASSWORD = "gen-password";

    volatile std::sig_atomic_t stop_requested = 0;

    void request_stop(int) {
        stop_requested = 1;
    }

    std::string joiner_login_hash() {
        return Utils::calculate_hash(std::filesystem::current_path().string() + ":joiner");
    }
} // namespace

int main(int argc, char* argv[]) {
    if (std::getenv("EXC_DEBUG_LOG") != nullptr) {
        Logger::instance().set_debug(true);
    }
    if (argc < 3) {
        std::printf(
            "usage: %s serve <home> [listen-port] [dfs-payload-bytes] | "
            "join <home> <peer-ip> <target-section> [listen-port] [peer-port] "
            "[dfs-owner] [dfs-name] [dfs-payload-bytes] | "
            "committee <home> <seed|joiner> <index> <listen-port> <first-port> "
            "<node-count> [intent-count] [run-seconds] [barrier-directory]\n",
            argv[0]);
        return 64;
    }
    const std::string mode        = argv[1];
    const std::string home        = argv[2];
    std::uint16_t     listen_port = 17593;
    if (mode == "serve" && argc > 3) {
        listen_port = static_cast<std::uint16_t>(std::atoi(argv[3]));
    } else if (mode == "join" && argc > 5) {
        listen_port = static_cast<std::uint16_t>(std::atoi(argv[5]));
    } else if (mode == "committee" && argc > 5) {
        listen_port = static_cast<std::uint16_t>(std::atoi(argv[5]));
    }

    std::error_code directory_error;
    std::filesystem::create_directories(home, directory_error);
    if (!directory_error) {
        std::filesystem::current_path(home, directory_error);
    }
    if (directory_error) {
        std::printf("[node-run] cannot use node home %s: %s\n", home.c_str(), directory_error.message().c_str());
        return 73;
    }

    // For a joining node, point first_node at the peer and force a fresh node id
    // BEFORE the node initialises (initialize_first_node reads settings on init).
    if (mode == "join") {
        if (argc < 5) {
            std::printf("usage: %s join <home> <peer-ip> <target-section> [listen-port] [peer-port]\n", argv[0]);
            return 64;
        }
        auto settings            = Utils::read_settings();
        settings.first_node      = std::string(argv[3]);
        settings.node_identifier = std::nullopt;
        Utils::write_settings(settings);
    }

    const char* bind_ip = std::getenv("EXC_BIND_IP");
    if (mode == "committee" && bind_ip != nullptr) {
        auto settings = Utils::read_settings();
        // Point every joiner at node 0 rather than at itself. Self as first_node
        // does suppress the automatic dial — the committee wires its own mesh
        // below — but it also makes reconnection() bail out on its first guard
        // ("do not dial ourselves"), which switches re-dial off for every peer.
        // A committee that loses a link then never gets it back, and that is the
        // one behaviour a chaos run most needs to exercise.
        const bool is_seed_node = argc > 4 && std::strtoull(argv[4], nullptr, 10) == 0;
        settings.first_node     = is_seed_node ? std::string(bind_ip) : "127.0.0.1";
        Utils::write_settings(settings);
    }
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false,
                                                                   false,
                                                                   listen_port,
                                                                   std::nullopt,
                                                                   std::string {},
                                                                   bind_ip == nullptr ? std::string {}
                                                                                      : std::string(bind_ip));
    node->process();

    if (mode == "committee") {
        using namespace ExtraChain::Consensus;

        if (argc < 8) {
            std::printf(
                "usage: %s committee <home> <seed|joiner> <index> <listen-port> "
                "<first-port> <node-count> [intent-count] [run-seconds] [barrier-directory] "
                "[first-intent-nonce] [stay-until-deadline]\n",
                argv[0]);
            return 64;
        }
        const std::string role       = argv[3];
        const auto        node_index = static_cast<std::size_t>(std::strtoull(argv[4], nullptr, 10));
        const auto        first_port = static_cast<std::uint16_t>(std::atoi(argv[6]));
        const auto        node_count = static_cast<std::size_t>(std::strtoull(argv[7], nullptr, 10));
        const auto intent_count = static_cast<std::size_t>(argc > 8 ? std::strtoull(argv[8], nullptr, 10) : 128);
        const auto run_seconds  = static_cast<std::uint64_t>(argc > 9 ? std::strtoull(argv[9], nullptr, 10) : 90);
        const auto barrier_directory = argc > 10 ? std::filesystem::path(argv[10]) : std::filesystem::path {};
        const auto first_intent_nonce =
            static_cast<std::uint64_t>(argc > 11 ? std::strtoull(argv[11], nullptr, 10) : 1);
        const bool stay_until_deadline = argc > 12 && std::atoi(argv[12]) != 0;
        if ((role != "seed" && role != "joiner") || node_count != ShadowCommitteeSize || node_index >= node_count
            || run_seconds < 10 || first_intent_nonce == 0
            || (intent_count > 0
                && intent_count - 1 > std::numeric_limits<std::uint64_t>::max() - first_intent_nonce)) {
            std::printf("[node-run] invalid committee arguments\n");
            return 64;
        }

        const auto login_hash = role == "seed" ? Utils::calculate_hash(LOGIN + PASSWORD) : joiner_login_hash();
        const auto login      = node->login(login_hash);
        if (!login.has_value()) {
            std::printf("[node-run] committee login failed (error %d)\n", static_cast<int>(login.error()));
            return 2;
        }
        node->dag()->set_mode(DagMode::Full);
        std::printf("[node-run] local node identifier=%s network=%s\n",
                    node->node_identifier().c_str(),
                    node->network_id().to_string().c_str());
        if (node->consensus() != nullptr && !node->consensus()->active()) {
            const auto activated = node->consensus()->activate(node->network_id());
            if (!activated.has_value()) {
                std::printf("[node-run] explicit Shadow activation failed (error %d)\n",
                            static_cast<int>(activated.error()));
            } else if (!activated.value()) {
                std::printf("[node-run] explicit Shadow activation found no configuration\n");
            }
        }
        if (node->consensus() == nullptr || !node->consensus()->active() || !node->consensus()->voting()) {
            std::printf("[node-run] Shadow Finality did not activate\n");
            node->cleanUp();
            return 3;
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        for (std::size_t peer = 0; peer < node_index; ++peer) {
            node->network()->request_endpoint("127.0.0." + std::to_string(peer + 1),
                                              static_cast<std::uint16_t>(first_port + peer),
                                              false,
                                              true);
        }
        const auto  connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        std::size_t connect_attempt  = 0;
        std::size_t stable_samples   = 0;
        while (stop_requested == 0 && std::chrono::steady_clock::now() < connect_deadline && stable_samples < 3) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            ++connect_attempt;
            if (node->network()->active_connections_count() == static_cast<int>(node_count - 1)) {
                ++stable_samples;
            } else {
                stable_samples = 0;
            }
            if (stable_samples == 0 && connect_attempt % 3 == 0) {
                for (std::size_t peer = 0; peer < node_index; ++peer) {
                    node->network()->request_endpoint("127.0.0." + std::to_string(peer + 1),
                                                      static_cast<std::uint16_t>(first_port + peer),
                                                      false,
                                                      true);
                }
            }
        }
        const auto connected = node->network()->active_connections_count();
        std::printf("[node-run] committee node=%zu connected=%d voting=yes\n", node_index, connected);
        std::fflush(stdout);
        if (connected < static_cast<int>(node_count - 1)) {
            node->cleanUp();
            return 4;
        }
        // Raw connectivity is not consensus readiness: proposals that arrive
        // before peer authentication completes are silently discarded by the
        // handlers and deduplicated forever, so the committee must not start
        // until every node sees the full shadow-capable mesh.
        const auto shadow_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (stop_requested == 0 && std::chrono::steady_clock::now() < shadow_deadline
               && node->network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY).size()
                      < node_count - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (node->network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY).size()
            < node_count - 1) {
            std::printf("[node-run] committee node=%zu shadow mesh incomplete\n", node_index);
            node->cleanUp();
            return 4;
        }
        // A short settle window lets the challenge/response authentication
        // round-trips behind the capability flags finish too.
        std::this_thread::sleep_for(std::chrono::seconds(2));
        if (!barrier_directory.empty()) {
            std::filesystem::create_directories(barrier_directory);
            const auto actor_marker =
                FileIo::write_atomic(barrier_directory / ("actor-" + std::to_string(node_index)),
                                     node->account_controller()->system_actor().id().to_string());
            const auto marker = barrier_directory / ("node-" + std::to_string(node_index));
            if (!actor_marker.has_value() || !FileIo::write_atomic(marker, "ready").has_value()) {
                node->cleanUp();
                return 4;
            }
            const auto barrier_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
            while (stop_requested == 0 && std::chrono::steady_clock::now() < barrier_deadline
                   && !std::filesystem::exists(barrier_directory / "go")) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
            if (!std::filesystem::exists(barrier_directory / "go")) {
                node->cleanUp();
                return 4;
            }
        }
        std::uint64_t funding_nonces = 0;
        if (const char* fund_nodes_env = std::getenv("EXC_FUND_NODES");
            fund_nodes_env != nullptr && !barrier_directory.empty()) {
            std::vector<std::size_t> fund_targets;
            {
                const std::string list(fund_nodes_env);
                std::size_t       pos = 0;
                while (pos <= list.size()) {
                    const auto comma = list.find(',', pos);
                    const auto item =
                        list.substr(pos, comma == std::string::npos ? std::string::npos : comma - pos);
                    if (!item.empty()) {
                        std::size_t target = 0;
                        const auto  parsed = std::from_chars(item.data(), item.data() + item.size(), target);
                        if (parsed.ec != std::errc {} || parsed.ptr != item.data() + item.size()
                            || target >= node_count
                            || std::find(fund_targets.begin(), fund_targets.end(), target) != fund_targets.end()) {
                            std::printf("[node-run] invalid EXC_FUND_NODES value\n");
                            node->cleanUp();
                            return 64;
                        }
                        fund_targets.push_back(target);
                    }
                    if (comma == std::string::npos) {
                        break;
                    }
                    pos = comma + 1;
                }
            }
            const bool is_funded_sender =
                std::find(fund_targets.begin(), fund_targets.end(), node_index) != fund_targets.end();
            if (node_index == 0) {
                const auto&              funder      = node->account_controller()->system_actor();
                const char*              amount_env  = std::getenv("EXC_FUND_AMOUNT");
                const std::string        fund_amount = amount_env != nullptr ? amount_env : "1.0";
                std::vector<std::string> funding_hashes;
                for (const auto target : fund_targets) {
                    if (target == 0) {
                        continue;
                    }
                    const auto id_text = FileIo::read_all(barrier_directory / ("actor-" + std::to_string(target)));
                    if (!id_text.has_value()) {
                        std::printf("[node-run] funding: no actor id for node %zu\n", target);
                        node->cleanUp();
                        return 5;
                    }
                    const auto target_actor = ActorId::create(id_text.value());
                    if (!target_actor.has_value()) {
                        std::printf("[node-run] funding: bad actor id for node %zu\n", target);
                        node->cleanUp();
                        return 5;
                    }
                    const auto metadata = "shadow-fund-" + std::to_string(target);
                    const auto intent   = make_intent(
                        TransactionIntentV2 {
                              .network_id           = node->network_id(),
                              .sender               = funder.id(),
                              .receiver             = target_actor.value(),
                              .token                = TokenId("468faf2f1be6504a9a26f7f027f7e43380b0d77d"),
                              .amount               = fund_amount,
                              .operation            = IntentOperation::Transfer,
                              .account_nonce        = ++funding_nonces,
                              .valid_after_height   = 0,
                              .expires_after_height = 1'000'000,
                        },
                        metadata,
                        funder);
                    if (!intent.has_value()) {
                        std::printf("[node-run] funding intent creation failed (error %d)\n",
                                    static_cast<int>(intent.error()));
                        node->cleanUp();
                        return 5;
                    }
                    const auto submitted = node->consensus()->submit_intent(IntentEnvelope {
                        .intent   = intent.value(),
                        .metadata = metadata,
                    });
                    if (!submitted.has_value()) {
                        std::printf("[node-run] funding submission failed for node %zu (error %d)\n",
                                    target,
                                    static_cast<int>(submitted.error()));
                        node->cleanUp();
                        return 5;
                    }
                    funding_hashes.push_back(submitted.value());
                }
                std::printf("[node-run] funding: submitted %zu transfers of %s\n",
                            funding_hashes.size(),
                            fund_amount.c_str());
                std::fflush(stdout);
                const auto funding_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(180);
                bool       funded           = funding_hashes.empty();
                while (stop_requested == 0 && !funded && std::chrono::steady_clock::now() < funding_deadline) {
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    funded = true;
                    for (const auto& hash : funding_hashes) {
                        const auto receipt = node->consensus()->intent_receipt(hash);
                        if (!receipt.has_value() || !receipt.value().has_value()
                            || receipt.value().value().status != IntentStatus::Finalized) {
                            funded = false;
                            break;
                        }
                    }
                }
                if (!funded) {
                    std::printf("[node-run] funding: transfers did not finalize in time\n");
                    node->cleanUp();
                    return 5;
                }
                std::printf("[node-run] funding: finalized, releasing senders\n");
                std::fflush(stdout);
                if (!FileIo::write_atomic(barrier_directory / "funded", "ok").has_value()) {
                    node->cleanUp();
                    return 5;
                }
            } else if (is_funded_sender && intent_count > 0) {
                const auto funded_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(200);
                while (stop_requested == 0 && std::chrono::steady_clock::now() < funded_deadline
                       && !std::filesystem::exists(barrier_directory / "funded")) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(200));
                }
                if (!std::filesystem::exists(barrier_directory / "funded")) {
                    std::printf("[node-run] funding marker did not appear in time\n");
                    node->cleanUp();
                    return 5;
                }
            }
        }

        std::vector<std::string> submitted_hashes;
        if (intent_count > 0) {
            const auto&              sender   = node->account_controller()->system_actor();
            const Actor<KeyPrivate>* receiver = nullptr;
            for (const auto& account : node->account_controller()->accounts()) {
                if (account.id() != sender.id()) {
                    receiver = &account;
                    break;
                }
            }
            if (receiver == nullptr) {
                std::printf("[node-run] a distinct intent receiver is absent\n");
                node->cleanUp();
                return 5;
            }
            submitted_hashes.reserve(intent_count);
            for (std::size_t index = 0; index < intent_count; ++index) {
                const auto metadata = "shadow-live-intent-" + std::to_string(index);
                const auto nonce    = funding_nonces + first_intent_nonce + index;
                const auto intent   = make_intent(
                    TransactionIntentV2 {
                          .network_id           = node->network_id(),
                          .sender               = sender.id(),
                          .receiver             = receiver->id(),
                          .token                = TokenId("468faf2f1be6504a9a26f7f027f7e43380b0d77d"),
                          .amount               = "0.0001",
                          .operation            = IntentOperation::Transfer,
                          .account_nonce        = nonce,
                          .valid_after_height   = 0,
                          .expires_after_height = 1'000'000,
                    },
                    metadata,
                    sender);
                if (!intent.has_value()) {
                    std::printf("[node-run] intent creation failed (error %d)\n",
                                static_cast<int>(intent.error()));
                    node->cleanUp();
                    return 5;
                }
                const auto submitted = node->consensus()->submit_intent(IntentEnvelope {
                    .intent   = intent.value(),
                    .metadata = metadata,
                });
                if (!submitted.has_value()) {
                    std::printf("[node-run] intent submission failed at %zu (error %d)\n",
                                index,
                                static_cast<int>(submitted.error()));
                    node->cleanUp();
                    return 5;
                }
                submitted_hashes.push_back(submitted.value());
            }
            std::printf("[node-run] submitted intents=%zu\n", submitted_hashes.size());
            std::fflush(stdout);
        }

        const auto run_deadline          = std::chrono::steady_clock::now() + std::chrono::seconds(run_seconds);
        bool       all_finalized         = submitted_hashes.empty();
        bool       finalization_reported = false;
        while (stop_requested == 0 && std::chrono::steady_clock::now() < run_deadline) {
            const auto metrics = node->consensus()->metrics();
            const auto ready   = node->consensus()->ready_intents(10'000, 8ULL * 1024ULL * 1024ULL).size();
            const auto shadow_peers =
                node->network()->active_full_peers_with_capability(SHADOW_CONSENSUS_CAPABILITY).size();
            std::printf(
                "[node-run] committee node=%zu conns=%d shadow_peers=%zu ready=%zu proposals=%llu votes=%llu "
                "timeouts=%llu certificates=%llu finalized=%llu\n",
                node_index,
                node->network()->active_connections_count(),
                shadow_peers,
                ready,
                static_cast<unsigned long long>(metrics.proposals_created),
                static_cast<unsigned long long>(metrics.votes_created),
                static_cast<unsigned long long>(metrics.timeout_votes),
                static_cast<unsigned long long>(metrics.certificates),
                static_cast<unsigned long long>(metrics.finalized));
            std::fflush(stdout);
            if (!submitted_hashes.empty()) {
                all_finalized = true;
                for (const auto& hash : submitted_hashes) {
                    const auto receipt = node->consensus()->intent_receipt(hash);
                    if (!receipt.has_value() || !receipt.value().has_value()
                        || receipt.value().value().status != IntentStatus::Finalized) {
                        all_finalized = false;
                        break;
                    }
                }
                if (all_finalized && !finalization_reported) {
                    std::printf("[node-run] finalized intents=%zu\n", submitted_hashes.size());
                    std::fflush(stdout);
                    finalization_reported = true;
                    if (!stay_until_deadline) {
                        break;
                    }
                }
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        const auto metrics    = node->consensus()->metrics();
        const bool progressed = metrics.certificates > 0 && metrics.finalized > 0;
        node->cleanUp();
        return progressed && all_finalized ? 0 : 6;
    }

    if (mode == "serve") {
        // The core/creator: load its existing profile + chain and serve.
        auto res = node->login(Utils::calculate_hash(LOGIN + PASSWORD)); // calls node->start()
        if (!res.has_value()) {
            std::printf("[node-run] serve login failed (error %d)\n", static_cast<int>(res.error()));
            return 2;
        }
        node->dag()->set_mode(DagMode::Full);

        if (argc > 4) {
            const auto                payload_size = static_cast<std::size_t>(std::strtoull(argv[4], nullptr, 10));
            std::vector<std::uint8_t> payload(payload_size);
            for (std::size_t index = 0; index < payload.size(); ++index) {
                payload[index] = static_cast<std::uint8_t>((index * 131U + 17U) & 0xffU);
            }

            const auto& owner = node->account_controller()->system_actor().id();
            auto        row   = node->dfs()->store_data_as_file(owner,
                                                       owner,
                                                       std::move(payload),
                                                       "validation",
                                                       "combined-network.bin");
            if (!row.has_value()) {
                std::printf("[node-run] DFS payload creation failed (error %d)\n", static_cast<int>(row.error()));
                return 3;
            }
            std::printf("[node-run] DFS payload owner=%s file_id=%s size=%zu\n",
                        owner.to_string().c_str(),
                        row->file_id.c_str(),
                        row->size);
        }
        std::printf("[node-run] serving from %s (sections=%s)\n",
                    home.c_str(),
                    node->dag()->current_section().to_string().c_str());
        std::fflush(stdout);
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        while (stop_requested == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        node->cleanUp();
        return 0;
    }

    if (mode == "join") {
        const long long   target     = std::atoll(argv[4]);
        const auto        peer_port  = static_cast<std::uint16_t>(argc > 6 ? std::atoi(argv[6]) : 17593);
        const std::string peer_ip    = argv[3];
        const bool        verify_dfs = argc > 9;
        const ActorId     dfs_owner  = verify_dfs ? ActorId(argv[7]) : ActorId();
        const std::string dfs_name   = verify_dfs ? argv[8] : std::string();
        const auto        expected_dfs_size =
            verify_dfs ? static_cast<std::uintmax_t>(std::strtoull(argv[9], nullptr, 10)) : 0;

        // A joining node must have its OWN distinct identity — reusing the core's
        // profile makes the core drop our requests as self-messages (init_sender_id
        // == its own system actor). Create a fresh profile (own seed, no genesis);
        // the genesis + chain arrive via sync. create_profile() also starts the node.
        auto unique = joiner_login_hash();
        node->account_controller()->create_profile(unique, ActorType::User);
        node->dag()->set_mode(DagMode::Full);

        std::printf("[node-run] joining %s, target section %lld\n", argv[3], target);
        std::fflush(stdout);

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        node->network()->request_endpoint(peer_ip, peer_port, true, true);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::minutes(4);
        int        ticks    = 0;
        int        result   = 1;
        while (std::chrono::steady_clock::now() < deadline) {
            const auto cur        = node->dag()->current_section();
            const auto conns      = node->network()->active_connections_count();
            const auto projection = node->dag()->state_projection();
            std::printf("[node-run] t=%ds conns=%d current_section=%s status=%d verified=%s reason=%s\n",
                        ticks * 3,
                        conns,
                        cur.to_string().c_str(),
                        static_cast<int>(projection.status),
                        projection.verified_section.to_string().c_str(),
                        projection.reason.c_str());
            std::fflush(stdout);
            ticks++;
            bool dfs_ready = !verify_dfs;
            if (verify_dfs) {
                auto row = node->dfs()->read_file_status(dfs_owner, dfs_name, "validation");
                if (row.has_value() && row->loaded()) {
                    auto path = Dfs::Path::file_path(dfs_owner, row->file_id);
                    dfs_ready = path.has_value() && std::filesystem::is_regular_file(path->native())
                                && std::filesystem::file_size(path->native()) == expected_dfs_size;
                }
            }
            if (cur >= SectionId(target) && dfs_ready) {
                std::printf("[node-run] REACHED target %lld\n", target);
                if (verify_dfs) {
                    std::printf("[node-run] DFS payload ready name=%s size=%ju\n",
                                dfs_name.c_str(),
                                expected_dfs_size);
                }
                // Warm the control index (find_last_control triggers the lazy
                // rebuild) and verify it populated, before exiting.
                auto lc = node->dag()->find_last_control();
                std::printf("[node-run] last_control section=%s\n",
                            lc.has_value() ? lc->section_id.to_string().c_str() : "(none)");
                std::fflush(stdout);
                result = 0;
                break;
            }
            if (conns == 0) {
                if (ticks % 3 == 0) {
                    node->network()->request_endpoint(peer_ip, peer_port, true, true);
                }
            } else {
                // Connected. A server-type node stays Ready and start_check() bails on
                // Ready, so kick the client-style pull explicitly. start_sync() guards
                // itself once a sync is already in flight.
                node->dag()->start_sync();
            }
            std::this_thread::sleep_for(std::chrono::seconds(3));
        }
        if (result != 0) {
            std::printf("[node-run] TIMEOUT at current_section=%s\n",
                        node->dag()->current_section().to_string().c_str());
            std::fflush(stdout);
        }
        std::printf("[node-run] stopping node\n");
        std::fflush(stdout);
        node->cleanUp();
        std::printf("[node-run] node stopped\n");
        std::fflush(stdout);
        return result;
    }

    std::printf("[node-run] unknown mode '%s'\n", mode.c_str());
    return 64;
}
