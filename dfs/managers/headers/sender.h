#ifndef SENDER_H
#define SENDER_H

#include <QObject>
#include <QThread>
#include "dfs/packages/headers/all.h"
#include "managers/account_controller.h"

#ifndef DFS_NETWORK_MANAGER_DEF
#define DFS_NETWORK_MANAGER_DEF
class DFSNetManager;
#include "dfs/managers/headers/dfsnetmanager.h"
#endif

class Sender : public QObject
{
    Q_OBJECT
    const int data_offset = DFSMessage::dataSize;
    DFSNetManager *NetManager;
    QByteArray userId;

    QMap<QByteArray, QString> titleHashs;
    QMap<QString, QByteArray> serializedTitle;

public:
    /**
     * @brief Sender
     * @param userId
     */
    Sender(const QByteArray &userId, QObject *parent = nullptr);
    void reloadFragments(QString path, QList<QByteArray> frags);
    void setNetManager(DFSNetManager *value);
    /**
     * @brief sendFile
     * @param filePath
     * @param receiver
     */
    void sendFile(const QString &filePath, const based_dfs_struct::Type &type, const SocketPair &receiver);

signals:
    /**
     * @brief finished
     */
    void finished();
    /**
     * @brief sendToPeer
     * @param msg
     * @param msgType
     * @param receiver
     */
    void sendPckg(const QByteArray &msg, const QByteArray &msgType, const SocketPair &receiver);
public slots:
    /**
     * @brief process
     */
    void process();
    /**
     * @brief checkClosing
     * @param titleHash
     * @param pckAF
     */
    void checkClosing(const QByteArray &titleHash, const long long &pckAF, const SocketPair &receiver);
};

#endif // SENDER_H
