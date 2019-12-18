#include "dfs/controls/headers/subscribe_controller.h"
#include "managers/node_manager.h"

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
    QByteArray path = "data/" + currentId + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::subscribeColumnTableName, "subscription", id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ "subscription", id.toStdString() });
        DB.insert(Config::DataStorage::subscribeColumnTableName, row);
    }
}

void SubscribeController::editMyFollower(QByteArray id, QByteArray currentId, bool isRemove)
{
    QByteArray path = "data/" + currentId + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableFollowerCreation);

    if (isRemove)
    {
        DB.deleteRow(Config::DataStorage::subscribeFollowerTableName,
                     Config::DataStorage::subscribeFollowerTableName, id.toStdString());
    }
    else
    {
        DBRow row;
        row.insert({ Config::DataStorage::subscribeFollowerTableName, id.toStdString() });
        DB.insert(Config::DataStorage::subscribeFollowerTableName, row);
    }
}

bool SubscribeController::checkSubscribe(QByteArray id)
{
    QByteArray path = "data/" + nodeManager->getIdPrivateProfile() + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::subscribeColumnTableName
                                       + " WHERE subscription = " + "'" + id.toStdString() + "';");
    return !res.empty();
}

int SubscribeController::checkCountSubscribe(QByteArray id)
{
    QByteArray path = "data/" + id + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);
    std::vector<DBRow> res = DB.select("SELECT COUNT (*) FROM " + Config::DataStorage::subscribeColumnTableName);
    int count = std::stoi(res[0]["COUNT (*)"]);
    return count;
}

std::vector<DBRow> SubscribeController::getAllSubscribe(QByteArray id)
{
    QByteArray path = "data/" + id + "/services/subscriber";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::subscribeColumnTableName);
    //    QList<std::string> sub;
    //    for (auto &tmp : res)
    //    {
    //        sub.append(tmp["subscription"]);
    //    }
    return res;
}

void SubscribeController::setNodeManager(NodeManager *value)
{
    nodeManager = value;
}
