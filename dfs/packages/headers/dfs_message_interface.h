#ifndef DFS_MESSAGE_INTERFACE_H
#define DFS_MESSAGE_INTERFACE_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <iostream>
#include <QObject>
#include "utils/utils.h"
namespace Message {
const int dataSize = 512;   // bytes
const short fieldsSize = 4; // bytes for size
enum dfsMessageType
{
    titleMessage,
    fileDataMessage,
    requestMessage,
    statusMessage,
    storageMessage,
    responseMessage
};

class IDfs_Message : public QObject
{
    Q_OBJECT

    dfsMessageType type;

public:
    IDfs_Message(const int &msgType, QObject *parent = nullptr)
        : QObject(parent)
    {
        type = static_cast<dfsMessageType>(msgType);
    }

    virtual ~IDfs_Message()
    {
    }

protected:
    virtual const QList<QByteArray> serializedParams() const = 0;
    virtual const QByteArray serialize() const
    {
        return Serialization::universalSerialize(serializedParams());
    }
    virtual const QList<QByteArray> deserialize(const QByteArray &serialized)
    {
        return Serialization::universalDeserialize(serialized);
    }
    virtual const QByteArray concatenate()
    {
        return serializedParams().join();
    }
    virtual const QByteArray hash()
    {
        return Utils::calcKeccak(concatenate());
    }
};
}

#endif // DFS_MESSAGE_INTERFACE_H
