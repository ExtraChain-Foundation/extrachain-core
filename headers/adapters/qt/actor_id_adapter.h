#pragma once

#include <QByteArray>
#include <QString>
#include <QStringView>

#include "chain/actor_id.h"

namespace ExtraChain::QtCompat {

    [[nodiscard]] QString    to_qstring(const ActorId &actor_id);
    [[nodiscard]] QByteArray to_qbyte_array(const ActorId &actor_id);
    [[nodiscard]] ActorId    actor_id_from_qstring(QStringView actor_id);

} // namespace ExtraChain::QtCompat
