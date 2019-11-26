#ifndef MESSAGE_STRCUT_H
#define MESSAGE_STRCUT_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include "dumessage.h"
namespace DFSMessage {
struct dfs_message : public DUMessage
{

    const short FIELDS_COUNT = 3;

    QByteArray dataHash;
    long long pckgNumber;
    QByteArray data;

    dfs_message(const QByteArray &hash, const unsigned long &pckgNumber, const QByteArray &data);
    dfs_message(const QByteArray &serialized);
    dfs_message(const dfs_message &temp);
    ~dfs_message() override final;

    const QList<QByteArray> serializedParams() const override;
};
}
#endif // MESSAGE_STRCUT_H
