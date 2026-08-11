#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include "core/byte_array.h"

namespace ExtraChain::QtCompat {

    [[nodiscard]] QByteArray to_qbyte_array(const ByteArray &bytes);
    [[nodiscard]] ByteArray  byte_array_from_qbyte_array(QByteArrayView bytes);

} // namespace ExtraChain::QtCompat
