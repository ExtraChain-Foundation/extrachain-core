#ifndef ACTORID_H
#define ACTORID_H

#include "extrachain_global.h"
#include "utils/bignumber.h"

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId() {
        m_id = "00000000000000000000";
    };

    ActorId(const std::string &actorId) {
#ifdef QT_DEBUG
        if (!actorId.empty() && !BigNumber::isValid(QByteArray::fromStdString(actorId)))
            qFatal("ActorId not valid");
#endif

        m_id = !actorId.empty() ? actorId : "00000000000000000000";
        normalize();
    }

    ActorId &operator=(const std::string &actorId) {
        this->m_id = actorId;
        normalize();
        return *this;
    }

    bool operator==(const ActorId &actorId) const {
        return m_id == actorId.m_id;
    }

    bool operator!=(const ActorId &actorId) const {
        return m_id != actorId.m_id;
    }

    bool operator<(const ActorId &actorId) const {
        return m_id < actorId.m_id;
    }

    QByteArray toByteArray() const {
        return QByteArray::fromStdString(m_id);
    }

    QString toString() const {
        return QString::fromStdString(m_id);
    }

    const std::string &toStdString() const {
        return m_id;
    }

    bool isEmpty() const {
        if (m_id == "000000000000000000-1")
            qFatal("ActorId: WTF");
        return m_id.empty() || m_id == "00000000000000000000";
    }

    friend QDebug operator<<(QDebug d, const ActorId &actorId) {
        d.noquote().nospace() << actorId.toByteArray();
        return d;
    }

    static bool empty(const std::string &actorId) {
        ActorId actor(actorId);
        return actor.isEmpty();
    }

    template <typename Packer>
    void msgpack_pack(Packer &msgpack_pk) const {
        msgpack_pk.pack_str(m_id.size());
        msgpack_pk.pack_str_body(m_id.data(), m_id.size());
    }

    void msgpack_unpack(msgpack::object const &msgpack_o) {
        m_id = msgpack_o.as<std::string>();
    }

private:
    void normalize() {
        m_id = QByteArray("0").repeated(20 - m_id.length()).toStdString() + m_id;
    }

    std::string m_id;
};

#endif // ACTORID_H
