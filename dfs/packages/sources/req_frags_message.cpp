#include "dfs/packages/headers/req_frags_message.h"

QString DFSMessage::req_frags_message::getFilePath() const
{
    return filePath;
}

QByteArray DFSMessage::req_frags_message::getListFrag() const
{
    return listFrag;
}

DFSMessage::req_frags_message::req_frags_message(const QByteArray &filePath, QByteArray listFrag)
    : DUMessage(dfsMessageType::requestFragments)
{
    this->filePath = filePath;
    this->listFrag = listFrag;
}

DFSMessage::req_frags_message::req_frags_message(const QByteArray &serialized)
    : DUMessage(dfsMessageType::requestFragments)
{
    QList<QByteArray> list = deserialize(serialized);

    if (list.size() != FIELDS_COUNT + 1)
    {
        qDebug() << "title_message_struct << incorrect input data";
        filePath = "-1";
        return;
    }

    if (dfsMessageType::requestFragments != list.takeFirst().toInt())
    {
        qDebug() << "[type_title]"
                 << "incorrect message type";
    }

    filePath = QString::fromUtf8(list.takeFirst());
    listFrag = list.takeFirst();
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
    list << QByteArray::number(type) << filePath.toUtf8() << listFrag;
    return list;
}
