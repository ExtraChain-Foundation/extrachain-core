#include "network/packages/base_message.h"

using namespace Messages;
void BaseMessage::setMsgData(const QByteArray &data)
{
    this->data = data;
}

void BaseMessage::calcDigSig(const Actor<KeyPrivate> &actor)
{
    signer = actor.id();
    digSig = actor.key()->sign(concatenateAllData());
}

bool BaseMessage::verifyDigSig(const Actor<KeyPublic> &actor) const
{
    return actor.key()->verify(concatenateAllData(), digSig);
}

void BaseMessage::operator=(BaseMessage b)
{
    protocol = b.protocol;
    type = b.type;
    signer = b.signer;
    digSig = b.digSig;
    data = b.data;
}

// IMessage interface
void BaseMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void BaseMessage::operator=(QList<QByteArray> &list)
{
    if (list.size() >= 5)
    {
        protocol = list.takeFirst();
        type = list.takeFirst().toUInt();
        QByteArray signBytes = list.takeFirst();
        signer = BigNumber::isValid(signBytes) ? BigNumber(signBytes) : BigNumber();
        digSig = list.takeFirst();
        data = list.takeFirst();
    }
}

bool BaseMessage::isEmpty() const
{
    if (protocol.isEmpty() || type == 0 || data.isEmpty())
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
    QByteArray signeR;
    if (signer == 0)
        signeR = "";
    else
        signeR = signer.toActorId();
    l << protocol << QByteArray::number(type) << signeR << digSig << data;
    return l;
}

short BaseMessage::getFieldsCount() const
{
    return BaseMessage::FIELDS_COUNT;
}

QByteArray BaseMessage::serialize() const
{
    //    QByteArray serialized = "";
    return Serialization::serialize(serializedParams(), Messages::FIELD_SIZE);
    //    for (const QByteArray &param : serializedParams())
    //    {

    //        serialized += Utils::intToByteArray(param.size(), Messages::FIELD_SIZE);
    //        serialized += param;
    //    }

    //    return serialized;
}

void BaseMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = {};
    int pos = 0;
    for (int i = 0; i < getFieldsCount(); i++)
    {
        int count = Utils::qByteArrayToInt(serialized.mid(pos, Messages::FIELD_SIZE));
        pos += Messages::FIELD_SIZE;
        //        QByteArray el =
        list << serialized.mid(pos, count);
        pos += count;
    }
    if (list.size() < getFieldsCount())
    {
        qDebug() << "Error: can't deserialize message:" << serialized;
    }
    // QList<QByteArray> l = Serialization::deserialize(serialized);
    operator=(list);
}

const QByteArray BaseMessage::hash() const
{
    return Utils::calcKeccak(data);
}
