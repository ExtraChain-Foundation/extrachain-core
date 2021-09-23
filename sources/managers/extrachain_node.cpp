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

#include "resolve/resolve_manager.h"
#include "network/network_manager.h"
#include "managers/tx_manager.h"
#include "managers/account_controller.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/blockchain.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "datastorage/actor.h"
#include "managers/thread_pool.h"
#include "dfs/controls/headers/dfs.h"
#include "managers/contract_manager.h"
#include "managers/sm_manager.h"
#include "dfs/managers/headers/dfs_networkmanager.h"
#include "managers/chatmanager.h"
#include "profile/private_profile.h"
#include "dfs/controls/headers/subscribe_controller.h"
#include "network/packages/service/message_types.h"
#include "managers/file_updater_manager.h"

ExtraChainNode::ExtraChainNode(const QString &localIp)
{
    static bool singleton = false;
    if (!singleton)
        singleton = true;
    else
        qFatal("Two instances of Node");

    if (sodium_init() != 0)
    {
        qDebug() << "Encryption init error";
        QCoreApplication::exit(-1);
    }

    prepareFolders();
    actorIndex = new ActorIndex();
    m_privateProfile = new PrivateProfile();
    smContractController = new SmartContractManager(actorIndex);
    accController = new AccountController(actorIndex);
    m_networkManager = new NetworkManager(actorIndex, localIp);
    subscribeController = new SubscribeController();
    subscribeController->setExtraChainNode(this);
    actorIndex->setAccController(accController);
    ThreadPool::addThread(m_networkManager);
    // this->thread()->sleep(1);
    m_blockchain = new Blockchain(accController, fileMode);
    accController->setBlockchain(m_blockchain);
    txManager = new TransactionManager(accController, m_blockchain, this);
    m_privateProfile->setAccountController(accController);
    chatManager = new ChatManager(accController, actorIndex);
    chatManager->setNetworkManager(m_networkManager);
    // contractManager = new ContractManager(accController, blockchain);
    dfs = new Dfs(actorIndex, accController, localIp);

    resolveManager = new ResolveManager(actorIndex, m_blockchain, m_networkManager, txManager, accController);
    resolveManager->setNode(this);
    resolveManager->setChatManager(chatManager);
    m_blockchain->setTxManager(txManager);
    m_networkManager->setResolveManager(resolveManager);
    // dfs->initDfsNetwork(resolveManager);
    m_privateProfile->setDfs(dfs);
    actorIndex->setResolveManager(resolveManager);
    connectSignals();

    static QTimer getAllActorsTimer;
    connect(&getAllActorsTimer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    getAllActorsTimer.start(30000);

    ThreadPool::addThread(m_blockchain);
    ThreadPool::addThread(actorIndex);
    ThreadPool::addThread(txManager);
    // ThreadPool::addThread(contractManager);
    ThreadPool::addThread(dfs);
    ThreadPool::addThread(smContractController);
    ThreadPool::addThread(resolveManager);
    ThreadPool::addThread(m_privateProfile);
    ThreadPool::addThread(chatManager);

    // QTimer::singleShot(2000, qApp, &QCoreApplication::quit);
    // FileUpdaterManager fl;
    // fl.verifyMyFiles("02c9b394cf3785389f82");
}

bool ExtraChainNode::createNewNetwork(const QString &email, const QString &password, const QString &tokenName,
                                      const QString &tokenCount, const QString &tokenColor)
{
    // TODO: check correct color in tokenColor

    if (QDir("keystore/profile").isEmpty())
    {
        qDebug() << "[Node] Create network with e-mail" << email << "and password" << password;
        QByteArray consoleHash = Utils::calcKeccak(email.toUtf8() + password.toUtf8());
        auto first = accController->createActor(ActorType::First, consoleHash);
        emit savePrivateProfile(consoleHash, first.id());
        actorIndex->setFirstId(first.id());
    }
    else
    {
        qInfo() << "You cannot create a new network, data is not empty";
        return false;
    }

    if (m_blockchain->getRecords() <= 0)
    {
        auto first = *accController->getMainActor();
        actorIndex->setFirstId(first.id());
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
        dfs->save(DfsStruct::DfsSave::Static, "usernames", "", DfsStruct::Type::Service);
    }

    return true;
}

void ExtraChainNode::start()
{
    QTimer::singleShot(500, this, &ExtraChainNode::ready);
}

void ExtraChainNode::showMessage(QString from, QString message)
{
    qDebug() << from << " " << message;
}

void ExtraChainNode::connectResolveManager()
{
    //    connect(networkManager, &NetworkManager::MsgReceived, resolveManager,
    //    &ResolveManager::resolveMessage); connect(resolveManager, &ResolveManager::coinRequest, this,
    //    &ExtraChainNode::coinResponse); connect(dfs->networkManager(), &DfsNetworkManager::newMessage,
    //    resolveManager,
    //            &ResolveManager::resolveMessage);
    // TODO: move
    //    connect(resolveManager, &ResolveManager::sendMsg, m_networkManager, &networkManager::sendMessage);

    connect(this, &ExtraChainNode::sendMsg, resolveManager, &ResolveManager::registrateMsg);
    connect(txManager, &TransactionManager::SendBlock, resolveManager, &ResolveManager::registrateMsg);
    connect(m_blockchain, &Blockchain::sendMessage, resolveManager, &ResolveManager::registrateMsg);
    //    connect(dfs, &Dfs::newSender, resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::connectSmContractManager()
{
    //    connect(smContractController, &SmartContractManager::verifyActor, m_networkManager,
    //    &networkManager::NewActor); TODO!!!
    //    connect(smContractController, &SmartContractManager::addContractActorInActorIndex, this,
    //            &ExtraChainNode::addActorInActorIndex);
    connect(smContractController, &SmartContractManager::saveActorInPrivateProfile,
            [this](const QByteArray &id, const QString &type, const bool &rewrite) { // TODO?
                auto mainId = accController->getMainActor()->id().toByteArray();
                emit nodeEditPrivateProfile({ m_privateProfile->hash(), mainId }, type, id, rewrite);
            });

    //[this](QString userId, Profile profile) { emit profileToUi(userId, profile); });
    connect(this, &ExtraChainNode::nodeEditPrivateProfile, m_privateProfile,
            &PrivateProfile::editPrivateProfile);

    connect(this, &ExtraChainNode::generateSmartContract, smContractController,
            &SmartContractManager::createContractProfile);
    connect(smContractController, &SmartContractManager::sendTransactionCreateContract, resolveManager,
            &ResolveManager::registrateMsg);

    // connect(smContractController, &SmartContractManager::sendCurrentToken, m_networkManager,
    // &networkManager::NewActor);
}

void ExtraChainNode::connectTxManager()
{
    // TODOD delete later (s)
    connect(this, &ExtraChainNode::NewTx, txManager, &TransactionManager::addTransaction);
}

ExtraChainNode::~ExtraChainNode()
{
    // m_networkManager->quit();
    // delete networkManager;
    m_networkManager->finished();
    delete txManager;
    // delete blockchain;
    delete accController;
    // delete actorIndex;
}

// DFSIndex *ExtraChainNode::getDFSIndex(){
//    return dfsIndex;
//}

Blockchain *ExtraChainNode::blockchain()
{
    return m_blockchain;
}

NetworkManager *ExtraChainNode::networkManager()
{
    return m_networkManager;
}

Transaction ExtraChainNode::createTransaction(Transaction tx)
{
    if (tx.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx:[%1]. Transaction is empty").arg(tx.toString());
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getCurrentActor();
    if (!actor.empty())
    {
        qDebug() << QString("Attempting to create tx:[%1] from user [%2]")
                        .arg(tx.toString(), QString(actor.id().toByteArray()));

        // 1) set prev block id
        BigNumber lastBlockId = m_blockchain->getLastBlock().getIndex();
        if (lastBlockId.isEmpty())
        {
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
        if (tx.getSender().isEmpty() || tx.getSender() == actorIndex->firstId() || tx.getReceiver().isEmpty()
            || tx.getReceiver() == actorIndex->firstId())
            emit NewTx(tx);
        else if (tx.getData() == Fee::FREEZE_TX || tx.getData() == Fee::UNFREEZE_TX)
        {
            emit sendMsg(tx.serialize(), Messages::ChainMessage::TxMessage);
        }
        else
        {
            BigNumber amountTemp(tx.getAmount());
            if (m_blockchain->getUserBalance(tx.getSender(), tx.getToken()) - amountTemp - amountTemp / 100
                >= 0)
            {
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
            }
            else
            {
                qDebug() << "Not enough money ";
                return Transaction();
            }
        }

        return tx;
    }
    else

        qDebug() << QString("Warning: can not create tx:[%1]. There no current user").arg(tx.toString());

    return Transaction();
}

Transaction ExtraChainNode::createTransaction(ActorId receiver, BigNumber amount, ActorId token)
{
    if (receiver.isEmpty() || amount.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getCurrentActor();
    if (!actor.empty())
    {
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
                                                    ActorId token)
{

    Actor<KeyPrivate> actor = accController->getCurrentActor();

    if (!actor.empty())
    {
        if (receiver.isEmpty())
        {
            qDebug() << "Create freeze tx to me";
            receiver = actor.id();
        }
        else
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
                                                  ActorId token)
{
    if (receiver.isEmpty() || amount.isEmpty())
    {
        qDebug() << QString("Warning: can not create tx without receiver or amount");
        return Transaction();
    }

    Actor<KeyPrivate> actor = accController->getActor(sender);
    if (!actor.empty())
    {
        qDebug() << actor.id();
        Transaction tx(actor.id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        // tx.setHop(2);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    }
    else
    {
        qDebug() << QString("Warning: can not create tx to [%1]. There no current user")
                        .arg(QString(receiver.toByteArray()));
    }
    return Transaction();
}

void ExtraChainNode::getAllActors()
{
    //    QByteArray res = getIdPrivateProfile();
    //    if (!res.isEmpty())
    //        emit getAllActorsNode(res, true);
}

void ExtraChainNode::getAllActorsTimerCall()
{
    if (accController->getAccountCount() > 0 && m_networkManager->connections().length() > 0)
    {
        ActorId actorId = accController->getMainActor()->id();

        if (!actorId.isEmpty())
            emit getAllActorsNode(actorId, true);
    }
}

void ExtraChainNode::createNetworkIdentifier()
{
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(BigNumber::random(64).toByteArray());
    file.flush();
    file.close();
}

#ifdef ECLIENT
void ExtraChainNode::notificationToken(QString os, QString actorId, QString token)
{
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto firstId = actorIndex->firstId();
    if (firstId.isEmpty())
        return;
    auto first = actorIndex->getActor(firstId);
    if (first.empty())
        return;
    auto mainKey = accController->getMainActor()->key();
    auto publicKey = first.key()->publicKey();

    QMap<QString, QByteArray> map = { { "actor", actorId.toLatin1() },
                                      { "token", mainKey->encrypt(token.toLatin1(), publicKey) },
                                      { "os", mainKey->encrypt(os.toLatin1(), publicKey) } };

    emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}
#endif

void ExtraChainNode::connectContractManager()
{
}

void ExtraChainNode::connectActorIndex()
{
    connect(actorIndex, &ActorIndex::sendMessage, resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection()
{
    // init dfs for user
    // connect(this, &ExtraChainNode::ready, networkManager, &NetworkManager::startNetwork);
    connect(this, &ExtraChainNode::ready, dfs, &Dfs::startDFS);
    connect(accController, &AccountController::initDfs, dfs, &Dfs::initMyLocalStorage);
    connect(actorIndex, &ActorIndex::initDfs, dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals()
{
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
    connect(this, &ExtraChainNode::removeConnection, dfs, &Dfs::removeConnection);
    connect(this, &ExtraChainNode::getAllActorsNode, actorIndex, &ActorIndex::getAllActors);
    connect(accController, &AccountController::loadWallets, m_blockchain, &Blockchain::updateBlockchain);

    connect(this, &ExtraChainNode::login, m_privateProfile, &PrivateProfile::loadPrivateProfileLogin);
    connect(this, &ExtraChainNode::savePrivateProfile, m_privateProfile, &PrivateProfile::savePrivateProfile);
}

void ExtraChainNode::prepareFolders()
{
    qDebug() << "Preparing folders";
    qDebug() << "Working directory:" << QDir::currentPath();

    FileSystem::createFolderIfNotExist(KeyStore::USER_KEYSTORE);
    FileSystem::createFolderIfNotExist(DataStorage::TMP_FOLDER);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::ACTOR_INDEX_FOLDER_NAME);
    FileSystem::createFolderIfNotExist(DataStorage::BLOCKCHAIN_INDEX + "/"
                                       + DataStorage::BLOCK_INDEX_FOLDER_NAME);
    if (!QFile(".settings").exists())
        createNetworkIdentifier();
}

AccountController *ExtraChainNode::getAccountController() const
{
    return accController;
}

ActorIndex *ExtraChainNode::getActorIndex() const
{
    return actorIndex;
}

ResolveManager *ExtraChainNode::getResolveManager() const
{
    return resolveManager;
}

PrivateProfile *ExtraChainNode::privateProfile() const
{
    return m_privateProfile;
}

SubscribeController *ExtraChainNode::getSubscribeController() const
{
    return subscribeController;
}

void ExtraChainNode::logOut()
{
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

void ExtraChainNode::tempareSlotForActors()
{
    emit sendActorStateList(accController->getCurrentState());
    emit sendActorToWallet(accController->getAccountID());
}

ChatManager *ExtraChainNode::getChatManager() const
{
    return chatManager;
}

Dfs *ExtraChainNode::getDfs() const
{
    return dfs;
}
