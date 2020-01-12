#ifndef DFS_REUEST_H
#define DFS_REUEST_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {
struct DfsRequest : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 1;
    QString filePath;

    const QList<QByteArray> serializedParams() const;

    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}
#endif // DFS_REUEST_H
