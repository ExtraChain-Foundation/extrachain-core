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
    sendEditSql(currentId, "subscribe", DfsStruct::Type::Service,
                isRemove ? DfsStruct::ChangeType::Delete : DfsStruct::ChangeType::Insert,
                { Config::DataStorage::subscribeColumnTableName.c_str(), "subscription", id });

    sendEditSql(
        id, "follower", DfsStruct::Type::Service,
        isRemove ? DfsStruct::ChangeType::Delete : DfsStruct::ChangeType::Insert,
        { Config::DataStorage::subscribeFollowerTableName.c_str(), "subscriber", currentId, "sign", "TODO" });
}

bool SubscribeController::checkSubscribe(QByteArray id)
{
    QString path =
        DfsStruct::ROOT_FOOLDER_NAME + "/" + nodeManager->getIdPrivateProfile() + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);
    std::vector<DBRow> res = DB.select("SELECT * FROM " + Config::DataStorage::subscribeColumnTableName
                                       + " WHERE subscription = " + "'" + id.toStdString() + "';");
    return !res.empty();
}

int SubscribeController::checkCountSubscribe(QByteArray id)
{
    QString path = DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/services/subscribe";
    DBConnector DB(path.toStdString());
    DB.createTable(Config::DataStorage::tableMySubscribeCreation);
    std::vector<DBRow> res =
        DB.select("SELECT COUNT (*) FROM " + Config::DataStorage::subscribeColumnTableName);
    int count = res.empty() ? 0 : std::stoi(res[0]["COUNT (*)"]);
    return count;
}

std::vector<DBRow> SubscribeController::getAllSubscribe(QByteArray id)
{
    QString path = DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/services/subscriber";
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
