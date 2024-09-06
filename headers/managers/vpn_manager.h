/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

#ifndef VPN_MANAGER_H
#define VPN_MANAGER_H

#include <QObject>

#include "extrachain_global.h"

class VPNManager;
typedef std::shared_ptr<VPNManager> VPNManagerPtr;

class EXTRACHAIN_EXPORT VPNManager : public QObject {
    Q_OBJECT

public:
    enum class Status {
        STOP = 0,
        SERVER,
        CLIENT
    };

    static std::shared_ptr<VPNManager> Instance(
        const QString& keysPrefix,
        const QString& interfaceName = "ExtrachainVPN");

    ~VPNManager();

    void startServiceServer(
        const QString& peerPublicKey,
        const QString& localServerIP      = "",
        const QString& listenPort         = "",
        const QString& peerLocalIPWithCDR = "");
    void startServiceClient();

    void stopService();

    QString getPublicKeyFilePath() const;

private:
    explicit VPNManager(const QString& keysPrefix, const QString& interfaceName);

    struct BashResultType {
        bool    success = false;
        QString output;
        QString errorOutput;
    };

    struct Interface {
        explicit
        Interface(bool isServer = true) {
            localIP = isServer
                          ? "10.0.0.1/24"
                          : "10.0.0.2/32";        // default local IP for VPN server and for VPN client
            listenPort = isServer ? "51820" : ""; // default Listen Port for VPN server
        }

        QString privateKey;
        QString publicKey;

        QString localIP;
        QString listenPort;
        QString dns;
    };

    struct Peer {
        explicit
        Peer(bool isServer = true) {
            allowedIPWithCDR = isServer ? "10.0.0.2/32" : "0.0.0.0/0";
        }

        QString publicKey;
        QString allowedIPWithCDR;
        QString endpoint;
    };

    BashResultType execLinuxBashCommand(const QString& command);
    void           createKeys();

    void setIPForwardLinux(QString& postUp, QString& postDown);

    void startServerInternal(
        const Interface& serverInterface,
        const Peer&      serverPeer,
        const QString&   postUp,
        const QString&   postDown);

    static VPNManagerPtr            m_instance;
    static inline const std::string m_wireguardFolderPath = "/etc/wireguard/";

    const QString m_interfaceName;
    const QString m_keysPrefix;
    QString       m_privateKey;
    QString       m_publicKey;
    Status        m_status = Status::STOP;
};

#endif // VPN_MANAGER_H