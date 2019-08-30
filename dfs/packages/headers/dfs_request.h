#ifndef DFS_REUEST_H
#define DFS_REUEST_H

#include "dfs/types/headers/dfstruct.h"
#include "network/packages/base_message.h"

namespace Messages {

static const QByteArray DFS_REQUEST_MESSAGE = "dfsRequestMessage";

class DfsRequest : public BaseMessage
{
    Q_OBJECT

private:
    int request;
    QString filePath;

    short getFieldsCount() const override;
    void initFields(QList<QByteArray> &list) override;
    QList<QByteArray> serializedParams() const override;

public:
    DfsRequest();
    DfsRequest(const int &reuest, const QString &filePath);
    DfsRequest(const QByteArray &serialize);
    DfsRequest(const DfsRequest &value);
    ~DfsRequest() override;
    DfsRequest operator=(const DfsRequest &value);
    QByteArray serialize() const override final;
    void deserialize(const QByteArray &serialized) override;
    QString getFilePath() const;
    int getRequest() const;
};
}
#endif // DFS_REUEST_H
