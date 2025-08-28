#include "../../headers/network/discovery_network.h"

Network::Discovery::DiscoveryScanner::DiscoveryScanner(QObject *parent)
    : QObject(parent) {
    randomMessageId = Utils::generate_random_hex(10);
}
Network::Discovery::DiscoveryScanner::DiscoveryScanner(quint32 startIp, QObject *parent)
    : QObject(parent)
    , startIpInt(startIp)
    , currentIpInt(startIp) {
}

Network::Discovery::DiscoveryScanner::DiscoveryScanner(quint32 startIp, quint32 endIp, QObject *parent)
    : QObject(parent)
    , startIpInt(startIp)
    , endIpInt(endIp)
    , currentIpInt(startIp) {
    randomMessageId = Utils::generate_random_hex(10);
}

Dfs::Packets::DiscoveryData Network::Discovery::DiscoveryScanner::getFoundedDiscoveryData() const {
    return foundedDiscoveryData;
}

void Network::Discovery::DiscoveryScanner::setFoundedDiscoveryData(const Dfs::Packets::DiscoveryData data) {
    foundedDiscoveryData = data;
    foundedServer        = true;
    emit finished();
}

void Network::Discovery::DiscoveryScanner::run() {
    QTimer::singleShot(0, this, &DiscoveryScanner::scanNext);
}

void Network::Discovery::DiscoveryScanner::scanNext() {
    if (currentIpInt > endIpInt || foundedServer) {
        emit finished();
        return;
    }

    int                 sent = 0;
    QList<QTcpSocket *> sockets;
    while (sent < batchSize && currentIpInt <= endIpInt && !foundedServer) {
        QHostAddress addr(currentIpInt++);
        auto        *tcpSocket = new QTcpSocket(this);
        sockets.append(tcpSocket);

        connect(tcpSocket, &QTcpSocket::connected, this, [tcpSocket, this]() {
            Dfs::Packets::DiscoveryData discoveryData(randomMessageId);
            QByteArray                  data = QByteArray::fromStdString(MessagePack::serialize(discoveryData));
            tcpSocket->write(data);
            tcpSocket->flush();
        });

        connect(tcpSocket, &QTcpSocket::readyRead, this, [tcpSocket, this]() {
            QByteArray data          = tcpSocket->readAll();
            auto       discoveryData = MessagePack::deserialize<Dfs::Packets::DiscoveryData>(data.toStdString());

            if (discoveryData->ready_to_connect) {
                setFoundedDiscoveryData(discoveryData.value());
                qDebug() << "Founded" << discoveryData.value().ip;
                emit ipFound(discoveryData.value());
            }

            tcpSocket->disconnectFromHost();
            tcpSocket->deleteLater();
        });

        connect(tcpSocket, &QTcpSocket::errorOccurred, tcpSocket, &QTcpSocket::deleteLater);

        tcpSocket->connectToHost(addr, port);
        sent++;
    }

    if (!foundedServer && currentIpInt <= endIpInt) {
        QTimer::singleShot(20, this, &DiscoveryScanner::scanNext);
    }
}

Network::Discovery::IpRange Network::Discovery::DiscoveryScanner::shiftSubnet(const IpRange &range, int shift) {
    quint32 start = range.start;
    quint32 end   = range.end;

    quint32 firstOctet  = (start >> 24) & 0xFF;
    quint32 secondOctet = (start >> 16) & 0xFF;

    secondOctet += shift;
    if (secondOctet > 255) {
        firstOctet += secondOctet / 256;
        secondOctet %= 256;
    }
    if (firstOctet > 255)
        firstOctet = 255;

    quint32 rangeSize         = ((end >> 16) & 0xFF) - ((start >> 16) & 0xFF);
    quint32 newEndSecondOctet = secondOctet + rangeSize;
    quint32 newEndFirstOctet  = firstOctet;

    if (newEndSecondOctet > 255) {
        newEndFirstOctet += newEndSecondOctet / 256;
        newEndSecondOctet %= 256;
    }
    if (newEndFirstOctet > 255)
        newEndFirstOctet = 255;

    IpRange newRange;
    newRange.start = (firstOctet << 24) | (secondOctet << 16);
    newRange.end   = (newEndFirstOctet << 24) | (newEndSecondOctet << 16);
    return newRange;
}

Network::Discovery::DiscoveryResponder::DiscoveryResponder(QObject *parent)
    : QObject(parent) {

    server = new QTcpServer(this);
    connect(server, &QTcpServer::newConnection, this, &DiscoveryResponder::onNewConnection);

    if (!server->listen(QHostAddress::Any, port)) {
        qFatal("Cannot start TCP server");
    }

    qDebug() << "TCP server started on port" << port;
}

void Network::Discovery::DiscoveryResponder::onNewConnection() {
    while (server->hasPendingConnections()) {
        QTcpSocket *clientSocket = server->nextPendingConnection();
        connect(clientSocket, &QTcpSocket::readyRead, this, &DiscoveryResponder::onReadyRead);
        connect(clientSocket, &QTcpSocket::disconnected, clientSocket, &QTcpSocket::deleteLater);
    }
}

void Network::Discovery::DiscoveryResponder::onReadyRead() {
    QTcpSocket *clientSocket = qobject_cast<QTcpSocket *>(sender());
    if (!clientSocket)
        return;

    QByteArray data = clientSocket->readAll();
    try {
        auto discoveryData = MessagePack::deserialize<Dfs::Packets::DiscoveryData>(data.toStdString());

        discoveryData->ip   = clientSocket->peerAddress().toString().toStdString();
        QByteArray response = QByteArray::fromStdString(MessagePack::serialize(discoveryData.value()));
        qDebug() << "Send answer ip " << discoveryData->ip;

        clientSocket->write(response);
        clientSocket->flush();
    } catch (const std::exception &ex) {
        qWarning() << "Failed to parse DiscoveryData:" << ex.what();
    }
}
