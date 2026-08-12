#include <cstdlib>
#include <iostream>

#include "adapters/qt/actor_id_adapter.h"
#include "adapters/qt/byte_array_adapter.h"
#include "utils/variant_model.h"

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

    const ByteArray binary(std::vector<std::uint8_t> { 0, 1, 127, 128, 255 });
    const auto      qt_binary = ExtraChain::QtCompat::to_qbyte_array(binary);
    require(qt_binary.size() == static_cast<qsizetype>(binary.size()),
            "QByteArray conversion must preserve binary size");
    require(ExtraChain::QtCompat::byte_array_from_qbyte_array(qt_binary) == binary,
            "QByteArray conversion must preserve binary bytes");
    const ByteArray empty_binary(std::vector<std::uint8_t> {});
    require(ExtraChain::QtCompat::to_qbyte_array(empty_binary).isEmpty(),
            "Empty ByteArray conversion must stay empty");
    require(ExtraChain::QtCompat::byte_array_from_qbyte_array(QByteArray()).empty(),
            "Empty QByteArray conversion must stay empty");

    VariantModel model(nullptr, { "name", "value" });
    model.append({ { "name", "first" }, { "value", 1 } });
    model.append({ { "name", "second" }, { "value", 2 } });
    model.remove(1, 8);
    require(model.count() == 1, "Variant model removal must clamp the requested range");
    require(model.get(0).value("name").toString() == "first", "Variant model must preserve remaining rows");
    require(!model.setData(model.index(0), "invalid", Qt::UserRole + 99),
            "Variant model must reject an unknown role");

    std::cout << "PASS: Qt compatibility conversions\n";
}
