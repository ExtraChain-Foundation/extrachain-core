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

void SubscribeController::editMySubscribe(QByteArray id, QByteArray currentId, bool isRemove)
{
    QByteArray path = "data/" + currentId + "/services/subscribers";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribe);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::subscribeColumn, "subscription", id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ "subscription", id.toStdString() });
        DB.insert(Config::DataStorage::subscribeColumn, row);
    }
}

void SubscribeController::editMyFollower(QByteArray id, QByteArray currentId, bool isRemove)
{
    QByteArray path = "data/" + currentId + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableFollower);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::subscribeFollowerColumn,
                     Config::DataStorage::subscribeFollowerColumn, id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ Config::DataStorage::subscribeFollowerColumn, id.toStdString() });
        DB.insert(Config::DataStorage::subscribeFollowerColumn, row);
    }
}

bool SubscribeController::checkSubscribe(QByteArray id)
{
    QByteArray path = "data/" + id + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribe);
    std::vector<DBRow> res =
        DB.select("SELECT * FROM " + Config::DataStorage::tableMySubscribe + " WHERE "
                  + Config::DataStorage::subscribeColumn + " = " + "'" + id.toStdString() + "'");
    return !res.empty();
}

int SubscribeController::checkCountSubscribe(QByteArray id)
{
    QByteArray path = "data/" + id + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribe);
    std::vector<DBRow> res = DB.select("SELECT COUNT (*) FROM " + Config::DataStorage::tableMySubscribe);
    return int(res[0].count(Config::DataStorage::subscribeColumn));
}

QList<std::string> SubscribeController::getAllSubscribe(QByteArray id)
{
    QByteArray path = "data/" + id + "/services/subscribers";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribe);
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::tableMySubscribe);
    QList<std::string> sub;
    for (auto &tmp : res)
    {
        sub.append(tmp[Config::DataStorage::subscribeColumn]);
    }
    return sub;
}
