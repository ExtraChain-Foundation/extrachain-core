#pragma once

#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"
#include <QObject>
#include <algorithm>

using DFSP::Connection;

static const std::string ConnectionsTableName = "Connections";
static const std::string dbPath = "tmp/temp.dat";
static const std::string hash_connection = "hash";
static const std::string port_connection = "port";
static const std::string address_connection = "address";
static const std::string active_connection = "active";


class ConnectionsManager : public QObject {
    Q_OBJECT

    DBConnector dbConnector;
    std::vector<Connection> activeConnections;
    std::vector<Connection> newConnections;
    std::string m_port, m_address;
    QByteArray m_key;

public:
    ConnectionsManager(const std::string address, const std::string port, const QByteArray key, QObject *parent = nullptr);

    void addConnection(const Connection &connection);
    void tryToNewConnect();
    void addNewConnection(const Connection &connection);
    void removeConnection(Connection &connection);
    bool isConnection(const Connection &connection);
    const std::string &port() const;
    void setPort(const std::string &newPort);
    const std::string &address() const;
    void setAddress(const std::string &newAddress);
    const std::vector<Connection> &getActiveConnection() const;

    DBRow ecryptConnection(const Connection &connection);
    Connection decryptConnection(const DBRow &row);
    void loadRecords();
    void removeConnection(const Connection &connection);

protected:
    bool createTable();
    bool insertConnection(const Connection &connection);

private:
    std::string hashConnection(const Connection &connection);
};
