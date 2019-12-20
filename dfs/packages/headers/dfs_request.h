#ifndef DFS_REUEST_H
#define DFS_REUEST_H

#include "dumessage.h"

namespace DFSMessage {
struct DfsRequest : public DUMessage
{
    const short FIELDS_COUNT = 1;
    QString filePath;

    DfsRequest(const QString &filePath);
    DfsRequest(const QByteArray &serialized);
    ~DfsRequest() override final = default;
    bool isEmpty() const;

    const QList<QByteArray> serializedParams() const override final;
};
}
#endif // DFS_REUEST_H
