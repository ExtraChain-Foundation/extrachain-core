#include "managers/connections_manager.h"
#include "utils/exc_utils.h"

ConnectionsManager::ConnectionsManager(const std::string address, const std::string port,
                                       const QByteArray key, QObject *parent)
    : QObject(parent)
    , m_address(address)
    , m_port(port)
    , m_key(key)
    , dbConnector(dbPath) {
    decryptDb();
    const bool createdTable = createTable();
    if (!createdTable) {
        loadRecords();
        tryToNewConnect();
    }
}

ConnectionsManager::~ConnectionsManager() {
    Utils::encryptFile(QString::fromStdString(dbPath), QString::fromStdString(dbPathEcrypted), m_key);
    if (std::filesystem::exists(dbPath)) {
        std::filesystem::remove(dbPath);
    }
}

bool ConnectionsManager::createTable() {
    dbConnector.open();
    bool createdTableConnnections = false;

    if (dbConnector.selectAll(ConnectionsTableName).empty()) {
        static const std::string CreateTableQuery = "CREATE TABLE Connections("
                                                    "address     TEXT             NOT NULL,"
                                                    "port        TEXT             NOT NULL,"
                                                    "active      INTEGER          NOT NULL);";
        createdTableConnnections = dbConnector.createTable(CreateTableQuery);
    }
    dbConnector.close();
    return createdTableConnnections;
}

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

bool ConnectionsManager::insertConnection(const DFS::Packets::Connection &connection) {
    dbConnector.open();
    bool result = dbConnector.insert(ConnectionsTableName,
                                     DBRow {
                                         { "address", connection.address },
                                         { "port", connection.port },
                                         { "active", std::to_string(connection.active) },
                                     });
    dbConnector.close();
    return result;
}

void ConnectionsManager::loadRecords() {
    dbConnector.open();

    const auto rows = dbConnector.selectAll(ConnectionsTableName);
    if (!rows.empty()) {
        for (const auto &row : rows) {
            Connection connection = { .port = row.at("port"),
                                      .address = row.at("address"),
                                      .active = std::stoi(row.at("active")) == 1 ? true : false };
            newConnections.push_back(connection);
        }
    }

    dbConnector.close();
}

void ConnectionsManager::decryptDb() {
    if (std::filesystem::exists(dbPathEcrypted)) {
        Utils::decryptFile(QString::fromStdString(dbPathEcrypted), QString::fromStdString(dbPath), m_key);
        std::filesystem::remove(dbPathEcrypted);
    }
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
