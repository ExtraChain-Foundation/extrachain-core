#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H
#include "utils/db_connector.h"
#include "utils/utils.h"
#include <QDateTime>

class NotificationManager : public QObject
{
    Q_OBJECT

private:
    QByteArray _currentActorId;
    uint DBCount = 100;
    const std::string PATH_NOTIFICATION_FILE = "keystore/notification/";

public:
    NotificationManager(QObject *parent = nullptr);

private:
    void loadNotificationFromDB();

public slots:
    void addNotify(notification newNtf);
    void setCurrentID(const QByteArray id);

signals:
    void allNotifyToUI(QList<notification> ntf);
    void newNotifyToUI(notification ntf);
};
#endif // NOTIFICATION_MANAGER_H
