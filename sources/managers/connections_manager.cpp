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

#include "managers/connections_manager.h"
#include "encryption/encryption_tools.h"
#include "utils/exc_utils.h"

ConnectionsManager::ConnectionsManager(
    const std::string address,
    const std::string port,
    const QByteArray  key,
    QObject          *parent)
    : QObject(parent)
    , m_address(address)
    , m_port(port)
    , m_key(key)
    , dbConnector(dbPath)
    , dbActivity(dbActPath) {
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

ConnectionsManager::~ConnectionsManager() {
    // synchroActivityDB();
}

bool ConnectionsManager::createTable() {
    dbConnector.open();
    bool createdTableConnnections = false;

    if (dbConnector.select_all(ConnectionsTableName).empty()) {
        static const std::string CreateTableQuery = fmt::format(
            "CREATE TABLE IF NOT EXISTS {}("
            "hash        TEXT             NOT NULL, "
            "address     TEXT             NOT NULL, "
            "port        TEXT             NOT NULL, "
            "active      TEXT             NOT NULL);",
            ConnectionsTableName);
        createdTableConnnections = dbConnector.create_table(CreateTableQuery);
    }
    dbConnector.close();
    return createdTableConnnections;
}

bool ConnectionsManager::createActivityTable() {
    dbActivity.open();
    bool createdTableActivity = false;

    if (dbActivity.select_all(ActivityTableName).empty()) {
        static const std::string CreateTableQuery = fmt::format(
            "CREATE TABLE IF NOT EXISTS {}("
            "hash          TEXT          NOT NULL, "
            "timeactivity  TEXT          NOT NULL, "
            "activity      TEXT          NOT NULL, "
            "score         TEXT          NOT NULL);",
            ActivityTableName);

        createdTableActivity = dbActivity.create_table(CreateTableQuery);
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

bool ConnectionsManager::insertConnection(const Dfs::Packets::Connection &connection) {
    dbConnector.open();
    DbRow row = ecryptConnection(connection);

    bool result = dbConnector.insert(ConnectionsTableName, row);
    dbConnector.close();
    return result;
}

bool ConnectionsManager::insertActivity(const std::string hash, const Dfs::Packets::Activity &activity) {
    dbActivity.open();
    DbRow row = ecryptActivity(hash, activity);

    bool result = dbActivity.insert(ActivityTableName, row);
    dbActivity.close();

    return result;
}

void ConnectionsManager::loadRecords() {
    dbConnector.open();

    const auto rows = dbConnector.select_all(ConnectionsTableName);
    if (!rows.empty()) {
        for (const auto &row : rows) {
            Connection connection = decryptConnection(row);
            newConnections.push_back(connection);
        }
    }

    dbConnector.close();
}

void ConnectionsManager::loadActivityRecords() {
    dbActivity.open();
    const auto rows = dbActivity.select_all(ActivityTableName);
    if (!rows.empty()) {
        for (const auto &row : rows) {
            auto act = decryptActivity(row);
            //         // act.second.timeactivity = std::time(nullptr);
            //         // clientActivity.insert(act);
        }
    }
    dbActivity.close();
}

void ConnectionsManager::removeConnection(const Dfs::Packets::Connection &connection) {
    dbConnector.open();
    dbConnector.query(
        fmt::format("DELETE FROM {} WHERE hash = '{}'", ConnectionsTableName, hashConnection(connection)));
    dbConnector.close();
}

std::string ConnectionsManager::hashConnection(const Dfs::Packets::Connection &connection) {
    return Utils::calculate_hash(connection.address + connection.port);
}

DbRow ConnectionsManager::ecryptConnection(const Dfs::Packets::Connection &connection) {
    std::string hash = hashConnection(connection);
    auto        key  = Cryptography::getKeyPassFromPassword(hash);

    std::string ecryptedAddress = Cryptography::encrypt(connection.address, key);
    std::string encryptedPort   = Cryptography::encrypt(connection.port, key);
    std::string encryptedActive = Cryptography::encrypt(std::to_string(connection.active), key);

    DbRow row { { hash_connection, hash },
                { port_connection, encryptedPort },
                { address_connection, ecryptedAddress },
                { active_connection, encryptedActive } };

    return row;
}

DbRow ConnectionsManager::ecryptActivity(const std::string hash, const Activity &activity) {
    auto key = Cryptography::getKeyPassFromPassword(hash);

    std::string timeactivity = Cryptography::encrypt(std::to_string(activity.timeactivity), key);
    std::string active       = Cryptography::encrypt(std::to_string(activity.active), key);
    std::string score        = Cryptography::encrypt(std::to_string(activity.score), key);

    DbRow row { { hash_connection, hash },
                { active_connection, active },
                { score_act, score },
                { time_act, timeactivity } };

    return row;
}

Connection ConnectionsManager::decryptConnection(const DbRow &row) {
    Connection connection;
    auto       key = Cryptography::getKeyPassFromPassword(row.at(hash_connection));

    connection.port    = Cryptography::decrypt(row.at(port_connection), key);
    connection.address = Cryptography::decrypt(row.at(address_connection), key);
    connection.active  = std::stoi(Cryptography::decrypt(row.at(active_connection), key));
    return connection;
}

std::pair<std::string, Activity> ConnectionsManager::decryptActivity(const DbRow &row) {
    Activity activity;
    auto     key = Cryptography::getKeyPassFromPassword(row.at(hash_connection));

    activity.timeactivity = std::stoull(Cryptography::decrypt(row.at(time_act), key));
    activity.active       = Cryptography::decrypt(row.at(active_connection), key) == "1" ? true : false;
    activity.score        = std::stoi(Cryptography::decrypt(row.at(score_act), key));
    return std::make_pair(ByteArray(key).toString(), activity);
}

void ConnectionsManager::addConnection(const Dfs::Packets::Connection &connection) {
    if (connection.active) {
        activeConnections.push_back(connection);
        insertConnection(connection);
    }
}

void ConnectionsManager::tryToNewConnect() {
    for (const auto &connection : newConnections) {
        // try connect

        // remove connection from new connections list
        newConnections.erase(
            std::remove_if(
                newConnections.begin(),
                newConnections.end(),
                [&](Connection const &item) {
                    return item.address == connection.address && item.port == connection.port;
                }),
            newConnections.end());
    }
}

void ConnectionsManager::addNewConnection(const Dfs::Packets::Connection &connection) {
    newConnections.push_back(connection);
}

void ConnectionsManager::addActivity(const Connection &connection) {
    std::string key = hashConnection(connection);
    // std::string key = SecretKey::getKeyFromPass(hash);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        it->second.active       = true;
        it->second.score        = std::time(nullptr) - clientActivity.at(key).timeactivity;
        it->second.timeactivity = std::time(nullptr);
        clientActivity.insert(std::make_pair(key, it->second));
    } else {
        Activity act;
        act.timeactivity = std::time(nullptr);
        act.score        = 0;
        act.active       = true;
        clientActivity.insert(std::make_pair(key, act));
    }
}

void ConnectionsManager::removeActivity(const Connection &connection) {
    std::string key = hashConnection(connection);
    Activity    act;
    act.active             = false;
    act.score              = std::time(nullptr) - clientActivity.at(key).timeactivity;
    act.timeactivity       = std::time(nullptr);
    clientActivity.at(key) = act;
}

std::uint64_t ConnectionsManager::getActivityScore(const Connection &connection) {
    std::string key = hashConnection(connection);

    auto it = clientActivity.find(key);
    if (it != clientActivity.end()) {
        if (it->second.timeactivity == 0 && !it->second.active)
            return 0;
        else if (it->second.timeactivity != 0 && it->second.active) {
            return it->second.score + (std::time(nullptr) - it->second.timeactivity);
        } else if (it->second.timeactivity != 0 && !it->second.active) {
            std::uint64_t score;
            if (it->second.score != 0) {
                auto delta_score = std::time(nullptr) - it->second.timeactivity;
                return it->second.score <= delta_score ? 0 : it->second.score - delta_score;
            } else
                return std::time(nullptr) - it->second.timeactivity;
        }
    }

    return 0;
}

void ConnectionsManager::synchroActivityDB() {
    for (const auto &pair : clientActivity) {
        Activity      act = pair.second;
        std::uint64_t score;
        auto          delta_score = std::time(nullptr) - pair.second.timeactivity;
        if (pair.second.active) {
            score = pair.second.score + delta_score;
        } else {
            score = pair.second.score - delta_score;
            if (score <= 0)
                score = 0;
        }
        // if(dbActivity.query(fmt::format("SELECT COUNT(*) FROM Activity WHERE hash ='{}';", pair.first))){
        //     // dbActivity.update(fmt::format("UPDATE Activity SET timeactivity ='{}' activity = {} score =
        //     {} WHERE hash ='{}';",
        //     //                        std::time(nullptr), pair.second.active, score, pair.first));
        // }
        // else{
        //     dbActivity.close();
        insertActivity(pair.first, pair.second);
        // }
    }
}

void ConnectionsManager::removeConnection(Dfs::Packets::Connection &connection) {
    activeConnections.erase(
        std::remove_if(
            activeConnections.begin(),
            activeConnections.end(),
            [&](Connection const &item) {
                return item.address == connection.address && item.port == connection.port;
            }),
        activeConnections.end());
}

bool ConnectionsManager::isConnection(const Dfs::Packets::Connection &connection) {
    return m_address == connection.address && m_port == connection.port;
}
