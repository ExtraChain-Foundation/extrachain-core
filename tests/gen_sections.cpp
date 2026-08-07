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

// Load generator: brings up a real ExtraChainNode and floods it with self
// reward transactions to fill the DAG with many sections, so the hot -> pack
// machinery (and later sync/migration) can be exercised on realistic data.
//
//   ./extrachain-gen-sections [N] [workdir] [--no-index]
//
// N defaults to 25000 (enough to seal 2 packs: SECTIONS_PER_PACK=10000 plus the
// HOT_PACK_LAG=200 trailing window). workdir defaults to ./gen-data.
//
// Reward transactions are used on purpose: prove_transaction accepts a self
// reward (sender == receiver, amount <= 3, valid signature) and the per-sender
// rate guard only applies to Regular transactions, so no guard has to be removed.

#include <QCoreApplication>
#include <QDir>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string_view>

#include "chain/actor.h"
#include "chain/dag.h"
#include "chain/transaction.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h" // Responder
#include "utils/bignumber_float.h"
#include "utils/exc_logs.h"
#include "utils/exc_utils.h"

namespace {

    std::size_t count_files(const std::filesystem::path &dir) {
        std::error_code ec;
        if (!std::filesystem::exists(dir, ec))
            return 0;
        std::size_t n = 0;
        for (auto &e : std::filesystem::directory_iterator(dir, ec)) {
            if (e.is_regular_file())
                ++n;
        }
        return n;
    }

} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    const long long   target  = (argc > 1) ? std::atoll(argv[1]) : 25000;
    const std::string workdir = (argc > 2) ? argv[2] : "gen-data";

    // Fresh working directory — create_new_network refuses to run if a profile
    // already exists. Data is cwd-relative, so chdir into it first.
    std::filesystem::remove_all(workdir);
    std::filesystem::create_directories(workdir);
    QDir::setCurrent(QString::fromStdString(workdir));
    Utils::wipeDataFiles();
    if (argc > 3 && std::string_view(argv[3]) == "--no-index") {
        ExtraChainSettings settings;
        settings.chain_index_mode = ChainIndexMode::Disabled;
        if (!Utils::write_settings(settings)) {
            eCritical("[Gen] cannot disable ChainIndex for the diagnostic run");
            return 1;
        }
    }

    ExtraChainNodeWrapper wrapper(&app, /*is_client*/ false, /*is_custom*/ false, /*port*/ 0);
    wrapper.init(/*makeAsync*/ false); // runs node->process() synchronously
    auto *node = wrapper.node;
    if (!node->create_new_network("gen-login", "gen-password")) {
        eCritical("[Gen] create_new_network failed");
        return 1;
    }

    auto         *dag   = node->dag();
    auto          actor = node->account_controller()->system_actor();
    const TokenId reward_token("468faf2f1be6504a9a26f7f027f7e43380b0d77d");

    eLog("[Gen] Generating {} reward sections, starting at section {}",
         target,
         dag->current_section().to_string());

    auto                     t0       = std::chrono::steady_clock::now();
    long long                ok       = 0;
    long long                rejected = 0;
    std::chrono::nanoseconds sign_time {};
    std::chrono::nanoseconds admission_time {};

    for (long long i = 0; i < target; ++i) {
        Transaction tx;
        tx.set_sender(actor.id());
        tx.set_receiver(actor.id());
        tx.set_amount(BigNumberFloat("0.0011")); // <= 3, > 0
        tx.set_type(TransactionType::Reward);
        tx.set_token(reward_token);
        tx.set_section(dag->current_section() + 1);
        const auto sign_started = std::chrono::steady_clock::now();
        const auto signed_ok    = tx.sign(actor);
        sign_time += std::chrono::steady_clock::now() - sign_started;
        if (!signed_ok) {
            ++rejected;
            continue;
        }

        Responder  responder(node->network());
        const auto admission_started = std::chrono::steady_clock::now();
        auto       res               = dag->network_transaction(tx, responder);
        admission_time += std::chrono::steady_clock::now() - admission_started;
        if (res.has_value()) {
            ++ok;
        } else {
            ++rejected;
            if (rejected <= 5) {
                eWarning("[Gen] tx {} rejected: {}", i, static_cast<int>(res.error()));
            }
        }

        if ((i + 1) % 2000 == 0) {
            std::printf("[Gen] %lld/%lld done, current section %s\n",
                        i + 1,
                        target,
                        dag->current_section().to_string().c_str());
            std::fflush(stdout);
        }
    }

    auto secs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()
        / 1000.0;

    // Report on-disk state.
    std::size_t hot_files  = count_files(ChainConst::DAG_HOT_FOLDER);
    std::size_t pack_files = count_files(ChainConst::DAG_PACKS_FOLDER);

    std::printf("\n[Gen] Done: saved %lld, rejected %lld, in %.1fs (%.0f/s)\n",
                ok,
                rejected,
                secs,
                secs > 0 ? ok / secs : 0.0);
    std::printf("[Gen] timing: sign %.1fs, admission/store %.1fs\n",
                std::chrono::duration<double>(sign_time).count(),
                std::chrono::duration<double>(admission_time).count());
    std::printf("[Gen] current_section=%s, hot files=%zu, pack files=%zu\n",
                dag->current_section().to_string().c_str(),
                hot_files,
                pack_files);

    // Spot-check: a packed section and a hot section read back correctly.
    if (dag->current_section() > SectionId(0)) {
        auto packed = dag->read_section(SectionId(5));
        auto hot    = dag->read_section(dag->current_section());
        std::printf("[Gen] read section 5: %s, read hot section %s: %s\n",
                    packed.has_value() ? "ok" : "MISSING",
                    dag->current_section().to_string().c_str(),
                    hot.has_value() ? "ok" : "MISSING");
    }
    std::fflush(stdout);

    return (ok > 0) ? 0 : 2;
}
