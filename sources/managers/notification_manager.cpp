#include "headers/managers/notification_manager.h"

NotificationManager::NotificationManager(QObject *parent)
    : QObject(parent)
{
    QDir().mkpath(PATH_NOTIFICATION_FILE.c_str());
}

void NotificationManager::loadNotificationFromDB()
{
    QList<notification> list;
    DBConnector db(PATH_NOTIFICATION_FILE + _currentActorId.toStdString());
    db.createTable(Config::DataStorage::notificationTableCreation);
    std::vector<DBRow> res = db.select("SELECT * FROM " + Config::DataStorage::notificationTable);
    for (const auto &temp : res)
    {
        std::string time = temp.at("time");
        std::string type = temp.at("type");
        std::string data = temp.at("data");
        notification tmp{ std::stoi(time), notification::NotifyType(std::stoi(type)), data.c_str() };
        list.append(tmp);
    }
    qDebug() << list.size() << " notify loaded";
    emit allNotifyToUI(list);
}

void NotificationManager::addNotify(const notification newNtf)
{
    DBConnector db(PATH_NOTIFICATION_FILE + _currentActorId.toStdString());
    db.createTable(Config::DataStorage::notificationTableCreation);
    DBRow row;
    row.insert({ "time", std::to_string(newNtf.time) });
    row.insert({ "type", std::to_string(newNtf.type) });
    row.insert({ "data", newNtf.data.toStdString() });
    db.insert(Config::DataStorage::notificationTable, row);
    emit newNotifyToUI(newNtf);
}

void NotificationManager::setCurrentID(const QByteArray id)
{
    this->_currentActorId = id;
    loadNotificationFromDB();
}
