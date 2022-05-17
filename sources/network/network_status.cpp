#include "network/network_status.h"

#include <QDebug>

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent) {
    QNetworkInformation::load(QNetworkInformation::Feature::Reachability);
    auto networkInfo = QNetworkInformation::instance();
    if (networkInfo == nullptr) {
        qDebug() << "[NetworkStatus] Can't detect network status";
        setNetworkStatus(Status::Unknown);
        return;
    }
    onReachabilityChanged(networkInfo->reachability());
    connect(networkInfo, &QNetworkInformation::reachabilityChanged, this,
            &NetworkStatus::onReachabilityChanged);
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
        setNetworkStatus(Status::Offline);
        break;
    case QNetworkInformation::Reachability::Online:
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
