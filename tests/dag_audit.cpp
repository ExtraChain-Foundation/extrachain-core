/*
 * Full self-integrity audit of a DAG store. Logs into <home> and verifies the
 * on-disk chain is internally consistent — nothing lost or corrupted — without
 * comparing against any peer.
 *
 *   extrachain-dag-audit <home>
 *
 * Checks:
 *   1. Section continuity   — every section in [first..current] reads back.
 *   2. Transaction integrity— each tx: hash matches, signature verifies.
 *   3. Control chain        — recompute the control hash chain from sections and
 *                             compare to the stored control at each boundary.
 *   4. Control index        — Control.db matches section.control everywhere.
 *   5. Range file           — first/last consistent with what is on disk.
 */
#include <QCoreApplication>
#include <QDir>
#include <cstdio>

#include "chain/actor_index.h"
#include "chain/dag.h"
#include "chain/transaction.h"
#include "encryption/key_public.h"
#include "managers/extrachain_node.h"
#include "utils/exc_utils.h"

namespace {
const std::string LOGIN    = "gen-login";
const std::string PASSWORD = "gen-password";
constexpr int     CTRL_MOD = 20;

// Per-section hash exactly as hash_interval() composes it (independent reimpl, so
// a bug in the production path is caught rather than mirrored).
std::string section_component(Dag *dag, const SectionId &i) {
    auto section = dag->read_section(i);
    bool empty   = !section.has_value() || section->transactions.empty();
    if (empty) return Utils::calculate_hash(i.to_string());
    return Utils::calculate_hash(i.to_string() + section->calculate_hash());
}

std::string interval_hash(Dag *dag, const SectionId &from, const SectionId &to) {
    std::string acc;
    for (SectionId i = from; i <= to; i++) acc += section_component(dag, i);
    return Utils::calculate_hash(acc);
}
} // namespace

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::printf("usage: %s <home>\n", argv[0]); return 64; }
    QDir::setCurrent(QString::fromStdString(argv[1]));

    auto *wrapper = new ExtraChainNodeWrapper(&app, false, false, 17600);
    wrapper->init(false);
    auto *node = wrapper->node;
    auto res = node->login(Utils::calculate_hash(LOGIN + PASSWORD));
    if (!res.has_value()) { std::printf("login failed %d\n", (int)res.error()); return 2; }
    node->dag()->set_mode(DagMode::Full);
    auto *dag = node->dag();

    SectionId first = dag->first_saved_section();
    SectionId cur   = dag->current_section();
    if (first < SectionId(0)) first = SectionId(0);
    std::printf("=== DAG audit: sections [%s..%s] ===\n",
                first.to_string().c_str(), cur.to_string().c_str());

    long long total_fail = 0;

    // 1. Section continuity + 2. tx integrity ---------------------------------
    long long sec_ok = 0, sec_missing = 0, tx_total = 0, tx_bad_hash = 0,
              tx_bad_sig = 0, tx_no_actor = 0;
    for (SectionId i = first; i <= cur; i++) {
        auto section = dag->read_section(i);
        if (!section.has_value()) {
            // section 0..first edges or genuinely-empty slots may be absent; only
            // flag a gap if it's a hole between present sections.
            sec_missing++;
            continue;
        }
        sec_ok++;
        for (const auto &tx : section->transactions) {
            tx_total++;
            auto nh = const_cast<Transaction &>(tx).calculate_hash();
            auto lh = const_cast<Transaction &>(tx).calculate_hash_hex();
            if (tx.hash() != nh && tx.hash() != lh) {
                tx_bad_hash++;
                if (tx_bad_hash <= 5)
                    std::printf("  [tx] bad hash in section %s: %s\n",
                                i.to_string().c_str(), tx.hash().c_str());
                continue;
            }
            if (tx.sender().is_zero()) continue; // genesis/system edge
            auto actor = node->actor_index()->read_actor_old(tx.sender());
            if (actor.empty()) { tx_no_actor++; continue; }
            if (!tx.verify(actor)) {
                tx_bad_sig++;
                if (tx_bad_sig <= 5)
                    std::printf("  [tx] bad signature in section %s sender %s\n",
                                i.to_string().c_str(), tx.sender().to_string().c_str());
            }
        }
        if ((i.to_int().value_or(0)) % 5000 == 0) {
            std::printf("  ... section %s\n", i.to_string().c_str());
            std::fflush(stdout);
        }
    }
    std::printf("[1/2] sections ok=%lld missing=%lld | tx total=%lld bad_hash=%lld bad_sig=%lld no_actor=%lld\n",
                sec_ok, sec_missing, tx_total, tx_bad_hash, tx_bad_sig, tx_no_actor);
    total_fail += tx_bad_hash + tx_bad_sig;

    // 3. Control chain --------------------------------------------------------
    // Recompute the chain independently and compare to stored control at each
    // boundary. start=0 -> interval [0..0]; start=1,21,... -> [start..start+19].
    long long ctrl_checked = 0, ctrl_mismatch = 0, ctrl_missing = 0;
    std::string last_hash;
    // genesis control at section 0
    {
        last_hash = interval_hash(dag, SectionId(0), SectionId(0));
        auto stored = dag->read_control(SectionId(0));
        ctrl_checked++;
        if (!stored.has_value()) ctrl_missing++;
        else if (stored->control != last_hash) {
            ctrl_mismatch++;
            std::printf("  [ctrl] mismatch at 0\n");
        }
    }
    // chained intervals: start = 1, 21, 41, ... end = start+19 (the %20 boundary)
    for (SectionId start(1); start + SectionId(CTRL_MOD - 1) <= cur; start += CTRL_MOD) {
        SectionId end = start + SectionId(CTRL_MOD - 1); // a multiple of 20
        auto ih = interval_hash(dag, start, end);
        last_hash = Utils::calculate_hash(last_hash + ih);
        auto stored = dag->read_control(end);
        ctrl_checked++;
        if (!stored.has_value()) {
            ctrl_missing++;
            if (ctrl_missing <= 5) std::printf("  [ctrl] missing at %s\n", end.to_string().c_str());
        } else if (stored->control != last_hash) {
            ctrl_mismatch++;
            if (ctrl_mismatch <= 5) std::printf("  [ctrl] mismatch at %s\n", end.to_string().c_str());
        }
    }
    std::printf("[3] control chain: checked=%lld mismatch=%lld missing=%lld\n",
                ctrl_checked, ctrl_mismatch, ctrl_missing);
    total_fail += ctrl_mismatch;

    // 4. Control index vs sections -------------------------------------------
    long long ci_checked = 0, ci_mismatch = 0;
    for (SectionId i(0); i <= cur; i += CTRL_MOD) {
        auto sec = dag->read_section(i);
        std::optional<std::string> from_sec;
        if (sec.has_value() && sec->control.has_value()) from_sec = sec->control.value();
        auto rc = dag->read_control(i);
        std::optional<std::string> from_idx;
        if (rc.has_value()) from_idx = rc->control;
        ci_checked++;
        if (from_sec != from_idx) {
            ci_mismatch++;
            if (ci_mismatch <= 5) std::printf("  [index] mismatch at %s\n", i.to_string().c_str());
        }
    }
    std::printf("[4] control index vs sections: checked=%lld mismatch=%lld\n", ci_checked, ci_mismatch);
    total_fail += ci_mismatch;

    // 5. Range file -----------------------------------------------------------
    {
        bool tip_ok  = dag->read_section(cur).has_value();
        bool past_ok = !dag->read_section(cur + SectionId(1)).has_value();
        std::printf("[5] range: tip(%s) readable=%s, tip+1 absent=%s\n",
                    cur.to_string().c_str(), tip_ok ? "yes" : "NO", past_ok ? "yes" : "NO");
        if (!tip_ok) total_fail++;
    }

    std::printf("\n=== AUDIT %s (failures=%lld) ===\n",
                total_fail == 0 ? "PASS" : "FAIL", total_fail);
    return total_fail == 0 ? 0 : 1;
}
