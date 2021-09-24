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

#ifndef DFSNETWORKMANAGER_H
#define DFSNETWORKMANAGER_H

#include "network/network_manager.h"
#include "dfs/packages/headers/all.h"
#include "resolve/dfs_resolver_service.h"
#include "utils/exc_utils.h"

class Dfs;

class EXTRACHAIN_EXPORT DfsNetworkManager : public NetworkManager
{
    Q_OBJECT

private:
    std::queue<Network::DataStruct> titleVector;
    Dfs *dfs;
    DFSResolverService *uResolver;
    QList<DFSResolverService *> dfsResolvers;

public:
    DfsNetworkManager(ActorIndex *actorIndex, const QString &localIp);
    ~DfsNetworkManager() override;

private:
    void connectResolver(DFSResolverService *resolver);
    void disconnectResolver(DFSResolverService *resolver);
    void createDFSResolver(Network::DataStruct ds);

public:
    void messageReceived(const QByteArray &msg, const SocketPair &receiver) override;
    void setDfs(Dfs *value);
    bool isLoading(const QString &fileName);

signals:
    void newMessage(Network::DataStruct data);

public slots:
    void process();
    void titleArrived(Network::DataStruct ds);
    void removeResolver(DFSResolverService::FinishStatus status);

private slots:
    void checkConnectionsStatus() override;
};

#endif // DFSNETWORKMANAGER_H
