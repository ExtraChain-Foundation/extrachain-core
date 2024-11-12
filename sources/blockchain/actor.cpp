#include "blockchain/actor.h"

ActorId::ActorId() {
    m_id = "00000000000000000000";
}

ActorId::ActorId(const std::string &actorId) {
    m_id = actorId;
    normalize();
}

ActorId::ActorId(const ActorId &other) {
    m_id = other.m_id;
    // normalize();
}

ActorId::ActorId(ActorId &&other) noexcept {
    m_id = std::move(other.m_id);
    // normalize();
    other.m_id = "00000000000000000000";
}

QByteArray ActorId::toQByteArray() const {
    return QByteArray::fromStdString(m_id);
}

QString ActorId::toQString() const {
    return QString::fromStdString(m_id);
}

const std::string &ActorId::toString() const {
    return m_id;
}

bool ActorId::isZero() const {
    return m_id == "00000000000000000000" || m_id == "";
}

ActorId &ActorId::operator=(const std::string &actorId) {
    this->m_id = actorId;
    normalize();
    return *this;
}

ActorId &ActorId::operator=(const ActorId &actorId) {
    this->m_id = actorId.m_id;
    normalize();
    return *this;
}

ActorId &ActorId::operator=(ActorId &&other) noexcept {
    if (this != &other) {
        m_id = std::move(other.m_id);
        normalize();
        other.m_id = "00000000000000000000";
    }

    return *this;
}

namespace magic {
std::string custom_magic<ActorId>::read(const ActorId &value) {
    return value.toString();
}

ActorId custom_magic<ActorId>::write(const std::string &value) {
    return ActorId(value);
}
}
