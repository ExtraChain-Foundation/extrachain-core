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

#include "headers/managers/notification_manager.h"
#ifdef ECLIENT

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
}

void NotificationManager::setNotifyClient(NotificationClient *newNtfCl)
{
    notifyClient = newNtfCl;
}

void NotificationManager::setActorIndex(ActorIndex *_actorIndex)
{
    actorIndex = _actorIndex;
}

void NotificationManager::setAccController(AccountController *value)
{
    accController = value;
}

void NotificationManager::loadNotificationFromDB()
{
    if (_currentActorId == "")
    {
        qDebug() << "NotificationManager haven't actorID";
        return;
    }

    QString dbPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + _currentActorId + "/private/notifications";
    if (!QFile::exists(dbPath))
    {
        qDebug() << "Error load notifications: no file exists";
        return;
    }

    auto mainActor = accController->getMainActor()->key();
    DBConnector db(dbPath.toStdString());
    std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::notificationTable);
    QList<Notification> list;
    for (const auto &temp : res)
    {
        QByteArray time = mainActor->decryptSymmetric(QByteArray::fromStdString(temp.at("time")));
        QByteArray type = mainActor->decryptSymmetric(QByteArray::fromStdString(temp.at("type")));
        QByteArray data = mainActor->decryptSymmetric(QByteArray::fromStdString(temp.at("data")));
        Notification tmp { time.toLongLong(), Notification::NotifyType(type.toInt()), data };
        list.append(tmp);
    }
    qDebug() << list.size() << "notify loaded";
    emit allNotifyToUI(list);
}

void NotificationManager::addNotify(const Notification newNtf)
{
    auto mainActor = accController->getMainActor()->key();
    sendEditSql(_currentActorId, "notifications", DfsStruct::Type::Private, DfsStruct::Insert,
                { Config::DataStorage::notificationTable.c_str(), "time",
                  mainActor->encryptSymmetric(QByteArray::number(newNtf.time)), "type",
                  mainActor->encryptSymmetric(QByteArray::number(newNtf.type)), "data",
                  mainActor->encryptSymmetric(newNtf.data) });

    emit newNotifyToUI(newNtf);
    sendToNotify(newNtf);
}

void NotificationManager::setCurrentID(const QByteArray id)
{
    qDebug() << "NotificationManager set ID" << id;
    if (id.isEmpty())
    {
        emit getCurrentID();
        return;
    }
    this->_currentActorId = id;
    loadNotificationFromDB();
}

void NotificationManager::process()
{
}

void NotificationManager::sendToNotify(const Notification newNtf)
{
    switch (newNtf.type)
    {
    case (Notification::NotifyType::TxToUser):
        newNotify("Transaction to *" + newNtf.data.right(5) + " completed", "");
        break;
    case (Notification::NotifyType::TxToMe):
        newNotify("New transaction from *" + newNtf.data.right(5), "");
        break;
    case (Notification::NotifyType::ChatMsg): {
        QByteArray chatId = newNtf.data.split(' ').at(0);
        newNotify("New message from ", chatId);
        break;
    }
    case (Notification::NotifyType::ChatInvite): {
        QByteArray userId = newNtf.data.split(' ').at(0);
        newNotify("New chat from ", userId);
        break;
    }
    case (Notification::NotifyType::NewPost): {
        QByteArray user = newNtf.data.split(' ').at(0);
        newNotify("New post from ", user);
        break;
    }
    case (Notification::NotifyType::NewEvent): {
        QByteArray user = newNtf.data.split(' ').at(0);
        newNotify("New event from ", user);
        break;
    }
    case (Notification::NotifyType::NewFollower):
        newNotify("New follower ", newNtf.data);
        break;
    }
}

void NotificationManager::newNotify(const QString &msg, const QByteArray &user)
{
    if (user.isEmpty())
        notifyClient->setNotification(msg);
    else
    {
        QByteArrayList profile = actorIndex->getProfile(user);
        if (profile.isEmpty())
            notifyClient->setNotification(msg);
        else
        {
            QString name = profile.at(3);
            QString secName = profile.at(4);
            notifyClient->setNotification(msg + name + " " + secName);
        }
    }
}
#endif
