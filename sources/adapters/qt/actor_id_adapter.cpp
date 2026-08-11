#include "adapters/qt/actor_id_adapter.h"

namespace ExtraChain::QtCompat {

    QString to_qstring(const ActorId &actor_id) {
        return QString::fromStdString(actor_id.to_string());
    }

    QByteArray to_qbyte_array(const ActorId &actor_id) {
        return QByteArray::fromStdString(actor_id.to_string());
    }

    ActorId actor_id_from_qstring(QStringView actor_id) {
        return ActorId(actor_id.toString().toStdString());
    }

} // namespace ExtraChain::QtCompat
