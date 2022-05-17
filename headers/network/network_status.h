#ifndef NETWORKSTATUS_H
#define NETWORKSTATUS_H

#include <QObject>
#include <QtNetwork/QNetworkInformation>

class NetworkStatus : public QObject {
    Q_OBJECT

public:
    enum class Status {
        Unknown,
        Online,
        Offline,
        Local
    };
    Q_ENUM(Status)

    explicit NetworkStatus(QObject* parent = nullptr);
    Status status();

private slots:
    void onReachabilityChanged(QNetworkInformation::Reachability reachability);

signals:
    void statusChanged(NetworkStatus::Status status);

private:
    void setNetworkStatus(Status status);
    Status m_networkStatus = Status::Offline;
};

#endif // NETWORKSTATUS_H
