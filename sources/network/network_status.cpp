#include "network/network_status.h"

#include <QDebug>

NetworkStatus::NetworkStatus(QObject *parent)
    : QObject(parent) {
#if QT_VERSION >= 0x060100
    QNetworkInformation::load(QNetworkInformation::Feature::Reachability);
    auto networkInfo = QNetworkInformation::instance();
    onReachabilityChanged(networkInfo->reachability());
    connect(networkInfo, &QNetworkInformation::reachabilityChanged, this,
            &NetworkStatus::onReachabilityChanged);
#endif
}

NetworkStatus::Status NetworkStatus::status() {
#if QT_VERSION < 0x060100
    return NetworkStatus::Status::Online;
#else
    return m_networkStatus;
#endif
}

#if QT_VERSION >= 0x060100
void NetworkStatus::onReachabilityChanged(QNetworkInformation::Reachability reachability) {
    switch (reachability) {
    case QNetworkInformation::Reachability::Unknown:
    case QNetworkInformation::Reachability::Disconnected:
    case QNetworkInformation::Reachability::Local:
    case QNetworkInformation::Reachability::Site:
        // qDebug() << "[NetworkStatus]" << reachability;
        setNetworkStatus(Status::Offline);
        break;
    case QNetworkInformation::Reachability::Online:
        setNetworkStatus(Status::Online);
        break;
    }
}
#endif

void NetworkStatus::setNetworkStatus(NetworkStatus::Status status) {
    if (m_networkStatus == status) {
        return;
    }

    m_networkStatus = status;
    emit statusChanged(status);
}
