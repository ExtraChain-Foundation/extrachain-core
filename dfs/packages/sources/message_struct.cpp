#include "dfs/packages/headers/message_struct.h"

Message::dfs_message::dfs_message(const QString &filePath, const long long &packageNumber,
                                  const long long &countFilePackage, const QByteArray &data)
{
    this->filePath = filePath;
    this->packageNumber = packageNumber;
    this->countFilePackage = countFilePackage;
    this->data_size = data.size();
    this->data = data;
}

Message::dfs_message::dfs_message(const QByteArray &serialized)
{
    QList<QByteArray> list = deserialize(serialized);
    if (list.size() != 5)
    {
        qDebug() << "[&Message::dfs_message_struct] incorrect list size";
        return;
    }
    filePath = list.at(0);
    packageNumber = list.at(1).toLong();
    countFilePackage = list.at(2).toLong();
    data_size = list.at(3).toInt();
    data = list.at(4);
}

Message::dfs_message::dfs_message(const Message::dfs_message &temp)
{
    filePath = temp.filePath;
    packageNumber = temp.packageNumber;
    countFilePackage = temp.countFilePackage;
    data_size = temp.data_size;
    data = temp.data;
}

const QByteArray Message::dfs_message::serialize() const
{
    QList<QByteArray> list;
    list << filePath.toUtf8() << QByteArray::number(packageNumber) << QByteArray::number(countFilePackage)
         << QByteArray::number(data_size) << data;
    return Serialization::universalSerialize(list, dfs_message_field_size);
}

const QList<QByteArray> Message::dfs_message::deserialize(const QByteArray &serialized) const
{
    return Serialization::universalDeserialize(serialized, dfs_message_field_size);
}
