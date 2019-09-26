#include "dfs/packages/headers/dfs_universal.h"

using namespace Messages;

// Geters
QByteArray DfsMessage::getData() const
{
    return data;
}

QString DfsMessage::getFilePath() const
{
    return filePath;
}

int DfsMessage::getSize() const
{
    return size;
}

void DfsMessage::setSize(int value)
{
    size = value;
}

int DfsMessage::getPackageNumber() const
{
    return packageNumber;
}

void DfsMessage::setPackageNumber(int value)
{
    packageNumber = value;
}

int DfsMessage::getNeedsByteCount() const
{
    return countFilePackage;
}

void DfsMessage::setNeedsByteCount(int value)
{
    countFilePackage = value;
}

short DfsMessage::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + 5;
}

void DfsMessage::initFields(QList<QByteArray> &list)
{
    BaseMessage::initFields(list);
    filePath = list.takeFirst();
    size = list.takeFirst().toInt();
    data = list.takeFirst();
    countFilePackage = list.takeFirst().toInt();
    packageNumber = list.takeFirst().toInt();
}
QList<QByteArray> DfsMessage::serializedParams() const
{
    QList<QByteArray> list = this->BaseMessage::serializedParams();
    list << filePath.toUtf8() << QByteArray::number(size) << data << QByteArray::number(countFilePackage)
         << QByteArray::number(packageNumber);
    return list;
}

const QByteArray DfsMessage::hash() const
{
    return Utils::calcKeccak(data + QByteArray::number(size) + QByteArray::number(packageNumber));
}

DfsMessage::DfsMessage(const QByteArray &data, int size, const QString &filePath, int packageNumber,
                       int countFilePackage)
    : BaseMessage(DFS_CHANGES_MESSAGE)
    , data(data)
    , size(size)
    , filePath(filePath)
    , packageNumber(packageNumber)
    , countFilePackage(countFilePackage)
{
}

DfsMessage::DfsMessage(const QByteArray &serialize)
    : BaseMessage()
{
    deserialize(serialize);
}

DfsMessage::DfsMessage(const DfsMessage &temp)
    : BaseMessage()
    , data(temp.data)
    , size(temp.size)
    , filePath(temp.filePath)
    , packageNumber(temp.packageNumber)
    , countFilePackage(temp.countFilePackage)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
}

DfsMessage::DfsMessage(QList<QByteArray> &list)
    : BaseMessage()
{
    this->BaseMessage::initFields(list);
    initFields(list);
}

DfsMessage::~DfsMessage()
{
}

DfsMessage DfsMessage::operator=(const DfsMessage &temp)
{
    QList<QByteArray> list = temp.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    filePath = temp.filePath;
    size = temp.size;
    data = temp.data;
    countFilePackage = temp.countFilePackage;
    packageNumber = temp.packageNumber;
    return *this;
}

QByteArray DfsMessage::serialize() const

{
    QByteArray serialize = BaseMessage::serialize(this->serializedParams());
    //    serialize += data;
    return serialize;
}

void DfsMessage::deserialize(const QByteArray &serialized)
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
    if (list.size() == getFieldsCount())
    {
        static auto checkMsgType = [](const QByteArray &msg, const QByteArray &type) {
            Messages::BaseMessage b;
            b.deserialize(msg);
            return b.getMsgType() == type;
        };

        if (!checkMsgType(serialized, Messages::DFS_CHANGES_MESSAGE))
            qDebug() << "Error: can't deserialize message:" << serialized;
    }
    initFields(list);
}
