#include "network/packages/base_message.h"

using namespace Messages;
void BaseMessage::setMsgData(const QByteArray &data)
{
    this->msgData = data;
}

void BaseMessage::calcDigSig(const Actor<KeyPrivate> &actor)
{
    signer = actor.getId();
    digSig = actor.getKey()->sign(concatenateAllData());
}

bool BaseMessage::verifyDigSig(const Actor<KeyPublic> &actor) const
{
    return actor.getKey()->verify(concatenateAllData(), digSig);
}

void BaseMessage::operator=(BaseMessage b)
{
    protocol = b.protocol;
    msgType = b.msgType;
    signer = b.signer;
    digSig = b.digSig;
    msgData = b.msgData;
}

// IMessage interface
void BaseMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void BaseMessage::operator=(QList<QByteArray> &list)
{
    protocol = list.takeFirst();
    msgType = list.takeFirst();
    QByteArray signBytes = list.takeFirst();
    signer = BigNumber::isValid(signBytes) ? BigNumber(signBytes) : BigNumber();
    digSig = list.takeFirst();
    msgData = list.takeFirst();
}

bool BaseMessage::isEmpty()
{
    if (protocol.isEmpty() || msgType.isEmpty() || msgData.isEmpty())
        return true;
    else
        return false;
}

QByteArray BaseMessage::concatenateAllData() const
{
    QByteArray concatenatedData;
    for (QByteArray d : serializedParams())
    {
        // in entry data for digSig calculation we don't need digSig field
        if (d != digSig)
            concatenatedData += d;
    }
    return concatenatedData;
}

QList<QByteArray> BaseMessage::serializedParams() const
{
    QList<QByteArray> l;
    QByteArray signeR = signer == 0 ? signer.toByteArray() : signer.toActorId();
    l << protocol << msgType << signeR << digSig << msgData;
    return l;
}

short BaseMessage::getFieldsCount() const
{
    return BaseMessage::FIELDS_COUNT;
}

QByteArray BaseMessage::serialize() const
{
    QByteArray serialized = "";

    for (const QByteArray &param : serializedParams())
    {

        serialized += Utils::intToByteArray(param.size(), Messages::FIELD_SIZE);
        serialized += param;
    }

    return serialized;
}

void BaseMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = {};
    int pos = 0;
    for (int i = 0; i < getFieldsCount(); i++)
    {
        int count = Utils::qByteArrayToInt(serialized.mid(pos, Messages::FIELD_SIZE));
        pos += Messages::FIELD_SIZE;
        QByteArray el = serialized.mid(pos, count);
        pos += count;
        list << el;
    }
    if (list.size() < getFieldsCount())
    {
        qDebug() << "Error: can't deserialize message:" << serialized;
    }
    operator=(list);
}

const QByteArray BaseMessage::hash() const
{
    return Utils::calcKeccak(msgData);
}
