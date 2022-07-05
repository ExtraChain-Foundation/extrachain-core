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

#include "managers/extrachain_node.h"

#include <QJsonObject>
#include <sodium/core.h>

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/dfs/dfs_controller.h"
#include "datastorage/dfs/permission_manager.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "enc/enc_tools.h"
#include "managers/account_controller.h"
#include "managers/thread_pool.h"
#include "managers/tx_manager.h"
#include "network/network_manager.h"

ExtraChainNode::ExtraChainNode() {
    static bool singleton = false;
    if (!singleton)
        singleton = true;
    else
        qFatal("Two instances of Node");

    if (sodium_init() != 0) {
        qDebug() << "Encryption init error";
        QCoreApplication::exit(-1);
    }

    prepareFolders();
    m_actorIndex = new ActorIndex(*this);
    m_accountController = new AccountController(*this);

    m_networkManager = new NetworkManager(*this);
    //    ThreadPool::addThread(m_networkManager);

    m_blockchain = new Blockchain(this, fileMode);
    m_txManager = new TransactionManager(m_accountController, m_blockchain, this);
    m_dfs = new DfsController(*this);

    m_blockchain->setTxManager(m_txManager);
    connectSignals();

    static QTimer getAllActorsTimer;
    connect(&getAllActorsTimer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    getAllActorsTimer.start(30000);

    // ThreadPool::addThread(m_blockchain);
    // ThreadPool::addThread(m_txManager);
}

ExtraChainNode::~ExtraChainNode() {
    emit m_networkManager->finished();
    delete m_dfs;
    delete m_actorIndex;
    delete m_txManager;
    delete m_blockchain;
    delete m_accountController;
}

bool ExtraChainNode::createNewNetwork(const QString &email, const QString &password, const QString &tokenName,
                                      const QString &tokenCount, const QString &tokenColor) {
    // TODO: check correct color in tokenColor

    if (QDir("keystore/profile").isEmpty()) {
        qDebug() << "[Node] Create network with e-mail" << email;
        auto consoleHash = Utils::calcHash(email.toStdString() + password.toStdString());
        auto first = m_accountController->createProfile(consoleHash, ActorType::ServiceProvider);
        m_actorIndex->setFirstId(first.id());
    } else {
        qInfo() << "You cannot create a new network, data is not empty";
        return false;
    }

    if (m_blockchain->getRecords() <= 0) {
        auto &first = m_accountController->mainActor();
        // QString firstId = first.id().toString();

        QMap<ActorId, BigNumber> tm;
        tm.insert(ActorId(), 0);
        GenesisBlock tmp = m_blockchain->createGenesisBlock(first, tm);
        m_blockchain->addBlock(tmp, true);

        // emit generateSmartContract(tokenCount.toLatin1(), tokenName.toUtf8(), first.id().toByteArray(),
        //                            tokenColor.toLatin1());

        //        // TODO: usernames: move to console
        //        DBConnector dbc(
        //            (DfsStruct::ROOT_FOOLDER_NAME + "/" + firstId + "/" +
        //            DfsStruct::ACTOR_CARD_FILE).toStdString());
        //        dbc.open();
        //        dbc.createTable(Config::DataStorage::cardTableCreation);
        //        dbc.createTable(Config::DataStorage::cardDeletedTableCreation);
        //        QString usernamesPath = QString(DfsStruct::ROOT_FOOLDER_NAME +
        //        "/%1/services/usernames").arg(firstId); DBConnector
        //        usernamesDB(usernamesPath.toStdString());
        //        usernamesDB.createTable(Config::DataStorage::userNameTableCreation);
        //        // m_dfs->save(DfsStruct::DfsSave::Static, "usernames", "", DfsStruct::Type::Service);
    }

    return true;
}

void ExtraChainNode::start() {
    if (!started) {
        QTimer::singleShot(500, this, &ExtraChainNode::ready);
        // emit startNetwork();
        started = true;
    }
}

void ExtraChainNode::showMessage(QString from, QString message) {
    qDebug() << from << " " << message;
}

// void ExtraChainNode::connectResolveManager() {
//    connect(networkManager, &NetworkManager::MsgReceived, resolveManager,
//    &ResolveManager::resolveMessage); connect(resolveManager, &ResolveManager::coinRequest, this,
//    &ExtraChainNode::coinResponse); connect(dfs->networkManager(), &DfsNetworkManager::newMessage,
//    resolveManager,
//            &ResolveManager::resolveMessage);
// TODO: move
//    connect(resolveManager, &ResolveManager::sendMsg, m_networkManager, &networkManager::sendMessage);

// connect(this, &ExtraChainNode::sendMsg, m_resolveManager, &ResolveManager::registrateMsg);
// connect(m_txManager, &TransactionManager::SendBlock, m_resolveManager, &ResolveManager::registrateMsg);
// connect(m_blockchain, &Blockchain::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
//    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
// }

void ExtraChainNode::connectTxManager() {
    // TODOD delete later (s)
    connect(this, &ExtraChainNode::NewTx, m_txManager, &TransactionManager::addTransaction);
}

// DFSIndex *ExtraChainNode::getDFSIndex(){
//    return dfsIndex;
//}

Blockchain *ExtraChainNode::blockchain() {
    return m_blockchain;
}

NetworkManager *ExtraChainNode::network() {
    return m_networkManager;
}

Transaction ExtraChainNode::createTransaction(Transaction tx) {
    if (tx.isEmpty()) {
        qDebug() << QString("Warning: can not create tx:[%1]. Transaction is empty").arg(tx.toString());
        return Transaction();
    }

    Actor<KeyPrivate> actor = m_accountController->currentWallet();
    if (!actor.empty()) {
        qDebug() << QString("Attempting to create tx:[%1] from user [%2]")
                        .arg(tx.toString(), QString(actor.id().toByteArray()));

        // 1) set prev block id
        BigNumber lastBlockId = m_blockchain->getLastBlock().getIndex();
        if (lastBlockId.isEmpty()) {
            qDebug() << QString("Warning: can not create tx:[%1]. There no last block in "
                                "blockchain")
                            .arg(tx.toString());
            return Transaction();
        }
        tx.setPrevBlock(lastBlockId);

        // 2) sign transaction

        tx.sign(actor);
        qDebug() << "send tx" << Transaction::amountToVisible(tx.getAmount()) << "to" << tx.getReceiver();

        // send without fee
        if (tx.getSender().isEmpty() || tx.getSender() == m_actorIndex->firstId()
            || tx.getReceiver().isEmpty() || tx.getReceiver() == m_actorIndex->firstId())
            emit NewTx(tx);
        else if (tx.getData() == Fee::FREEZE_TX || tx.getData() == Fee::UNFREEZE_TX) {
            // TODONEW emit sendMsg(tx.serialize(), Messages::ChainMessage::TxMessage);
        } else {
            BigNumber amountTemp(tx.getAmount());
            if (m_blockchain->getUserBalance(tx.getSender(), tx.getToken()) - amountTemp - amountTemp / 100
                >= 0) {
                // send with fee

                Transaction txFee = tx;
                // restructure tx for fee
                {
                    amountTemp /= 100;
                    txFee.setAmount(amountTemp);
                    txFee.setReceiver(actor.id()); // send fee to my freezeFee
                    // ENUM | Tx hash that fee refer
                    txFee.setData(Serialization::serialize({ tx.getHash(), Fee::FEE_FREEZE_TX }));
                    txFee.sign(actor);
                }

                // send fee tx
                // TODONEW emit sendMsg(txFee.serialize(), Messages::ChainMessage::TxMessage); // send fee
                // TODONEW emit sendMsg(tx.serialize(), Messages::ChainMessage::TxMessage);
            } else {
                qDebug() << "Not enough money ";
                return Transaction();
            }
        }

        return tx;
    } else

        qDebug() << QString("Warning: can not create tx:[%1]. There no current user").arg(tx.toString());

    return Transaction();
}

Transaction ExtraChainNode::createTransaction(ActorId receiver, BigNumber amount, ActorId token) {
    if (receiver.isEmpty() || amount.isEmpty()) {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = m_accountController->currentWallet();
    if (!actor.empty()) {
        qDebug() << actor.id();
        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));

        return this->createTransaction(tx);
    }
    qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                    .arg(QString(receiver.toByteArray()));
    return Transaction();
}

Transaction ExtraChainNode::createFreezeTransaction(ActorId receiver, BigNumber amount, bool toFreeze,
                                                    ActorId token) {
    Actor<KeyPrivate> actor = m_accountController->currentWallet();

    if (!actor.empty()) {
        if (receiver.isEmpty()) {
            qDebug() << "Create freeze tx to me";
            receiver = actor.id();
        } else
            qDebug() << "Create freeze tx to" << receiver;

        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances
        tx.setData(toFreeze ? Fee::FREEZE_TX : Fee::UNFREEZE_TX);
        tx.setToken(token);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));

        return this->createTransaction(tx);
    }
    qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                    .arg(QString(receiver.toByteArray()));
    return Transaction();
}

std::string ExtraChainNode::exportUser() {
    auto hash = m_accountController->currentProfile().hash();

    QJsonArray array;
    array << QString("ExtraChain %1").arg(qApp->applicationVersion()); // 0
    array << QDateTime::currentSecsSinceEpoch();                       // 1

    auto privateProfile = m_accountController->currentProfile().toJson();
    array << privateProfile; // 3

    auto json = QJsonDocument(array).toJson(QJsonDocument::Compact).toStdString();
    auto data = SecretKey::encryptWithPassword(json, hash);
    return data;
}

bool ExtraChainNode::importUser(const std::string &data, const std::string &login,
                                const std::string &password) {
    auto hash = Utils::calcHash(login + password);

    auto json = SecretKey::decryptWithPassword(data, hash);
    if (hash.empty() || json.empty()) {
        return false;
    }

    auto array = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    if (array.count() != 3) {
        return false;
    }

    auto extrachainVersion = array[0].toString();
    auto date = array[1].toInteger();
    auto profile = array[2].toObject();
    auto profileBytes = QJsonDocument(profile).toJson(QJsonDocument::Compact);
    auto profileBytesEncrypted =
        QByteArray::fromStdString(SecretKey::encryptWithPassword(profileBytes.toStdString(), hash));

    Q_UNUSED(extrachainVersion)
    Q_UNUSED(date)

    QString privateProfile = "keystore/" + profile["main"].toString() + ".profile";

    QFile file(privateProfile);
    file.open(QFile::WriteOnly);
    file.write(profileBytesEncrypted);
    file.close();

    m_accountController->addToProfileList(profile["main"].toString().toStdString());

    return true;
}

Transaction ExtraChainNode::createTransactionFrom(ActorId sender, ActorId receiver, BigNumber amount,
                                                  ActorId token) {
    if (receiver.isEmpty() || amount.isEmpty()) {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = m_accountController->currentProfile().getActor(sender);
    if (!actor.empty()) {
        qDebug() << actor.id();
        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        // tx.setHop(2);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    } else {
        qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                        .arg(QString(receiver.toByteArray()));
    }
    return Transaction();
}

void ExtraChainNode::getAllActorsTimerCall() {
    if (m_accountController->count() > 0 && m_networkManager->connections().length() > 0) {
        ActorId actorId = m_accountController->mainActor().id();

        if (!actorId.isEmpty())
            m_actorIndex->getAllActors(actorId, true);
    }
}

void ExtraChainNode::createNetworkIdentifier() {
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(Utils::calcHash(BigNumber::random(64).toByteArray()));
    file.flush();
    file.close();
}

void ExtraChainNode::notificationToken(QString os, QString actorId, QString token) {
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto firstId = m_actorIndex->firstId();
    if (firstId.isEmpty())
        return;
    auto first = m_actorIndex->getActor(firstId);
    if (first.empty())
        return;
    auto &mainKey = m_accountController->mainActor().key();
    auto &publicKey = first.key().publicKey();

    // std::map<std::string, std::string> map = { { "actor", actorId.toStdString() },
    //                                            { "token", mainKey.encrypt(token.toStdString(), publicKey)
    //                                            }, { "os", mainKey.encrypt(os.toStdString(), publicKey) } };

    // TODONEW emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}

void ExtraChainNode::connectContractManager() {
}

void ExtraChainNode::connectActorIndex() {
    // connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection() {
    // init dfs for user
    connect(m_networkManager, &NetworkManager::addFragSignal, m_dfs, &DfsController::threadAddFragment);
    connect(m_networkManager, &NetworkManager::fetchFragments, m_dfs, &DfsController::fetchFragments);
    connect(this, &ExtraChainNode::ready, m_networkManager, &NetworkManager::startNetwork);
    // connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    // connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    // connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals() {
    connect(this, &ExtraChainNode::ready, []() { qInfo() << "Node: started"; });
    connectTxManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    dfsConnection();

    connect(m_networkManager, &NetworkManager::newSocket, this, &ExtraChainNode::getAllActorsTimerCall);

    // temp for tests, maybe only for console
    connect(m_networkManager, &NetworkManager::newSocket, m_blockchain, &Blockchain::updateBlockchain);
    connect(m_networkManager, &NetworkManager::newSocket, [this]() { m_dfs->requestSync(); });
    // connect(m_accountController, &AccountController::loadWallets, m_blockchain,
    //         &Blockchain::updateBlockchain);
}

void ExtraChainNode::prepareFolders() {
    qDebug() << "Preparing folders";
    qDebug() << "Working directory:" << QDir::currentPath();

    QDir().mkpath(QString::fromStdString(KeyStore::folder));
    QDir().mkpath(DataStorage::TMP_FOLDER);
    QDir().mkpath(DataStorage::BLOCKCHAIN_INDEX + "/" + DataStorage::ACTOR_INDEX_FOLDER_NAME);
    QDir().mkpath(DataStorage::BLOCKCHAIN_INDEX + "/" + DataStorage::BLOCK_INDEX_FOLDER_NAME);

    if (!QFile(".settings").exists())
        createNetworkIdentifier();
}

AccountController *ExtraChainNode::accountController() const {
    return m_accountController;
}

ActorIndex *ExtraChainNode::actorIndex() const {
    return m_actorIndex;
}

DfsController *ExtraChainNode::dfs() const {
    return m_dfs;
}

bool ExtraChainNode::login(const std::string &login, const std::string &password) {
    return m_accountController->load(Utils::calcHash(login + password));
}

bool ExtraChainNode::login(const std::string &hash) {
    return m_accountController->load(hash);
}

void ExtraChainNode::logout() {
    m_accountController->clear();
    // auto hash remove
    std::exit(0);
}

void ExtraChainNode::testPermissions() const {
    /*
    // Mock actor create
    const std::string userEmail = "test@test.com";
    const std::string userPass = "12345678";
    const QByteArray userHash = QByteArray::fromStdString(userEmail + userPass); //
    Utils::calcHash(userEmail.toUtf8() + userPass.toUtf8()); auto actor =
    m_accountController->createActor(ActorType::Account, userHash);

    // Mock actor create
    const std::string userEmail1 = "test@test.com";
    const std::string userPass1 = "12345678";
    const QByteArray userHash1 = QByteArray::fromStdString(userEmail + userPass); //
    Utils::calcHash(userEmail.toUtf8() + userPass.toUtf8()); auto actor1 =
    m_accountController->createActor(ActorType::Account, userHash1);

    DFSController dfsController;
    dfsController.initDB(actor);

    QStringList testFiles = {
        FileSystem::pathConcat(QDir::homePath(), "test-file-1.txt"),
        FileSystem::pathConcat(QDir::homePath(), "test-file-2.txt")
    };

    auto orgFilePublic = QFile(testFiles[0]);
    orgFilePublic.open(QIODevice::ReadOnly);
    auto orgFilePrivate = QFile(testFiles[1]);
    orgFilePrivate.open(QIODevice::ReadOnly);

    QByteArray fHashPublic = dfsController.addFile(actor, testFiles[0], DFSController::Public);
    QByteArray fHashPrivate = dfsController.addFile(actor, testFiles[1], DFSController::Private);

    PermissionManager permManager;
    permManager.initPermissionDB(actor);

    struct TestSet {
        TestSet(){}
        TestSet(QString cmd, Actor<KeyPrivate> actor, QString userId, QString fileHash) :
            cmd(cmd),
            actor(actor),
            userId(userId),
            fileHash(fileHash) {}
        QString cmd;
        Actor<KeyPrivate> actor;
        QString userId;
        QString fileHash;
    };

    struct SetPermission : public TestSet{
        SetPermission(QString cmd, Actor<KeyPrivate> actor, QString userId, QString fileHash,
    PermissionManager::Permission permission, bool result) : TestSet(cmd, actor, userId, fileHash),
            permission(permission),
            resultSet(result) {}
        PermissionManager::Permission permission;
        bool resultSet;
    };

    struct GetPermission : public TestSet{
        GetPermission(QString cmd, Actor<KeyPrivate> actor, QString userId, QString fileHash,
    PermissionManager::Permission permission) : TestSet(cmd, actor, userId, fileHash), resultGet(permission)
    {} PermissionManager::Permission resultGet;
    };

    std::vector<TestSet*> testSet;
    testSet.emplace_back(new GetPermission("get", actor, actor1.idStd().c_str(), ".perm",
    PermissionManager::Read)); testSet.emplace_back(new GetPermission("get", actor, actor.idStd().c_str(),
    ".perm", PermissionManager::Edit)); testSet.emplace_back(new GetPermission("get", actor,
    actor1.idStd().c_str(), "fHashPublic", PermissionManager::NoPermission)); testSet.emplace_back(new
    GetPermission("get", actor, actor.idStd().c_str(), "fHashPrivate", PermissionManager::NoPermission));

    testSet.emplace_back(new SetPermission("set", actor1, actor1.idStd().c_str(), "fHashPublic",
    PermissionManager::Edit, false)); testSet.emplace_back(new SetPermission("set", actor,
    actor1.idStd().c_str(), ".perm", PermissionManager::Edit, true)); testSet.emplace_back(new
    SetPermission("set", actor1, actor.idStd().c_str(), "fHashPrivate", PermissionManager::Edit, true));

    testSet.emplace_back(new GetPermission("get", actor1, actor.idStd().c_str(), "fHashPrivate",
    PermissionManager::Edit)); testSet.emplace_back(new GetPermission("get", actor1, actor1.idStd().c_str(),
    ".perm", PermissionManager::Edit));

    for(auto & test: testSet)
    {
        if(test->cmd == "get")
        {
            GetPermission* getPerm = static_cast<GetPermission*>(test);
            auto permission = permManager.getPermission(getPerm->actor,
                                                        {getPerm->userId.toStdString(),
                                                         getPerm->fileHash.toStdString()});
            assert(permission == getPerm->resultGet);
        }
        else
        {
            SetPermission* setPerm = static_cast<SetPermission*>(test);
            auto permission = permManager.setPermission(setPerm->actor,
                                                        {setPerm->userId.toStdString(),
                                                         setPerm->fileHash.toStdString(),
                                                         permManager.permissions[setPerm->permission].toStdString()});
            assert(permission == setPerm->resultSet);
        }
    }

    for(auto & ptr: testSet)
    {
        delete ptr;
    }
    */
}

void ExtraChainNode::test() const {
    /*
    // Mock actor create
    const std::string userEmail = "test@test.com";
    const std::string userPass = "12345678";
    const QByteArray userHash = QByteArray::fromStdString(userEmail + userPass); //
    Utils::calcHash(userEmail.toUtf8() + userPass.toUtf8()); auto actor =
    m_accountController->createActor(ActorType::ServiceProvider, userHash);

    DFSController dfsController;
    dfsController.initDB(actor);
    dfsController.flushDirContent(actor.idStd().c_str());

    QStringList testFiles = {
        FileSystem::pathConcat(QDir::homePath(), "test-file-1.txt"),
        FileSystem::pathConcat(QDir::homePath(), "test-file-2.txt")
    };

    auto orgFilePublic = QFile(testFiles[0]);
    orgFilePublic.open(QIODevice::ReadOnly);

    auto orgFilePrivate = QFile(testFiles[1]);
    orgFilePrivate.open(QIODevice::ReadOnly);


    QByteArray fHashPublic = dfsController.addFile(actor, testFiles[0], DFSController::Public);
    QByteArray fHashPrivate = dfsController.addFile(actor, testFiles[1], DFSController::Private);
    if (!fHashPublic.isEmpty() || !fHashPrivate.isEmpty())
        qDebug() << "addFile succeeded";
    else
        qDebug() << "addFile failed";

    auto validate = [&](const QString & publicCompare, const QString & privateCompare){
        auto fileContentPublic = dfsController.readFile(actor, fHashPublic);
        if(fileContentPublic == publicCompare)
            qDebug() << "Files are equal";
        else
            qDebug() << "Files are different '" << fileContentPublic << "' != '" << publicCompare << "'";

        auto fileContentPrivate = dfsController.readFile(actor, fHashPrivate);
        if(fileContentPrivate == privateCompare)
            qDebug() << "Files are equal";
        else
            qDebug() << "Files are different '" << fileContentPrivate << "' != '" << privateCompare << "'";
    };

    validate(orgFilePublic.readAll(), orgFilePrivate.readAll());

    DFSController::AddFileMsg addFileMsg;
    addFileMsg.userId = actor.idStd();
    addFileMsg.fileHash = "test_file_hash";
    addFileMsg.path = "dfs/public/test_file_name";
    addFileMsg.size = "123";

    dfsController.addFile(actor, addFileMsg);

    addFileMsg.fileHash = "test_file_hash_private";
    addFileMsg.path = "dfs/private/test_file_name_private";
    addFileMsg.size = "321";

    dfsController.addFile(actor, addFileMsg);
    QByteArray newContent = "Completely new content!";

    DFSController::EditFileMsg editFileMsg;
    editFileMsg.userId = actor.idStd();
    editFileMsg.fileHash = fHashPublic;
    editFileMsg.data = newContent;
    editFileMsg.offset = "0";
    fHashPublic = dfsController.insertFragment(actor, editFileMsg);

    editFileMsg.fileHash = fHashPrivate;
    fHashPrivate = dfsController.insertFragment(actor, editFileMsg);
    if (!fHashPublic.isEmpty() || !fHashPrivate.isEmpty())
        qDebug() << "editFile succeeded";
    else
        qDebug() << "editFile failed";

    validate(newContent, newContent);

    // Add segment tests
    newContent.insert(0, "qwe");

    DFSController::AddSegmentMsg addSegmentMsg;
    addSegmentMsg.userId = actor.idStd();
    addSegmentMsg.fileHash = fHashPublic;
    addSegmentMsg.data = "qwe";
    addSegmentMsg.offset = "0";
    fHashPublic = dfsController.addFileSegment(actor, addSegmentMsg);

    addSegmentMsg.fileHash = fHashPrivate;
    fHashPrivate = dfsController.addFileSegment(actor, addSegmentMsg);

    qDebug() << "New value: " << newContent;
    validate(newContent, newContent);

    //

    newContent.insert(10, "qwe");

    addSegmentMsg.offset = "10";
    addSegmentMsg.fileHash = fHashPublic;
    fHashPublic = dfsController.addFileSegment(actor, addSegmentMsg);

    addSegmentMsg.fileHash = fHashPrivate;
    fHashPrivate = dfsController.addFileSegment(actor, addSegmentMsg);

    qDebug() << "New value: " << newContent;
    validate(newContent, newContent);

    //

    addSegmentMsg.offset = std::to_string(newContent.size());
    addSegmentMsg.fileHash = fHashPublic;
    fHashPublic = dfsController.addFileSegment(actor, addSegmentMsg);

    addSegmentMsg.fileHash = fHashPrivate;
    fHashPrivate = dfsController.addFileSegment(actor, addSegmentMsg);

    newContent.insert(newContent.size(), "qwe");
    qDebug() << "New value: " << newContent;
    validate(newContent, newContent);

    // Delete segment tests

    newContent = newContent.toStdString().erase(0, 10).c_str();

    DFSController::DeleteSegmentMsg delSegmentMsg;
    delSegmentMsg.userId = actor.idStd();
    delSegmentMsg.fileHash = fHashPublic;
    delSegmentMsg.offset = "0";
    delSegmentMsg.size = "10";

    fHashPublic = dfsController.deleteFileSegment(actor, delSegmentMsg);

    delSegmentMsg.fileHash = fHashPrivate;
    fHashPrivate = dfsController.deleteFileSegment(actor, delSegmentMsg);
    qDebug() << "New value: " << newContent;
    validate(newContent, newContent);

    DFSController::RemoveFileMsg removeFileMsg;
    removeFileMsg.userId = actor.idStd().c_str();
    removeFileMsg.fileHash = fHashPublic;

    bool result = dfsController.removeFile(actor, removeFileMsg);
    qDebug() << "Remove file:" << fHashPublic << ", status:" << result;

    removeFileMsg.fileHash = fHashPrivate;
    result = dfsController.removeFile(actor, removeFileMsg);
    qDebug() << "Remove file:" << fHashPrivate << ", status:" << result;
    */
}
