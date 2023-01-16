#pragma once

#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"
#include <QObject>
#include <algorithm>

using DFSP::Connection;

static const std::string dbPath = "tmp/temp.dat";

class ConnectionsManager : public QObject {
    Q_OBJECT

    DBConnector dbConnector;
    std::vector<Connection> activeConnections;
    std::vector<Connection> newConnections;
    std::string m_port, m_address;

public:
    ConnectionsManager(const std::string address, const std::string port, QObject *parent = nullptr);

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

protected:
    void createTable();
    bool insertConnection(const Connection &connection);
};
