#include "network/packages/service/chat_message.h"

using namespace Messages;

ChatMessage::ChatMessage(const QByteArray &message) {
//    initFields(Serialization::deserialize(message, Serialization::NET_MESSAGE_FIELD_SPLITTER));
    QList<QByteArray> newList = Serialization::deserialize(message, Serialization::NET_MESSAGE_HEADER_FIELD_SPLITTER);
    initFields(newList);
}

short ChatMessage::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + 2;
}

void ChatMessage::initFields(QLinkedList<QByteArray> &list)
{
    list.removeLast();
    recipient = BigNumber(list.takeLast());
    message = QByteArray(list.takeLast());
    BaseMessage::initFields(list);
}

void ChatMessage::initFields(QList<QByteArray> &list)
{
    list.removeLast();
    recipient = BigNumber(list.takeLast());
    message = QByteArray(list.takeLast());
    BaseMessage::initFields(list);
}


QList<QByteArray> ChatMessage::serializedParams() const
{
    QList<QByteArray> l = BaseMessage::serializedParams();
    l << message;
    l << recipient.toActorId();
    qDebug() << "MESSAGES::CHATMESSAGE: serializedParams(): " << l;
    return l;
}

QByteArray  ChatMessage::getMessage() const
{
    return message;
}

//QByteArray ChatMessage::serialize() const
//{
//    return QByteArray("test message");
//}
