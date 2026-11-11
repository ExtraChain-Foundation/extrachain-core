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

#include "network/network_status.h"

#include "utils/exc_logs.h"

#ifdef Q_OS_ANDROID
    #include <QJniEnvironment>
    #include <QJniObject>
    #include <QtCore/qcoreapplication_platform.h>
#endif

namespace {
constexpr int kOfflineDelayMs = 2000;

#ifdef Q_OS_ANDROID
// Whether Android has a network that carries internet at all. An answer it cannot give counts as yes: dropping
// every connection on a guess costs more than keeping one that died
bool androidHasNetwork() {
    QJniEnvironment env;
    QJniObject      context = QNativeInterface::QAndroidApplication::context();
    if (!context.isValid()) {
        return true;
    }
    QJniObject name = QJniObject::getStaticObjectField("android/content/Context", "CONNECTIVITY_SERVICE",
                                                       "Ljava/lang/String;");
    QJniObject manager =
        context.callObjectMethod("getSystemService", "(Ljava/lang/String;)Ljava/lang/Object;", name.object<jstring>());
    if (env.checkAndClearExceptions() || !manager.isValid()) {
        return true;
    }
    QJniObject network = manager.callObjectMethod("getActiveNetwork", "()Landroid/net/Network;");
    if (env.checkAndClearExceptions()) {
        return true;
    }
    if (!network.isValid()) {
        return false;
    }
    QJniObject capabilities = manager.callObjectMethod(
        "getNetworkCapabilities", "(Landroid/net/Network;)Landroid/net/NetworkCapabilities;", network.object());
    if (env.checkAndClearExceptions()) {
        return true;
    }
    if (!capabilities.isValid()) {
        return false;
    }
    constexpr jint kNetCapabilityInternet = 12;
    const bool     internet = capabilities.callMethod<jboolean>("hasCapability", "(I)Z", kNetCapabilityInternet);
    if (env.checkAndClearExceptions()) {
        return true;
    }
    return internet;
}
#endif
}

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent) {
    m_offlineDelay.setSingleShot(true);
    m_offlineDelay.setInterval(kOfflineDelayMs);
    connect(&m_offlineDelay, &QTimer::timeout, this, [this] {
        setNetworkStatus(Status::Offline);
    });
#ifdef Q_OS_ANDROID
    m_androidPoll.setInterval(kOfflineDelayMs / 2);
    connect(&m_androidPoll, &QTimer::timeout, this, [this] {
        if (androidHasNetwork()) {
            m_androidMisses = 0;
            setNetworkStatus(Status::Online);
        } else if (++m_androidMisses >= 2) {
            setNetworkStatus(Status::Offline);
        }
    });
    setNetworkStatus(androidHasNetwork() ? Status::Online : Status::Offline);
    m_androidPoll.start();
    return;
#endif
    auto networkInfo = QNetworkInformation::instance();
    if (networkInfo == nullptr) {
        eLog("[NetworkStatus] Can't detect network status");
        setNetworkStatus(Status::Unknown);
        return;
    }
    onReachabilityChanged(networkInfo->reachability());
    connect(networkInfo, &QNetworkInformation::reachabilityChanged, this, &NetworkStatus::onReachabilityChanged);
}

NetworkStatus::Status NetworkStatus::status() {
    return m_networkStatus;
}

void NetworkStatus::onReachabilityChanged(QNetworkInformation::Reachability reachability) {
    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        // Already offline (the start-up state) needs no wait; online waits to see whether it lasts
        if (m_networkStatus != Status::Online) {
            setNetworkStatus(Status::Offline);
        } else if (!m_offlineDelay.isActive()) {
            m_offlineDelay.start();
        }
        break;
    case QNetworkInformation::Reachability::Online:
        m_offlineDelay.stop();
        setNetworkStatus(Status::Online);
        break;
    }
}

void NetworkStatus::setNetworkStatus(NetworkStatus::Status status) {
    if (m_networkStatus == status) {
        return;
    }

    m_networkStatus = status;
    emit statusChanged(status);
}
