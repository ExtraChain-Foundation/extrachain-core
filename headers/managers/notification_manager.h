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

#ifndef NOTIFICATION_MANAGER_H
#define NOTIFICATION_MANAGER_H
#include "utils/db_connector.h"
#include "utils/utils.h"
#include <QDateTime>

#ifdef ECLIENT
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
