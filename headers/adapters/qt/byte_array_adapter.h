#pragma once

#include <QByteArray>
#include <QByteArrayView>

#include "adapters/qt/qt_compat_global.h"
#include "core/byte_array.h"

namespace ExtraChain::QtCompat {

    [[nodiscard]] EXTRACHAIN_QT_EXPORT QByteArray to_qbyte_array(const ByteArray &bytes);
    [[nodiscard]] EXTRACHAIN_QT_EXPORT ByteArray  byte_array_from_qbyte_array(QByteArrayView bytes);

} // namespace ExtraChain::QtCompat
