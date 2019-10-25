#ifndef SENDER_H
#define SENDER_H

#include <QObject>
#include <QThread>
#include "dfs/packages/headers/all.h"
#include "managers/account_controller.h"

class Sender : public QObject
{
    Q_OBJECT
    const int data_offset = Message::dataSize;

    QByteArray userId;

public:
    /**
     * @brief Sender
     * @param userId
     */
    Sender(const QByteArray &userId, QObject *parent = nullptr);

signals:
    /**
     * @brief finished
     */
    void finished();
    /**
     * @brief send
     * @param msg
     * @param msgType
     */
    void sendS(const QByteArray &msg, const QByteArray &msgType);
    /**
     * @brief sendToPeer
     * @param msg
     * @param msgType
     * @param receiver
     */
    void sendToPeer(const QByteArray &msg, const QByteArray &msgType, const SocketPair &receiver);
public slots:
    /**
     * @brief process
     */
    void process();
    /**
     * @brief sendFile
     * @param filePath
     * @param receiver
     */
    void sendFile(const QString &filePath, const based_dfs_struct::Type &type, const SocketPair &receiver);
};

#endif // SENDER_H
