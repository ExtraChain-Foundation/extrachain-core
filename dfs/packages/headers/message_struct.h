#ifndef MESSAGE_STRCUT_H
#define MESSAGE_STRCUT_H

#include "dfs/types/headers/dfstruct.h"
namespace Message {
const short dfs_message_field_size = 4;
struct dfs_message
{
    QString filePath;
    long long packageNumber;
    long long countFilePackage;
    int data_size;
    QByteArray data;

    dfs_message(const QString &filePath, const long long &packageNumber, const long long &countNumber,
                const QByteArray &data);
    dfs_message(const QByteArray &serialized);
    dfs_message(const dfs_message &temp);

    const QByteArray serialize() const;
    const QList<QByteArray> deserialize(const QByteArray &serialized) const;
};
}
#endif // MESSAGE_STRCUT_H
