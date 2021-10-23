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

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "dfs/controls/headers/dfs.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include "dfs/managers/headers/dfs_networkmanager.h"
#include "managers/account_controller.h"
#include "managers/chatmanager.h"
#include "managers/contract_manager.h"
#include "managers/file_updater_manager.h"
#include "managers/sm_manager.h"
#include "managers/thread_pool.h"
#include "managers/tx_manager.h"
#include "network/network_manager.h"
#include "network/packages/service/message_types.h"
#include "profile/private_profile.h"
#include "resolve/resolve_manager.h"

#include <sodium.h>

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
    m_actorIndex = new ActorIndex();
    m_privateProfile = new PrivateProfile();
    m_smartContractManager = new SmartContractManager(m_actorIndex);
    m_accountController = new AccountController(m_actorIndex);
    m_networkManager = new NetworkManager(m_actorIndex);
    m_subscribeController = new SubscribeController();
    m_subscribeController->setExtraChainNode(this);
    m_actorIndex->setAccController(m_accountController);
    ThreadPool::addThread(m_networkManager);
    // this->thread()->sleep(1);
    m_blockchain = new Blockchain(m_accountController, fileMode);
    m_accountController->setBlockchain(m_blockchain);
    m_txManager = new TransactionManager(m_accountController, m_blockchain, this);
    m_privateProfile->setAccountController(m_accountController);
    m_chatManager = new ChatManager(m_accountController, m_actorIndex);
    m_chatManager->setNetworkManager(m_networkManager);
    // contractManager = new ContractManager(accController, blockchain);
    m_dfs = new Dfs(m_actorIndex, m_accountController);

    m_resolveManager =
        new ResolveManager(m_actorIndex, m_blockchain, m_networkManager, m_txManager, m_accountController);
    m_resolveManager->setNode(this);
    m_resolveManager->setChatManager(m_chatManager);
    m_blockchain->setTxManager(m_txManager);
    m_networkManager->setResolveManager(m_resolveManager);
    // dfs->initDfsNetwork(resolveManager);
    m_privateProfile->setDfs(m_dfs);
    m_actorIndex->setResolveManager(m_resolveManager);
    connectSignals();

    static QTimer getAllActorsTimer;
    connect(&getAllActorsTimer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    getAllActorsTimer.start(30000);

    ThreadPool::addThread(m_blockchain);
    ThreadPool::addThread(m_actorIndex);
    ThreadPool::addThread(m_txManager);
    // ThreadPool::addThread(contractManager);
    ThreadPool::addThread(m_dfs);
    ThreadPool::addThread(m_smartContractManager);
    ThreadPool::addThread(m_resolveManager);
    ThreadPool::addThread(m_privateProfile);
    ThreadPool::addThread(m_chatManager);

    // QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
    // FileUpdaterManager fl;
    // fl.verifyMyFiles("02c9b394cf3785389f82");
}

bool ExtraChainNode::createNewNetwork(const QString &email, const QString &password, const QString &tokenName,
                                      const QString &tokenCount, const QString &tokenColor) {
    // TODO: check correct color in tokenColor

    if (QDir("keystore/profile").isEmpty()) {
        qDebug() << "[Node] Create network with e-mail" << email << "and password" << password;
        QByteArray consoleHash = Utils::calcKeccak(email.toUtf8() + password.toUtf8());
        auto first = m_accountController->createActor(ActorType::Token, consoleHash);
        emit savePrivateProfile(consoleHash, first.id());
        m_actorIndex->setFirstId(first.id());
    } else {
        qInfo() << "You cannot create a new network, data is not empty";
        return false;
    }

    if (m_blockchain->getRecords() <= 0) {
        auto first = *m_accountController->mainActor();
        QString firstId = first.id().toString();

        QMap<ActorId, BigNumber> tm;
        tm.insert(ActorId(), 0);
        GenesisBlock tmp = m_blockchain->createGenesisBlock(first, tm);
        m_blockchain->addBlock(tmp, true);

        emit generateSmartContract(tokenCount.toLatin1(), tokenName.toUtf8(), first.id().toByteArray(),
                                   tokenColor.toLatin1());

        // TODO: usernames: move to console
        DBConnector dbc(
            (DfsStruct::ROOT_FOOLDER_NAME + "/" + firstId + "/" + DfsStruct::ACTOR_CARD_FILE).toStdString());
        dbc.createTable(Config::DataStorage::cardTableCreation);
        dbc.createTable(Config::DataStorage::cardDeletedTableCreation);
        QString usernamesPath = QString(DfsStruct::ROOT_FOOLDER_NAME + "/%1/services/usernames").arg(firstId);
        DBConnector usernamesDB(usernamesPath.toStdString());
        usernamesDB.createTable(Config::DataStorage::userNameTableCreation);
        m_dfs->save(DfsStruct::DfsSave::Static, "usernames", "", DfsStruct::Type::Service);
    }

    return true;
}

void ExtraChainNode::start() {
    QTimer::singleShot(500, this, &ExtraChainNode::ready);
}

void ExtraChainNode::showMessage(QString from, QString message) {
    qDebug() << from << " " << message;
}

void ExtraChainNode::connectResolveManager() {
    //    connect(networkManager, &NetworkManager::MsgReceived, resolveManager,
    //    &ResolveManager::resolveMessage); connect(resolveManager, &ResolveManager::coinRequest, this,
    //    &ExtraChainNode::coinResponse); connect(dfs->networkManager(), &DfsNetworkManager::newMessage,
    //    resolveManager,
    //            &ResolveManager::resolveMessage);
    // TODO: move
    //    connect(resolveManager, &ResolveManager::sendMsg, m_networkManager, &networkManager::sendMessage);

    connect(this, &ExtraChainNode::sendMsg, m_resolveManager, &ResolveManager::registrateMsg);
    connect(m_txManager, &TransactionManager::SendBlock, m_resolveManager, &ResolveManager::registrateMsg);
    connect(m_blockchain, &Blockchain::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::connectSmContractManager() {
    //    connect(smContractController, &SmartContractManager::verifyActor, m_networkManager,
    //    &networkManager::NewActor); TODO!!!
    //    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
    //            &ExtraChainNode::addActorInActorIndex);
    connect(m_smartContractManager, &SmartContractManager::saveActorInPrivateProfile,
            [this](const QByteArray &id, const QString &type, const bool &rewrite) { // TODO?
                auto mainId = m_accountController->mainActor()->id().toByteArray();
                emit nodeEditPrivateProfile({ m_privateProfile->hash(), mainId }, type, id, rewrite);
            });

    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });
    connect(this, &ExtraChainNode::nodeEditPrivateProfile, m_privateProfile,
            &PrivateProfile::editPrivateProfile);

    connect(this, &ExtraChainNode::generateSmartContract, m_smartContractManager,
            &SmartContractManager::createContractProfile);
    connect(m_smartContractManager, &SmartContractManager::sendTransactionCreateContract, m_resolveManager,
            &ResolveManager::registrateMsg);

    // connect(smContractController, &SmartContractManager::sendCurrentToken, m_networkManager,
    // &networkManager::NewActor);
}

void ExtraChainNode::connectTxManager() {
    // TODOD delete later (s)
    connect(this, &ExtraChainNode::NewTx, m_txManager, &TransactionManager::addTransaction);
}

ExtraChainNode::~ExtraChainNode() {
    emit m_networkManager->finished();
    emit m_txManager->finished();
    emit m_txManager->finished();
    emit m_blockchain->finished();
    emit m_accountController->finished();
    emit m_actorIndex->finished();
}

// DFSIndex *ExtraChainNode::getDFSIndex(){
//    return dfsIndex;
//}

Blockchain *ExtraChainNode::blockchain() {
    return m_blockchain;
}

NetworkManager *ExtraChainNode::networkManager() {
    return m_networkManager;
}

Transaction ExtraChainNode::createTransaction(Transaction tx) {
    if (tx.isEmpty()) {
        qDebug() << QString("Warning: can not create tx:[%1]. Transaction is empty").arg(tx.toString());
        return Transaction();
    }

    Actor<KeyPrivate> actor = m_accountController->getCurrentActor();
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
            emit sendMsg(tx.serialize(), Messages::ChainMessage::TxMessage);
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
                emit sendMsg(txFee.serialize(), Messages::ChainMessage::TxMessage); // send fee
                emit sendMsg(tx.serialize(), Messages::ChainMessage::TxMessage);
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

    Actor<KeyPrivate> actor = m_accountController->getCurrentActor();
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
    Actor<KeyPrivate> actor = m_accountController->getCurrentActor();

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

Transaction ExtraChainNode::createTransactionFrom(ActorId sender, ActorId receiver, BigNumber amount,
                                                  ActorId token) {
    if (receiver.isEmpty() || amount.isEmpty()) {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = m_accountController->getActor(sender);
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

void ExtraChainNode::getAllActors() {
    //    QByteArray res = getIdPrivateProfile();
    //    if (!res.isEmpty())
    //        emit getAllActorsNode(res, true);
}

void ExtraChainNode::getAllActorsTimerCall() {
    if (m_accountController->getAccountCount() > 0 && m_networkManager->connections().length() > 0) {
        ActorId actorId = m_accountController->mainActor()->id();

        if (!actorId.isEmpty())
            emit getAllActorsNode(actorId, true);
    }
}

void ExtraChainNode::createNetworkIdentifier() {
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
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
    auto mainKey = m_accountController->mainActor()->key();
    auto publicKey = first.key()->publicKey();

    QMap<QString, QByteArray> map = { { "actor", actorId.toLatin1() },
                                      { "token", mainKey->encrypt(token.toLatin1(), publicKey) },
                                      { "os", mainKey->encrypt(os.toLatin1(), publicKey) } };

    emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}

void ExtraChainNode::connectContractManager() {
}

void ExtraChainNode::connectActorIndex() {
    connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection() {
    // init dfs for user
    // connect(this, &ExtraChainNode::ready, networkManager, &NetworkManager::startNetwork);
    connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals() {
    connect(this, &ExtraChainNode::ready, []() { qInfo() << "Node: started"; });
    connectTxManager();
    connectResolveManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    connectSmContractManager();
    dfsConnection();

    connect(m_networkManager, &NetworkManager::newSocket, this, &ExtraChainNode::getAllActorsTimerCall);

    // temp for tests, maybe only for console
    connect(m_networkManager, &NetworkManager::newSocket, m_blockchain, &Blockchain::updateBlockchain);

    connect(this, &ExtraChainNode::removeConnection, m_networkManager, &NetworkManager::removeConnection);
    connect(this, &ExtraChainNode::removeConnection, m_dfs, &Dfs::removeConnection);
    connect(this, &ExtraChainNode::getAllActorsNode, m_actorIndex, &ActorIndex::getAllActors);
    connect(m_accountController, &AccountController::loadWallets, m_blockchain,
            &Blockchain::updateBlockchain);

    connect(this, &ExtraChainNode::login, m_privateProfile, &PrivateProfile::loadPrivateProfileLogin);
    connect(this, &ExtraChainNode::savePrivateProfile, m_privateProfile, &PrivateProfile::savePrivateProfile);
}

void ExtraChainNode::prepareFolders() {
    qDebug() << "Preparing folders";
    qDebug() << "Working directory:" << QDir::currentPath();

    QDir().mkpath(KeyStore::USER_KEYSTORE);
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

ResolveManager *ExtraChainNode::resolveManager() const {
    return m_resolveManager;
}

PrivateProfile *ExtraChainNode::privateProfile() const {
    return m_privateProfile;
}

SubscribeController *ExtraChainNode::subscribeController() const {
    return m_subscribeController;
}

void ExtraChainNode::logOut() {
}

// void ExtraChainNode::createActorWith

// void ExtraChainNode::makeContractFirstTransaction(Contract &contract)
//{
//    qDebug() << "ExtraChainNode::makeContractFirstTransaction";
//    //    contract.setFirst_transaction_hash(
//    //        createTransaction(BigNumber(0), contract.getAmount()).getHash());
//    m_networkManager->shareContract(contract);
//}

// void ExtraChainNode::makeContractFinalTransaction(Contract &contract)
//{
//    contract.setFinal_transaction_hash(
//        createTransaction(contract.getPerformer(), contract.getAmount()).getHash());
//    qDebug() << contract.serialize();
//    contract.setIsCompleted(true);
//    m_networkManager->shareContract(contract);
//}

ChatManager *ExtraChainNode::chatManager() const {
    return m_chatManager;
}

Dfs *ExtraChainNode::dfs() const {
    return m_dfs;
}
