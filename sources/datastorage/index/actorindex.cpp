#include "datastorage/index/actorindex.h"

ActorIndex::ActorIndex(QObject *parent)
    : QObject(parent)

{
}

ActorIndex::~ActorIndex()
{
}

Actor<KeyPublic> ActorIndex::getActor(const BigNumber &id)
{
    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty())
    {
        return Actor<KeyPublic>(serializedActor);
    }
    else
    {
        Messages::GetActorMessage msg(id);
        emit sendMessage(msg.serialize(), getActorMessage);
        qDebug() << "There no actor with id:" << id;
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::validateBlock(const Block &block)
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

bool ActorIndex::validateTx(const Transaction &tx)
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

void ActorIndex::handleGetActor(const BigNumber &actorId, QByteArray reqHash, const SocketPair &receiver)
{
    // receive id
    // create response message
    Actor<KeyPublic> actor = getActor(actorId);
    if (actor.isEmpty())
        return;
    emit responseReady(actor.serialize(), Messages::GET_ACTOR_RESPONSE_MESSAGE, reqHash, receiver);
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

void ActorIndex::getActorCount(const QByteArray &requestHash, const SocketPair &receiver)
{

    qDebug() << "BLOCKCHAIN: getActorCount() count - " << this->getRecords();
    emit responseReady(this->getRecords().toByteArray(), Messages::GET_ACTOR_COUNT_RESPONSE_MESSAGE,
                       requestHash, receiver);
}

void ActorIndex::saveProfileFromNetwork(const QByteArray &newProfile)
{
    PublicProfile profile(newProfile);
    if (profile.sign == "")
        return;
    Actor<KeyPublic> key = getActor(profile.id);
    if (key.isEmpty())
    {
        qDebug() << "ACTOR INDEX: WE DON`T HAVE ACTOR";
        return;
    }
    if (key.getKey()->verify(key.profile().getProfile(), key.profile().sign))
    {
        qDebug() << "Save publicProfile with id:" << profile.id;
        sendMessage(profile.serialize(), profileType);
    }
    else
        qDebug() << "saveProfileFromNetwork: incorrect profile verify" << profile.id;
}

void ActorIndex::saveProfile(Actor<KeyPrivate> *key, QByteArrayList newProfile)
{
    if (key->getHash().isEmpty())
        return;
    qDebug() << "Save profile with id" << newProfile.at(2);
    QByteArray path = buildFilePath(BigNumber(newProfile.at(2)).toActorId()).toUtf8();
    QByteArray sign = key->getKey()->sign(PublicProfile::serialize(newProfile));
    if (key->profile().sign.isEmpty())
        emit sendMessage("", Messages::GET_BLOCK_COUNT_MESSAGE);
    PublicProfile pubProfile(newProfile, sign, path, newProfile.at(2));
    if (pubProfile.sign == "")
    {
        qDebug() << "saveProfile: incorrect profile" << newProfile.at(2);
        return;
    }
    else
    {
        emit sendMessage(pubProfile.serialize(), profileType);
    }
}

void ActorIndex::requestProfile(QString id)
{
    if (id == "0")
    {
        Profile profile;
        profile.setUserId("0");
        profile.setFirstName("Etalonium");
        profile.setLastName("Fashion Group");
        profile.setRegisterDate(1549290447779);
        profile.setFacebook("https://www.facebook.com/etalonium/");
        profile.setInstagram("etalonium");
        profile.setAvatar({ "3" });
        profile.setBio("ANOTHER WAY TO TOP:\n - Find a job\n - Create stars\n - Your ideal model");
        profile.setUrl("https://etalonium.io/");
        profile.setUrlName("Our site");
        return emit sendProfileToUi("0", QByteArrayList(profile.list()));
    }

    QString path = buildFilePath(id.toUtf8());
    Actor<KeyPublic> key = getActor(id.toUtf8());
    if (key.getKey() == nullptr || key.getHash().isEmpty())
        return;
    if (key.getKey()->verify(key.profile().getProfile(), key.profile().sign))
        emit sendProfileToUi(id, key.profile().getListProfile());
    else
        qDebug() << "requestProfile: incorrect profile" << id;
}

QByteArrayList ActorIndex::getProfile(QString id)
{
    QString path = buildFilePath(id.toUtf8());
    Actor<KeyPublic> key = getActor(id.toUtf8());
    if (key.profile().sign == "")
        return QByteArrayList();

    if (key.getKey()->verify(key.profile().getProfile(), key.profile().sign))
        return key.profile().getListProfile();
    else
    {
        qDebug() << "getProfile: incorrect profile" << id;
        return QByteArrayList();
    }
}

bool ActorIndex::actorExist(BigNumber actorId)
{
    if (getById(actorId) == QByteArray())
        return false;
    return true;
}

QString ActorIndex::buildFilePath(const QByteArray &id) const
{
    QByteArray Id = id;
    if (Id.length() == 19)
        Id = "0" + id;

    QByteArray section = Id.right(SECTION_NAME_SIZE);
    QString pathToFolder = folderPath + section;

    QDir dir(pathToFolder);
    if (!dir.exists())
    {
        qDebug() << "Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

void ActorIndex::setCompanyId(QByteArray *value)
{
    companyId = value;
}

BigNumber ActorIndex::getRecords() const
{
    return records;
}

int ActorIndex::add(const BigNumber &id, const QByteArray &data)
{
    QString path = buildFilePath(id.toActorId());
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
    QString filePath = folderPath + id.toActorId().right(SECTION_NAME_SIZE) + '/' + id.toActorId();
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

        if (actor.getAccount() == 1)
        {
            qDebug() << "emit signal for init dfs for user" << actor.getId().toActorId();
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
            Profile profile = getProfile(profilePath.mid(0, profilePath.size() - 8));

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
