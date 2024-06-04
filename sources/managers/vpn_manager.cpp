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

#include "managers/vpn_manager.h"

VPNManagerPtr VPNManager::m_instance;

VPNManager::VPNManager(const QString& keysPrefix, const QString& interfaceName)
    : m_interfaceName(interfaceName)
      , m_keysPrefix(keysPrefix) {
    createKeys();
}

VPNManager::~VPNManager() {
    stopService();

    QDir dir(QString::fromStdString(m_wireguardFolderPath) + "keys");
    dir.removeRecursively();
    qInfo() << "VPNManager succeffuly cleared!";
}

std::shared_ptr<VPNManager> VPNManager::Instance(const QString& keysPrefix, const QString& interfaceName) {
    if (!m_instance)
        m_instance = std::shared_ptr<VPNManager>(new VPNManager(keysPrefix, interfaceName));

    return m_instance;
}

VPNManager::BashResultType VPNManager::execLinuxBashCommand(const QString& command) {
    BashResultType result;

    QProcess process;
    process.start("bash", QStringList() << "-c" << command);
    if (!process.waitForFinished()) {
        result.errorOutput = "Bash command not finished, Error: " + process.error();
        result.errorOutput += ". Command: " + command;
        return result;
    }

    if (process.exitCode() != 0) {
        result.errorOutput = process.readAllStandardError();
        return result;
    }

    result.output  = process.readAllStandardOutput();
    result.success = true;
    return result;
}

void VPNManager::createKeys() {
    #ifdef Q_OS_LINUX
    static auto clearOutput = [](QString& output) {
        auto index = output.indexOf(QChar('\xa'));
        while (index != -1) {
            output.remove(index, 1);
            index = output.indexOf(QChar('\xa'), index);
        }
    };

    auto bashCommandRes = execLinuxBashCommand("wg genkey");
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::createKeys private key creation failed, error: "
            + bashCommandRes.errorOutput.toStdString());
    m_privateKey = bashCommandRes.output;
    clearOutput(m_privateKey);

    bashCommandRes = execLinuxBashCommand("echo \"" + m_privateKey + "\" | wg pubkey");
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::createKeys public key creation failed, error: "
            + bashCommandRes.errorOutput.toStdString());

    m_publicKey = bashCommandRes.output;
    clearOutput(m_publicKey);

    static auto createFile = [&](const QString& keyName, const QString& inputData) -> void {
        static const QString path = QString::fromStdString(m_wireguardFolderPath) + "keys/" + m_keysPrefix;

        auto dir = QFileInfo(path).absoluteDir().absolutePath();
        if (!QDir().mkpath(dir))
            throw std::runtime_error(
                "VPNManager::createKeys, Failed to create all folders: "
                + dir.toStdString());

        QFile file(path + keyName);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            throw std::runtime_error(
                "VPNManager::createKeys, Failed to open file for writing(\""
                + (path + keyName).toStdString()
                + "\"): " + file.errorString().toStdString());

        QTextStream out(&file);
        out << inputData;
        file.close();
        qDebug() << "VPN private key file created:" << path + keyName;
    };

    createFile("_privateKey", m_privateKey);
    createFile("_publicKey", m_publicKey);

    #elif defined(Q_OS_WIN)
    qCritical() << "VPNManager::createKeys, not supported for Windows";
    #endif
}

QString VPNManager::getPublicKeyFilePath() const {
    return QString::fromStdString(m_wireguardFolderPath) + "keys/" + m_keysPrefix + "_publicKey";
}

void VPNManager::stopService() {
    if (m_status != Status::STOP) {
        #ifdef Q_OS_LINUX
        auto bashCommandRes = execLinuxBashCommand("wg-quick down " + m_interfaceName);
        if (!bashCommandRes.success) {
            qCritical() << "VPNManager::stopService, failed to stop " + m_interfaceName + "; Error:"
                << bashCommandRes.errorOutput;
            return;
        }

        if (!bashCommandRes.output.isEmpty())
            qInfo() << "VPNManager::stopService, " << bashCommandRes.output;

        qInfo() << "VPNManager::stopService, Server " + m_interfaceName + " stopped.";

        QFile file(QString::fromStdString(m_wireguardFolderPath) + m_interfaceName + ".conf");
        if (!file.exists())
            qWarning() << "VPNManager::stopService, file config \"" + m_interfaceName + "\" does not exist.";
        if (file.remove())
            qInfo() << "VPNManager::stopService, file config \"" + m_interfaceName
                + "\" has been successfully deleted.";
        else
            qWarning() << "VPNManager::stopService, failed to delete file config \"" + m_interfaceName
                + "\".";
        #elif defined(Q_OS_WIN)
        qCritical() << "VPNManager::createKeys, not supported for Windows";
        #endif
    }

    m_status = Status::STOP;
}

void VPNManager::setIPForwardLinux(QString& postUp, QString& postDown) {
    auto bashCommandRes = execLinuxBashCommand("sysctl -w net.ipv4.ip_forward=1");
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::setIPForwardLinux, cannot enable IP routing, Error: "
            + bashCommandRes.errorOutput.toStdString());

    if (bashCommandRes.output.isEmpty())
        throw std::runtime_error(
            "VPNManager::setIPForwardLinux, Unknown error. Make sure you have administrator privileges.");

    bashCommandRes = execLinuxBashCommand("ip route | grep default | awk '{print $5}'");
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::setIPForwardLinux, cannot get default interface name, Error: "
            + bashCommandRes.errorOutput.toStdString());

    if (bashCommandRes.output.isEmpty())
        throw std::runtime_error("VPNManager::setIPForwardLinux, cannot get default interface name.");

    int index = bashCommandRes.output.indexOf(QChar('\xa'));
    while (index != -1) {
        bashCommandRes.output.remove(index, 1);
        index = bashCommandRes.output.indexOf(QChar('\xa'), index);
    }

    postUp = "iptables -A FORWARD -i %i -j ACCEPT; iptables -A FORWARD -o %i -j ACCEPT; iptables -t nat -A "
             "POSTROUTING -o "
             + bashCommandRes.output + " -j MASQUERADE";
    postDown = "iptables -D FORWARD -i %i -j ACCEPT; iptables -D FORWARD -o %i -j ACCEPT; iptables -t nat -D "
               "POSTROUTING -o "
               + bashCommandRes.output + " -j MASQUERADE";

    qInfo() << "PostUp Created: " << postUp;
    qInfo() << "PostDown Created: " << postDown;
}

void VPNManager::startServiceServer(
    const QString& peerPublicKey,
    const QString& localServerIP,
    const QString& listenPort,
    const QString& peerLocalIPWithCDR) {
    if (m_status == Status::STOP) {
        #ifdef Q_OS_LINUX
        QString postUp;
        QString postDown;
        setIPForwardLinux(postUp, postDown);

        Interface serverInterface;
        serverInterface.privateKey = m_privateKey;
        serverInterface.publicKey  = m_publicKey;

        if (!localServerIP.isEmpty())
            serverInterface.localIP = localServerIP;

        if (!listenPort.isEmpty())
            serverInterface.listenPort = listenPort;

        Peer serverPeer;
        serverPeer.publicKey = peerPublicKey;

        if (!peerLocalIPWithCDR.isEmpty())
            serverPeer.allowedIPWithCDR = peerLocalIPWithCDR;

        startServerInternal(serverInterface, serverPeer, postUp, postDown);
        #elif defined(Q_OS_WIN)
        qCritical() << "VPNManager::createKeys, not supported for Windows";
        #endif
    } else
        throw std::runtime_error(
            "VPNManager::startServiceServer, the VPN service is already running as a "
            + std::string(magic_enum::enum_name(m_status)));
}

void VPNManager::startServerInternal(
    const Interface& serverInterface,
    const Peer&      serverPeer,
    const QString&   postUp,
    const QString&   postDown) {
    QString filePath = QString::fromStdString(m_wireguardFolderPath) + m_interfaceName + ".conf";

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        throw std::runtime_error(
            "VPNManager::startServerInternal, Failed to open file for writing: "
            + file.errorString().toStdString());

    QTextStream out(&file);
    out << "[Interface]\n"
        "PrivateKey = " + serverInterface.privateKey + "\n"
        "Address = " + serverInterface.localIP + "\n"
        "ListenPort = " + serverInterface.listenPort + "\n"
        "PostUp = " + postUp + "\n"
        "PostDown = " + postDown + "\n\n"
        "[Peer]\n"
        + "PublicKey = " + serverPeer.publicKey + "\n"
        "AllowedIPs = " + serverPeer.allowedIPWithCDR + "\n";

    file.close();
    qDebug() << "VPN config file created:" << filePath;

    auto bashCommandRes = execLinuxBashCommand("wg-quick up " + m_interfaceName);
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::startServerInternal, start failed; Error: "
            + bashCommandRes.errorOutput.toStdString());

    bashCommandRes = execLinuxBashCommand("systemctl enable wg-quick@" + m_interfaceName + ".service");
    if (!bashCommandRes.success)
        throw std::runtime_error(
            "VPNManager::startServerInternal, enable start server after system restart; Error: "
            + bashCommandRes.errorOutput.toStdString());

    qInfo() << "VPNManager::startServerInternal, Server " + m_interfaceName + " started";
}

void VPNManager::startServiceClient() {
}