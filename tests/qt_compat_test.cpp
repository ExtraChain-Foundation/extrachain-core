#include <cstdlib>
#include <iostream>

#include "adapters/qt/actor_id_adapter.h"

namespace {
    void require(bool condition, const char* message) {
        if (!condition) {
            std::cerr << "FAIL: " << message << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
} // namespace

int main() {
    const ActorId actor_id("abc");
    const auto    text  = ExtraChain::QtCompat::to_qstring(actor_id);
    const auto    bytes = ExtraChain::QtCompat::to_qbyte_array(actor_id);

    require(text.toStdString() == actor_id.to_string(), "QString conversion must preserve Actor ID bytes");
    require(bytes.toStdString() == actor_id.to_string(), "QByteArray conversion must preserve Actor ID bytes");
    require(ExtraChain::QtCompat::actor_id_from_qstring(text).to_string() == actor_id.to_string(),
            "QString Actor ID conversion must round trip");

    std::cout << "PASS: Qt Actor ID compatibility conversions\n";
}
