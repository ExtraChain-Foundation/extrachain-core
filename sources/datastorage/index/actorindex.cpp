#include "datastorage/index/actorindex.h"
#include "resolve/resolve_manager.h"

void ActorIndex::setAccController(AccountController *value)
{
    accController = value;
}

ActorIndex::ActorIndex(QObject *parent)
    : QObject(parent)

{
}

ActorIndex::~ActorIndex()
{
}

Actor<KeyPublic> ActorIndex::getActor(const BigNumber &id)
{
    if (id == 0)
    {
        qDebug() << "Error: try get actor with id = 0";
        return Actor<KeyPublic>();
    }
    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty())
    {
        return Actor<KeyPublic>(serializedActor);
    }
    else
    {
        Messages::GetActorMessage msg;
        msg.actorId = id;
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
        //        emit sendMessage(msg.serialize(), getActorMessage);
        qDebug() << "There no actor with id:" << id;
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::hasActor(const BigNumber &id)
{
    QString filePath = folderPath + id.toActorId().right(SECTION_NAME_SIZE) + '/' + id.toActorId();
    return QFileInfo(filePath).size() > 0;
}

void ActorIndex::removeActor(const BigNumber &id, bool resend)
{
    QString filePath = folderPath + id.toActorId().right(SECTION_NAME_SIZE) + '/' + id.toActorId();
    QFile::remove(filePath);
    QFile::remove(filePath + "/profile/" + id.toActorId() + ".profile");

    if (resend)
    {
        Messages::GetActorMessage msg;
        msg.actorId = id;
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
    }
}

bool ActorIndex::validateBlock(const Block &block)
{
    Actor<KeyPublic> actor = this->getActor(block.getApprover());
    if (actor.empty())
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
    if (actor.empty())
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
    if (!actor.empty())
    {
        resolveManager->sendMessageResponse(actor.serialize(), Messages::GeneralResponse::getActorResponse,
                                            reqHash, receiver);
        //        emit responseReady(actor.serialize(), Messages::GET_ACTOR_RESPONSE_MESSAGE, reqHash,
        //        receiver);

        if (!actor.profile().getProfile().isEmpty())
            resolveManager->registrateMsg(actor.profile().serialize(),
                                          Messages::ChainMessage::profileMessage);
        else if (actor.account() != ActorType::Wallet && actor.account() != ActorType::Company)
        {
            qDebug() << "NO PROFILE >" << actorId;
            Messages::GetActorMessage msg;
            msg.actorId = actorId;
            resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
        }
        //            emit sendMessage(actor.profile().serialize(), Messages::PROFILE_FILE);
    }
    else
    {
        Messages::GetActorMessage msg;
        msg.actorId = actorId;
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
    }
}

void ActorIndex::handleGetAllActor(QByteArray reqHash, const SocketPair &receiver)
{
    if (accController->getAccountCount() == 0)
        return;

    QByteArrayList result = allActors();
    if (!result.isEmpty())
    {
        QByteArray data = Serialization::serialize(result, 4);
        resolveManager->sendMessageResponse(data, Messages::GeneralResponse::getAllActorsResponse, reqHash,
                                            receiver);
        //        emit responseReady(Serialization::serialize(result, 4),
        //                           Messages::GET_ALL_ACTORS_RESPONSE_MESSAGE, reqHash, receiver);
    }
    return;
}

void ActorIndex::getAllActors(BigNumber id, bool isUser)
{
    if (accController->getAccountCount() > 0)
    {
        Messages::GetAllActorMessage msg;
        msg.actorId = id.toActorId();
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetAllActors);
        qDebug() << "GetAllActors";
        //    emit sendMessage(msg.serialize(), getAllActorMessage);
    }
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor)
{
    switch (addActor(actor))
    {
    case 0:
        qDebug() << QString("New actor [%1] is successfully saved").arg(QString(actor.serialize()));
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << QString("New actor [%1] can't be added: it is already in storage")
                        .arg(QString(actor.serialize()));
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning() << QString("Error: new actor [%1] is not saved").arg(QString(actor.serialize()));
        break;
    default:
        qWarning() << "Error: unexpected return type";
    }
}

void ActorIndex::handleNewAllActors(QByteArrayList actors)
{
    for (const QByteArray &actor : actors)
        getActor(actor);
}

void ActorIndex::handleNewActorCheck(Actor<KeyPublic> actor)
{
    if (getActor(actor.id()).empty())
    {
        handleNewActor(actor);
        emit ActorIsMissing(actor);
    }
}

void ActorIndex::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const SocketPair &receiver)
{

    qDebug() << "BLOCKCHAIN: getActorCount() count - " << this->getRecords();
    resolveManager->sendMessageResponse(this->getRecords().toByteArray(),
                                        Messages::GeneralResponse::getActorCountResponse, requestHash,
                                        receiver);
    //    emit responseReady(this->getRecords().toByteArray(), Messages::GET_ACTOR_COUNT_RESPONSE_MESSAGE,
    //                       requestHash, receiver);
}

void ActorIndex::saveProfileFromNetwork(const QByteArray &newProfile)
{
    PublicProfile profile(newProfile);
    if (profile.sign == "")
        return;
    Actor<KeyPublic> actor = getActor(profile.id);
    if (actor.empty())
    {
        qDebug() << "ACTOR INDEX: WE DON`T HAVE ACTOR";
        return;
    }
    if (actor.key()->verify(actor.profile().getProfile(), actor.profile().sign))
    {
        qDebug() << "Save publicProfile with id:" << profile.id;
        emit sendProfileToUi(profile.id, actor.profile().getListProfile());
        resolveManager->registrateMsg(profile.serialize(), Messages::ChainMessage::profileMessage);
        // emit sendMessage(profile.serialize(), profileType)
    }
    else
        qDebug() << "saveProfileFromNetwork: incorrect profile verify" << profile.id;
}

void ActorIndex::saveProfile(Actor<KeyPrivate> *actor, QByteArrayList newProfile)
{
    if (actor->empty())
        return;

    qDebug() << "Save PublicProfile with id" << newProfile.at(2);
    QByteArray path = buildPathPubProfile(BigNumber(newProfile.at(2)).toActorId()).toUtf8();
    QByteArray sign = actor->key()->sign(PublicProfile::serialize(newProfile));
    PublicProfile pubProfile(newProfile, sign, path, newProfile.at(2));

    if (pubProfile.sign == "")
    {
        qDebug() << "saveProfile: incorrect profile" << newProfile.at(2);
        return;
    }
    else
    {
        resolveManager->registrateMsg(pubProfile.serialize(), Messages::ChainMessage::profileMessage);
        // emit sendMessage(pubProfile.serialize(), profileType);
    }
}

void ActorIndex::requestProfile(QString id)
{
    Actor<KeyPublic> actor = getActor(id.toUtf8());
    if (actor.empty())
        return;
    if (actor.profile().getProfile() == "")
        return;
    // if (actor.getKey()->verify(actor.profile().getProfile(), actor.profile().sign))

    QByteArrayList list = actor.profile().getListProfile();

    // for test data: start
    //    if (id == "e29c3ac05137ccfc3cde" || id == "6a502ef66fc591980a25" || id == "5078dfb53efc693e1291"
    //        || id == "91609376cc6ee0694255")
    //        list.insert(15, "static/avatar");
    // for test data: remove

    emit sendProfileToUi(id, list);
    // else
    //     qDebug() << "requestProfile: incorrect profile" << id;
}

QByteArrayList ActorIndex::getProfile(QString id)
{
    Actor<KeyPublic> actor = getActor(id.toUtf8());
    PublicProfile pProfile = actor.profile();
    QByteArrayList pList = pProfile.getListProfile();
    if (pProfile.sign == "" || pList.isEmpty())
    {
        if (actor.account() != ActorType::Wallet && actor.account() != ActorType::Company
            && resolveManager != nullptr)
        {
            Messages::GetActorMessage msg;
            msg.actorId = BigNumber(id.toLocal8Bit());
            resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
        }

        return QByteArrayList();
    }

    // if (actor.getKey()->verify(key.profile().getProfile(), pProfile.sign))
    return pList;
    // else
    // {
    //     qDebug() << "getProfile: incorrect profile" << id;
    //     return QByteArrayList();
    // }
}

bool ActorIndex::actorExist(BigNumber actorId)
{
    if (getById(actorId) == QByteArray())
        return false;
    return true;
}

QString ActorIndex::getFolderPath() const
{
    return folderPath;
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

QString ActorIndex::buildPathPubProfile(const QByteArray &id)
{
    QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/profile/";

    QDir dir(pathToFolder);
    if (!dir.exists())
    {
        qDebug() << "Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + id + ".profile";
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
    if (id <= 1000)
        qFatal("Try to add actor with id %s", id.toByteArray().data());
    QString path = buildFilePath(id.toActorId());
    QFile file(path);
    qDebug() << "Saving the file:" << path;
    QString profilePath = buildPathPubProfile(id.toActorId());
    if (file.exists())
    {
        qDebug() << "Can't save the file" << path << "(File already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }
    if (!file.exists())
        this->records++;
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(data);
        file.flush();
        file.close();

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
    int result = this->add(actor.id(), actor.serialize());
    if (actor.account() == ActorType::Company && companyId == nullptr)
    {
        qDebug() << "Save company ID->" << actor.id().toByteArray();
        companyId = new QByteArray(actor.id().toActorId());
    }
    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        qDebug() << "ActorIndex: actor - " << actor.id() << " was added";
        resolveManager->registrateMsg(actor.serialize(), Messages::ChainMessage::actorMessage);
        // emit sendMessage(actor.serialize(), classType);
        qDebug() << "emit signal for init dfs for user" << actor.id().toActorId();
        emit initDfs(actor.id());
    }
    return result;
}

QByteArrayList ActorIndex::allActors()
{
    QByteArrayList result;
    QDir folder(folderPath);
    QStringList listFolder = folder.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &folderName : listFolder)
    {
        QDir folderActor(folderPath + "/" + folderName);
        QStringList listActor = folderActor.entryList(QDir::Files | QDir::NoDotAndDotDot);
        for (const QString &nameActor : listActor)
        {
            QFile file(folderPath + "/" + folderName + "/" + nameActor);
            if (file.exists())
                result.append(nameActor.toUtf8());
        }
    }

    return result;
}

void ActorIndex::removeAll()
{
    qDebug() << "Clearing file index:" << folderPath;

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
    QStringList sectionList =
        QDir(DfsStruct::ROOT_FOOLDER_NAME).entryList(QDir::QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString &section : sectionList)
    {
        QString profileFolderPath = DfsStruct::ROOT_FOOLDER_NAME + "/" + section + "/" + section + ".profile";
        QStringList profilePathList = // TODO: NOT ENTRY LIST
            QDir(profileFolderPath).entryList(QDir::QDir::Files | QDir::NoDotAndDotDot);
        if (section.length() != 20)
            continue;
        Profile profile = getProfile(section);

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
