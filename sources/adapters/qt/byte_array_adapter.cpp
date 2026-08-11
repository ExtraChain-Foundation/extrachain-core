#include "adapters/qt/byte_array_adapter.h"

#include <limits>
#include <stdexcept>

namespace ExtraChain::QtCompat {

    QByteArray to_qbyte_array(const ByteArray &bytes) {
        if (bytes.empty()) {
            return {};
        }
        if (bytes.size() > static_cast<std::size_t>(std::numeric_limits<qsizetype>::max())) {
            throw std::length_error("ByteArray is too large for QByteArray");
        }
        return QByteArray(reinterpret_cast<const char *>(bytes.data()), static_cast<qsizetype>(bytes.size()));
    }

    ByteArray byte_array_from_qbyte_array(QByteArrayView bytes) {
        if (bytes.empty()) {
            return ByteArray(std::vector<std::uint8_t> {});
        }
        return ByteArray(bytes.data(), static_cast<std::size_t>(bytes.size()));
    }

} // namespace ExtraChain::QtCompat
