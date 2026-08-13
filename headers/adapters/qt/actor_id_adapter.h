#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include "adapters/qt/qt_compat_global.h"
#include "chain/actor_id.h"

namespace ExtraChain::QtCompat {

    [[nodiscard]] EXTRACHAIN_QT_EXPORT QString    to_qstring(const ActorId &actor_id);
    [[nodiscard]] EXTRACHAIN_QT_EXPORT QByteArray to_qbyte_array(const ActorId &actor_id);
    [[nodiscard]] EXTRACHAIN_QT_EXPORT ActorId    actor_id_from_qstring(QStringView actor_id);

} // namespace ExtraChain::QtCompat
