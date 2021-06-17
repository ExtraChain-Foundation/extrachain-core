/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "datastorage/index/actorindex.h"
#include "resolve/resolve_manager.h"

void ActorIndex::setAccController(AccountController *value)
{
    accController = value;
}

ActorIndex::ActorIndex(QObject *parent)
    : QObject(parent)

{
    DBConnector db;
    bool isDbOpen = db.open(folderPath.toStdString() + "actors");
    bool isDbCreate = db.createTable(Config::DataStorage::actorsTableCreate);

    if (!isDbOpen || !isDbCreate)
        qFatal(QString("db for actors (open: %1, create: %2)").arg(isDbOpen, isDbCreate).toLatin1().data());

    records = db.count("Actors");
    qDebug() << "[ActorIndex] Count:" << records;
}

ActorIndex::~ActorIndex()
{
}

Actor<KeyPublic> ActorIndex::getActor(const ActorId &id)
{
    if (id.isEmpty())
    {
        qDebug() << "[ActorIndex] Error: try get actor with id =" << id;
        return Actor<KeyPublic>();
    }

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty())
    {
        auto actor = Actor<KeyPublic>(serializedActor);
        if (actor.account() == ActorType::Account && actor.profile().sign.isEmpty())
        {
            Messages::GetActorMessage msg;
            msg.actorId = id;
            resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
        }

        return actor;
    }
    else
    {
        Messages::GetActorMessage msg;
        msg.actorId = id;
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
        // emit sendMessage(msg.serialize(), getActorMessage);
        qDebug() << "[ActorIndex] There no actor with id:" << id;
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::hasActor(const ActorId &id)
{
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    return QFileInfo(filePath).size() > 0;
}

void ActorIndex::removeActor(const ActorId &id, bool resend)
{
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    QFile::remove(filePath);
    QFile::remove(filePath + "/profile/" + id.toByteArray() + ".profile");

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

void ActorIndex::handleGetActor(const ActorId &actorId, QByteArray reqHash, const SocketPair &receiver)
{
#ifdef QT_DEBUG
    if (actorId.toByteArray().size() < 18)
        qFatal("handleGetActor, size < 18");
#endif
    // receive id
    // create response message
    Actor<KeyPublic> actor = getActor(actorId);
    if (!actor.empty())
    {
        // emit responseReady(actor.serialize(), Messages::GET_ACTOR_RESPONSE_MESSAGE, reqHash, receiver);
        auto profileData = actor.profile().serialize();
        bool isProfile = !profileData.isEmpty();

        resolveManager->sendMessageResponse(actor.serialize(), Messages::GeneralResponse::getActorResponse,
                                            reqHash, receiver);

        if (isProfile)
        {
            resolveManager->registrateMsg(profileData, Messages::ChainMessage::profileMessage);
        }
        else if (actor.account() != ActorType::Wallet && actor.account() != ActorType::Company)
        { // if profile not exist
            static QMap<QByteArray, qint64> tempCheck;
            qDebug() << "[ActorIndex] No profile for actor" << actorId;

            auto current = QDateTime::currentSecsSinceEpoch();
            auto actorIdBytes = actorId.toByteArray();
            if (tempCheck[actorIdBytes] < current - 10)
            {
                qDebug() << "[ActorIndex] Send get actor if no profile:" << actorId;
                tempCheck[actorIdBytes] = current;
                Messages::GetActorMessage msg;
                msg.actorId = actorId;
                resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetActor);
            }
        }

        // emit sendMessage(actor.profile().serialize(), Messages::PROFILE_FILE);
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

void ActorIndex::getAllActors(ActorId id, bool isUser)
{
    Q_UNUSED(isUser)

    if (accController->getAccountCount() > 0)
    {
        Messages::GetAllActorMessage msg;
        msg.actorId = id.toByteArray();
        resolveManager->registrateMsg(msg.serialize(), Messages::GeneralRequest::GetAllActors);
        qDebug() << "[ActorIndex] Get all actors request";
        // emit sendMessage(msg.serialize(), getAllActorMessage);
    }
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor)
{
    switch (addActor(actor))
    {
    case 0:
        qDebug()
            << QString("[ActorIndex] New actor [%1] is successfully saved").arg(QString(actor.serialize()));

        // TODO: remove me?
        if (actor.account() == ActorType::Account && profilesHandle.contains(actor.id().toByteArray()))
        {
            saveProfileFromNetwork(profilesHandle[actor.id().toByteArray()]);
        }
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << QString("[ActorIndex] New actor [%1] can't be added: it is already in storage")
                        .arg(QString(actor.serialize()));
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning()
            << QString("[ActorIndex] Error: new actor [%1] is not saved").arg(QString(actor.serialize()));
        break;
    default:
        qWarning() << "[ActorIndex] Error: unexpected return type";
    }
}

void ActorIndex::handleNewAllActors(QByteArrayList actors)
{
    for (const QByteArray &actor : actors)
        getActor(actor);
}

void ActorIndex::setResolveManager(ResolveManager *value)
{
    resolveManager = value;
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const SocketPair &receiver)
{

    qDebug() << "[ActorIndex] Get actor count response:" << this->getRecords();
    resolveManager->sendMessageResponse(QByteArray::number(this->getRecords()),
                                        Messages::GeneralResponse::getActorCountResponse, requestHash,
                                        receiver);
    //    emit responseReady(this->getRecords().toByteArray(), Messages::GET_ACTOR_COUNT_RESPONSE_MESSAGE,
    //                       requestHash, receiver);
}

void ActorIndex::saveProfileFromNetwork(const QByteArray &newProfile)
{
    PublicProfile profile(newProfile);
    if (profile.sign == "" || newProfile.isEmpty())
        return;
    Actor<KeyPublic> actor = getActor(profile.id);
    if (actor.empty())
    {
        qDebug() << "[ActorIndex] We don't have actor for profile" << profile.id;
        profilesHandle[profile.id] = newProfile;
        return;
    }

    QByteArray profileData = PublicProfile::getProfileDataFromNetwork(newProfile);
    if (profileData.isEmpty())
    {
        return;
    }

    if (actor.key()->verify(profileData, profile.sign))
    {
        qDebug() << "[ActorIndex] Save public profile with id:" << profile.id;
        bool isSaved = actor.profile().saveProfileFromNet(profile.dataToProfile);

        if (isSaved)
        {
            if (profile.serialize().isEmpty())
            {
                return;
            }

            resolveManager->registrateMsg(profile.serialize(), Messages::ChainMessage::profileMessage);
            emit profileAvailabled(profile.id, actor.profile().getListProfile());
        }
    }
    else
        qDebug() << "[ActorIndex] Save profile from network: incorrect profile verify" << profile.id;
}

void ActorIndex::saveProfile(Actor<KeyPrivate> *actor, QByteArrayList newProfile)
{
    if (actor->empty())
        return;

    qDebug() << "[ActorIndex] Save public profile with id" << newProfile.at(2);
    QByteArray path = buildPathPubProfile(ActorId(newProfile.at(2)).toByteArray()).toUtf8();
    QByteArray sign = actor->key()->sign(PublicProfile::serialize(newProfile));
    PublicProfile pubProfile(newProfile, sign, path, newProfile.at(2));

    if (pubProfile.sign == "")
    {
        qDebug() << "[ActorIndex] Save profile: incorrect profile" << newProfile.at(2);
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

    emit profileAvailabled(id, list);
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
        qDebug() << "[ActorIndex] Creating dir:" << pathToFolder;
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
        qDebug() << "[ActorIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + id + ".profile";
}

void ActorIndex::setCompanyId(QByteArray *value)
{
    companyId = value;
}

qint64 ActorIndex::getRecords() const
{
    return records;
}

int ActorIndex::add(const ActorId &id, const QByteArray &data)
{
    // if (id <= 1000)
    //     qFatal("Try to add actor with id %s", id.toByteArray().constData());

    QString path = buildFilePath(id.toByteArray());
    QFile file(path);
    qDebug() << "[ActorIndex] Saving the file:" << path;
    // QString profilePath = buildPathPubProfile(id.toActorId());
    if (file.exists())
    {
        qDebug() << "[ActorIndex] Can't save the file" << path << "(file already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (file.open(QIODevice::WriteOnly))
    {
        file.write(data);
        file.flush();
        file.close();

        return 0;
    }

    qCritical() << "[ActorIndex] Can't save the file" << path << "(file is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

QByteArray ActorIndex::getById(const ActorId &id) const
{
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    QFile file(filePath);
    if (!file.exists())
    {
        qCritical() << "[ActorIndex] File with path" << filePath << "not found";
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
    auto actorId = actor.id().toByteArray();

    if (actor.account() == ActorType::Company && companyId == nullptr)
    {
        qDebug() << "[ActorIndex] Save company id:" << actorId;
        companyId = new QByteArray(actorId);
    }

    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED)
    {
        this->records++;
        DBConnector db;
        db.open(folderPath.toStdString() + "actors");
        bool dbInsert = db.insert(Config::DataStorage::actorsTable,
                                  { { "id", actorId.toStdString() }, { "type", actor.accountStdString() } });
        if (!dbInsert)
            qFatal("db actor insert error");

        qDebug() << "[ActorIndex] Actor " << actor.id() << "was added";
        resolveManager->registrateMsg(actor.serialize(), Messages::ChainMessage::actorMessage);
        // emit sendMessage(actor.serialize(), classType);
        // qDebug() << "emit signal for init dfs for user" << actorId;
        emit initDfs(actor.id());
    }

    return result;
}

QByteArrayList ActorIndex::allActors()
{
    QByteArrayList result;

    DBConnector db;
    db.open(folderPath.toStdString() + "actors");
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors)
    {
        result << actor["id"].data();
    }

    return result;
}

void ActorIndex::removeAll()
{
    qDebug() << "[ActorIndex] Clearing file index:" << folderPath;

    QDir folder(folderPath);
    const auto folders =
        folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
    for (const QString &section : qAsConst(folders))
    {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records = 0;
}
