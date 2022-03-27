#ifndef ACTORID_H
#define ACTORID_H
#include "extrachain_global.h"
#include "utils/bignumber.h"

class EXTRACHAIN_EXPORT ActorId {
public:
    ActorId() {
        m_id = "00000000000000000000";
    };

    ActorId(const QByteArray &actorId) {
        if (!actorId.isEmpty() && !BigNumber::isValid(actorId))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.isEmpty() ? actorId.toStdString() : "00000000000000000000";
        normalize();
    }

    ActorId(const std::string &actorId) {
        if (!actorId.empty() && !BigNumber::isValid(QByteArray::fromStdString(actorId)))
            qFatal("ActorId not valid"); // TODO: remove after tests

        m_id = !actorId.empty() ? actorId : "00000000000000000000";
        normalize();
    }

    ActorId &operator=(const QByteArray &actorId) {
        this->m_id = actorId.toStdString();
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

private:
    void normalize() {
        m_id = QByteArray("0").repeated(20 - m_id.length()).toStdString() + m_id;
    }

    std::string m_id;
};
#endif // ACTORID_H
