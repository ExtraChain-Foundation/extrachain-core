#ifndef DFS_CHANGES_H
#define DFS_CHANGES_H

#include "dumessage.h"
#include <QFile>
namespace DFSMessage {
struct DfsChanges : public DUMessage
{
    const short FIELDS_COUNT = 6;

    QString filePath;

    QByteArrayList data;
    QByteArray range;
    int type = -1;
    QByteArray userId;
    QByteArray signature;

    DfsChanges() = default;
    DfsChanges(const QByteArray &serialized);
    DfsChanges(const QString &filePath, const QByteArray &range, int type, const QByteArray &actorId,
               const QByteArray &signature, const QByteArrayList &data);
    ~DfsChanges() = default;

    bool empty() const;
    const QList<QByteArray> serializedParams() const override final;

    DfsChanges operator=(const DfsChanges &msg); ///
};
}

#endif // DFS_CHANGES_H
