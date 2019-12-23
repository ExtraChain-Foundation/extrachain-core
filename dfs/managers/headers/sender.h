#ifndef SENDER_H
#define SENDER_H

#include <QObject>
#include <QThread>
#include "dfs/packages/headers/all.h"
#include "managers/account_controller.h"
#include <vector>
#include <type_traits>

#ifndef DFS_NETWORK_MANAGER_DEF
#define DFS_NETWORK_MANAGER_DEF
class DFSNetManager;
#include "dfs/managers/headers/dfsnetmanager.h"
#endif

class Sender : public QObject
{
    Q_OBJECT
    const int data_offset = DFSMessage::dataSize;
    DFSNetManager *NetManager = nullptr;
    QByteArray userId;

    QMap<QByteArray, QString> titleHashs;
    QMap<QString, QByteArray> serializedTitle;

public:
    /**
     * @brief Sender
     * @param userId
     */
    Sender(const QByteArray &userId, QObject *parent = nullptr);
    void setNetManager(DFSNetManager *value);
    /**
     * @brief Send file
     * @param filePath
     * @param receiver
     */
    void sendFile(const QString &filePath, const DfsStruct::Type &type, const SocketPair &receiver);

    /**
     * @brief Send any dfs message (template function)
     */
    template <typename T>
    void sendDfsMessage(const T &dfsMessage, const SocketPair &receiver = {})
    {
        static_assert(std::is_base_of<DFSMessage::DUMessage, T>::value, "Derived not derived from DUMessage");

        if (dfsMessage.isEmpty())
        {
            qDebug() << "Empty dfs message" << typeid(T).name();
            return;
        }
        if (NetManager != nullptr)
            NetManager->send(dfsMessage.serialize(), Messages::DFS_MESSAGE, receiver);
    }

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
    void sendFragments(QString path, DfsStruct::Type type, QByteArray frag, SocketPair receiver);

    /**
     * @brief process
     */
    //    void resendFragmentsSlot(QString path, based_dfs_struct::Type type, QList<QByteArray> frags);

    void process();
    /**
     * @brief checkClosing
     * @param titleHash
     * @param pckAF
     */
    void checkClosing(const QByteArray &titleHash, const long long &pckAF, const SocketPair &receiver);
};

#endif // SENDER_H
