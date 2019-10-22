#include "dfs/packages/headers/title_message.h"

Message::title_message::title_message(const QString &filePath)
    : IDfs_Message(m_type)
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
}

Message::title_message::title_message(const QByteArray &serialized)
    : IDfs_Message(m_type)
{
    QList<QByteArray> list = deserialize(serialized);
    if (list.size() != FIELDS_COUNT)
    {
        qDebug() << "title_message_struct << incorrect input data";
        return;
    }
    filePath = QString::fromUtf8(list.takeFirst());
    pckgsAmount = list.takeFirst().toLongLong();
    fileSize = list.takeFirst().toLongLong();
    dataHash = list.takeFirst();
}

Message::title_message::title_message(const QString &filePath, const long long &pckgsAmount,
                                      const long long &fileSize, const QByteArray &hash)
    : IDfs_Message(m_type)
{
    this->filePath = filePath;
    this->pckgsAmount = pckgsAmount;
    this->fileSize = fileSize;
    this->dataHash = hash;
}

Message::title_message::~title_message()
{
}

const QList<QByteArray> Message::title_message::serializedParams() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << QByteArray::number(pckgsAmount) << QByteArray::number(fileSize) << dataHash;
    return list;
}
