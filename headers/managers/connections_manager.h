/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#pragma once

#include "utils/db_connector.h"
#include "dfs/dfs_utils.h"
#include "utils/exc_utils.h"
#include <QObject>
#include <algorithm>

using DfsP::Activity;
using DfsP::Connection;

static const std::string ConnectionsTableName = "Connections";
static const std::string ActivityTableName    = "Activity";
static const std::string dbPath               = "tmp/temp.dat";
static const std::string dbActPath            = "tmp/activity.dat";
static const std::string hash_connection      = "hash";
static const std::string port_connection      = "port";
static const std::string address_connection   = "address";
static const std::string active_connection    = "activity";
static const std::string time_act             = "timeactivity";
static const std::string status_act           = "active";
static const std::string score_act            = "score";

class ConnectionsManager : public QObject {
    Q_OBJECT

    DbConnector                               dbConnector;
    DbConnector                               dbActivity;
    std::vector<Connection>                   activeConnections;
    std::vector<Connection>                   newConnections;
    std::unordered_map<std::string, Activity> clientActivity;
    std::string                               m_port, m_address;
    QByteArray                                m_key;

public:
    ConnectionsManager(
        const std::string address,
        const std::string port,
        const QByteArray  key,
        QObject          *parent = nullptr);
    ~ConnectionsManager();

    void                           addConnection(const Connection &connection);
    void                           tryToNewConnect();
    void                           addNewConnection(const Connection &connection);
    void                           addActivity(const Connection &connection);
    void                           removeActivity(const Connection &connection);
    std::uint64_t                  getActivityScore(const Connection &connection);
    void                           synchroActivityDB();
    void                           removeConnection(Connection &connection);
    bool                           isConnection(const Connection &connection);
    const std::string             &port() const;
    void                           setPort(const std::string &newPort);
    const std::string             &address() const;
    void                           setAddress(const std::string &newAddress);
    const std::vector<Connection> &getActiveConnection() const;

    DbRow                            ecryptConnection(const Connection &connection);
    DbRow                            ecryptActivity(const std::string hash, const Activity &activity);
    Connection                       decryptConnection(const DbRow &row);
    std::pair<std::string, Activity> decryptActivity(const DbRow &row);
    void                             loadRecords();
    void                             loadActivityRecords();
    void                             removeConnection(const Connection &connection);

protected:
    bool createTable();
    bool createActivityTable();
    bool insertConnection(const Connection &connection);
    bool insertActivity(const std::string hash, const Activity &activity);

private:
    std::string hashConnection(const Connection &connection);
};
