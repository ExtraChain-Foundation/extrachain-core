#ifndef HASH_OPERATIONS_H
#define HASH_OPERATIONS_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {
// TODO: package root array

struct requestLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 1;
    QByteArray actorId;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct responseLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 3;
    QByteArray actorId;
    QByteArray pHash;
    QByteArray cHash;
    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

// TODO: add remove (insert for now)
struct CardFileChange : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 7;
    int key = -1;
    QByteArray actorId;
    QByteArray fileId;
    QByteArray prevId;
    QByteArray nextId;
    int type = -1;
    QByteArray sign;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // HASH_OPERATIONS_H
