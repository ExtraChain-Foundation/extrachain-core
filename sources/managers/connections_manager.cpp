#include "managers/connections_manager.h"
#include "enc/enc_tools.h"
#include "utils/exc_utils.h"

ConnectionsManager::ConnectionsManager(const std::string address, const std::string port,
                                       const QByteArray key, QObject *parent)
    : QObject(parent)
    , m_address(address)
    , m_port(port)
    , m_key(key)
    , dbConnector(dbPath)
    , dbActivity(dbActPath){
    const bool createdTable = createTable();
    if (!createdTable) {
        loadRecords();
        tryToNewConnect();
    }
    const bool createdActTable = createActivityTable();
    if (!createdActTable) {
        loadActivityRecords();
    }
}

ConnectionsManager::~ConnectionsManager()
{
    synchroActivityDB();
}

bool ConnectionsManager::createTable() {
    dbConnector.open();
    bool createdTableConnnections = false;

    if (dbConnector.selectAll(ConnectionsTableName).empty()) {
        static const std::string CreateTableQuery = fmt::format("CREATE TABLE IF NOT EXISTS {}("
                                                                "hash        TEXT             NOT NULL, "
                                                                "address     TEXT             NOT NULL, "
                                                                "port        TEXT             NOT NULL, "
                                                                "active      TEXT             NOT NULL);",
                                                                ConnectionsTableName);
        createdTableConnnections = dbConnector.createTable(CreateTableQuery);
    }
    dbConnector.close();
    return createdTableConnnections;
}

bool ConnectionsManager::createActivityTable()
{
    dbActivity.open();
    bool createdTableActivity = false;

    if (dbActivity.selectAll(ActivityTableName).empty()) {
        static const std::string CreateTableQuery = fmt::format("CREATE TABLE IF NOT EXISTS {}("
                                                                "hash          TEXT   PRIMARY   NOT NULL, "
                                                                "timeactivity  INTEGER          NOT NULL, "
                                                                "activity      BOOLEAN          NOT NULL, "
                                                                "score         INTEGER          NOT NULL);",
                                                                ActivityTableName);

        createdTableActivity = dbActivity.createTable(CreateTableQuery);
    }

    return createdTableActivity;
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
    DBRow row = ecryptConnection(connection);

    bool result = dbConnector.insert(ConnectionsTableName, row);
    dbConnector.close();
    return result;
}

void ConnectionsManager::loadRecords() {
    dbConnector.open();

    const auto rows = dbConnector.selectAll(ConnectionsTableName);
    if (!rows.empty()) {
        for (const auto &row : rows) {
            Connection connection = decryptConnection(row);
            newConnections.push_back(connection);
        }
    }

    dbConnector.close();
}

void ConnectionsManager::loadActivityRecords()
{
    dbActivity.open();
    const auto rows = dbActivity.selectAll(ActivityTableName);
    if (!rows.empty()) {
        for (const auto &row : rows) {
            auto act = decryptActivity(row);
            act.second.timeactivity = std::time(nullptr);
            clientActivity.insert(act);
        }
    }
    dbActivity.close();
}

void ConnectionsManager::removeConnection(const DFS::Packets::Connection &connection) {
    dbConnector.open();
    dbConnector.query(
        fmt::format("DELETE FROM {} WHERE hash = '{}'", ConnectionsTableName, hashConnection(connection)));
    dbConnector.close();
}

std::string ConnectionsManager::hashConnection(const DFS::Packets::Connection &connection) {
    return Utils::calcHash(connection.address + connection.port);
}

DBRow ConnectionsManager::ecryptConnection(const DFS::Packets::Connection &connection) {
    std::string hash = hashConnection(connection);
    std::string key = SecretKey::getKeyFromPass(hash);

    std::string ecryptedAddress = SecretKey::encrypt(connection.address, key);
    std::string encryptedPort = SecretKey::encrypt(connection.port, key);
    std::string encryptedActive = SecretKey::encrypt(std::to_string(connection.active), key);

    DBRow row { { hash_connection, hash },
                { port_connection, encryptedPort },
                { address_connection, ecryptedAddress },
                { active_connection, encryptedActive } };

    return row;
}

Connection ConnectionsManager::decryptConnection(const DBRow &row) {
    Connection connection;
    std::string key = SecretKey::getKeyFromPass(row.at(hash_connection));

    connection.port = SecretKey::decrypt(row.at(port_connection), key);
    connection.address = SecretKey::decrypt(row.at(address_connection), key);
    connection.active = std::stoi(SecretKey::decrypt(row.at(active_connection), key));
    return connection;
}

std::pair<std::string, Activity> ConnectionsManager::decryptActivity(const DBRow &row)
{
    Activity activity;
    std::string key = SecretKey::getKeyFromPass(row.at(hash_connection));

    activity.timeactivity = std::stoi(SecretKey::decrypt(row.at(time_act), key));
    activity.active = std::stoi(SecretKey::decrypt(row.at(status_act), key));
    activity.score = std::stoi(SecretKey::decrypt(row.at(score_act), key));
    return std::make_pair(key, activity);
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

void ConnectionsManager::addActivity(const Connection &connection)
{
    std::string hash = hashConnection(connection);
    std::string key = SecretKey::getKeyFromPass(hash);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        it->second.active = true;
        auto score = std::time(nullptr) - clientActivity.at("key").timeactivity;
        it->second.timeactivity = std::time(nullptr);
        it->second.score =  score <= 0 ? 0 : score;
        clientActivity.insert(std::make_pair(key, it->second));
    } else {
        Activity act;
        act.timeactivity = std::time(nullptr);
        act.score = 0;
        act.active = true;
        clientActivity.insert(std::make_pair(key, act));
    }
}

void ConnectionsManager::removeActivity(const Connection &connection)
{
    std::string hash = hashConnection(connection);
    std::string key = SecretKey::getKeyFromPass(hash);
    Activity act;
    act.active = false;
    act.score =  std::time(nullptr) - clientActivity.at(key).timeactivity;
    act.timeactivity = std::time(nullptr);
    clientActivity.at("key") = act;
}

uint64_t ConnectionsManager::getActivityScore(const Connection &connection)
{
    std::string hash = hashConnection(connection);
    std::string key = SecretKey::getKeyFromPass(hash);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        if(clientActivity.at(key).timeactivity == 0 && !clientActivity.at(key).active)
            return 0;
        auto delta_score = std::time(nullptr) - clientActivity.at(key).timeactivity;
        if(clientActivity.at(key).active){
            return clientActivity.at(key).score + delta_score;
        }
        else{
            auto score = clientActivity.at(key).score - delta_score;
            return  score <= 0 ? 0 : score;
        }
    }

    return 0;
}

void ConnectionsManager::synchroActivityDB()
{
    for (const auto& pair : clientActivity) {
        Activity act = pair.second;
        uint64_t score;
        auto delta_score = std::time(nullptr) - pair.second.timeactivity;
        if(pair.second.active){
            score = pair.second.score + delta_score;
        }
        else{
            score = pair.second.score - delta_score;
            if(score <= 0)
                score = 0 ;
        }
        dbActivity.update(fmt::format("UPDATE Activity SET timeactivity ='{}' activity = {} score = {} WHERE hash ='{}';",
                                   std::time(nullptr), pair.second.active, score, pair.first));
    }
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
