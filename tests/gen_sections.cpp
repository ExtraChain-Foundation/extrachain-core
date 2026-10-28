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

// Generate a funded chain for pack and synchronization tests.
// Usage: extrachain-gen-sections [N=25000] [workdir=./gen-data] [--no-index]
// Transactions pass ledger validation before storage. Network admission rate
// limits do not apply to offline fixture generation.

#include <charconv>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string_view>

#include "chain/actor.h"
#include "chain/actor_index.h"
#include "chain/dag.h"
#include "chain/transaction.h"
#include "managers/account_controller.h"
#include "core/extrachain_node.h"
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
    long long target = 25000;
    if (argc > 1) {
        const std::string_view count(argv[1]);
        const auto             parsed = std::from_chars(count.data(), count.data() + count.size(), target);
        if (parsed.ec != std::errc { } || parsed.ptr != count.data() + count.size() || target < 1
            || target > 10'000'000) {
            std::fprintf(stderr, "[Gen] N must be between 1 and 10000000\n");
            return 64;
        }
    }
    if (argc > 4 || (argc > 3 && std::string_view(argv[3]) != "--no-index"))
        return 64;
    const std::string workdir = (argc > 2) ? argv[2] : "gen-data";
    std::error_code   directory_error;
    const auto        created = std::filesystem::create_directories(workdir, directory_error);
    if (directory_error || (!created && !std::filesystem::is_empty(workdir, directory_error)) || directory_error) {
        std::fprintf(stderr, "[Gen] work directory must be new or empty: %s\n", workdir.c_str());
        return 1;
    }
    std::filesystem::current_path(workdir, directory_error);
    if (directory_error) {
        std::fprintf(stderr, "[Gen] cannot use work directory: %s\n", directory_error.message().c_str());
        return 1;
    }
    if (argc > 3 && std::string_view(argv[3]) == "--no-index") {
        ExtraChainSettings settings;
        settings.chain_index_mode = ChainIndexMode::Disabled;
        if (!Utils::write_settings(settings)) {
            eCritical("[Gen] cannot disable ChainIndex for the diagnostic run");
            return 1;
        }
    }

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 0);
    node->process();
    if (!node->create_new_network("gen-login", "gen-password")) {
        eCritical("[Gen] create_new_network failed");
        return 1;
    }

    auto             *dag   = node->dag();
    const auto        actor = node->account_controller()->system_actor();
    Actor<KeyPrivate> bank;
    bank.create(ActorType::User);
    if (!node->actor_index()->save_actor(bank.to_public()).has_value())
        return 1;
    const auto  token = TokenId::create("468faf2f1be6504a9a26f7f027f7e43380b0d77d").value();
    Transaction allocation;
    allocation.set_type(TransactionType::Balance);
    allocation.set_sender(actor.id());
    allocation.set_receiver(bank.id());
    allocation.set_token(token);
    allocation.set_section(SectionId(1));
    allocation.set_timestamp(0);
    allocation.set_amount(BigNumberFloat(std::to_string(target)));
    if (!allocation.sign(actor) || dag->prove_transaction(allocation, { }) != TransactionProveError::NoError
        || !dag->save_transaction(allocation)) {
        std::fprintf(stderr, "[Gen] initial allocation failed\n");
        return 1;
    }

    eLog("[Gen] Generating {} funded transfer sections, starting at section {}",
         target,
         dag->current_section().to_string());
    const auto                 t0       = std::chrono::steady_clock::now();
    long long                  ok       = 0;
    long long                  rejected = 0;
    std::chrono::nanoseconds   sign_time { };
    std::optional<Transaction> last_transaction;
    for (long long i = 0; i < target; ++i) {
        Transaction tx;
        tx.set_sender(bank.id());
        tx.set_receiver(actor.id());
        tx.set_amount(BigNumberFloat("0.0011"));
        tx.set_type(TransactionType::Regular);
        tx.set_token(token);
        tx.set_section(dag->current_section() + 1);
        tx.set_timestamp(static_cast<std::uint64_t>(i + 1));
        const auto sign_started = std::chrono::steady_clock::now();
        const auto signed_ok    = tx.sign(bank);
        sign_time += std::chrono::steady_clock::now() - sign_started;
        const auto proof = signed_ok ? dag->prove_transaction(tx, { }) : TransactionProveError::InvalidSignature;
        if (proof != TransactionProveError::NoError || !dag->save_transaction(tx)) {
            ++rejected;
            std::fprintf(stderr, "[Gen] tx %lld failed: %d\n", i, static_cast<int>(proof));
            break;
        }
        last_transaction = tx;
        ++ok;
        if ((i + 1) % 2000 == 0) {
            std::printf("[Gen] %lld/%lld committed\n", i + 1, target);
            std::fflush(stdout);
        }
    }

    auto secs =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count()
        / 1000.0;

    bool admission_checks = last_transaction.has_value();
    if (last_transaction.has_value()) {
        const auto section   = dag->read_section(last_transaction.value().section());
        const auto duplicate = section.has_value()
                                   ? dag->prove_transaction(last_transaction.value(), section.value().transactions)
                                   : TransactionProveError::NoError;
        auto       invalid   = last_transaction.value();
        invalid.set_section(dag->current_section() + 1);
        invalid.set_amount(BigNumberFloat("0.0012"));
        const auto rejected_invalid = dag->prove_transaction(invalid, { });
        admission_checks =
            duplicate == TransactionProveError::Duplicate && rejected_invalid == TransactionProveError::WrongHash;
        if (!admission_checks)
            std::fprintf(stderr,
                         "[Gen] probe errors: duplicate=%d invalid=%d\n",
                         static_cast<int>(duplicate),
                         static_cast<int>(rejected_invalid));
    }

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
                std::max(0.0, secs - std::chrono::duration<double>(sign_time).count()));
    std::printf("[Gen] current_section=%s, hot files=%zu, pack files=%zu\n",
                dag->current_section().to_string().c_str(),
                hot_files,
                pack_files);
    std::printf("[Gen] duplicate and invalid admission checks: %s\n", admission_checks ? "ok" : "FAILED");

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

    const int result = (ok == target && rejected == 0 && admission_checks) ? 0 : 2;
    node->cleanUp();
    return result;
}
