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

#include <QCoreApplication>
#include <QDir>
#include <QTimer>

#include <cstdio>
#include <filesystem>
#include <vector>

#include "chain/dag.h"
#include "dfs/dfs_controller.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {
    const std::string LOGIN    = "gen-login";
    const std::string PASSWORD = "gen-password";
} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    if (argc < 3) {
        std::printf(
            "usage: %s serve <home> [listen-port] [dfs-payload-bytes] | "
            "join <home> <peer-ip> <target-section> [listen-port] [peer-port] "
            "[dfs-owner] [dfs-name] [dfs-payload-bytes]\n",
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
    }

    QDir::setCurrent(QString::fromStdString(home)); // node data is cwd-relative

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

    auto *wrapper = new ExtraChainNodeWrapper(&app, /*is_client*/ false, /*is_custom*/ false, listen_port);
    wrapper->init(/*makeAsync*/ false); // runs ExtraChainNode::process() synchronously
    auto *node = wrapper->node;

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

            const auto &owner = node->account_controller()->system_actor().id();
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
        return app.exec(); // listen + serve forever (killed by the harness)
    }

    if (mode == "join") {
        const long long   target     = std::atoll(argv[4]);
        const auto        peer_port  = static_cast<std::uint16_t>(argc > 6 ? std::atoi(argv[6]) : 17593);
        const QString     peer_ip    = QString::fromUtf8(argv[3]);
        const bool        verify_dfs = argc > 9;
        const ActorId     dfs_owner  = verify_dfs ? ActorId(argv[7]) : ActorId();
        const std::string dfs_name   = verify_dfs ? argv[8] : std::string();
        const auto        expected_dfs_size =
            verify_dfs ? static_cast<std::uintmax_t>(std::strtoull(argv[9], nullptr, 10)) : 0;

        // A joining node must have its OWN distinct identity — reusing the core's
        // profile makes the core drop our requests as self-messages (init_sender_id
        // == its own system actor). Create a fresh profile (own seed, no genesis);
        // the genesis + chain arrive via sync. create_profile() also starts the node.
        auto unique = Utils::calculate_hash(home + ":joiner");
        node->account_controller()->create_profile(unique, ActorType::User);
        node->dag()->set_mode(DagMode::Full);

        std::printf("[node-run] joining %s, target section %lld\n", argv[3], target);
        std::fflush(stdout);

        // Kick off the outbound connection to the peer (reconnect timer is not
        // wired in this build path, so dial explicitly).
        QTimer::singleShot(500, [node, peer_ip, peer_port]() {
            node->network()->connect_to_endpoint(peer_ip, peer_port, true, true);
        });

        // Poll sync progress; exit when we reach the target or time out.
        auto *poll     = new QTimer(&app);
        auto *deadline = new QTimer(&app);
        int   ticks    = 0;
        QObject::connect(poll, &QTimer::timeout, [&]() {
            auto cur   = node->dag()->current_section();
            auto conns = node->network()->active_connections_count();
            std::printf("[node-run] t=%ds conns=%d current_section=%s status=%d\n",
                        ticks * 3,
                        conns,
                        cur.to_string().c_str(),
                        static_cast<int>(node->dag()->status()));
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
                app.exit(0);
            }
            if (conns == 0) {
                if (ticks % 3 == 0) {
                    node->network()->connect_to_endpoint(peer_ip, peer_port, true, true);
                }
                return;
            }
            // Connected. A server-type node stays Ready and start_check() bails on
            // Ready, so kick the client-style pull explicitly. start_sync() guards
            // itself once a sync is already in flight.
            node->dag()->start_sync();
        });
        poll->start(3000);

        QObject::connect(deadline, &QTimer::timeout, [&]() {
            std::printf("[node-run] TIMEOUT at current_section=%s\n",
                        node->dag()->current_section().to_string().c_str());
            std::fflush(stdout);
            app.exit(1);
        });
        deadline->setSingleShot(true);
        deadline->start(240000); // 4 min

        const int exit_code = app.exec();
        // Destroy the node while Qt can still process queued shutdown work.
        // If the wrapper stays parented to QCoreApplication, child destruction
        // starts after the event dispatcher is gone. Active network timers can
        // then access invalid Qt state during a normal test exit.
        std::printf("[node-run] stopping node\n");
        std::fflush(stdout);
        delete wrapper;
        std::printf("[node-run] node stopped\n");
        std::fflush(stdout);
        return exit_code;
    }

    std::printf("[node-run] unknown mode '%s'\n", mode.c_str());
    return 64;
}
