#ifndef DFS_MESSAGE_INTERFACE_H
#define DFS_MESSAGE_INTERFACE_H

#include <QByteArray>
#include <QString>
#include <QDebug>
#include <iostream>
#include <QObject>
#include "utils/utils.h"

namespace DFSMessage {

const long long dataSize = 2048; // bytes
const short fieldsSize = 4;      // bytes for size
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
    none
};

const int type_title = dfsMessageType::titleMessage;
const int type_dfs_message = dfsMessageType::fileDataMessage;
const int type_status = dfsMessageType::statusMessage;
const int type_dfs_request = dfsMessageType::requestMessage;
const int type_closing = dfsMessageType::closingMessage;

const int type_req_frags = dfsMessageType::requestFragments;

class IDfs_Message /* : public QObject*/
{
    //    Q_OBJECT

protected:
    IDfs_Message(QObject *parent = nullptr)
    //        : QObject(parent)
    {
    }
    virtual ~IDfs_Message()
    {
    }
    virtual const QByteArray serialize() const = 0;

    virtual const QList<QByteArray> serializedParams() const = 0;

    virtual const QList<QByteArray> deserialize(const QByteArray &serialized) = 0;
    virtual const QByteArray concatenate() = 0;
    virtual const QByteArray hash() = 0;
};
}

#endif // DFS_MESSAGE_INTERFACE_H
