#include "dclosing.h"

DFSMessage::DClosing::DClosing(const QByteArray &title_hash, const long long &pckAF)
    : DUMessage(dfsMessageType::closingMessage)
{
    this->title_hash = title_hash;
    PckgAmoutR = pckAF;
}

DFSMessage::DClosing::DClosing(const QByteArray &serialized)
    : DUMessage(dfsMessageType::closingMessage)
{
    QList<QByteArray> list = deserialize(serialized);
    if (dfsMessageType::closingMessage != list.takeFirst().toInt())
    {
        qDebug() << "[type_closing]"
                 << "incorrect message type";
    }
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "title_message_struct << incorrect input data";
        return;
    }
    title_hash = list.takeFirst();
    PckgAmoutR = list.takeFirst().toLongLong();
    //    pckgUpset = pckgUpsetDeserialize(list.takeFirst());
}

DFSMessage::DClosing::~DClosing()
{
}

const QList<QByteArray> DFSMessage::DClosing::serializedParams() const
{
    QList<QByteArray> list;
    list << title_hash << QByteArray::number(PckgAmoutR); // << pckgUpsetSerialize();
    return list;
}
