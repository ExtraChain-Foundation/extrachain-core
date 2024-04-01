#pragma once

#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"
#include <QObject>
#include <algorithm>

using DFSP::Connection;
using DFSP::Activity;

static const std::string ConnectionsTableName = "Connections";
static const std::string ActivityTableName = "Activity";
static const std::string dbPath = "tmp/temp.dat";
static const std::string dbActPath = "tmp/activity.dat";
static const std::string hash_connection = "hash";
static const std::string port_connection = "port";
static const std::string address_connection = "address";
static const std::string active_connection = "active";
static const std::string time_act = "timeactivity";
static const std::string status_act = "active";
static const std::string score_act = "score";


class ConnectionsManager : public QObject {
    Q_OBJECT

    DBConnector dbConnector;
    DBConnector dbActivity;
    std::vector<Connection> activeConnections;
    std::vector<Connection> newConnections;
    std::unordered_map<std::string, Activity> clientActivity;
    std::string m_port, m_address;
    QByteArray m_key;

public:
    ConnectionsManager(const std::string address, const std::string port, const QByteArray key, QObject *parent = nullptr);
    ~ConnectionsManager();

    void addConnection(const Connection &connection);
    void tryToNewConnect();
    void addNewConnection(const Connection &connection);
    void addActivity(const Connection &connection);
    void removeActivity(const Connection &connection);
    uint64_t getActivityScore(const Connection &connection);
    void synchroActivityDB();
    void removeConnection(Connection &connection);
    bool isConnection(const Connection &connection);
    const std::string &port() const;
    void setPort(const std::string &newPort);
    const std::string &address() const;
    void setAddress(const std::string &newAddress);
    const std::vector<Connection> &getActiveConnection() const;

    DBRow ecryptConnection(const Connection &connection);
    Connection decryptConnection(const DBRow &row);
    std::pair<std::string, Activity> decryptActivity(const DBRow &row);
    void loadRecords();
    void loadActivityRecords();
    void removeConnection(const Connection &connection);

protected:
    bool createTable();
    bool createActivityTable();
    bool insertConnection(const Connection &connection);

private:
    std::string hashConnection(const Connection &connection);
};
