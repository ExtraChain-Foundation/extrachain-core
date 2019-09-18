#include "datastorage/index/actorindex.h"

PublicProfile::PublicProfile(Profile _profile, QByteArray _sign, QString path)
{
    profile = saveProfile(_profile, path, _sign);
    sign = _sign;
}

PublicProfile::PublicProfile(Profile _profile, QByteArray _sign)
{
    profile = _profile;
    sign = _sign;
}
PublicProfile::PublicProfile()
{
    profile = Profile();
    sign = "";
}
PublicProfile::PublicProfile(const QByteArray &serialize)
{
    int signSize = Utils::qByteArrayToInt(serialize.mid(serialize.size() - 4, 4));
    QByteArray _sign = serialize.mid(serialize.size() - signSize - 4, signSize);
    QByteArray data = serialize.mid(0, serialize.size() - signSize - 4);
    QByteArrayList list = deserialize(data);
    sign = _sign;
    profile = list;
}
QByteArray PublicProfile::serialize() const
{
    QByteArray data = serialize(profile.getConstList());
    QByteArray _sign = Serialization::universalSerialize({ sign }, 4);
    QByteArray signSize = _sign.mid(0, 4);
    _sign += signSize;
    _sign = _sign.mid(4, _sign.size());
    data += _sign;
    return data;
}

Profile PublicProfile::saveProfile(Profile newProfile, const QString &path, QByteArray sign)
{
    QByteArrayList &list = newProfile.list();

    QString pathProfile =
        path.mid(0, path.size() - newProfile.at(2).size()) + "profile/" + newProfile.at(2) + ".profile";
    QDir().mkdir(path.mid(0, path.size() - newProfile.at(2).size()) + "profile/");
    QFile profile(pathProfile);
    QByteArray serializeProfile = serialize(list);
    if (profile.exists())
    {
        profile.open(QIODevice::ReadOnly);
        QByteArray oldProfile = profile.readAll();
        profile.flush();
        profile.close();
        if (serializeProfile == oldProfile)
        {
            qDebug() << "profile exist";
            return Profile();
        }
        else
            profile.resize(0);
    }
    QByteArray signWrite = Serialization::universalSerialize({ sign }, 4);
    QByteArray sign2 = signWrite.mid(4, signWrite.size());
    QByteArray signSize = signWrite.mid(0, 4);
    signWrite = sign2 + signSize;
    profile.open(QIODevice::WriteOnly);
    profile.write(serializeProfile + signWrite);
    profile.flush();
    profile.close();

    return newProfile;
}

PublicProfile PublicProfile::getProfile(const QString &path, const QString id)
{
    QDir().mkdir(path.mid(0, path.size() - id.size()) + "profile/");
    QString pathProfile = path.mid(0, path.size() - id.size()) + "profile/" + id + ".profile";
    QFile profile(pathProfile);
    if (!profile.exists())
    {
        qDebug() << "Profile isn`t exist";
        return PublicProfile();
    }
    profile.open(QIODevice::ReadOnly);
    QByteArray serializeData = profile.readAll();
    profile.flush();
    profile.close();
    int signSize = Utils::qByteArrayToInt(serializeData.mid(serializeData.size() - 4, 4));
    QByteArray sign = serializeData.mid(serializeData.size() - 4 - signSize, signSize);
    serializeData = serializeData.mid(0, serializeData.size() - 4 - signSize);
    QByteArrayList listProfile = deserialize(serializeData);
    PublicProfile pubProfile(listProfile, sign);

    return pubProfile;
}

Profile PublicProfile::saveProfileFromNet(Profile newProfile, QString path)
{
    QByteArrayList &list = newProfile.list();

    QString pathProfile =
        path.mid(0, path.size() - newProfile.at(2).size()) + "profile/" + newProfile.at(2) + ".profile";
    QDir().mkdir(path.mid(0, path.size() - newProfile.at(2).size()) + "profile/");
    QFile profile(pathProfile);
    QByteArray serializeProfile = serialize(list);
    if (profile.exists())
    {
        profile.open(QIODevice::ReadOnly);
        QByteArray oldProfile = profile.readAll();
        profile.flush();
        profile.close();
        if (serializeProfile == oldProfile)
        {
            qDebug() << "profile exist";
            return Profile();
        }
        else
            profile.resize(0);
    }

    profile.open(QIODevice::WriteOnly);
    profile.write(serializeProfile);
    profile.flush();
    profile.close();

    return newProfile;
}

QByteArray PublicProfile::serialize(QByteArrayList actorList)
{
    QByteArray data = "";
    QByteArray actorData = "";
    uint count = 0;

    for (auto element : actorList)
    {
        if (count <= 2)
        {
            if (count > 0)
            {
                data = Serialization::universalSerialize({ element }, 4);
                actorData.append(data);
                data.clear();
                count++;
                continue;
            }
            actorData.append(element);
            count++;
            continue;
        }
        if (element == "")
        {
            data += "1| ";
            actorData.append(data);
            data.clear();
            continue;
        }
        data += QByteArray::number(element.size());
        data += "|";
        data += element;
        actorData.append(data);
        data.clear();
    }

    return actorData;
}

QByteArrayList PublicProfile::deserialize(QByteArray serializeData)
{
    QByteArrayList profileData;
    int position = 0, sizeField = 0;

    for (int i = 0; i < serializeData.size(); i++)
    {
        if (i == 0)
        {
            profileData.append(serializeData.mid(i, 1));
            ++position;
            continue;
        }
        if (i <= 2)
        {
            profileData.append(
                serializeData.mid(position + 4, Utils::qByteArrayToInt(serializeData.mid(position, 4))));
            position += 4 + Utils::qByteArrayToInt(serializeData.mid(position, 4));
            continue;
        }
        sizeField = Utils::qByteArrayToInt(
            serializeData.mid(position, serializeData.indexOf("|", position) - position));
        position += serializeData.mid(position, serializeData.indexOf("|", position) - position).size() + 1;
        if (serializeData.mid(position, sizeField) == " ")
        {
            profileData.append("");
            position += sizeField;
            i = position;
            continue;
        }
        profileData.append(serializeData.mid(position, sizeField));
        position += sizeField;
        i = position;
    }

    return profileData;
}

indexList::indexList(long long curPos, int _size)
{
    currentPosition = curPos;
    size = _size;
}

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
    if (newProfile.sign == "")
    {
        qDebug() << "ActorIndex::saveProfileFromNetwork : empty sign";
        return;
    }
    qDebug() << "Save profile with id" << newProfile.profile.at(2);
    QString path = buildFilePath(BigNumber(newProfile.profile.at(2)));
    Actor<KeyPublic> key = getActor(newProfile.profile.at(2));
    PublicProfile profile;
    if (!key.getKey()->verify(PublicProfile::serialize(newProfile.profile.list()), newProfile.sign))
        qDebug() << "ActorIndex::saveProfileFromNetwork : profile isn`t verify";
    else
        PublicProfile profile(newProfile.profile, newProfile.sign, path);
    if (profile.profile.at(2) == "")
        return;
    else
        sendProfile(profile);
}

void ActorIndex::saveProfile(Actor<KeyPrivate> *key, Profile newProfile)
{

    qDebug() << "Save profile with id" << newProfile.at(2);
    QString path = buildFilePath(BigNumber(newProfile.at(2)));
    QByteArray sign = key->getKey()->sign(PublicProfile::serialize(newProfile.list()));
    PublicProfile pubProfile(newProfile, sign, path);
    if (pubProfile.profile.at(2) == "")
        return;
    else
        sendProfile(pubProfile);
}

PublicProfile ActorIndex::getProfileToSend(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);

    return pubProfile;
}

void ActorIndex::requestProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        emit sendProfileToUi(id, pubProfile.profile);
    else
        qDebug() << "incorrect profile, fuck off";
}

Profile ActorIndex::getProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (pubProfile.sign == "")
        return pubProfile.profile;
    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        return pubProfile.profile;
    else
    {
        qDebug() << "incorrect profile, fuck off";
        return Profile();
    }
}
PublicProfile ActorIndex::getPublicProfile(QString id)
{
    QString path = buildFilePath(BigNumber(id.toUtf8()));
    Actor<KeyPublic> key = getActor(id.toUtf8());
    PublicProfile pubProfile = PublicProfile::getProfile(path, id);
    if (pubProfile.sign == "")
        return pubProfile;
    if (key.getKey()->verify(PublicProfile::serialize(pubProfile.profile.list()), pubProfile.sign))
        return pubProfile;
    else
    {
        qDebug() << "incorrect profile, fuck off";
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
