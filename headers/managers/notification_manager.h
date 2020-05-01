#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H
#include "utils/db_connector.h"
#include "utils/utils.h"
#include <QDateTime>

#ifdef ETALONIUM_CLIENT
#include "ui/notificationclient.h"
#include "datastorage/index/actorindex.h"
#include "dfs/types/headers/dfstruct.h"
#include "managers/account_controller.h"

class NotificationManager : public QObject
{
    Q_OBJECT

private:
    QByteArray _currentActorId;
    uint DBCount = 100;

    NotificationClient *notifyClient = nullptr;

    ActorIndex *actorIndex = nullptr;
    AccountController *accController = nullptr;

public:
    NotificationManager(QObject *parent = nullptr);

    void setNotifyClient(NotificationClient *newNtfCl);
    void setActorIndex(ActorIndex *_actorIndex);
    void setAccController(AccountController *value);

private:
    void loadNotificationFromDB();
    void sendToNotify(const Notification newNtf);
    void newNotify(const QString &msg, const QByteArray &user);

public slots:
    void addNotify(const Notification newNtf);
    void setCurrentID(const QByteArray id);
    void process();
signals:
    void allNotifyToUI(QList<Notification> ntf);
    void newNotifyToUI(Notification ntf);
    void getCurrentID();
    void finished();
    void sendEditSql(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                     QByteArrayList sqlChanges);
};
#endif
#endif // NOTIFICATION_MANAGER_H
