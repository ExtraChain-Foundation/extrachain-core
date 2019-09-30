#include "datastorage/index/actorindex.h"

ActorIndex::ActorIndex()
    : FileIndex(/*DataStorage::BLOCKCHAIN_INDEX + '/' +*/ DataStorage::ACTOR_INDEX_FOLDER_NAME)
{
}

ActorIndex::ActorIndex(QString folderName)
    : FileIndex(folderName)
{
}

Actor<KeyPublic> ActorIndex::getActor(const BigNumber &id) const
{

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty())
    {
        return Actor<KeyPublic>(serializedActor);
    }
    qDebug() << "There no actor with id:" << id;
    return Actor<KeyPublic>();
}

bool ActorIndex::validateBlock(const Block &block) const
{
    Actor<KeyPublic> actor = this->getActor(block.getApprover());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate block" << block.getIndex() << ": There no actor"
                   << block.getApprover() << " in local storage";
        return false;
    }
    return block.verify(actor);
}

bool ActorIndex::validateTx(const Transaction &tx) const
{
    Actor<KeyPublic> actor = this->getActor(tx.getApprover());
    if (actor.isEmpty())
    {
        qWarning() << "Can not validate tx" << tx.getHash() << ": There no actor" << tx.getApprover()
                   << " in local storage";
        return false;
    }
    return tx.verify(actor);
}

void ActorIndex::process()
{
}

// todo: look closely at this method!
void ActorIndex::validatePrivateActor(Actor<KeyPrivate> *actor)
{
    if (actor == nullptr)
    {
        qCritical() << "Null pointer";
        return;
    }

    KeyPrivate *prKey = actor->getKey();
    BigNumber last = getLastSavedId();

    for (BigNumber i = getFirstSavedId(); i < last; ++i)
    {
        if (getActor(i).getKey()->extractPublicKey() == prKey->extractPublicKey())
        {
            qDebug() << "Error: Created actor is not unique";
            return;
        }
    }

    emit PrivateActorIsVerified(*actor);
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor)
{
    //    qDebug() << "adfklsfkl;adskl;afsdl;afsdl;";
    switch (addActor(actor))
    {
    case 0:
        qDebug() << QString("New actor [%1] is successfully saved").arg(actor.toString());
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << QString("New actor [%1] can't be added: it is already in storage").arg(actor.toString());
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning() << QString("Error: new actor [%1] is not saved").arg(actor.toString());
        break;
    default:
        qWarning() << "Error: unexpected return type";
    }
}

void ActorIndex::handleNewActorCheck(Actor<KeyPublic> actor)
{
    if (getActor(actor.getId()).isEmpty())
    {
        handleNewActor(actor);
        emit ActorIsMissing(actor);
    }
}

void ActorIndex::saveProfileFromNetwork(PublicProfile newProfile)
{
    if (newProfile.sign == "" || newProfile.profile.list().at(2) == "")
    {
        qDebug() << "ActorIndex::saveProfileFromNetwork : empty sign";
        return;
    }
    QString path = buildFilePath(BigNumber(newProfile.profile.at(2)));
    Actor<KeyPublic> key = getActor(newProfile.profile.at(2));
    if (key.getHash().isEmpty())
    {
        qDebug() << "saveProfileFromNetwork: Key " << newProfile.profile.at(2) << " is empty";
        return;
    }

    if (!key.getKey()->verify(PublicProfile::serialize(newProfile.profile.list()), newProfile.sign))
    {
        qDebug() << "ActorIndex::saveProfileFromNetwork : profile isn`t verify";
        return;
    }

    PublicProfile profile(newProfile.profile, newProfile.sign, path);
    if (profile.profile.at(2) == "")
        return;
    else
    {
        qDebug() << "Save profile with id" << newProfile.profile.at(2);
        key.setProfile(profile);
        sendProfile(profile);
    }
}

void ActorIndex::saveProfile(Actor<KeyPrivate> *key, Profile newProfile)
{
    if (key->getHash().isEmpty())
        return;
    qDebug() << "Save profile with id" << newProfile.at(2);
    QString path = buildFilePath(BigNumber(newProfile.at(2)));
    QByteArray sign = key->getKey()->sign(PublicProfile::serialize(newProfile.list()));
    PublicProfile pubProfile(newProfile, sign, path);
    if (pubProfile.profile.at(2) == "")
    {
        qDebug() << "saveProfile: incorrect profile" << newProfile.at(2);
        return;
    }
    else
    {
        key->setProfile(pubProfile);
        sendProfile(pubProfile);
    }
}

PublicProfile ActorIndex::getProfileToSend(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));

    return PublicProfile::getProfile(path, id);
}

void ActorIndex::requestProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    if (key.getHash().isEmpty())
    {
        qDebug() << "requestProfile: Key " << id << " is empty";
        return;
    }
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (pubProfile.sign == "")
    {
        qDebug() << "requestProfile: incorrect profile" << id;
        return;
    }
    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        emit sendProfileToUi(id, pubProfile.profile);
    else
        qDebug() << "requestProfile: incorrect profile" << id;
}

Profile ActorIndex::getProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (pubProfile.sign == "" || key.getHash() == "")
    {
        qDebug() << "getProfile: incorrect profile" << id;
        return Profile();
    }

    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        return pubProfile.profile;
    else
    {
        qDebug() << "getProfile: incorrect profile" << id;
        return Profile();
    }
}
PublicProfile ActorIndex::getPublicProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (pubProfile.sign == "")
    {
        qDebug() << "getPublicProfile: incorrect profile";
        return PublicProfile();
    }
    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        return pubProfile;
    else
    {
        qDebug() << "getPublicProfile: incorrect profile";
        return PublicProfile();
    }
}

bool ActorIndex::actorExist(BigNumber actorId)
{
    if (getById(actorId) == QByteArray())
        return false;
    return true;
}

int ActorIndex::addActor(const Actor<KeyPublic> &actor)
{
    int result = this->add(actor.getId(), actor.serialize());
    qDebug() << actor.getId().serialize() << " =~= " << lastSavedId.serialize();
    if (actor.getId() + 1 == lastSavedId || lastSavedId == BigNumber(1))
        emit actorIndexUpdated();
    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        qDebug() << "ActorIndex: actor - " << actor.getId() << " was added "
                 << "lsd: " << lastSavedId;
        // todo: Event should be emited only on CREATING new actors, not on RECEIVING new
        // one's make methods:
        // * addActor -> add actor to storage
        // * addNewActor -> add actor to storage and emit event NewActor
        if (actor.getId() > BigNumber(0))
        {
            //            ++lastSavedId;
            emit NewActor(actor);
        }
        if (actor.getAccount())
        {
            qDebug() << "emit signal for init dfs for user" << actor.getId().toByteArray();
            emit initDfs(actor.getId());
        }
    }
    return result;
}

void ActorIndex::profileToSearch(SearchFilters filters)
{
    QList<Profile> profiles;
    qDebug() << "first last ids" << firstSavedId << lastSavedId;

    for (int id = 0; id <= lastSavedId; id++)
    {
        Profile profile = getProfile(QString::number(id, 16));

        if (profile.at(2) == "")
            continue;
        if (profile.userId() == filters.currentId)
            continue;
        qint16 type = profile.type();
        if (type == 0 || type == 6)
            continue;

        QString firstName = profile.firstName().toLower();
        QString lastName = profile.lastName().toLower();

        if (!(profile.firstName().toLower().startsWith(filters.name.toLower())
              || profile.lastName().toLower().startsWith(filters.name.toLower())))
            continue;

        /*
        if (profile.type() != filters.userType && filters.userType != -1)
            continue;
        if (profile.country() != filters.location && filters.location != -1)
            continue;
        if (profile.gender() != filters.gender && filters.gender != -1)
            continue;
        if (filters.heightMax != -1 && !(filters.heightMax > profile.sizes().at(0) > filters.heightMin))
            continue;
        if (filters.bustMax != -1 && !(filters.bustMax > profile.sizes().at(5) > filters.bustMin))
            continue;
        if (filters.waistMax != -1 && !(filters.waistMax > profile.sizes().at(4) > filters.waistMin))
            continue;
        if (filters.hipsMax != -1 && !(filters.hipsMax > profile.sizes().at(6) > filters.hipsMin))
            continue;
        if (filters.shoesMax != -1 && !(filters.shoesMax > profile.sizes().at(2) > filters.shoesMin))
            continue;
        if (filters.category != profile.category() && !filters.category.isEmpty())
            continue;
        if (filters.body != profile.body() && !filters.body.isEmpty())
            continue;
        if (filters.hair != profile.hair() && !filters.hair.isEmpty())
            continue;
        if (filters.hairLength != profile.hairLength() && !filters.hairLength.isEmpty())
            continue;
        if (filters.eye != profile.eye() && !filters.eye.isEmpty())
            continue;
        if (filters.ethnicity != profile.ethnicity() && !filters.ethnicity.isEmpty())
            continue;
        if (filters.style != profile.style() && !filters.style.isEmpty())
            continue;
        if (filters.sports != profile.sports() && !filters.sports.isEmpty())
            continue;
        if (filters.skin != profile.skin() && !filters.skin.isEmpty())
            continue;
        if (filters.scope != profile.scope() && !filters.scope.isEmpty())
            continue;
        if (filters.direction != profile.direction() && !filters.direction.isEmpty())
            continue;
        if (filters.workStyle != profile.workStyle() && !filters.workStyle.isEmpty())
            continue;
        if (filters.fashion != profile.fashion() && !filters.fashion.isEmpty())
            continue;
        */

        profiles.append(profile);
    }

    emit sendProfileToSearchToUi(profiles);
}
