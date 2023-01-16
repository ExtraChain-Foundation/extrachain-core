#include "managers/connections_manager.h"

const std::string &ConnectionsManager::port() const {
    return m_port;
}

void ConnectionsManager::setPort(const std::string &newPort) {
    m_port = newPort;
}

const std::string &ConnectionsManager::address() const {
    return m_address;
}

void ConnectionsManager::setAddress(const std::string &newAddress) {
    m_address = newAddress;
}

const std::vector<Connection> &ConnectionsManager::getActiveConnection() const {
    return activeConnections;
}

void ConnectionsManager::createTable() {
    dbConnector.open();
    bool createdTableConnnections = false;
    static const std::string ClearTableQuery = "DROP TABLE IF EXISTS Connections;";
    static const std::string CreateTableQuery = "CREATE TABLE IF NOT EXISTS Connections("
                                                "address     TEXT             NOT NULL,"
                                                "port        TEXT             NOT NULL,"
                                                "active      INTEGER          NOT NULL);";
    bool clearedTable = dbConnector.query(ClearTableQuery);
    createdTableConnnections = dbConnector.createTable(CreateTableQuery);
    dbConnector.close();
}

bool ConnectionsManager::insertConnection(const DFS::Packets::Connection &connection) {
    dbConnector.open();
    bool result = dbConnector.insert("Connections",
                                   DBRow {
                                       { "address", connection.address },
                                       { "port", connection.port },
                                       { "active", std::to_string(connection.active) },
                                   });
    dbConnector.close();
    return result;
}

ConnectionsManager::ConnectionsManager(const std::string address, const std::string port, QObject *parent)
    : QObject(parent)
    , m_address(address)
    , m_port(port)
    , dbConnector(dbPath) {
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }

    createTable();
}

void ConnectionsManager::addConnection(const DFS::Packets::Connection &connection) {
    if (connection.active) {
        activeConnections.push_back(connection);
        insertConnection(connection);
    }
}

void ConnectionsManager::tryToNewConnect() {
    for (const auto &connection : newConnections) {
        // try connect

        // remove connection from new connections list
        newConnections.erase(std::remove_if(newConnections.begin(), newConnections.end(),
                                            [&](Connection const &item) {
                                                return item.address == connection.address
                                                    && item.port == connection.port;
                                            }),
                             newConnections.end());
    }
}

void ConnectionsManager::addNewConnection(const DFS::Packets::Connection &connection) {
    newConnections.push_back(connection);
}

void ConnectionsManager::removeConnection(DFS::Packets::Connection &connection) {
    activeConnections.erase(std::remove_if(activeConnections.begin(), activeConnections.end(),
                                           [&](Connection const &item) {
                                               return item.address == connection.address
                                                   && item.port == connection.port;
                                           }),
                            activeConnections.end());
}

bool ConnectionsManager::isConnection(const DFS::Packets::Connection &connection) {
    return m_address == connection.address && m_port == connection.port;
}
