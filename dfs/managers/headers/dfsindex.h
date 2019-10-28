#ifndef DFSINDEX_H
#define DFSINDEX_H

#include "dfs/types/headers/dfsitem.h"
#include "managers/account_controller.h"
#include "utils/utils.h"
#include "dfs/types/headers/stored.h"
#include "dfs/managers/headers/card_manager.h"
#include <iostream>
#include <fstream>
#include <QByteArray>
#include <QDateTime>
#include <QThread>
#include <QString>
#include <QObject>
#include <QList>
#include <QDate>
#include <QFile>
#include <QMap>
#include <QDir>
#include "dfs/packages/headers/dfs_request.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "managers/thread_pool.h"

class DfsIndex : public QObject
{
    Q_OBJECT

    AccountController *accControler;
    ActorIndex *actorIndex;
    // file pool
    QList<DfsItem *> dfsItemList;

public:
signals:
public slots:
};

#endif // DFSINDEX_H
