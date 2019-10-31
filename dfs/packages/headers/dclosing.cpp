#include "dclosing.h"


Message::DClosing::DClosing(const QByteArray &title_hash, const long long &pckAF)
    : DUMessage(type_closing)
{
    this->title_hash = title_hash;
    PckgAmoutR = pckAF;

}

Message::DClosing::DClosing(const QByteArray &serialized)
    : DUMessage(type_closing)
{
    QList<QByteArray> list = deserialize(serialized);
    if (type_closing != list.takeFirst().toInt())
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

Message::DClosing::~DClosing()
{

}

const QList<QByteArray> Message::DClosing::serializedParams() const
{
    QList<QByteArray> list;
    list << title_hash << QByteArray::number(PckgAmoutR);// << pckgUpsetSerialize();
    return list;
}
