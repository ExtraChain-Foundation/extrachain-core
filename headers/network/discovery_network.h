#pragma once

#include <QObject>
#include <QUdpSocket>
#include <QThread>
#include "dfs/dfs_utils.h"

namespace Network {
    namespace Discovery {
        struct IpRange {
            quint32 start;
            quint32 end;
        };

        class DiscoveryScanner : public QObject {
            Q_OBJECT

        public:
            DiscoveryScanner(QObject* parent = nullptr);
            DiscoveryScanner(quint32 startIp, QObject* parent = nullptr);
            DiscoveryScanner(quint32 startIp, quint32 endIp, std::string messageId, QObject* parent = nullptr);

            Dfs::Packets::DiscoveryData getFoundedDiscoveryData() const;
            void                        setFoundedDiscoveryData(const Dfs::Packets::DiscoveryData data);
            void                        multiThreadScan();
            IpRange                     shiftSubnet(const IpRange& range, int shift);
            ;

        signals:
            void ipFound(Dfs::Packets::DiscoveryData discoveryData);
            void finished();
            void startSocket();

        public slots:
            void run();
            void initSocket();
            void scanNext();
            void onReadyRead();

        private:
            QUdpSocket*                 socket     = nullptr;
            quint16                     port       = 17594;
            int                         batchSize  = 100;
            int                         intervalMs = 30;
            quint32                     startIpInt, endIpInt, currentIpInt;
            bool                        foundedServer;
            Dfs::Packets::DiscoveryData foundedDiscoveryData;
            std::string                 randomMessageId;
        };

        class DiscoveryResponder : public QObject {
            Q_OBJECT
            uint        port = 17594;
            std::string ip;

        public:
            DiscoveryResponder(QObject* parent = nullptr);

        private slots:
            void onReadyRead();

        private:
            QUdpSocket* socket;
        };
    } // namespace Discovery

} // namespace Network
