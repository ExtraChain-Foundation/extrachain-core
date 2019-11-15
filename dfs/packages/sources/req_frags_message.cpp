#include "dfs/packages/headers/req_frags_message.h"

QString DFSMessage::req_frags_message::getFilePath() const
{
    return filePath;
}

QList<QByteArray> DFSMessage::req_frags_message::getListFrag() const
{
    return listFrag;
}

DFSMessage::req_frags_message::req_frags_message(const QByteArray &filePath, QList<QByteArray> listFrag)
    : DUMessage(type_req_frags)
{
    this->filePath = filePath;
    this->listFrag = listFrag;
}

DFSMessage::req_frags_message::req_frags_message(const QByteArray &serialized)
    : DUMessage(type_req_frags)
{
    QList<QByteArray> list = deserialize(serialized);
    if (type_req_frags != list.takeFirst().toInt())
    {
        qDebug() << "[type_title]"
                 << "incorrect message type";
    }
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "title_message_struct << incorrect input data";
        return;
    }
    filePath = QString::fromUtf8(list.takeFirst());
    listFrag = Serialization::universalDeserialize(list.takeFirst());
}

DFSMessage::req_frags_message::~req_frags_message()
{
}

bool DFSMessage::req_frags_message::empty() const
{
    if (filePath.isEmpty())
        return true;
    if (listFrag.isEmpty())
        return true;
    return false;
}

const QList<QByteArray> DFSMessage::req_frags_message::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << filePath.toUtf8() << Serialization::universalSerialize(listFrag);
    return list;
}
