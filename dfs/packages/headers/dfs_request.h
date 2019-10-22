#ifndef DFS_REUEST_H
#define DFS_REUEST_H

#include "dfs_message_interface.h"

namespace Message {

struct dfs_request : public IDfs_Message
{
    const short FIELDS_COUNT = 2;
    const int m_type = dfsMessageType::requestMessage;

    QString filePath;
    QByteArray asker;

    dfs_request(const QString &filePath, const QByteArray &asker);
    dfs_request(const QByteArray &serialized);
    ~dfs_request() override final;

    const QList<QByteArray> serializedParams() const override;
};
}
#endif // DFS_REUEST_H
