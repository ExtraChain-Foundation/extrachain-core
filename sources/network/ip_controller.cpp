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

void IPController::connectToIP(const std::string &ip, const int &port, const std::string &actor) {
    IPConnection newIpConnection(ip, port, actor);
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
    connectToIP(decryptedIpAddress, ipConnection.IP_Port, ipConnection.Actor);
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

IPConnection::IPConnection(const std::string ip, const int port, const std::string actor)
    : ip(ip)
    , port(port)
    , actor(actor) {
}

IPConnection::IPConnection(DFS::Packets::IPConnection ipConnection)
    : ip(ipConnection.IP_Address)
    , port(ipConnection.IP_Port)
    , actor(ipConnection.Actor) {
}

bool IPConnection::operator==(const IPConnection &ipConnection) const {
    return ipConnection.ip == ip && ipConnection.port == port;
}

IPLoader::IPLoader() {
    DFSIP::createTable();
}

void IPLoader::save(IPConnection ipConnection) {
    DBConnector dbConnector(DFSB::ipdirsPath);
    dbConnector.open();
    if (!dbConnector.isOpen()) {
        exit(-1);
    }
    const auto count = dbConnector.count(DFSIP::ipTableName,
                                         fmt::format("ip='{}' AND port = {} AND actor = '{}'",
                                                     ipConnection.ip, ipConnection.port, ipConnection.actor));
    if (!count) {
        auto row = makeDBRow(ipConnection.ip, ipConnection.port, ipConnection.actor);
        dbConnector.insert(DFSIP::ipTableName, row);
    }
}

void IPLoader::save(std::vector<IPConnection> ipConnections) {
    DBConnector dbConnector(DFSB::ipdirsPath);
    dbConnector.open();
    for (const auto &connection : ipConnections) {
        save(connection);
    }
}

std::vector<IPConnection> IPLoader::load() {
    DBConnector dbConnector(DFSB::ipdirsPath);
    dbConnector.open();
    const auto ipConnectionData = DFSIP::getAllIpConnections();
    std::vector<IPConnection> ipConnections;
    for (const auto connection : ipConnectionData) {
        ipConnections.push_back(connection);
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
    }
}

float IPLoader::ranking(IPConnection &ipconnection) {
    const auto countConnected = getCount(ipconnection, IPConnectionEvent::Connected);
    const auto countDisonnected = getCount(ipconnection, IPConnectionEvent::Disconnected);

    ipconnection.rank = round((((float)countConnected / (float)countDisonnected) * 5) * 100) / 100;
    return ipconnection.rank;
}

DBRow IPLoader::makeDBRow(const std::string ip, const uint64_t port, const std::string anchor,
                          const uint64_t connectedCount, const uint64_t disconnectedCount) {
    DBRow row;
    row.insert({ "ip", ip });
    row.insert({ "port", std::to_string(port) });
    row.insert({ "actor", anchor });
    row.insert({ "connected_count", std::to_string(connectedCount) });
    row.insert({ "disconnected_count", std::to_string(disconnectedCount) });

    return row;
}

DFSP::IPConnection IPLoader::convertIntoIPConnection(const IPConnection &ipconnection) {
    return DFS::Packets::IPConnection { .Actor = ipconnection.actor,
                                        .IP_Address = ipconnection.ip,
                                        .IP_Port = static_cast<uint64_t>(ipconnection.port) };
}
