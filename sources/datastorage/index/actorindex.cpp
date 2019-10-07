#include "datastorage/index/actorindex.h"

ActorIndex::ActorIndex(QObject *parent)
    : QObject(parent)

{
}

ActorIndex::~ActorIndex()
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

void ActorIndex::handleGetActor(const BigNumber &actorId, QByteArray reqHash, const QHostAddress &peerAddress)
{
    // receive id
    // create response message
    Actor<KeyPublic> actor = getActor(actorId);
    emit getActorResponse(actor, reqHash, peerAddress);
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

void ActorIndex::saveProfileFromNetwork(const QByteArray &newProfile)
{
    //    if (newProfile.sign == "" || newProfile.profile.list().at(2) == "")
    //    {
    //        qDebug() << "ActorIndex::saveProfileFromNetwork : empty sign";
    //        return;
    //    }
    //    QString path = buildFilePath(BigNumber(newProfile.profile.at(2)));
    //    Actor<KeyPublic> key = getActor(newProfile.profile.at(2));
    //    if (key.getHash().isEmpty())
    //    {
    //        qDebug() << "saveProfileFromNetwork: Key " << newProfile.profile.at(2) << " is empty";
    //        return;
    //    }

    //    if (!key.getKey()->verify(PublicProfile::serialize(newProfile.profile.list()), newProfile.sign))
    //    {
    //        qDebug() << "ActorIndex::saveProfileFromNetwork : profile isn`t verify";
    //        return;
    //    }

    //    PublicProfile profile(newProfile.profile, newProfile.sign, path);
    //    if (profile.profile.at(2) == "")
    //        return;
    //    else
    //    {
    //        qDebug() << "Save profile with id" << newProfile.profile.at(2);
    //        key.setProfile(profile);
    //        sendMessage(profile.serialize(), profileType);
    //    }
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
        sendMessage(pubProfile.serialize(), profileType);
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
QString ActorIndex::buildFilePath(const BigNumber &id) const
{
    BigNumber section = id.getHexValue().right(SECTION_NAME_SIZE).toUtf8();
    QString pathToFolder = folderPath + section.toString();

    QDir dir(pathToFolder);
    if (!dir.exists())
    {
        qDebug() << "Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + id.toString();
}
BigNumber ActorIndex::getRecords() const
{
    return records;
}

int ActorIndex::add(const BigNumber &id, const QByteArray &data)
{
    QString path = buildFilePath(id);
    QFile file(path);

    qDebug() << "Saving the file:" << path;

    if (file.exists())
    {
        qDebug() << "Can't save the file" << path << "(File already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (file.open(QIODevice::WriteOnly))
    {
        file.write(data);
        file.flush();
        file.close();

        this->records++;

        return 0;
    }

    qCritical() << "Can't save the file" << path << "(File is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

QByteArray ActorIndex::getById(const BigNumber &id) const
{
    QString filePath = folderPath + id.getHexValue().right(SECTION_NAME_SIZE) + '/' + id.getHexValue();
    QFile file(filePath);
    if (!file.exists())
    {
        qCritical() << "[&ActorIndex] file with path >>> " << filePath << "not found";
        return QByteArray();
    }
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    return data;
}
int ActorIndex::addActor(const Actor<KeyPublic> &actor)
{
    int result = this->add(actor.getId(), actor.serialize());
    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        qDebug() << "ActorIndex: actor - " << actor.getId() << " was added "
                 << "lsd: ";

        emit sendMessage(actor.serialize(), classType);

        if (actor.getAccount())
        {
            qDebug() << "emit signal for init dfs for user" << actor.getId().toByteArray();
            emit initDfs(actor.getId());
        }
    }
    return result;
}
void ActorIndex::removeAll()
{
    qDebug() << "Clearing file index: " << folderPath;

    QDir folder(folderPath);
    for (const QString &section :
         folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name))
    {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records = 0;
}
void ActorIndex::profileToSearch(SearchFilters filters)
{
    QList<Profile> profiles;
    QStringList sectionList = QDir(folderPath).entryList(QDir::QDir::Dirs | QDir::NoDot | QDir::NoDotDot);
    for (const QString &section : sectionList)
    {
        QString profileFolderPath = folderPath + section + "/profile";
        QStringList profilePathList =
            QDir(profileFolderPath).entryList(QDir::QDir::Files | QDir::QDir::NoDot | QDir::QDir::NoDotDot);
        for (const QString &profilePath : profilePathList)
        {
            Profile profile = getProfile(profilePath);

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
    }
    emit sendProfileToSearchToUi(profiles);
}
