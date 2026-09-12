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
#include <fstream>
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
#include "dfs/collection_template.h"
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
        auto              settings = Utils::read_settings();
        const auto        index    = argc > 4 ? std::strtoull(argv[4], nullptr, 10) : 0;
        const std::string topology =
            std::getenv("EXC_SHADOW_TOPOLOGY") ? std::getenv("EXC_SHADOW_TOPOLOGY") : "mesh";
        // The reconnect uplink must be an edge in the selected test topology.
        settings.first_node = index == 0           ? std::string(bind_ip)
                              : topology == "mesh" ? "127.0.0.1"
                                                   : "127.0.0." + std::to_string(index);
        if (topology == "observer-chain") {
            if (index == 3)
                settings.first_node = "127.0.0.8";
            if (index == 7)
                settings.first_node = "127.0.0.3";
        }
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
        if ((role != "seed" && role != "joiner")
            || (node_count != ShadowCommitteeSize && node_count != ShadowCommitteeSize + 1)
            || node_index >= node_count || run_seconds < 10 || first_intent_nonce == 0
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
        // ExDFS runs Light by default (DfsService hard-codes it): a Light node never
        // pulls a payload it only knows from the catalog, so replication under
        // message loss rests on gossip alone. EXC_DFS_MODE=full makes the committee
        // pull every Known row too, which is what a storage node has to do.
        if (const char* dfs_mode = std::getenv("EXC_DFS_MODE");
            dfs_mode != nullptr && std::string(dfs_mode) == "full") {
            node->dfs()->set_mode(DfsMode::Full);
            std::printf("[node-run] DFS mode: full\n");
        }
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
        if (node->consensus() == nullptr || !node->consensus()->active()
            || (node_index < ShadowCommitteeSize && !node->consensus()->voting())
            || (node_index >= ShadowCommitteeSize && node->consensus()->voting())) {
            std::printf("[node-run] Shadow Finality did not activate\n");
            node->cleanUp();
            return 3;
        }

        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        const std::string topology =
            std::getenv("EXC_SHADOW_TOPOLOGY") ? std::getenv("EXC_SHADOW_TOPOLOGY") : "mesh";
        if (topology != "mesh" && topology != "ring" && topology != "chain" && topology != "degree3"
            && topology != "observer-chain") {
            std::printf("[node-run] unknown topology: %s\n", topology.c_str());
            node->cleanUp();
            return 64;
        }
        const auto adjacent = [&](std::size_t peer) {
            if (topology == "observer-chain") {
                const auto position = [](std::size_t index) {
                    return index == 7 ? 3 : index >= 3 ? index + 1 : index;
                };
                return position(peer) + 1 == position(node_index) || position(node_index) + 1 == position(peer);
            }
            return topology == "mesh" || peer + 1 == node_index || node_index + 1 == peer
                   || (topology == "ring"
                       && ((node_index == 0 && peer + 1 == node_count)
                           || (peer == 0 && node_index + 1 == node_count)))
                   || (topology == "degree3"
                       && ((node_index < 3 && peer == node_index + 3) || (peer < 3 && node_index == peer + 3)));
        };
        std::size_t required_peers = 0;
        for (std::size_t peer = 0; peer < node_count; ++peer) {
            if (peer != node_index && adjacent(peer))
                ++required_peers;
        }
        for (std::size_t peer = 0; peer < node_index; ++peer) {
            if (!adjacent(peer)) {
                continue;
            }
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
            if (node->network()->active_connections_count() == static_cast<int>(required_peers)) {
                ++stable_samples;
            } else {
                stable_samples = 0;
            }
            if (stable_samples == 0 && connect_attempt % 3 == 0) {
                for (std::size_t peer = 0; peer < node_index; ++peer) {
                    if (!adjacent(peer)) {
                        continue;
                    }
                    node->network()->request_endpoint("127.0.0." + std::to_string(peer + 1),
                                                      static_cast<std::uint16_t>(first_port + peer),
                                                      false,
                                                      true);
                }
            }
        }
        const auto connected = node->network()->active_connections_count();
        std::printf("[node-run] committee node=%zu connected=%d voting=%s\n",
                    node_index,
                    connected,
                    node->consensus()->voting() ? "yes" : "no");
        std::fflush(stdout);
        if (connected < static_cast<int>(required_peers)) {
            node->cleanUp();
            return 4;
        }
        // Wait for capability exchange before publishing signed relay messages.
        const auto shadow_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        while (stop_requested == 0 && std::chrono::steady_clock::now() < shadow_deadline
               && node->network()->active_full_peers_with_capability(SHADOW_RELAY_CAPABILITY).size()
                      < required_peers) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        if (node->network()->active_full_peers_with_capability(SHADOW_RELAY_CAPABILITY).size() < required_peers) {
            std::printf("[node-run] committee node=%zu shadow links incomplete\n", node_index);
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

        // Optional ExDFS load: every committee node publishes one file of
        // EXC_DFS_BYTES bytes while consensus runs, so the harness can check that
        // content replicates across the whole mesh — through chaos included. The
        // payload is deterministic per node, so a corrupt copy is told from a
        // missing one.
        if (const char* dfs_bytes_env = std::getenv("EXC_DFS_BYTES");
            dfs_bytes_env != nullptr && std::strtoull(dfs_bytes_env, nullptr, 10) > 0) {
            const auto dfs_bytes = static_cast<std::size_t>(std::strtoull(dfs_bytes_env, nullptr, 10));
            std::vector<std::uint8_t> payload(dfs_bytes);
            for (std::size_t index = 0; index < payload.size(); ++index) {
                payload[index] =
                    static_cast<std::uint8_t>((index * (131U + node_index) + 17U + node_index * 7U) & 0xffU);
            }
            const auto& owner = node->account_controller()->system_actor().id();
            const auto  row   = node->dfs()->store_data_as_file(owner,
                                                             owner,
                                                             std::move(payload),
                                                             "soak",
                                                             "node-" + std::to_string(node_index) + ".bin");
            if (!row.has_value()) {
                std::printf("[node-run] DFS store failed (error %d)\n", static_cast<int>(row.error()));
                node->cleanUp();
                return 5;
            }
            std::printf("[node-run] DFS stored owner=%s file_id=%s size=%zu\n",
                        owner.to_string().c_str(),
                        row->file_id.c_str(),
                        row->size);
            std::fflush(stdout);

            // Network removal (EXC_DFS_REMOVE_AFTER_S): a second, small file is
            // published now and removed by its owner that many seconds later. The
            // tombstone travels by DfsFileRemove gossip and by catalog sync, and the
            // harness expects every node to end with the row Removed and no payload.
            if (const char* remove_env = std::getenv("EXC_DFS_REMOVE_AFTER_S");
                remove_env != nullptr && std::strtoull(remove_env, nullptr, 10) > 0) {
                const auto remove_after = std::strtoull(remove_env, nullptr, 10);
                std::vector<std::uint8_t> doomed(65536);
                for (std::size_t index = 0; index < doomed.size(); ++index) {
                    doomed[index] = static_cast<std::uint8_t>((index * 7U + node_index) & 0xffU);
                }
                const auto doomed_row = node->dfs()->store_data_as_file(owner,
                                                                        owner,
                                                                        std::move(doomed),
                                                                        "soak",
                                                                        "doomed-" + std::to_string(node_index) + ".bin");
                if (!doomed_row.has_value()) {
                    std::printf("[node-run] DFS doomed store failed (error %d)\n", static_cast<int>(doomed_row.error()));
                    node->cleanUp();
                    return 5;
                }
                std::printf("[node-run] DFS doomed owner=%s file_id=%s size=%zu\n",
                            owner.to_string().c_str(),
                            doomed_row->file_id.c_str(),
                            doomed_row->size);
                std::fflush(stdout);
                std::thread([node = node.get(), owner, file_id = doomed_row->file_id, remove_after, node_index]() {
                    for (unsigned long long waited = 0; waited < remove_after && stop_requested == 0; ++waited) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (stop_requested != 0) {
                        return;
                    }
                    const auto removed = node->dfs()->remove_stored_file(owner, file_id);
                    std::printf("[node-run] DFS removed owner=%s file_id=%s ok=%d\n",
                                owner.to_string().c_str(),
                                file_id.c_str(),
                                removed.has_value() ? 1 : 0);
                    std::fflush(stdout);
                }).detach();
            }
        }

        // Optional ExDFS vector load: every committee node creates a vector from
        // its own template and appends EXC_DFS_VECTOR_ROWS rows to it. Vectors
        // replicate row by row over a different path than plain files (gossiped
        // DfsVectorAdd rather than fragment transfers), so the harness can tell
        // the two apart when one of them stops working.
        if (const char* rows_env = std::getenv("EXC_DFS_VECTOR_ROWS");
            rows_env != nullptr && std::strtoull(rows_env, nullptr, 10) > 0) {
            const auto  row_count = static_cast<std::size_t>(std::strtoull(rows_env, nullptr, 10));
            const auto& owner     = node->account_controller()->system_actor().id();
            const auto  name      = "soak_vector_" + std::to_string(node_index);
            auto        collection_template = Dfs::CollectionTemplate::create(name);
            if (!collection_template.has_value()) {
                std::printf("[node-run] vector template creation failed\n");
                node->cleanUp();
                return 5;
            }
            auto vector_template = collection_template.value()
                                       .use_id()
                                       .add_fields({ Dfs::Field::String("payload").not_null(),
                                                     Dfs::Field::Integer("position").not_null() });
            // Store the template first and create the vector from that stored row:
            // the variant overload is ambiguous against the (actor, file id) one.
            const auto stored_template = node->dfs()->store_template(owner, vector_template);
            if (!stored_template.has_value()) {
                std::printf("[node-run] vector template store failed (error %d)\n",
                            static_cast<int>(stored_template.error()));
                node->cleanUp();
                return 5;
            }
            const auto row =
                node->dfs()->store_vector(owner, owner, name, owner, stored_template->file_id);
            if (!row.has_value()) {
                std::printf("[node-run] vector creation failed (error %d)\n", static_cast<int>(row.error()));
                node->cleanUp();
                return 5;
            }
            std::size_t appended = 0;
            for (std::size_t index = 0; index < row_count; ++index) {
                DbRow entry;
                // use_id() makes "id" the primary field, and DfsVector::calculate_hash
                // reads it with .at() without checking — an absent id terminates the
                // process. The caller must supply it.
                entry["id"]       = name + "_" + std::to_string(index);
                entry["payload"]  = name + "_row_" + std::to_string(index);
                entry["position"] = std::to_string(index);
                if (node->dfs()->add_vector_row(owner, row->file_id, entry)) {
                    ++appended;
                }
            }
            std::printf("[node-run] DFS vector owner=%s file_id=%s rows=%zu/%zu\n",
                        owner.to_string().c_str(),
                        row->file_id.c_str(),
                        appended,
                        row_count);
            std::fflush(stdout);
            if (appended != row_count) {
                node->cleanUp();
                return 5;
            }

            // Multi-writer vectors (a chat is one): with EXC_DFS_VECTOR_CROSS=K every
            // node also appends K rows to every other node's vector, signed with its
            // own actor. The handle travels through the barrier directory; the vector
            // itself has to arrive over the network first (creation broadcast or
            // catalog sync), so each target is polled until it is readable here.
            const char* cross_env = std::getenv("EXC_DFS_VECTOR_CROSS");
            const auto  cross_rows =
                cross_env ? static_cast<std::size_t>(std::strtoull(cross_env, nullptr, 10)) : std::size_t(0);
            if (!barrier_directory.empty()) {
                (void)FileIo::write_atomic(barrier_directory / ("vector-" + std::to_string(node_index)),
                                           owner.to_string() + " " + row->file_id);
            }
            // Row removal (EXC_DFS_REMOVE_AFTER_S, shared with the doomed file): the
            // owner flips its row 0 to a tombstone after that many seconds. The
            // tombstone travels by DfsVectorAdd gossip and, when that is lost, by the
            // catalog digest (the vector's content hash changes), and every node
            // must end with row 0 at status 0.
            if (const char* remove_env = std::getenv("EXC_DFS_REMOVE_AFTER_S");
                remove_env != nullptr && std::strtoull(remove_env, nullptr, 10) > 0) {
                const auto remove_after = std::strtoull(remove_env, nullptr, 10);
                std::thread([node = node.get(), owner, file_id = row->file_id, name, remove_after]() {
                    for (unsigned long long waited = 0; waited < remove_after && stop_requested == 0; ++waited) {
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (stop_requested != 0) {
                        return;
                    }
                    const bool removed = node->dfs()->remove_vector_row(owner, file_id, name + "_0");
                    std::printf("[node-run] DFS vector row removed owner=%s file_id=%s id=%s ok=%d\n",
                                owner.to_string().c_str(),
                                file_id.c_str(),
                                (name + "_0").c_str(),
                                removed ? 1 : 0);
                    std::fflush(stdout);
                }).detach();
            }

            if (cross_rows > 0 && !barrier_directory.empty()) {
                std::size_t targets = 0, targets_done = 0;
                for (std::size_t other = 0; other < node_count; ++other) {
                    if (other == node_index) {
                        continue;
                    }
                    ++targets;
                    const auto handle_path = barrier_directory / ("vector-" + std::to_string(other));
                    std::string owner_text, file_id_other;
                    const auto  handle_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(90);
                    while (stop_requested == 0 && std::chrono::steady_clock::now() < handle_deadline) {
                        std::ifstream handle(handle_path);
                        if (handle >> owner_text >> file_id_other) {
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    }
                    if (owner_text.empty() || file_id_other.empty()) {
                        std::printf("[node-run] DFS cross target=%zu: no vector handle\n", other);
                        continue;
                    }
                    const ActorId owner_other(owner_text);
                    // Wait for the vector to be readable locally (it replicates over the network).
                    bool       readable      = false;
                    const auto ready_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
                    while (stop_requested == 0 && std::chrono::steady_clock::now() < ready_deadline) {
                        if (node->dfs()->read_vector_rows(owner_other, file_id_other).has_value()) {
                            readable = true;
                            break;
                        }
                        std::this_thread::sleep_for(std::chrono::seconds(1));
                    }
                    if (!readable) {
                        std::printf("[node-run] DFS cross target=%zu: vector never became readable\n", other);
                        continue;
                    }
                    std::size_t cross_appended = 0;
                    for (std::size_t index = 0; index < cross_rows; ++index) {
                        DbRow entry;
                        entry["id"]       = "from_" + std::to_string(node_index) + "_" + std::to_string(index);
                        entry["payload"]  = "cross_" + std::to_string(node_index) + "_" + std::to_string(index);
                        entry["position"] = std::to_string(1000 * (node_index + 1) + index);
                        if (node->dfs()->add_vector_row(owner_other, file_id_other, entry, owner)) {
                            ++cross_appended;
                        }
                    }
                    std::printf("[node-run] DFS cross target=%zu owner=%s rows=%zu/%zu\n",
                                other,
                                owner_text.c_str(),
                                cross_appended,
                                cross_rows);
                    std::fflush(stdout);
                    if (cross_appended == cross_rows) {
                        ++targets_done;
                    }
                }
                std::printf("[node-run] DFS cross done targets=%zu/%zu\n", targets_done, targets);
                std::fflush(stdout);
            }
        }

        // Every ExDFS load phase (file, vector, cross-writes) is over: tell the
        // stand. A chaos agent that hits before this cuts a publication short, and
        // a node it relaunches has none of the load knobs, so the vector would
        // never exist anywhere — a harness artifact, not a finding.
        if (!barrier_directory.empty()) {
            (void)FileIo::write_atomic(barrier_directory / ("loaded-" + std::to_string(node_index)), "ok");
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
                const auto submitted_at_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                                 std::chrono::steady_clock::now().time_since_epoch())
                                                 .count();
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
                std::printf("[node-run] intent hash=%s submitted_at_ms=%lld\n",
                            submitted.value().c_str(),
                            static_cast<long long>(submitted_at_ms));
                std::fflush(stdout);
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
