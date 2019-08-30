#include "network/packages/service/downloaddfsrequest.h"

bool Messages::DownloadDfsRequestData::getStatus() const
{
    return status;
}

QByteArray Messages::DownloadDfsRequestData::getHeader() const
{
    return header;
}

short Messages::DownloadDfsRequestData::getFieldsCount() const
{
    return this->BaseMessage::getFieldsCount() + 2;
}

void Messages::DownloadDfsRequestData::initFields(QList<QByteArray> &list)
{
    this->BaseMessage::initFields(list);
    status = list.takeFirst().toInt();
    header = list.takeFirst();
}

QList<QByteArray> Messages::DownloadDfsRequestData::serializedParams() const
{
    QList<QByteArray> list = this->BaseMessage::serializedParams();
    list << QByteArray::number(status) << header;
    return list;
}

Messages::DownloadDfsRequestData::DownloadDfsRequestData()
    : BaseMessage(DOWNLOAD_DFS_REQUEST)
{
}

Messages::DownloadDfsRequestData::DownloadDfsRequestData(const bool status,
                                                         const QByteArray &header)
    : BaseMessage(DOWNLOAD_DFS_REQUEST)
    , status(status)
    , header(header)
{
}

Messages::DownloadDfsRequestData::DownloadDfsRequestData(const QByteArray &serialize)
    : BaseMessage(DOWNLOAD_DFS_REQUEST)
{
    QList<QByteArray> list = Serialization::deserialize(
        serialize, Serialization::NET_MESSAGE_HEADER_FIELD_SPLITTER);
    this->BaseMessage::initFields(list);
    initFields(list);
}

Messages::DownloadDfsRequestData::DownloadDfsRequestData(
    const Messages::DownloadDfsRequestData &value)
    : BaseMessage(DOWNLOAD_DFS_REQUEST)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
}

Messages::DownloadDfsRequestData::DownloadDfsRequestData(QList<QByteArray> &list)
    : BaseMessage(DOWNLOAD_DFS_REQUEST)
{
    this->BaseMessage::initFields(list);
    initFields(list);
}

Messages::DownloadDfsRequestData::~DownloadDfsRequestData()
{
}

Messages::DownloadDfsRequestData Messages::DownloadDfsRequestData::
operator=(const Messages::DownloadDfsRequestData &value)
{
    QList<QByteArray> list = value.BaseMessage::serializedParams();
    this->BaseMessage::initFields(list);
    status = value.status;
    header = value.header;
    return *this;
}
