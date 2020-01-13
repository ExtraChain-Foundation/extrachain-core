#ifndef DFS_MESSAGE_INTERFACE_H
#define DFS_MESSAGE_INTERFACE_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <iostream>
#include <QObject>
#include "utils/utils.h"

namespace DistFileSystem {

const long long dataSize = 20480; // bytes
const short fieldsSize = 8;       // bytes for size
const QByteArray stateDelimetr = "|";

enum dfsMessageType
{
    titleMessage,
    fileDataMessage,
    requestMessage,
    statusMessage,
    storageMessage,
    responseMessage,
    closingMessage,
    requestFragments,
    changesMessage,
    none
};

// class IDfs_Message /* : public QObject*/
//{
//    //    Q_OBJECT

// protected:
//    IDfs_Message(QObject *parent = nullptr)
//    {
//    }
//    virtual ~IDfs_Message() = default;

//    virtual const QByteArray serialize() const = 0;

//    virtual const QList<QByteArray> serializedParams() const = 0;

//    virtual const QList<QByteArray> deserialize(const QByteArray &serialized) = 0;
//    virtual const QByteArray concatenate() const = 0;
//    virtual const QByteArray hash() const = 0;
//};
}

#endif // DFS_MESSAGE_INTERFACE_H
