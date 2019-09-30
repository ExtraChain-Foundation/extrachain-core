#ifndef ACTORINDEX_H
#define ACTORINDEX_H

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/index/fileindex.h"
#include <datastorage/searchfilters.h>
#include "profile/public_profile.h"
/**
 * @brief Actors that stored in blockchain
 */

class ActorIndex : public FileIndex
{
    Q_OBJECT
public:
    ActorIndex();
    ActorIndex(QString folderName); // custom folder name
public:
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
    Actor<KeyPublic> getActor(const BigNumber &id) const;

    /**
     * @brief Validates block digital signature
     * @param block
     * @return true if block is valid
     */
    bool validateBlock(const Block &block) const;

    /**
     * @brief Validates transaction digital signature
     * @param tx
     * @return true if transaction is valid
     */
    bool validateTx(const Transaction &tx) const;

public slots:
    void process();
    /**
     * @brief Validates private actor created by ActorController. Checks the uniqueness of the
     * key. Emit's PrivateActorIsVerified signal on success.
     * @param private actor
     */
    void validatePrivateActor(Actor<KeyPrivate> *actor);
    /**
     * @brief Attempts to save actor to local storage
     * @param actor
     */
    void handleNewActor(Actor<KeyPublic> actor);
    /**
     * @brief The same as handleNewActor, but emit's ActorIsMissing signal
     * if there no such actor in storage
     * @param actor
     */
    void handleNewActorCheck(Actor<KeyPublic> actor);

    void saveProfile(Actor<KeyPrivate> *key, Profile newProfile);
    void saveProfileFromNetwork(PublicProfile newProfile);
    void requestProfile(QString id);
    PublicProfile getProfileToSend(QString id);
    Profile getProfile(QString id);
    PublicProfile getPublicProfile(QString id);
    void profileToSearch(SearchFilters filters);

    /**
     * @brief Serializes an actor and make a file in fs.
     * @param actor
     * @return resultCode, 0 - actor is saved
     */
    int addActor(const Actor<KeyPublic> &actor);

signals:
    void sendProfile(PublicProfile profile);
    void sendProfileToUi(QString userID, Profile profile);
    void PrivateActorIsVerified(Actor<KeyPrivate> actor);
    void PublicActorIsVerified(Actor<KeyPublic> actor); // unused
    /**
     * @brief New actor is created
     * @param actor
     */
    void NewActor(Actor<KeyPublic> actor);
    void actorIndexUpdated();

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
