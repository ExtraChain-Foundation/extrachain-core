#ifndef MESSAGE_STRCUT_H
#define MESSAGE_STRCUT_H

#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"
#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {
struct DfsMessage : Messages::ISmallMessage
{

    const short FIELDS_COUNT = 3;

    QByteArray dataHash;
    long long pckgNumber = ULONG_MAX;
    QByteArray data;

public:
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
#endif // MESSAGE_STRCUT_H
