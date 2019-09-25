#ifndef DFSINDEX_H
#define DFSINDEX_H

#include "dfs/types/headers/dfsitem.h"
#include "managers/account_controller.h"
#include "crypt/ecc/ecc.h"
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

private:
    QList<DfsItem *> dfsItemList;
    AccountController *accControler;
    ActorIndex *actorIndex;

    void createdDfsItemConnection(const DfsItem *dfsItem);

    void appendsFromDirectory();

public:
    void dfsSender(const QString &filePath, QString peerAdrress);

public:
    DfsIndex(ActorIndex *actorIndex, AccountController *accountControler, QObject *parent = nullptr);
    DfsIndex(const DfsIndex &dfsIndex, QObject *parent = nullptr);
    ~DfsIndex();

    const DfsIndex operator=(const DfsIndex &dfsIndex);
    bool operator==(const DfsIndex &dfsIndex);
    //    NewActor

    QByteArray getFileByPath(const QByteArray &path) const;
    QList<QByteArray> getFileByHash(const QByteArray &hash) const;

    int changedData(const QString &path, const based_dfs_struct::Type &type,
                    const based_dfs_struct::SubType &subType, const based_dfs_struct::Status &status);
    int makeSystemDir(const BigNumber &userId) const;

    void initNewDfsItem(const QString &path, based_dfs_struct::Status status);

    QStringList fileCompareAndReturnDifference(const QString &first, const QString &second) const;

signals:

    void makeChanges(const Stored &stored);
    //    void initDfsItem(ActorIndex *actorIndex, AccountController *accountControler);
    void statusRequest(bool status);
    //
    /*
     * request from namespace dfs Requests
     * data comments could be path of neede files
     * if request card file -> data = type.toByteArray
     *    image -> data = subType / REQUESTS_DATA_DELIMETRS/file.name
     */
    //    void requestData(Messages::DfsRequest request);

    void sendRequest(const Messages::DfsRequest &msg);

    //
    void usersChanged(QByteArray data, based_dfs_struct::Type type, BigNumber actorId);

    void sendProfile(QString userId, QByteArray data);
    //
    void sendData(Messages::DfsMessage msg);
    void sendToUser(const Messages::DfsMessage &msg, const QString &peerAddress);
public slots:
    void dfsItemStatus(bool status);

    void getProfileById(QString userId);
};

#endif // DFSINDEX_H
