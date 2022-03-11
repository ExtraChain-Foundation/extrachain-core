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
#include "network/packages/base_message.h"
#include "network/packages/base_message_response.h"
#include "network/packages/service/message_types.h"
#include "resolve/resolve_manager.h"

ActorId ActorIndex::firstId() {
    return m_firstId;
}

ActorIndex::ActorIndex(ExtraChainNode *node, QObject *parent)
    : QObject(parent)

{
    this->node = node;

    DBConnector db;
    bool isDbOpen = db.open(folderPath.toStdString() + "actors");
    bool isDbCreate = db.createTable(Config::DataStorage::actorsTableCreate);

    if (!isDbOpen || !isDbCreate)
        qFatal("%s",
               QString("db for actors (open: %1, create: %2)").arg(isDbOpen, isDbCreate).toLatin1().data());

    records = db.count("Actors");
    qDebug() << "[ActorIndex] Count:" << records;
}

ActorIndex::~ActorIndex() {
}

Actor<KeyPublic> ActorIndex::getActor(const ActorId &id) {
    if (id.isEmpty()) {
        qDebug() << "[ActorIndex] Error: try get actor with id =" << id;
        return Actor<KeyPublic>();
    }

    QByteArray serializedActor = this->getById(id);
    if (!serializedActor.isEmpty()) {
        auto actor = MessagePack::deserializeQt<Actor<KeyPublic>>(serializedActor);
        if ((actor.type() == ActorType::Account || actor.type() == ActorType::ServiceProvider)
            && actor.profile().sign.isEmpty()) {
            sendGetActorMessage(id);
        }

        return actor;
    } else {
        sendGetActorMessage(id);
        // emit sendMessage(msg.serialize(), getActorMessage);
        // resolveManager->network()->send_message()
        qDebug() << "[ActorIndex] There no actor with id:" << id;
        return Actor<KeyPublic>();
    }
}

bool ActorIndex::hasActor(const ActorId &id) {
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    return QFileInfo(filePath).size() > 0;
}

void ActorIndex::removeActor(const ActorId &id, bool resend) {
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    QFile::remove(filePath);
    QFile::remove(filePath + "/profile/" + id.toByteArray() + ".profile");

    if (resend) {
        sendGetActorMessage(id);
    }
}

bool ActorIndex::validateBlock(const Block &block) {
    Actor<KeyPublic> actor = this->getActor(block.getApprover());
    if (actor.empty()) {
        qWarning() << "Can not validate block" << block.getIndex() << ": There no actor"
                   << block.getApprover() << " in local storage";
        return false;
    }
    return block.verify(actor);
}

bool ActorIndex::validateTx(const Transaction &tx) {
    Actor<KeyPublic> actor = this->getActor(tx.getApprover());
    if (actor.empty()) {
        qWarning() << "Can not validate tx" << tx.getHash() << ": There no actor" << tx.getApprover()
                   << " in local storage";
        return false;
    }
    return tx.verify(actor);
}

void ActorIndex::process() {
}

void ActorIndex::handleGetActor(const ActorId &actorId, const std::string &messageId) {
    // receive id
    // create response message
    if (actorId.isEmpty())
        qFatal("handleGetActor: empty actor");
    Actor<KeyPublic> actor = getActor(actorId);
    if (!actor.empty()) {
        // emit responseReady(actor.serialize(), Messages::GET_ACTOR_RESPONSE_MESSAGE, reqHash, receiver);
        auto profileData = actor.profile().serialize();
        bool isProfile = !profileData.isEmpty();

        // TODONEW: Send GetActorResponse
        node->network()->send_message(actor, MessageType::Actor, MessageStatus::Response, messageId);

        if (isProfile) {
            // TODONEW node->resolveManager()->registrateMsg(profileData, Messages::ChainMessage::ProfileMessage);
        } else if (actor.type() != ActorType::User
                   && actor.type() != ActorType::ServiceProvider) { // if profile not exist
            static QMap<QByteArray, qint64> tempCheck;
            qDebug() << "[ActorIndex] No profile for actor" << actorId;

            auto current = QDateTime::currentSecsSinceEpoch();
            auto actorIdBytes = actorId.toByteArray();
            if (tempCheck[actorIdBytes] < current - 10) {
                qDebug() << "[ActorIndex] Send get actor if no profile:" << actorId;
                tempCheck[actorIdBytes] = current;
                sendGetActorMessage(actorId);
            }
        }

        // emit sendMessage(actor.profile().serialize(), Messages::PROFILE_FILE);
    } else {
        sendGetActorMessage(actorId);
    }
}

void ActorIndex::handleGetAllActor(const std::string &messageId) {
    if (node->accountController()->getAccountCount() == 0)
        return;

    std::vector<std::string> result = allActorsStd();
    if (!result.empty()) {
        node->network()->send_message(result, MessageType::ActorAll, MessageStatus::Response, messageId);
    }
    return;
}

void ActorIndex::getAllActors(ActorId id, bool isUser) {
    Q_UNUSED(isUser)

    if (node->accountController()->getAccountCount() > 0) {
        node->network()->send_message(id, MessageType::ActorAll, MessageStatus::Request, "",
                                      Config::Net::TypeSend::All);

        qDebug() << "[ActorIndex] Get all actors request";
        // emit sendMessage(msg.serialize(), getAllActorMessage);
    }
}

void ActorIndex::handleNewActor(Actor<KeyPublic> actor) {
    switch (addActor(actor)) {
    case 0:
        qDebug() << "[ActorIndex] New actor" << actor << "is successfully saved";

        // TODO: remove me?
        if ((actor.type() == ActorType::Account || actor.type() == ActorType::ServiceProvider)
            && profilesHandle.contains(actor.id().toByteArray())) {
            saveProfileFromNetwork(profilesHandle[actor.id().toByteArray()]);
        }
        break;
    case Errors::FILE_ALREADY_EXISTS:
        qDebug() << "[ActorIndex] New actor" << actor << "can't be added: it is already in storage";
        break;
    case Errors::FILE_IS_NOT_OPENED:
        qWarning() << "[ActorIndex] Error: new actor" << actor << "is not saved";
        break;
    default:
        qWarning() << "[ActorIndex] Error: unexpected return type";
    }
}

void ActorIndex::handleNewAllActors(QByteArrayList actors) {
    for (const QByteArray &actor : actors)
        getActor(actor.toStdString());
}

void ActorIndex::getActorCount(const QByteArray &requestHash, const std::string &messageId) {
    qDebug() << "[ActorIndex] Get actor count response:" << this->getRecords();

    node->network()->send_message(std::to_string(this->getRecords()), MessageType::ActorCount,
                                  MessageStatus::Response, messageId);
}

void ActorIndex::saveProfileFromNetwork(const QByteArray &newProfile) {
    PublicProfile profile(newProfile);
    if (profile.sign == "" || newProfile.isEmpty())
        return;
    Actor<KeyPublic> actor = getActor(profile.id.toStdString());
    if (actor.empty()) {
        qDebug() << "[ActorIndex] We don't have actor for profile" << profile.id;
        profilesHandle[profile.id] = newProfile;
        return;
    }

    QByteArray profileData = PublicProfile::getProfileDataFromNetwork(newProfile);
    if (profileData.isEmpty()) {
        return;
    }

    if (actor.key().verify(profileData, profile.sign)) {
        qDebug() << "[ActorIndex] Save public profile with id:" << profile.id;
        bool isSaved = actor.profile().saveProfileFromNet(profile.dataToProfile);

        if (isSaved) {
            if (profile.serialize().isEmpty()) {
                return;
            }

            // TODONEW node->resolveManager()->registrateMsg(profile.serialize(),
            //                                       Messages::ChainMessage::ProfileMessage);
            emit profileAvailabled(profile.id, actor.profile().getListProfile());
        }
    } else
        qDebug() << "[ActorIndex] Save profile from network: incorrect profile verify" << profile.id;
}

void ActorIndex::saveProfile(const Actor<KeyPrivate> &actor, QByteArrayList newProfile) {
    if (actor.empty())
        return;

    qDebug() << "[ActorIndex] Save public profile with id" << newProfile.at(2);
    QByteArray path = buildPathPubProfile(ActorId(newProfile.at(2).toStdString()).toByteArray()).toUtf8();
    QByteArray sign = actor.key().sign(PublicProfile::serialize(newProfile));
    PublicProfile pubProfile(newProfile, sign, path, newProfile.at(2));

    if (pubProfile.sign == "") {
        qDebug() << "[ActorIndex] Save profile: incorrect profile" << newProfile.at(2);
        return;
    } else {
        // TODONEW node->resolveManager()->registrateMsg(pubProfile.serialize(),
        // Messages::ChainMessage::ProfileMessage); emit sendMessage(pubProfile.serialize(), profileType);
    }
}

void ActorIndex::requestProfile(QString id) {
    Actor<KeyPublic> actor = getActor(id.toStdString());
    if (actor.empty())
        return;
    if (actor.profile().getProfile() == "")
        return;
    // if (actor.key().verify(actor.profile().getProfile(), actor.profile().sign))

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

QByteArrayList ActorIndex::getProfile(QString id) {
    Actor<KeyPublic> actor = getActor(id.toStdString());
    PublicProfile pProfile = actor.profile();
    QByteArrayList pList = pProfile.getListProfile();
    if (pProfile.sign == "" || pList.isEmpty()) {
        if (actor.type() != ActorType::User && actor.type() != ActorType::ServiceProvider
            && node->resolveManager() != nullptr) {
            sendGetActorMessage(id.toStdString());
        }

        return QByteArrayList();
    }

    // if (actor.key().verify(key.profile().getProfile(), pProfile.sign))
    return pList;
    // else
    // {
    //     qDebug() << "getProfile: incorrect profile" << id;
    //     return QByteArrayList();
    // }
}

bool ActorIndex::actorExist(const ActorId &actorId) {
    return !getById(actorId).isEmpty();
}

QString ActorIndex::getFolderPath() const {
    return folderPath;
}

QString ActorIndex::buildFilePath(const QByteArray &id) const {
    QByteArray Id = ActorId(id.toStdString()).toByteArray();

    QByteArray section = Id.right(SECTION_NAME_SIZE);
    QString pathToFolder = folderPath + section;

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        qDebug() << "[ActorIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + "/" + Id;
}

QString ActorIndex::buildPathPubProfile(const QByteArray &id) {
    QString pathToFolder = DfsStruct::ROOT_FOOLDER_NAME + "/" + id + "/profile/";

    QDir dir(pathToFolder);
    if (!dir.exists()) {
        qDebug() << "[ActorIndex] Creating dir:" << pathToFolder;
        dir = QDir();
        dir.mkpath(pathToFolder);
    }

    return pathToFolder + id + ".profile";
}

void ActorIndex::setFirstId(const ActorId &value) {
    if (!m_firstId.isEmpty()) {
        if (firstId() != value)
            qFatal("Another FirstId");
        return;
    }

    qDebug() << "[ActorIndex] Save first id:" << value;
    m_firstId = value;
}

qint64 ActorIndex::getRecords() const {
    return records;
}

int ActorIndex::add(const ActorId &id, const QByteArray &data) {
    // if (id <= 1000)
    //     qFatal("Try to add actor with id %s", id.toByteArray().constData());

    QString path = buildFilePath(id.toByteArray());
    QFile file(path);
    qDebug() << "[ActorIndex] Saving the file:" << path;
    // QString profilePath = buildPathPubProfile(id.toActorId());
    if (file.exists()) {
        qDebug() << "[ActorIndex] Can't save the file" << path << "(file already exits)";
        return Errors::FILE_ALREADY_EXISTS;
    }

    if (file.open(QIODevice::WriteOnly)) {
        file.write(data);
        file.flush();
        file.close();

        return 0;
    }

    qDebug() << "[ActorIndex] Can't save the file" << path << "(file is not opened)";
    return Errors::FILE_IS_NOT_OPENED;
}

void ActorIndex::sendGetActorMessage(const ActorId &actorId) {
    if (actorId.isEmpty()) {
        qFatal("Can't get actor by empty id");
    }

    node->network()->send_message(actorId.toStdString(), MessageType::Actor, MessageStatus::Request);
}

QByteArray ActorIndex::getById(const ActorId &id) const {
    QString filePath = folderPath + id.toByteArray().right(SECTION_NAME_SIZE) + '/' + id.toByteArray();
    QFile file(filePath);
    if (!file.exists()) {
        qDebug() << "[ActorIndex] File with path" << filePath << "not found";
        return QByteArray();
    }
    file.open(QIODevice::ReadOnly);
    QByteArray data = file.readAll();
    file.close();
    return data;
}

int ActorIndex::addActor(const Actor<KeyPublic> &actor) {
    int result = this->add(actor.id(), actor.serialize());
    auto actorId = actor.id().toStdString();

    if (result != Errors::FILE_ALREADY_EXISTS && result != Errors::FILE_IS_NOT_OPENED) {
        this->records++;
        DBConnector db;
        db.open(folderPath.toStdString() + "actors");
        bool dbInsert = db.insert(Config::DataStorage::actorsTable,
                                  { { "id", actorId }, { "type", std::to_string(int(actor.type())) } });
        if (!dbInsert)
            qFatal("db actor insert error");

        qDebug() << "[ActorIndex] Actor" << actor.id() << "was added";
        node->network()->send_message(actor, MessageType::Actor, MessageStatus::Response, "",
                                      Config::Net::TypeSend::All);
        emit initDfs(actor.id());
    }

    return result;
}

QByteArrayList ActorIndex::allActors() {
    QByteArrayList result;

    DBConnector db;
    db.open(folderPath.toStdString() + "actors");
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors) {
        result << actor["id"].data();
    }

    return result;
}

std::vector<std::string> ActorIndex::allActorsStd() {
    std::vector<std::string> result;

    DBConnector db;
    db.open(folderPath.toStdString() + "actors");
    auto actors = db.select("SELECT id FROM Actors");
    for (auto &actor : actors) {
        result.push_back(actor["id"]);
    }

    return result;
}

void ActorIndex::removeAll() {
    qDebug() << "[ActorIndex] Clearing file index:" << folderPath;

    QDir folder(folderPath);
    const auto folders =
        folder.entryList(QDir::Filter::AllEntries | QDir::Filter::NoDotAndDotDot, QDir::SortFlag::Name);
    for (const QString &section : qAsConst(folders)) {
        QDir dir(folderPath + QString("/") + section);
        dir.removeRecursively();
    }

    // update state
    this->records = 0;
}
