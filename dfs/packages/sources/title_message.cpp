#include "dfs/packages/headers/title_message.h"

DFSMessage::title_message::title_message()
    : DUMessage()
{
}

DFSMessage::title_message::title_message(const QString &filePath)
    : DUMessage(type_title)
{
    QFile file(filePath);
    file.open(QIODevice::ReadOnly);
    fileSize = file.size();
    this->filePath = filePath;
    while (file.pos() + dataSize < file.size())
    {
        pckgsAmount++;
        QByteArray sgmHash = Utils::calcKeccak(file.read(dataSize));
        dataHash = Utils::calcKeccak(dataHash + sgmHash);
    }
    pckgsAmount++;
    QByteArray sgmHash = Utils::calcKeccak(file.read(file.size() - file.pos()));
    dataHash = Utils::calcKeccak(dataHash + sgmHash);
    file.close();
}

DFSMessage::title_message::title_message(const QByteArray &serialized)
    : DUMessage(type_title)
{
    QList<QByteArray> list = deserialize(serialized);
    if (type_title != list.takeFirst().toInt())
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
    pckgsAmount = list.takeFirst().toLongLong();
    fileSize = list.takeFirst().toLongLong();
    dataHash = list.takeFirst();
    f_type = list.takeFirst();
}

DFSMessage::title_message::title_message(const QString &filePath, const long long &pckgsAmount,
                                         const long long &fileSize, const QByteArray &hash,
                                         const QByteArray &f_type)
    : DUMessage(type_title)
{
    this->filePath = filePath;
    this->pckgsAmount = pckgsAmount;
    this->fileSize = fileSize;
    this->dataHash = hash;
    this->f_type = f_type;
}

bool DFSMessage::title_message::empty() const
{
    if (filePath.isEmpty())
        return true;
    if (pckgsAmount == 0)
        return true;
    if (fileSize == 0)
        return true;
    if (dataHash.isEmpty())
        return true;
    return false;
}

const QList<QByteArray> DFSMessage::title_message::serializedParams() const
{
    QList<QByteArray> list;
    list << QByteArray::number(type) << filePath.toUtf8()
         << QByteArray::number(static_cast<long long>(pckgsAmount)) << QByteArray::number(fileSize)
         << dataHash << f_type;
    return list;
}

DFSMessage::title_message DFSMessage::title_message::operator=(const DFSMessage::title_message &msg)
{
    this->f_type = msg.f_type;
    this->dataHash = msg.dataHash;
    this->filePath = msg.filePath;
    this->fileSize = msg.fileSize;
    this->pckgsAmount = msg.pckgsAmount;
    return *this;
}
