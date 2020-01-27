#ifndef HASH_OPERATIONS_H
#define HASH_OPERATIONS_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {
struct requestLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 1;
    QByteArray id;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);
    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct responseLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 2;
    QByteArray pHash;
    QByteArray cHash;
    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);
    // ISmallMessage interface
public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // HASH_OPERATIONS_H
