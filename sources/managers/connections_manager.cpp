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
    // synchroActivityDB();
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
                                                                "hash          TEXT          NOT NULL, "
                                                                "timeactivity  TEXT          NOT NULL, "
                                                                "activity      TEXT          NOT NULL, "
                                                                "score         TEXT          NOT NULL);",
                                                                ActivityTableName);

        createdTableActivity = dbActivity.createTable(CreateTableQuery);
    }
    dbActivity.close();
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

bool ConnectionsManager::insertActivity(const std::string hash, const DFS::Packets::Activity &activity)
{
    dbActivity.open();
    DBRow row = ecryptActivity(hash, activity);

    bool result = dbActivity.insert(ActivityTableName, row);
    dbActivity.close();

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
    //         // act.second.timeactivity = std::time(nullptr);
    //         // clientActivity.insert(act);
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
    auto key = Cryptography::getKeyFromPass(hash);

    std::string ecryptedAddress = Cryptography::encrypt(connection.address, key);
    std::string encryptedPort = Cryptography::encrypt(connection.port, key);
    std::string encryptedActive = Cryptography::encrypt(std::to_string(connection.active), key);

    DBRow row { { hash_connection, hash },
                { port_connection, encryptedPort },
                { address_connection, ecryptedAddress },
                { active_connection, encryptedActive } };

    return row;
}

DBRow ConnectionsManager::ecryptActivity(const std::string hash, const Activity &activity)
{
    auto key = Cryptography::getKeyFromPass(hash);

    std::string timeactivity = Cryptography::encrypt(std::to_string(activity.timeactivity), key);
    std::string active = Cryptography::encrypt(std::to_string(activity.active), key);
    std::string score = Cryptography::encrypt(std::to_string(activity.score), key);

    DBRow row { { hash_connection, hash },
              { active_connection, active },
              { score_act, score },
              { time_act, timeactivity } };

    return row;
}

Connection ConnectionsManager::decryptConnection(const DBRow &row) {
    Connection connection;
    auto key = Cryptography::getKeyFromPass(row.at(hash_connection));

    connection.port = Cryptography::decrypt(row.at(port_connection), key);
    connection.address = Cryptography::decrypt(row.at(address_connection), key);
    connection.active = std::stoi(Cryptography::decrypt(row.at(active_connection), key));
    return connection;
}

std::pair<std::string, Activity> ConnectionsManager::decryptActivity(const DBRow &row)
{
    Activity activity;
    auto key = Cryptography::getKeyFromPass(row.at(hash_connection));

    activity.timeactivity = std::stoull(Cryptography::decrypt(row.at(time_act), key));
    activity.active = Cryptography::decrypt(row.at(active_connection), key) == "1"? true: false;
    activity.score = std::stoi(Cryptography::decrypt(row.at(score_act), key));
    return std::make_pair(ByteArray(key).toString(), activity);
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
    std::string key = hashConnection(connection);
    // std::string key = SecretKey::getKeyFromPass(hash);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        it->second.active = true;
        it->second.score =  std::time(nullptr) - clientActivity.at(key).timeactivity;
        it->second.timeactivity = std::time(nullptr);
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
    std::string key = hashConnection(connection);
    Activity act;
    act.active = false;
    act.score =  std::time(nullptr) - clientActivity.at(key).timeactivity;
    act.timeactivity = std::time(nullptr);
    clientActivity.at(key) = act;
}

uint64_t ConnectionsManager::getActivityScore(const Connection &connection)
{
    std::string key = hashConnection(connection);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        if(it->second.timeactivity == 0 && !it->second.active)
            return 0;
        else if(it->second.timeactivity != 0 && it->second.active){
            return it->second.score + (std::time(nullptr) - it->second.timeactivity);
        }
        else if(it->second.timeactivity != 0 && !it->second.active){
            uint64_t score;
            if(it->second.score != 0){
                auto delta_score = std::time(nullptr) - it->second.timeactivity;
                return it->second.score <= delta_score ? 0 : it->second.score - delta_score;
            }
            else
                return std::time(nullptr) - it->second.timeactivity;
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
        // if(dbActivity.query(fmt::format("SELECT COUNT(*) FROM Activity WHERE hash ='{}';", pair.first))){
        //     // dbActivity.update(fmt::format("UPDATE Activity SET timeactivity ='{}' activity = {} score = {} WHERE hash ='{}';",
        //     //                        std::time(nullptr), pair.second.active, score, pair.first));
        // }
        // else{
        //     dbActivity.close();
            insertActivity(pair.first, pair.second);
        // }
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
