// Temporary benchmark: measure find_last_control cost on an existing chain.
// Logs into <home>, then times find_last_control from several cold points.
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <thread>

#include "chain/dag.h"
#include "core/extrachain_node.h"
#include "utils/exc_utils.h"

namespace {
    const std::string LOGIN    = "gen-login";
    const std::string PASSWORD = "gen-password";
} // namespace

int main(int argc, char *argv[]) {
    if (argc < 2) {
        std::printf("usage: %s <home>\n", argv[0]);
        return 64;
    }
    std::error_code directory_error;
    std::filesystem::current_path(argv[1], directory_error);
    if (directory_error) {
        std::printf("cannot use node home %s: %s\n", argv[1], directory_error.message().c_str());
        return 73;
    }

    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, false, 17599);
    node->process();
    auto res = node->login(Utils::calculate_hash(LOGIN + PASSWORD));
    if (!res.has_value()) {
        std::printf("login failed %d\n", (int)res.error());
        return 2;
    }
    node->dag()->set_mode(DagMode::Full);

    auto cur = node->dag()->current_section();
    std::printf("current_section=%s\n", cur.to_string().c_str());

    // Give the background control-index rebuild (kicked from Dag::start) time to
    // finish before measuring the warm path.
    std::this_thread::sleep_for(std::chrono::seconds(3));

    auto bench = [&](const char *label, SectionId from) {
        const int N     = 50;
        auto      t0    = std::chrono::steady_clock::now();
        int       found = 0;
        for (int i = 0; i < N; i++) {
            auto c = node->dag()->find_last_control(from);
            if (c.has_value())
                found++;
        }
        auto us =
            std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count();
        std::printf("[bench] %s from=%s: %d calls, %lld us total, %.1f us/call (found=%d)\n",
                    label,
                    from.to_string().c_str(),
                    N,
                    (long long)us,
                    us / (double)N,
                    found);
        std::fflush(stdout);
    };

    // Cold deep, cold mid, near tip — worst case is a control far from a boundary hit.
    bench("cold-deep", SectionId(99)); // forces walk back to section 80
    bench("cold-mid", SectionId(10099));
    bench("near-tip", cur);
    bench("worst-gap", SectionId(19999)); // just under pack boundary, walk into pack

    // Consensus-equivalence: index must match the section's control field exactly,
    // for every control-aligned section across the whole chain.
    {
        long long checked = 0, mism = 0;
        for (SectionId i(0); i <= cur; i += 20) {
            auto                       sec = node->dag()->read_section(i);
            std::optional<std::string> from_section;
            if (sec.has_value() && sec->control.has_value())
                from_section = sec->control.value();
            auto                       from_index = node->dag()->read_control(i); // index-backed
            std::optional<std::string> idx;
            if (from_index.has_value())
                idx = from_index->control;
            checked++;
            if (from_section != idx) {
                mism++;
                if (mism <= 5)
                    std::printf("[verify] MISMATCH at %s: section=%s index=%s\n",
                                i.to_string().c_str(),
                                from_section.value_or("<none>").c_str(),
                                idx.value_or("<none>").c_str());
            }
        }
        std::printf("[verify] control-aligned sections checked=%lld mismatches=%lld\n", checked, mism);
    }

    node->cleanUp();
    return 0;
}
