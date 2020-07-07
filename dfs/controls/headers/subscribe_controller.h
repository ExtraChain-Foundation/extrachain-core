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

#ifndef SUBSCRIBE_CONTROLLER_H
#define SUBSCRIBE_CONTROLLER_H

#include <QObject>
#include <QByteArray>

#include "utils/db_connector.h"
#include "utils/utils.h"
#include "dfs/types/headers/dfstruct.h"

class NodeManager;

class SubscribeController : public QObject
{
    Q_OBJECT

public:
    SubscribeController(QObject *parent = nullptr);
    SubscribeController(const SubscribeController &);
    ~SubscribeController();

signals:
    void sendEditSql(QString userId, QString fileName, DfsStruct::Type type, int sqlType,
                     QByteArrayList sqlChanges);

public slots:
    void editMySubscribe(QByteArray id, bool isRemove);
    int checkCountSubscribe(QByteArray id);
    std::vector<DBRow> getAllSubscribe(QByteArray id);

public:
    bool checkSubscribe(QByteArray id);
    void setNodeManager(NodeManager *value);

private:
    NodeManager *nodeManager;

    // get all (offset, count)
};
#endif // SUBSCRIBE_CONTROLLER_H
