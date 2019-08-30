#ifndef STOREDINDEX_H
#define STOREDINDEX_H
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "dfs/types/headers/stored.h"
#include <QMap>
#include <QStack>
//#include
#define DELIM QString("ENDLINE|\n")
class StoredIndex : public QObject
{
    Q_OBJECT
public:
    //  StoredIndex(QObject *parent = nullptr);
    StoredIndex(ActorIndex *_actor, AccountController *_account_contrlr);
    void addStoredInIndexAllParametres();
    void addStoredInIndex(Stored getStrored); //[TESTED] Status:WORKING
    Stored addSerializedStoredInIndex(QByteArray serialized);
    ~StoredIndex();
    void SendTempToVerify(QString path);
    QByteArray calcChangeSig(QByteArray _changeData); //[TESTED] Status:WORKING
    /**
     * @brief Validates stored digital signature
     * @param _stored
     * @return true if stored is valid
     */
    bool validateStored(const Stored &_stored) const; //[TESTED] Status:WORKING
    /**
     * @brief append Data to file
     * @param int firstbyte - first byte
     * @param QByteArray _data data that will add to file
     * @param _path  - path to file
     * @return result
     */
    //    int addDataToFile(QByteArray _data, QByteArray _path);
    //    /**
    //     * @brief change data in file
    //     * @param int firstbyte - first byte
    //     * @param QByteArray _data data that will replace
    //     * @param _path  - path to file
    //     * @return result
    //     */
    //    int changeDataInFile(int firstbyte, QByteArray _data, QByteArray _path);
    //    /**
    //     * @brief delete data from file
    //     * @param int fistbyte - first byte
    //     * @param int size data need to remove
    //     * @param _path  - path to file
    //     * @return result
    //     */
    //    int deleteDataFromFile(int firstbyte, int _size, QByteArray _path);
    //    /**
    //     * @brief Gets Stored from local storage
    //     * @param _hash - file hash that need to find
    //     * @return found stored
    //     */
    //    QByteArray getPreviousHash();
    //    ///
    //    /// \brief calcChangeSig
    //    /// \param _changeData
    //    /// \return
    //    ///
    /// \brief getStoredByHash
    /// \param _hash
    /// \return
    /////    QByteArray calcChangeSig(QByteArray _changeData);

    //    ///
    //    /// \brief getLastStoredByPath
    //    /// \param _path
    //    /// \return
    //    ///
    QList<Stored> getStoredByHash(QByteArray path,
                                  QByteArray _hash) const; //[TESTED] Status:WORKING
    /**
     * @brief Gets previous hash
     * @return previous hash
     */
    ///
    /// \brief getStoredByAuthor
    /// \param _path
    /// \return
    ///
    QList<Stored> getStoredByAuthor(QByteArray path,
                                    BigNumber _author) const; //[TESTED] Status:WORKING
    ///
    /// \brief getStoredByPath
    /// \param _path
    /// \return
    ///
    QList<Stored> getStoredByPath(QByteArray _path) const; //[TESTED] Status:WORKING

    Stored getLastStoredByPath(QByteArray _path) const; //[TESTED] Status:WORKING

private:
    int addStored(const Stored &_stored); //[TESTED] Status:WORKING
    ActorIndex *s_ActorIndex;
    AccountController *account_contrlr;

    // get changed from external source
    // state - changed, delete, add
    // _datachange - int - first byte to change
    // QByteArray - data that will delete or change or add
    // DigSig - dignital signature that approve author that want to change file
    // path - path to file that need to change

signals:

    void
    NewStored(Stored _stored); // emit after execute slot getChanged, connect to NetworkManager

    //    void StoredIsMissing(Stored _stored);

    // emit when find Stored by some criterious

    //    void StoredByPathFound(QHostAddress peerAddress, QList<Stored> listStored);
    //    void LastStoredByPathFound(QHostAddress peerAddress, Stored sStored);
    //    void StoredByAuthorFound(QHostAddress peerAddress, QList<Stored> listStored);
};

#endif // STOREDINDEX_H
