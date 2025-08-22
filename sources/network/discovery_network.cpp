#include "../../headers/network/discovery_network.h"

Network::Discovery::DiscoveryScanner::DiscoveryScanner(QObject *parent)
    : QObject(parent) {
    connect(this, &DiscoveryScanner::startSocket, this, &DiscoveryScanner::initSocket, Qt::QueuedConnection);
    randomMessageId = Utils::generate_random_hex(10);
}

Network::Discovery::DiscoveryScanner::DiscoveryScanner(quint32 startIp, QObject *parent)
    : QObject(parent)
    , startIpInt(startIp)
    , currentIpInt(startIp) {
    endIpInt = startIp + (1 << 16);
    qDebug() << QHostAddress(endIpInt).toString();

    connect(this, &DiscoveryScanner::startSocket, this, &DiscoveryScanner::initSocket, Qt::QueuedConnection);
    randomMessageId = Utils::generate_random_hex(10);
}

Network::Discovery::DiscoveryScanner::DiscoveryScanner(quint32     startIp,
                                                       quint32     endIp,
                                                       std::string messageId,
                                                       QObject    *parent)
    : QObject(parent)
    , startIpInt(startIp)
    , endIpInt(endIp)
    , randomMessageId(messageId)
    , currentIpInt(startIp) {
    connect(this, &DiscoveryScanner::startSocket, this, &DiscoveryScanner::initSocket, Qt::QueuedConnection);
}

Dfs::Packets::DiscoveryData Network::Discovery::DiscoveryScanner::getFoundedDiscoveryData() const {
    return foundedDiscoveryData;
}

void Network::Discovery::DiscoveryScanner::setFoundedDiscoveryData(const Dfs::Packets::DiscoveryData data) {
    foundedDiscoveryData = data;
    emit finished();
    foundedServer = true;
}

void Network::Discovery::DiscoveryScanner::multiThreadScan() {
    IpRange     current { startIpInt, endIpInt };
    const QTime startedTime = QDateTime::currentDateTime().time();
    qDebug() << "Started at:" << startedTime.toString("hh:mm:ss");

    bool found = false;

    while (current.end < QHostAddress("192.170.0.0").toIPv4Address() && !found) {
        qDebug() << "Scan next" << QHostAddress(current.start).toString() << QHostAddress(current.end).toString();

        quint32 startIp      = current.start;
        quint32 endIp        = current.end;
        int     threadsCount = 4;
        quint32 step         = (endIp - startIp + 1) / threadsCount;

        QElapsedTimer timer;
        timer.start();

        QList<QThread *>          threads;
        QList<DiscoveryScanner *> scanners;

        QEventLoop loop;
        int        finishedScanners = 0;

        for (int i = 0; i < threadsCount; ++i) {
            quint32 rangeStart = startIp + i * step;
            quint32 rangeEnd   = (i == threadsCount - 1) ? endIp : (rangeStart + step - 1);

            QThread          *thread  = new QThread;
            DiscoveryScanner *scanner = new DiscoveryScanner(rangeStart, rangeEnd, randomMessageId);
            scanners.append(scanner);

            scanner->moveToThread(thread);

            QObject::connect(thread, &QThread::started, scanner, &DiscoveryScanner::run);

            QObject::connect(scanner, &DiscoveryScanner::finished, thread, &QThread::quit);
            QObject::connect(scanner, &DiscoveryScanner::finished, [&finishedScanners, &loop, threadsCount]() {
                finishedScanners++;
                if (finishedScanners == threadsCount) {
                    loop.quit();
                }
            });

            // QObject::connect(thread, &QThread::finished, scanner, &QObject::deleteLater);
            // QObject::connect(thread, &QThread::finished, thread, &QObject::deleteLater);

            QObject::connect(scanner,
                             &DiscoveryScanner::ipFound,
                             [this, &timer, &loop, &found](Dfs::Packets::DiscoveryData discoveryData) {
                                 qDebug() << "Found server"
                                          << "Elapsed:" << (timer.elapsed() / 1000) << "sec";
                                 if (discoveryData.ready_to_connect && !discoveryData.message_id.empty()) {
                                     setFoundedDiscoveryData(discoveryData);
                                     found = true;
                                     loop.quit();
                                 }
                             });

            threads.append(thread);
            thread->start();
        }

        loop.exec();

        for (QThread *thread : threads) {
            thread->quit();
            thread->wait();
            delete thread; // видаляємо безпосередньо
        }
        for (auto *scanner : scanners) {
            delete scanner;
        }

        if (found) {
            break;
        }

        current = shiftSubnet(current, 1);
    }

    qDebug() << "Scanning finished";
}

Network::Discovery::IpRange Network::Discovery::DiscoveryScanner::shiftSubnet(const IpRange &range, int shift) {
    quint32 start = range.start;
    quint32 end   = range.end;

    quint32 firstOctet  = (start >> 24) & 0xFF;
    quint32 secondOctet = (start >> 16) & 0xFF;

    secondOctet += shift;
    if (secondOctet > 255) {
        firstOctet += secondOctet / 256;
        secondOctet = secondOctet % 256;
    }
    if (firstOctet > 255)
        firstOctet = 255;

    quint32 rangeSize = ((end >> 16) & 0xFF) - ((start >> 16) & 0xFF);

    quint32 newEndSecondOctet = secondOctet + rangeSize;
    quint32 newEndFirstOctet  = firstOctet;

    if (newEndSecondOctet > 255) {
        newEndFirstOctet += newEndSecondOctet / 256;
        newEndSecondOctet = newEndSecondOctet % 256;
    }
    if (newEndFirstOctet > 255)
        newEndFirstOctet = 255;

    IpRange newRange;
    newRange.start = (firstOctet << 24) | (secondOctet << 16);
    newRange.end   = (newEndFirstOctet << 24) | (newEndSecondOctet << 16);

    return newRange;
}

void Network::Discovery::DiscoveryScanner::run() {
    emit startSocket();
    QTimer::singleShot(20, this, &DiscoveryScanner::scanNext);
}

void Network::Discovery::DiscoveryScanner::initSocket() {
    socket = new QUdpSocket(this);
    socket->bind(QHostAddress::Any, port, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint);
    connect(socket, &QUdpSocket::readyRead, this, &DiscoveryScanner::onReadyRead);
}

void Network::Discovery::DiscoveryScanner::scanNext() {
    if (currentIpInt > endIpInt || foundedServer) {
        qDebug() << "Scan finished for thread";
        emit finished();
        return;
    }

    int                         sent = 0;
    Dfs::Packets::DiscoveryData discoveryData(randomMessageId);
    auto                        byteData = QByteArray::fromStdString(MessagePack::serialize(discoveryData));
    while (sent < batchSize && currentIpInt <= endIpInt && !foundedServer) {
        QHostAddress addr(currentIpInt);
        QString      ip = addr.toString();
        socket->writeDatagram(byteData, addr, port);
        currentIpInt++;
        sent++;
    }

    if (!foundedServer)
        QTimer::singleShot(intervalMs, this, &DiscoveryScanner::scanNext);
    else
        emit finished();
}

void Network::Discovery::DiscoveryScanner::onReadyRead() {
    while (socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(socket->pendingDatagramSize());
        QHostAddress sender;
        quint16      senderPort;

        socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        auto discoveryData = MessagePack::deserialize<Dfs::Packets::DiscoveryData>(datagram.toStdString());

        qDebug() << "Response from" << sender.toString() << "| Message ID:" << discoveryData->message_id
                 << "| Ready to connect:" << discoveryData->ready_to_connect;

        if (discoveryData->ready_to_connect) {
            foundedServer        = true;
            foundedDiscoveryData = discoveryData.value();

            emit ipFound(discoveryData.value());
            emit finished();
        }
    }
}

Network::Discovery::DiscoveryResponder::DiscoveryResponder(QObject *parent)
    : QObject(parent) {
    socket = new QUdpSocket(this);
    if (!socket->bind(QHostAddress::Any, port)) {
        qFatal("Cannot bind UDP socket");
    }

    connect(socket, &QUdpSocket::readyRead, this, &DiscoveryResponder::onReadyRead);
    qDebug() << "UDP Server started on port" << port;
}

void Network::Discovery::DiscoveryResponder::onReadyRead() {
    while (socket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(socket->pendingDatagramSize());
        QHostAddress sender;
        quint16      senderPort;

        socket->readDatagram(datagram.data(), datagram.size(), &sender, &senderPort);
        auto discoveryData = MessagePack::deserialize<Dfs::Packets::DiscoveryData>(datagram.toStdString());

        qDebug() << "Got request from" << discoveryData->message_id << ":" << datagram;
        discoveryData->ip = sender.toIPv4Address() ? QHostAddress(sender.toIPv4Address()).toString().toStdString()
                                                   : sender.toString().toStdString();
        socket->writeDatagram(QByteArray::fromStdString(MessagePack::serialize(discoveryData.value())),
                              sender,
                              senderPort);
    }
}
