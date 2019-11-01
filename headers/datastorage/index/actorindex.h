#ifndef ACTORINDEX_H
#define ACTORINDEX_H

#include <QHostAddress>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/index/fileindex.h"
#include <datastorage/searchfilters.h>
#include "profile/public_profile.h"
#include "network/packages/entities/entity_message.h"
#include "network/socket_pair.h"
#include "headers/network/packages/base_message_response.h"
#include "headers/network/packages/service/response_messages.h"
#include "headers/network/packages/service/all_messages.h"
/**
 * @brief Actors that stored in blockchain
 */

class ActorIndex : public QObject
{
    Q_OBJECT
    const QByteArray classType = Messages::ACTOR_MESSAGE;
    const QByteArray profileType = Messages::PROFILE_FILE;
    const QByteArray getActorMessage = Messages::GET_ACTOR_MESSAGE;

private:
    BigNumber records = 0;
    const QString folderPath =
        DataStorage::BLOCKCHAIN_INDEX + "/" + DataStorage::ACTOR_INDEX_FOLDER_NAME + '/';
    short SECTION_NAME_SIZE = 2;
    /**
     * @brief buildFilePath
     * @param id
     * @return
     */
    QString buildFilePath(const QByteArray &id) const;

public:
    /**
     * @brief companyId
     */
    QByteArray *companyId = nullptr;
    /**
     * @brief ActorIndex
     */
    ActorIndex(QObject *parent = nullptr);
    /**
     * @brief ~ActorIndex
     */
    ~ActorIndex();
    /**
     * @brief Check actor with actorId exist
     * @param actorId
     * @return resultCode, true - exist, false - none
     */
    bool actorExist(BigNumber actorId);

    /**
     * @brief Gets actor from local storage
     * @param id - actor's id
     * @return Found actor, or empty actor (if not found)
     */
    Actor<KeyPublic> getActor(const BigNumber &id);

    /**
     * @brief Validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const Block &block);

    /**
     * @brief Validates transaction digital signature
     * @param tx
     * @return true if transaction is valid
     */
    bool validateTx(const Transaction &tx);

    /**
     * @brief getById
     * @param id
     * @return
     */
    QByteArray getById(const BigNumber &id) const;
    /**
     * @brief add
     * @param BigNumber id actorId for add
     * @param data
     * @return
     */
    int add(const BigNumber &id, const QByteArray &data);
    BigNumber getRecords() const;

    void setCompanyId(QByteArray *value);

    QString getFolderPath() const;

public:
    /**
     * @brief Attempts to save actor to local storage
     * @param actor
     */
    void handleNewActor(Actor<KeyPublic> actor);
public slots:
    void process();
    void handleGetActor(const BigNumber &actorId, QByteArray reqHash, const SocketPair &receiver);

    /**
     * @brief The same as handleNewActor, but emit's ActorIsMissing signal
     * if there no such actor in storage
     * @param actor
     */
    void handleNewActorCheck(Actor<KeyPublic> actor);
    void getActorCount(const QByteArray &requestHash, const SocketPair &receiver);

    void saveProfile(Actor<KeyPrivate> *key, QByteArrayList newProfile);
    void saveProfileFromNetwork(const QByteArray &newProfile);
    void requestProfile(QString id);
    QByteArrayList getProfile(QString id);
    void profileToSearch(SearchFilters filters);

    /**
     * @brief Serializes an actor and make a file in fs.
     * @param actor
     * @return resultCode, 0 - actor is saved
     */
    int addActor(const Actor<KeyPublic> &actor);

    /**
     * @brief
     */
    void removeAll();

signals:
    /**
     * @brief sendMessage to NetManager slot: sendMessage
     * @param data
     * @param type
     */
    void sendMessage(const QByteArray &data, const QByteArray &type);
    /**
     * @brief responseReady
     * @param data
     * @param msgType
     * @param requestHash
     * @param receiver
     */
    void responseReady(const QByteArray &data, const QByteArray &msgType, const QByteArray &requestHash,
                       const SocketPair &receiver);

    void sendProfileToUi(QString userID, QByteArrayList profile);
    void PrivateActorIsVerified(Actor<KeyPrivate> actor);
    void PublicActorIsVerified(Actor<KeyPublic> actor); // unused

    void initDfs(BigNumber userId);
    void initContractList(QVariantMap map);
    /**
     * @brief There no such actor in the local storage
     * @param actor
     */
    void ActorIsMissing(Actor<KeyPublic> actor);
    void finished();
    void sendProfileToSearchToUi(QList<Profile> profiles);
};

#endif // ACTORINDEX_H
