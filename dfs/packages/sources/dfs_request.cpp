#include "dfs/packages/headers/dfs_request.h"

using namespace Messages;

int DfsRequest::getRequest() const
{
    return request;
}

QString DfsRequest::getFilePath() const
{
    return filePath;
}

short DfsRequest::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + 2;
}

void DfsRequest::initFields(QList<QByteArray> &list)
{
    this->BaseMessage::initFields(list);
    request = list.takeFirst().toInt();
    filePath = list.takeFirst();
}

QList<QByteArray> DfsRequest::serializedParams() const
{
    QList<QByteArray> list = this->BaseMessage::serializedParams();
    list << QByteArray::number(request) << filePath.toUtf8();
    return list;
}

DfsRequest::DfsRequest()
    : BaseMessage(DFS_REQUEST_MESSAGE)
{
}

DfsRequest::DfsRequest(const int &reuest, const QString &filePath)
    : BaseMessage(DFS_REQUEST_MESSAGE)
    , request(reuest)
    , filePath(filePath)
{
}

DfsRequest::DfsRequest(const QByteArray &serialize)
    : BaseMessage(DFS_REQUEST_MESSAGE)
{
    QList<QByteArray> list =
        Serialization::universalDeserialize(serialize, Messages::FIELD_SIZES);
    initFields(list);
}

DfsRequest::DfsRequest(const DfsRequest &value)
    : BaseMessage(DFS_REQUEST_MESSAGE)
    , request(value.request)
    , filePath(value.filePath)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
}

DfsRequest::~DfsRequest()
{
}

DfsRequest DfsRequest::operator=(const DfsRequest &value)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    request = value.request;
    filePath = value.filePath;
    return *this;
}

QByteArray DfsRequest::serialize() const
{
    return Serialization::universalSerialize(this->serializedParams(), Messages::FIELD_SIZES);
}

void DfsRequest::deserialize(const QByteArray &serialized)
{
    QList<QByteArray> list =
        Serialization::universalDeserialize(serialized, Messages::FIELD_SIZES);
    initFields(list);
}
