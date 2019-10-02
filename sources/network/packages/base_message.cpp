#include "network/packages/base_message.h"

using namespace Messages;

QByteArray BaseMessage::getDigSig() const
{
    return digSig;
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
    signer = BigNumber::fromByteArray(list.takeLast());
    digSig = list.takeFirst();
}

void BaseMessage::initFields(QList<QByteArray> &list)
{
    protocol = list.takeFirst();
    msgType = list.takeFirst();
    signer = BigNumber(list.takeFirst());
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
    l << protocol << msgType << signer.serialize() << digSig << msg_data;
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
        if (d == digSig)
            continue;
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
    digSig = actor.getKey()->sign(concatenateAllData()).toBase64();
    qDebug() << "pubk: " << actor.convertToPublic().getKey()->extractPublicKey();
}

bool BaseMessage::verifyDigSig(const Actor<KeyPublic> &actor) const
{
    qDebug() << actor.getKey()->getPublicKey();
    return actor.getKey()->verify(concatenateAllData(), QByteArray::fromBase64(digSig));
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

const QByteArray BaseMessage::init(const QByteArray &data)
{
    this->msg_data = data;
    QByteArray result = serialize();

    return result;
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

// BaseMessage Response
short BaseMessageResponse::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + FIELDS_COUNT;
}

void BaseMessageResponse::initFields(QList<QByteArray> &list)
{
    BaseMessage::initFields(list);
    dataHash = list.takeFirst();
}

QList<QByteArray> BaseMessageResponse::serializedParams() const
{
    QList<QByteArray> list = BaseMessage::serializedParams();
    list << dataHash;
    return list;
}

BaseMessageResponse::BaseMessageResponse(const BaseMessageResponse &temp)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    dataHash = temp.dataHash;
}

BaseMessageResponse::~BaseMessageResponse()
{
}

BaseMessageResponse BaseMessageResponse::operator=(const BaseMessageResponse &temp)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    dataHash = temp.dataHash;
    return *this;
}

void BaseMessageResponse::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list = deserializeToList(serialized);
    this->initFields(list);
}

const QByteArray BaseMessageResponse::hash() const
{
    return dataHash;
}
