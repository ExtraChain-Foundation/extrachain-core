#include "network/packages/base_message.h"

using namespace Messages;

QByteArray BaseMessage::getDigSig() const
{
    return digSig;
}

QByteArray BaseMessage::getMsg_data() const
{
    return msg_data;
}

BaseMessage::BaseMessage()
    : IMessage()

{
}

BaseMessage::BaseMessage(const BaseMessage &msg)
    : BaseMessage()
{
    protocol = msg.protocol;
    msgType = msg.msgType;
    signer = msg.signer;
    digSig = msg.digSig;
}

BaseMessage::BaseMessage(const QByteArray &msgType)
    : BaseMessage()
{
    this->msgType = msgType;
}

BaseMessage::~BaseMessage()
{
}

void BaseMessage::initFields(QLinkedList<QByteArray> &list)
{
    protocol = list.takeFirst();
    msgType = list.takeFirst();
    signer = BigNumber(list.takeLast());
    digSig = list.takeFirst();
}

void BaseMessage::initFields(QList<QByteArray> &list)
{
    auto list2 = list; // for debuuger
    protocol = list.takeFirst();
    msgType = list.takeFirst();
    QByteArray signBytes = list.takeFirst();
    signer = BigNumber::isValid(signBytes) ? BigNumber(signBytes) : BigNumber();
    digSig = list.takeFirst();
    msg_data = list.takeFirst();
}

short BaseMessage::getFieldsCount() const
{
    return FIELDS_COUNT + IMessage::FIELDS_COUNT;
}

QList<QByteArray> BaseMessage::serializedParams() const
{
    QList<QByteArray> l;
    QByteArray signeR = signer == 0 ? signer.toByteArray() : signer.toActorId();
    l << protocol << msgType << signeR << digSig << msg_data;
    return l;
}

void BaseMessage::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = {};
    int pos = 0;
    for (int i = 0; i < getFieldsCount(); i++)
    {
        int count = Utils::qByteArrayToInt(serialized.mid(pos, Messages::FIELD_SIZES));
        pos += Messages::FIELD_SIZES;
        QByteArray el = serialized.mid(pos, count);
        pos += count;
        list << el;
    }
    //    serialized.remove(0, pos);
    if (list.size() < getFieldsCount())
    {
        qDebug() << "Error: can't deserialize message:" << serialized;
    }
    initFields(list);
}

QList<QByteArray> BaseMessage::deserializeToList(const QByteArray &serialized)
{
    QList<QByteArray> list = {};
    int pos = 0;
    for (int i = 0; i < getFieldsCount(); i++)
    {
        int count = Utils::qByteArrayToInt(serialized.mid(pos, Messages::FIELD_SIZES));
        pos += Messages::FIELD_SIZES;
        QByteArray el = serialized.mid(pos, count);
        pos += count;
        list << el;
    }
    //    serialized.remove(0, pos);
    if (list.size() < getFieldsCount())
    {
        qDebug() << "Error: can't deserialize message:" << serialized;
    }
    return list;
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

// IMessage

QByteArray BaseMessage::serialize() const
{
    QByteArray serialized = "";

    for (const QByteArray &param : serializedParams())
    {

        serialized += Utils::intToByteArray(param.size(), Messages::FIELD_SIZES);
        serialized += param;
    }

    return serialized;
}

QByteArray BaseMessage::serialize(const QList<QByteArray> &list) const
{
    QByteArray serialized = "";
    for (const QByteArray &param : list)
    {
        serialized += Utils::intToByteArray(param.size(), Messages::FIELD_SIZES);
        serialized += param;
    }
    return serialized;
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

BaseMessage BaseMessage::deserializeMsg(const QByteArray serialized)
{
    BaseMessage b;
    b.deserialize(serialized);
    return b;
}

const QByteArray BaseMessage::hash() const
{
    return Utils::calcKeccak(msg_data);
}

void BaseMessage::init(const QByteArray &data)
{
    this->msg_data = data;
}

// Getters

QByteArray BaseMessage::getProtocol() const
{
    return protocol;
}

QByteArray BaseMessage::getMsgType() const
{
    return msgType;
}

BigNumber BaseMessage::getSigner() const
{
    return signer;
}
