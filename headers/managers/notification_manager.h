#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H
#include "utils/db_connector.h"
#include "utils/utils.h"
#include <QDateTime>
#ifdef ETALONIUM_CLIENT
#include "headers/ui/notificationclient.h"
#include "datastorage/index/actorindex.h"

class NotificationManager : public QObject
{
    Q_OBJECT

private:
    QByteArray _currentActorId;
    uint DBCount = 100;
    const std::string PATH_NOTIFICATION_FILE = "keystore/notification/";

    NotificationClient *notifyClient = nullptr;

    ActorIndex *actorIndex = nullptr;

public:
    NotificationManager(QObject *parent = nullptr);

    void setNotifyClient(NotificationClient *newNtfCl);

    void setActorIndex(ActorIndex *_actorIndex);

private:
    void loadNotificationFromDB();
    void sendToNotify(const notification newNtf);
    void newNotify(const QString &msg, const QByteArray &user);

public slots:
    void addNotify(const notification newNtf);
    void setCurrentID(const QByteArray id);
    void process();
signals:
    void allNotifyToUI(QList<notification> ntf);
    void newNotifyToUI(notification ntf);
    void getCurrentID();
    void finished();
};
#endif
#endif // NOTIFICATION_MANAGER_H
