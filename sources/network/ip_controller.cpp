#include "network/ip_controller.h"
#include "network/network_manager.h"
#include "utils/dfs_utils.h"
#include <QDebug>

IPController::IPController(ExtraChainNode &node)
    : m_node(node)
    , ipLoader(new IPLoader) {
    ipConnections = ipLoader->load();
    qDebug() << "Count ip connections: " << ipConnections.size();
}

IPController::~IPController() {
    delete ipLoader;
}

void IPController::connectToIp(IPConnection connection) {
    if (std::find(ipConnections.begin(), ipConnections.end(), connection) == ipConnections.end()) {
        ipConnections.push_back(connection);
        ipLoader->save(connection);
    }
}

void IPController::connectToIP(const std::string &ip, const int &port, const std::string &actor,
                               const std::string &identifier) {
    IPConnection newIpConnection(ip, port, actor, identifier);
    if (std::find(ipConnections.begin(), ipConnections.end(), newIpConnection) == ipConnections.end()) {
        ipConnections.push_back(newIpConnection);
        ipLoader->save(newIpConnection);
    }
}

void IPController::connectToIP(const DFS::Packets::IPConnection &ipConnection) {
    auto first = m_node.actorIndex()->getActor(ipConnection.Actor);
    auto &mainKey = m_node.accountController()->mainActor().key();
    auto &publicKey = first.key().publicKey();
    const auto decryptedIpAddress = mainKey.decrypt(ipConnection.IP_Address, publicKey);
    qDebug() << "Decrypt ip connection: " << decryptedIpAddress.c_str();
    connectToIP(decryptedIpAddress, ipConnection.IP_Port, ipConnection.Actor, ipConnection.Identifier);
}

void IPController::getIpConnections(const ActorId &actor) {
    auto first = m_node.actorIndex()->getActor(actor);
    auto &mainKey = m_node.accountController()->mainActor().key();
    auto &publicKey = first.key().publicKey();

    for (const auto ipConnection : ipConnections) {
        DFSP::IPConnection msg = { .Actor = actor.toStdString(),
                                   .IP_Address = mainKey.encrypt(ipConnection.ip, publicKey),
                                   .IP_Port = static_cast<uint64_t>(ipConnection.port) };
        qDebug() << "IP data port[" << msg.IP_Port << "][" << msg.IP_Address.c_str() << "]";
        qDebug() << "Decrypt: " << mainKey.decrypt(msg.IP_Address, publicKey).c_str();
        m_node.network()->send_message(msg, MessageType::IpNewConnection);
    }
}

std::vector<IPConnection> IPController::allIpConnections() {
    return ipConnections;
}

void IPController::saveRate(const IPConnection &ipconnection, IPConnection::Rate &rate) {
    ipLoader->saveRate(ipconnection, rate);
}

IPConnection::IPConnection(const std::string ip, const int port, const std::string actor,
                           const std::string identifier)
    : ip(ip)
    , port(port)
    , actor(actor)
    , identifier(identifier) {
}

IPConnection::IPConnection(const SocketService *socket, const std::string actor)
    : ip(socket->ip().toStdString())
    , port(socket->port())
    , actor(actor)
    , identifier(socket->identifier().toStdString()) {
}

IPConnection::IPConnection(DFS::Packets::IPConnection ipConnection)
    : ip(ipConnection.IP_Address)
    , port(ipConnection.IP_Port)
    , actor(ipConnection.Actor)
    , identifier(ipConnection.Identifier) {
}

bool IPConnection::operator==(const IPConnection &ipConnection) const {
    return ipConnection.ip == ip && ipConnection.port == port;
}

IPLoader::IPLoader() {
    DFSIP::createTable();
}

void IPLoader::save(IPConnection ipConnection) {
    DBConnector dbConnector(DFSIP::ipdirsPath);
    dbConnector.open();
    if (!dbConnector.isOpen()) {
        exit(-1);
    }
    const auto count = dbConnector.count(DFSIP::ipTableName,
                                         fmt::format("ip='{}' AND port = {} AND actor = '{}'",
                                                     ipConnection.ip, ipConnection.port, ipConnection.actor));
    if (!count) {
        auto row = makeDBRow(ipConnection);
        dbConnector.insert(DFSIP::ipTableName, row);
    }

    if (!ipConnection.rates.empty()) {
        for (const auto rate : ipConnection.rates) {
            auto rateRow = makeRateDBRow(ipConnection, rate);
            dbConnector.insert(DFSIP::Rate::connectionRateTableName, rateRow);
        }
    }
}

void IPLoader::save(std::vector<IPConnection> ipConnections) {
    DBConnector dbConnector(DFSIP::ipdirsPath);
    dbConnector.open();
    for (const auto &connection : ipConnections) {
        save(connection);
    }
}

void IPLoader::saveRate(const IPConnection &ipconnection, IPConnection::Rate &rate) {
    save(ipconnection);
    DBConnector dbConnector(DFSIP::ipdirsPath);
    dbConnector.open();
    if (!dbConnector.isOpen()) {
        exit(-1);
    }

    const auto count = dbConnector.count(
        DFSIP::Rate::connectionRateTableName,
        fmt::format("ip='{}' AND port = {} AND actor = '{}' AND identifier= '{}'", ipconnection.ip,
                    ipconnection.port, ipconnection.actor, ipconnection.identifier));
    if (!count) {
        auto rateRow = makeRateDBRow(ipconnection, rate);
        dbConnector.insert(DFSIP::Rate::connectionRateTableName, rateRow);
    } else {
        dbConnector.update(fmt::format("UPDATE '{}' SET connection_rate='{}' WHERE identifier='{}'",
                                       DFSIP::Rate::connectionRateTableName, std::to_string(rate.calcRate()),
                                       ipconnection.identifier));
    }
}

std::vector<IPConnection> IPLoader::load() {
    DBConnector dbConnector(DFSIP::ipdirsPath);
    dbConnector.open();
    const auto ipConnectionData = DFSIP::getAllIpConnections();
    std::vector<IPConnection> ipConnections;
    for (const auto connection : ipConnectionData) {
        ipConnections.push_back(IPConnection(connection));
    }
    return ipConnections;
}

void IPLoader::update(const IPConnection &ipconnection, const IPConnectionEvent &connectionEvent) {
    switch (connectionEvent) {
    case IPConnectionEvent::Connected:
        DFSIP::increaseConnectionCount(convertIntoIPConnection(ipconnection));
        break;
    case IPConnectionEvent::Disconnected:
        DFSIP::increaseDisconnectedCount(convertIntoIPConnection(ipconnection));
        break;
    }
}

int IPLoader::getCount(const IPConnection &ipconnection, const IPConnectionEvent &connectionEvent) {
    switch (connectionEvent) {
    case IPConnectionEvent::Connected:
        return DFSIP::getCountConnected(convertIntoIPConnection(ipconnection));
    case IPConnectionEvent::Disconnected:
        return DFSIP::getCountDisconnected(convertIntoIPConnection(ipconnection));
    default:
        return {};
    }
}

DBRow IPLoader::makeDBRow(const IPConnection &ipconnection, const uint64_t connectedCount,
                          const uint64_t disconnectedCount) {
    DBRow row;
    row.insert({ "ip", ipconnection.ip });
    row.insert({ "port", std::to_string(ipconnection.port) });
    row.insert({ "actor", ipconnection.actor });
    row.insert({ "identifier", ipconnection.identifier });
    row.insert({ "connected_count", std::to_string(connectedCount) });
    row.insert({ "disconnected_count", std::to_string(disconnectedCount) });

    return row;
}

DBRow IPLoader::makeRateDBRow(const IPConnection &ipconnection, const IPConnection::Rate &rate) const {
    DBRow row;
    row.insert({ "ip", ipconnection.ip });
    row.insert({ "port", std::to_string(ipconnection.port) });
    row.insert({ "actor", ipconnection.actor });
    row.insert({ "connection_rate", std::to_string(rate.connectionRate) });
    row.insert({ "ping_time", std::to_string(rate.pingTime) });
    row.insert({ "sent_data", std::to_string(rate.sentData) });
    row.insert({ "bandwidth", std::to_string(rate.bandWidth) });
    row.insert({ "identifier", ipconnection.identifier });

    return row;
}

DFSP::IPConnection IPLoader::convertIntoIPConnection(const IPConnection &ipconnection) {
    return DFS::Packets::IPConnection { .Actor = ipconnection.actor,
                                        .IP_Address = ipconnection.ip,
                                        .IP_Port = static_cast<uint64_t>(ipconnection.port),
                                        .Identifier = ipconnection.identifier };
}

IPConnection::Rate::Rate(const int &rPingTime, const int &rSentData)
    : pingTime(rPingTime)
    , sentData(rSentData) {
    lastRate = calcRate();
}

int IPConnection::Rate::calcRate() {
    if (pingTime == 0)
        pingTime = 1;
    bandWidth = (sentData / pingTime);
    connectionRate = (bandWidth / (pingTime * connectionRate));
    return connectionRate;
}
