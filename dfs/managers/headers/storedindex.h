#ifndef STOREDINDEX_H
#define STOREDINDEX_H
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "dfs/types/headers/stored.h"
#include <QMap>
#include <QStack>
class StoredIndex : public QObject
{
    Q_OBJECT
public:
    StoredIndex(ActorIndex *_actor, AccountController *_account_contrlr);
    void addStoredInIndex(Stored getStrored); //[TESTED] Status:WORKING
    Stored addSerializedStoredInIndex(QByteArray serialized);
    void SendTempToVerify(QString path);              //[NOT TESTED]
    QByteArray calcChangeSig(QByteArray _changeData); //[TESTED] Status:WORKING
    bool validateStored(const Stored &_stored) const; //[TESTED] Status:WORKING
    QList<Stored> getStoredByHash(QByteArray path,
                                  QByteArray _hash) const; //[TESTED] Status:WORKING
    QList<Stored> getStoredByAuthor(QByteArray path,
                                    BigNumber _author) const; //[TESTED] Status:WORKING
    QList<Stored> getStoredByPath(QByteArray _path) const;    //[TESTED] Status:WORKING
    Stored getLastStoredByPath(QByteArray _path) const;       //[TESTED] Status:WORKING
    ~StoredIndex();

private:
    int addStored(const Stored &_stored);                //[TESTED] Status:WORKING
    BigNumber searchCurrentStoredIndex(QByteArray path); //[TESTED] Status:WORKING
    ActorIndex *s_ActorIndex;
    AccountController *account_contrlr;
signals:
    void NewStored(Stored _stored); // emit after execute slot getChanged, connect to NetworkManager
    //    void StoredIsMissing(Stored _stored);
    // emit when find Stored by some criterious
    //    void StoredByPathFound(QHostAddress peerAddress, QList<Stored> listStored);
    //    void LastStoredByPathFound(QHostAddress peerAddress, Stored sStored);
    //    void StoredByAuthorFound(QHostAddress peerAddress, QList<Stored> listStored);
};

#endif // STOREDINDEX_H
