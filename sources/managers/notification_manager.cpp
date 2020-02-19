#include "headers/managers/notification_manager.h"
#ifdef ETALONIUM_CLIENT
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

void NotificationManager::loadNotificationFromDB()
{
    if (_currentActorId == "")
    {
        qDebug() << "NotificationManager haven't actorID";
        return;
    }

    QString dbPath = "data/" + _currentActorId + "/private/notifications";
    if (!QFile::exists(dbPath))
    {
        qDebug() << "Error load notifications: no file exists";
        return;
    }

    DBConnector db(dbPath.toStdString());
    std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::notificationTable);
    QList<notification> list;
    for (const auto &temp : res)
    {
        std::string time = temp.at("time");
        std::string type = temp.at("type");
        std::string data = temp.at("data");
        notification tmp { std::stoll(time), notification::NotifyType(std::stoi(type)), data.c_str() };
        list.append(tmp);
    }
    qDebug() << list.size() << "notify loaded";
    emit allNotifyToUI(list);
}

void NotificationManager::addNotify(const notification newNtf)
{
    sendEditSql(_currentActorId, "notifications", DfsStruct::Type::Private, DfsStruct::Insert,
                { Config::DataStorage::notificationTable.c_str(), "time", QByteArray::number(newNtf.time),
                  "type", QByteArray::number(newNtf.type), "data", newNtf.data });

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

void NotificationManager::sendToNotify(const notification newNtf)
{
    switch (newNtf.type)
    {
    case (notification::NotifyType::TxToUser):
        newNotify("Transaction to *" + newNtf.data.right(5) + " completed", "");
        break;
    case (notification::NotifyType::TxToMe):
        newNotify("New transaction from *" + newNtf.data.right(5), "");
        break;
    case (notification::NotifyType::ChatMsg): {
        QByteArray chatId = newNtf.data.split(' ').at(0);
        newNotify("New message from ", chatId);
        break;
    }
    case (notification::NotifyType::ChatInvite): {
        QByteArray userId = newNtf.data.split(' ').at(0);
        newNotify("New chat from ", userId);
        break;
    }
    case (notification::NotifyType::NewPost): {
        QByteArray user = newNtf.data.split(' ').at(0);
        newNotify("New post from ", user);
        break;
    }
    case (notification::NotifyType::NewFollower):
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
