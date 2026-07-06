// Does my node's stored decimal amount round-trip back to the old node's hex form?
// If yes, calculate_hash_hex() reproduces the data the old node signed, so the
// legacy branch of tx.verify() succeeds — i.e. nothing is actually broken.
#include <QCoreApplication>
#include <cstdio>

#include "utils/bignumber_float.h"
#include "utils/exc_utils.h"

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    struct Case { const char *dec; const char *expect_hex; };
    Case cases[] = {
        {"1.123", "1.7b"},                  // section 4 from 57
        {"2.508365007279145217569479552672250155503303792747469886211086",
         "2.50fcba534d2c94a5c94d5c8fb30c18ef09be66ff4e41e4440e"}, // section 3
        {"0", "0"},
        {"2.5", nullptr},
    };

    for (auto &c : cases) {
        BigNumberFloat v(c.dec);
        std::string h = v.to_hex_string();
        // and back
        BigNumberFloat back = BigNumberFloat::from_hex(h);
        std::string d2 = back.to_string();
        bool hex_ok = c.expect_hex ? (h == c.expect_hex) : true;
        bool rt_ok  = (d2 == std::string(c.dec));
        std::printf("dec=%s -> hex=%s (expect %s: %s) -> dec=%s roundtrip=%s\n",
                    c.dec, h.c_str(),
                    c.expect_hex ? c.expect_hex : "(any)",
                    hex_ok ? "OK" : "MISMATCH",
                    d2.c_str(), rt_ok ? "OK" : "BROKEN");
    }
    return 0;
}
