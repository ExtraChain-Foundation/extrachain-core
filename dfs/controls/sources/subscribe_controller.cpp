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

void SubscribeController::editMySubscribe(QByteArray id, bool isRemove)
{
    QByteArray currentId = nodeManager->getIdPrivateProfile();
    sendEditSql(currentId, "subscribe", dfsStruct::Type::service,
                isRemove ? dfsStruct::Delete : dfsStruct::Insert,
                { Config::DataStorage::subscribeColumnTableName.c_str(), "subscription", id });
    // sendEditSql for followers
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
    std::vector<DBRow> res =
        DB.select("SELECT COUNT (*) FROM " + Config::DataStorage::subscribeColumnTableName);
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

void SubscribeController::initSubscribe()
{
    QByteArray currentId = nodeManager->getIdPrivateProfile();
    QDir().mkpath("data/" + currentId + "/services");

    DBConnector dbSubscribe("data/" + currentId.toStdString() + "/services/subscribe");
    dbSubscribe.createTable(Config::DataStorage::tableMySubscribeCreation);
    dbSubscribe.close();

    DBConnector dbFollower("data/" + currentId.toStdString() + "/services/follower");
    dbFollower.createTable(Config::DataStorage::tableFollowerCreation);
    dbFollower.close();

    send(dfsStruct::DfsSave::Static, "subscribe", "", dfsStruct::service, dfsStruct::SubType::undef);
    send(dfsStruct::DfsSave::Static, "follower", "", dfsStruct::service, dfsStruct::SubType::undef);
}

void SubscribeController::setNodeManager(NodeManager *value)
{
    nodeManager = value;
}
