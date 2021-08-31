/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

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
class DfsNetworkManager;
#include "dfs/managers/headers/dfs_networkmanager.h"
#endif

class Sender : public QObject
{
    Q_OBJECT
    const int data_offset = DistFileSystem::dataSize;
    DfsNetworkManager *m_networkManager = nullptr;

public:
    /**
     * @brief Sender
     * @param userId
     */
    Sender(QObject *parent = nullptr);
    void setNetworkManager(DfsNetworkManager *value);
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
    void sendDfsMessage(const T &dfsMessage, const unsigned int &type,
                        const SocketPair &receiver = SocketPair(),
                        Config::Net::TypeSend typeSend = Config::Net::TypeSend::Default)
    {
        static_assert(std::is_base_of<Messages::ISmallMessage, T>::value,
                      "Derived not derived from Messages::ISmallMessage");

        if (dfsMessage.isEmpty())
        {
            qDebug() << "Empty dfs message" << typeid(T).name();
            return;
        }

        if (m_networkManager != nullptr)
        {
            m_networkManager->send(dfsMessage.serialize(), type, receiver, typeSend);
        }
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
    void sendFragments(QString path, DfsStruct::Type type, QByteArray frag, const SocketPair &receiver);

    /**
     * @brief process
     */
    //    void resendFragmentsSlot(QString path, based_dfs_struct::Type type, QList<QByteArray> frags);

    void process();
};

#endif // SENDER_H
