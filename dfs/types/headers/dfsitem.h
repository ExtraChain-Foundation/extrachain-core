#ifndef DFSITEM_H
#define DFSITEM_H
#include <QObject>
#include "dfs/types/headers/dfstruct.h"
#include "dfs/managers/headers/storedindex.h"
#include "managers/account_controller.h"
#include "dfs/managers/headers/card_manager.h"
//#include "dfs/managers/headers/card_manager.h"

class DfsItem : public QObject, based_dfs_struct::DfStruct
{
    Q_OBJECT
private:
    StoredIndex *storedIndex;
    bool status;

public:
    DfsItem(const DfsItem &dfsItem, QObject *parent = nullptr);
    DfsItem(QByteArray &serialized, QObject *parent = nullptr);
    DfsItem(const QString &fileName, based_dfs_struct::Status status, QObject *parent = nullptr);
    DfsItem(const QString &path, based_dfs_struct ::Status status, ActorIndex *actorIndex,
            AccountController *accountControler, const QByteArray &data, QObject *parent = nullptr);
    DfsItem(const QString &path, based_dfs_struct::Status status, ActorIndex *actorIndex,
            AccountController *accountControler, QObject *parent = nullptr);
    //    DfsItem(StoredIndex *storedIndex, QObject *parent = nullptr);
    ~DfsItem() override;

    const DfsItem operator=(const DfsItem &dfsItem);
    bool operator==(const DfsItem &dfsItem);
    const QByteArray serialize() const override;
    based_dfs_struct::Type getType() const;

    based_dfs_struct::Status getStatus() const;

    BigNumber getName() const;

    long long getSize() const;

    QDateTime getTime() const;

    QByteArray getHash() const;

    QByteArray getPath() const;

    based_dfs_struct::SubType getSubType() const;

    QByteArray getData() const;

    BigNumber getActorId() const;

    const QString makeDir() const;
    const QString makeDir(const DfsItem *dfsItem) const;

    StoredIndex *getStoredIndex() const;
    void setStoredIndex(StoredIndex *value);

signals:
    void sendStatus(bool status);
    void finish();
public slots:

    int makeChanges(QByteArray data);
};

#endif // DFSITEM_H
