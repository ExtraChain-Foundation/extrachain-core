#ifndef NETWORKSTATUS_H
#define NETWORKSTATUS_H

#include <QObject>

#if QT_VERSION >= 0x060100
    #include <QtNetwork/QNetworkInformation>
#endif

class NetworkStatus : public QObject {
    Q_OBJECT

public:
    enum class Status
    {
        Online,
        Offline,
        Local
    };
    Q_ENUM(Status)

    explicit NetworkStatus(QObject* parent = nullptr);
    Status status();

private slots:
#if QT_VERSION >= 0x060100
    void onReachabilityChanged(QNetworkInformation::Reachability reachability);
#endif

signals:
    void statusChanged(NetworkStatus::Status status);

private:
    void setNetworkStatus(Status status);
    Status m_networkStatus = Status::Offline;
};

#endif // NETWORKSTATUS_H
