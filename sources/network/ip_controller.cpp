#include "network/ip_controller.h"
#include <QDebug>
#include "network/network_manager.h"
#include "utils/dfs_utils.h"

IPController::IPController(ExtraChainNode &node)
    : m_node(node) {
}

void IPController::connectToIP(const std::string &ip, const int &port) {
    IPConnection newIpConnection(ip, port);
    if (std::find(ipConnections.begin(), ipConnections.end(), newIpConnection) == ipConnections.end()) {
        ipConnections.push_back(newIpConnection);
    }
}

void IPController::connectToIP(const DFS::Packets::IPConnection &ipConnection) {
    auto first = m_node.actorIndex()->getActor(ipConnection.Actor);
    auto &mainKey = m_node.accountController()->mainActor().key();
    auto &publicKey = first.key().publicKey();
    const auto decryptedIpAddress = mainKey.decrypt(ipConnection.IP_Address, publicKey);
    qDebug() << "Decrypt ip connection: " << decryptedIpAddress.c_str();
    connectToIP(decryptedIpAddress, ipConnection.IP_Port);
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

IPConnection::IPConnection(const std::string ip, const int port)
    : ip(ip)
    , port(port) {
}

bool IPConnection::operator==(const IPConnection &ipConnection) const {
    return ipConnection.ip == ip && ipConnection.port == port;
}
