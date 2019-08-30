#ifndef DOWNLOADDFSREQUEST_H
#define DOWNLOADDFSREQUEST_H

#include <QByteArray>
#include <QObject>
#include "network/packages/base_message.h"
#include "utils/utils.h"
namespace Messages {

static const QByteArray DOWNLOAD_DFS_REQUEST = "downdloadDfsRequest";
class DownloadDfsRequestData : public BaseMessage
{
    bool status;
    QByteArray header;

    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    DownloadDfsRequestData();
    DownloadDfsRequestData(const bool status, const QByteArray &header);
    DownloadDfsRequestData(const QByteArray &data);
    DownloadDfsRequestData(const DownloadDfsRequestData &temp);
    DownloadDfsRequestData(QList<QByteArray> &list);
    ~DownloadDfsRequestData() override;
    DownloadDfsRequestData operator=(const DownloadDfsRequestData &temp);
    bool getStatus() const;
    QByteArray getHeader() const;
};
}
#endif // DOWNLOADDFSREQUEST_H
