#include "dfs/controls/headers/subscribe_controller.h"

SubscribeController::SubscribeController(QObject *parent)
    : QObject(parent)
{
}

SubscribeController::SubscribeController(const SubscribeController &)
{
}

SubscribeController::~SubscribeController()
{
}

void SubscribeController::editSubscribe(QByteArray id, QByteArray currentId, bool isRemove)
{
    QByteArray path = "data/" + currentId + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableSubscribe);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::tableSubscribe, Config::DataStorage::mySubTableName,
                     id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ Config::DataStorage::mySubTableName, id.toStdString() });
        DB.insert(Config::DataStorage::mySubTableName, row);
    }
}

void SubscribeController::editSubscriptions(QByteArray id, QByteArray currentId, bool isRemove)
{
    QByteArray path = "data/" + currentId + "/services/subscribers";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableSubscribe);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::tableSubscribe, Config::DataStorage::mySubTableName,
                     id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ Config::DataStorage::mySubTableName, id.toStdString() });
        DB.insert(Config::DataStorage::mySubTableName, row);
    }
}

void SubscribeController::checkSubscribe(QByteArray id)
{
}
