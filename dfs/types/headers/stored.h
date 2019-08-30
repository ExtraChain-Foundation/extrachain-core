#ifndef STORED_H
#define STORED_H
#include <QDir>
#include <QObject>
#include <QString>
#include <QDateTime>
#include <QByteArray>
#include <QDataStream>
#include <unordered_map>
#include "utils/bignumber.h"
#include "utils/utils.h"
#include "datastorage/transaction.h"
#include "network/packages/base_message.h"

class Stored
{

private:
    int FirstByte;
    QByteArray ChangeData;     // changing data
    storedSpace::State State;  // State (deleted, changed, added)
    QByteArray ChangeDataSig;  // QByteArray ChangeSig;          // changing signature func
    QByteArray PrevSig;        // QByteArray PrevChangeSig;  // previous Sig
    QByteArray PrevStoredHash; // QByteArray PrevFileChange; // previous changeSigs
    QByteArray hash;
    QByteArray path;
    BigNumber actorId;

public:
    Stored();                      //+++
    Stored(const Stored &_object); //+++
    Stored(const BigNumber actorId, const int first, const QByteArray changedata,
           const QByteArray sign, QByteArray path, QByteArray prevSig,
           QByteArray prevStoredHash,
           const storedSpace::State state = storedSpace::State::NEWSTATE); //+++
    Stored(const QByteArray &serialized);                                  //+++
    //    void initStored(const QByteArray &serialize);
    virtual ~Stored();
    const Stored operator=(const Stored &temp); //+++
    QByteArray serializedHeaderTail();
    QByteArray serialized() const;           //+++
    QByteArray serializedUserField() const;  //+++
    void init(const QByteArray &serialized); //+++
//    Stored init(const QByteArray &serilaize) const;
    QByteArray getPath() const;
    BigNumber getAuthor() const;
    QByteArray getChangeData() const;
    storedSpace::State getState() const;
    QByteArray getStateBytes() const;
    void setChangeDataSig(QByteArray changeDataSig);
    int getFirstByte() const;
    QByteArray getHash() const;
    QByteArray getChangeDataSig() const;
    QByteArray getPrevSig() const;
    QByteArray getPrevStoredHash() const;
    bool verify(const Actor<KeyPublic> &actor) const;
};
#endif // STORED_H
