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
#include "datastorage/block_variant.h"
#include "datastorage/blockchain.h"
#include "datastorage/dfs/dfs_controller.h"
#include "datastorage/dfs/permission_manager.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "enc/enc_tools.h"
#include "managers/account_controller.h"
#include "managers/connections_manager.h"
#include "managers/data_mining_manager.h"
#include "managers/thread_pool.h"
#include "managers/tx_manager.h"
// #include "managers/restApiServerManager.h"
#include "network/network_manager.h"

ExtraChainNode::ExtraChainNode(bool isClientApp, bool allowRunRestApiServer)
    : isClientApplication(isClientApp) {
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
    m_actorIndex        = new ActorIndex(*this);
    m_accountController = new AccountController(*this);

    m_networkManager = new NetworkManager(*this);
    //    ThreadPool::addThread(m_networkManager);

    m_blockchain = new Blockchain(this);
    m_txManager  = new TransactionManager(m_accountController, m_blockchain, this);
    m_dfs        = new DfsController(*this);

    m_blockchain->setTxManager(m_txManager);

    m_dmm = new DataMiningManager(this);
    // test port and address
    m_connectionsManager =
        new ConnectionsManager("12.12.12.12", "1212", actorIndex()->firstId().toByteArray(), this);

    connectSignals();

    static QTimer getAllActorsTimer;
    connect(&getAllActorsTimer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    getAllActorsTimer.start(30000);

    if (allowRunRestApiServer) {
        // m_restApiServerManager = new RestApiServerManager(this);
    }

    // ThreadPool::addThread(m_blockchain);
    // ThreadPool::addThread(m_txManager);
}

uint64_t ExtraChainNode::getBlockCount() const {
    return blockCount;
}

ExtraChainNode::~ExtraChainNode() {
    emit m_networkManager->finished();
    delete m_dfs;
    delete m_actorIndex;
    delete m_txManager;
    delete m_blockchain;
    delete m_accountController;
    delete m_dmm;
    delete m_connectionsManager;
}

bool ExtraChainNode::createNewNetwork(
    const QString& email,
    const QString& password,
    const QString& tokenName,
    const QString& tokenCount,
    const QString& tokenColor) {
    // TODO: check correct color in tokenColor

    if (QDir("keystore/profile").isEmpty()) {
        qDebug() << "[Node] Create network with e-mail" << email;
        auto consoleHash = Utils::calcHash(email.toStdString() + password.toStdString());
        auto first       = m_accountController->createProfile(consoleHash, ActorType::ServiceProvider);
        m_actorIndex->setFirstId(first.id());
    } else {
        qInfo() << "You cannot create a new network, data is not empty";
        return false;
    }

    if (m_blockchain->getRecords() <= 0) {
        auto& first = m_accountController->mainActor();
        // QString firstId = first.id().toString();

        QMap<ActorId, BigNumberFloat> tm;
        tm.insert(ActorId(), 0);
        GenesisBlock tmp        = m_blockchain->createGenesisBlock(first, tm);
        auto         tmpVariant = BlockVariant(tmp);
        m_blockchain->addBlock(tmpVariant, true);

        // TEST
        //        Block lastBlock = m_blockchain->getLastBlock();
        //        Block block(std::string(""), lastBlock);
        //        m_blockchain->addBlock(block);
        // TEST

        //  emit generateSmartContract(tokenCount.toLatin1(), tokenName.toUtf8(), first.id().toByteArray(),
        //                             tokenColor.toLatin1());

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
}

Blockchain* ExtraChainNode::blockchain() {
    return m_blockchain;
}

NetworkManager* ExtraChainNode::network() {
    return m_networkManager;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransaction(Transaction tx) {
    if (tx.getAmount() == 0) {
        qWarning() << "Can not create tx without amount";
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (tx.isEmpty() && !tx.isBurn()) {
        qWarning() << fmt::format(
            "Can not create tx:[{}]. Transaction is empty",
            tx.toStdString());
        return std::unexpected(TransactionError::EmptyTransaction);
    }

    auto actor = m_accountController->currentWallet();
    if (actor->empty()) {
        qWarning() << fmt::format(
            "Can not create tx:[{}]. There no current user",
            tx.toStdString());
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    qWarning() << fmt::format(
        "Attempting to create tx:[{}] from user [{}]",
        tx.toStdString(),
        actor->id().toStdString());

    // 1) set prev block id
    BigNumber lastBlockId = m_blockchain->getLastRealBlock().getIndex();
    if (lastBlockId.isEmpty()) {
        qWarning() << fmt::format(
            "Can not create tx:[{}]. There is no last block in blockchain",
            tx.toStdString());
        return std::unexpected(TransactionError::NoLastBlock);
    }
    tx.setPrevBlock(lastBlockId);

    // 2) check coin availability
    if (blockchain()->getUserBalance(actor->id(), tx.getToken()) < tx.getAmount()) {
        qWarning() << fmt::format(
            "Can not create tx:[{}]. There is not enough coins/tokens in wallet",
            tx.toStdString());
        return std::unexpected(TransactionError::InsufficientFunds);
    }

    // 3) sign transaction
    tx.sign(actor);
    qDebug() << "[Transaction] Send" << tx.getAmountDec() << "to" << tx.getReceiver();

    if (tx.isFarmingTransaction() || tx.isLockedFarmingTransaction()) {
        m_txManager->addTransaction(tx);
    }
    if (tx.getSender().isZero() || tx.getSender() == m_actorIndex->firstId())
        m_txManager->addTransaction(tx);

    return tx;
}

std::expected<Transaction, TransactionError>
ExtraChainNode::createTransaction(ActorId receiver, BigNumberFloat amount, ActorId token) {
    auto actor = m_accountController->currentWallet();

    Transaction tx(actor->id(), receiver, amount, token);
    // add sent tx balances
    tx.setToken(token);

    if (actor->empty()) {
        qWarning() << fmt::format("Can not create tx. There no current user", tx.toStdString());
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return this->createTransaction(tx);
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

bool ExtraChainNode::importUser(
    const std::string& data,
    const std::string& login,
    const std::string& password) {
    auto hash = Utils::calcHash(login + password);

    auto json = SecretKey::decryptWithPassword(data, hash);
    if (hash.empty() || json.empty()) {
        return false;
    }

    auto array = QJsonDocument::fromJson(QByteArray::fromStdString(json)).array();
    if (array.count() != 3) {
        return false;
    }

    auto extrachainVersion     = array[0].toString();
    auto date                  = array[1].toInteger();
    auto profile               = array[2].toObject();
    auto profileBytes          = QJsonDocument(profile).toJson(QJsonDocument::Compact);
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

std::expected<Transaction, TransactionError> ExtraChainNode::createTransactionFrom(
    ActorId        sender,
    ActorId        receiver,
    BigNumberFloat amount,
    ActorId        token) {
    if (sender == ActorId()) { // TODO: remove hack
        sender = m_accountController->currentWallet()->id();
    }

    auto actor = m_accountController->currentProfile().getActor(sender);
    if (amount.isEmpty()) {
        qWarning() << "Can not create tx without amount";
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (receiver.isZero() && !amount.isEmpty()) {
        if (!actor->empty()) {
            Transaction tx(actor->id(), receiver, amount);
            tx.setToken(token);

            qDebug() << QString("Attempting to create tx: [%1] from user [%2]")
                            .arg(tx.toString(), QString(actor->id().toByteArray()));

            // 1) set prev block id
            BigNumber lastBlockId = m_blockchain->getLastRealBlock().getIndex();
            if (lastBlockId.isEmpty()) {
                qWarning() << QString("Can not create tx: [%1]. There is no last block in blockchain")
                                  .arg(tx.toString())
                                  .toStdString();
                return std::unexpected(TransactionError::NoLastBlock);
            }
            tx.setPrevBlock(lastBlockId);

            // 2) check coin availability
            //                if (blockchain()->getUserBalance(actor.id(), tx.getToken()) <
            //                tx.getAmount()) {
            //                    qDebug() << QString("Warning: can not create tx:[%1]. There is not
            //                    enough "
            //                                        "coins/tokens in wallet")
            //                                    .arg(tx.toString());
            //                    return Transaction();
            //                }

            // 3) sign transaction

            tx.sign(actor);
            qDebug() << "[Transaction] Send tx" << tx.getAmountDec() << "to" << tx.getReceiver();
            auto createdTx = this->createTransaction(tx);
            if (createdTx.has_value()) {
                m_txManager->addTransaction(createdTx.value());
                return createdTx;
            }
        }

        return std::unexpected(TransactionError::Unknown);
    }

    if (!actor->empty()) {
        qDebug() << actor->id();
        Transaction tx(actor->id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    } else {
        qWarning() << QString("Can not create tx to [%1]. There no current user")
                          .arg(QString(receiver.toByteArray()));
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return std::unexpected(TransactionError::Unknown);
}

std::expected<Transaction, TransactionError>
ExtraChainNode::createFarmingTransaction(ActorId sender, const BigNumberFloat& amount, const TypeTx& typeTx) {
    qDebug() << sender;
    Transaction tx(sender, sender, 0);
    tx.setTypeTx(typeTx);
    tx.setAmount(amount);
    return this->createTransaction(tx);
}

std::string ExtraChainNode::transactionErrorDescription(const TransactionError &error)
{
    switch(error) {
    case TransactionError::Unknown: return "Unknown error";
    case TransactionError::ZeroAmount: return "Can not create transaction without amount.";
    case TransactionError::EmptyTransaction: return "Can not create transaction. Transaction is empty.";
    case TransactionError::NoLastBlock: return "There is no last block in blockchain.";
    case TransactionError::InsufficientFunds: return "Can not create transaction. There is not enough coins/tokens in wallet.";
    case TransactionError::NoCurrentUser: return "Can not create transaction. There no current user.";
    }
}

void ExtraChainNode::getAllActorsTimerCall() {
    if (m_accountController->count() > 0 && m_networkManager->connections().length() > 0) {
        ActorId actorId = m_accountController->mainActor()->id();

        if (!actorId.isZero())
            m_actorIndex->getAllActors(actorId, true);
    }
}

void ExtraChainNode::createNetworkIdentifier() {
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(Utils::calcHash(BigNumber::random(64).toStdString()).c_str());
    file.flush();
    file.close();
}

void ExtraChainNode::notificationToken(QString os, QString actorId, QString token) {
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto firstId = m_actorIndex->firstId();
    if (firstId.isZero())
        return;
    auto first = m_actorIndex->getActor(firstId);
    if (first.empty())
        return;
    auto& mainKey   = m_accountController->mainActor()->key();
    auto& publicKey = first.key().publicKey();

    // std::map<std::string, std::string> map = { { "actor", actorId.toStdString() },
    //                                            { "token", mainKey.encrypt(token.toStdString(),
    //                                            publicKey)
    //                                            }, { "os", mainKey.encrypt(os.toStdString(), publicKey)
    //                                            } };

    // TODONEW emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}

void ExtraChainNode::handleCountMessageReceived(BigNumber count) {
    size_t connection = m_connectionsManager->getActiveConnection().size();
    resiveCounts.push_back(count);
    if (m_connectionsManager->getActiveConnection().size() == resiveCounts.size()) {
        BigNumber sum = 0;
        for (const BigNumber& number : resiveCounts) {
            sum += number;
        }
        BigNumber middleCount = sum / resiveCounts.size();

        // Ef = Tc / Tu
        // Tc - current coefficient
        // Tu - total network uptime. Blocks are formed every 2 seconds. Therefore, by taking the total number
        //      of blocks in the network and multiplying them by 2, it is possible to determine how many
        //      seconds the network has been online.
        // Ef - efficiency coefficient

        std::string ip = m_networkManager->localIp().toStdString();
        std::string port = QString::number(m_networkManager->wsPort).toStdString();
        blockCount = m_connectionsManager->getActivityScore(Connection { ip, port, true })
                     / (std::stoi(middleCount.toStdString()) * 2);
    }
}

void ExtraChainNode::connectContractManager() {
}

void ExtraChainNode::connectActorIndex() {
    // connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection() {
    // init dfs for user
    connect(m_networkManager, &NetworkManager::addFragSignal, m_dfs, &DfsController::threadAddFragment);
    connect(m_networkManager, &NetworkManager::fetchFragment, m_dfs, &DfsController::fetchFragment);
    connect(this, &ExtraChainNode::ready, m_networkManager, &NetworkManager::startNetwork);
    // connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    // connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    // connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals() {
    connect(
        this,
        &ExtraChainNode::ready,
        []() {
            qInfo() << "Node: started";
        });
    connectTxManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    dfsConnection();

    connect(m_networkManager, &NetworkManager::newSocket, this, &ExtraChainNode::getAllActorsTimerCall);

    // temp for tests, maybe only for console
    connect(m_networkManager, &NetworkManager::newSocket, m_blockchain, &Blockchain::updateBlockchain);
    connect(
        m_networkManager,
        &NetworkManager::newSocket,
        [this]() {
            m_dfs->requestSync();
        });
    connect(
        m_networkManager,
        &NetworkManager::newSocket,
        [this]() {
            m_dfs->sendSizeRequestMsg(m_accountController->mainActor()->id());
        });
    connect(
        m_networkManager,
        &NetworkManager::newSocket,
        [this]() {
            m_dfs->sendCountRequestMsg(m_accountController->mainActor()->id());
        });
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
    QDir().mkpath(QString::fromStdString(KeyStore::encrypt));
    QDir().mkpath(QString::fromStdString(Scripts::folder));

    if (!QFile(".settings").exists())
        createNetworkIdentifier();
}

void ExtraChainNode::calculateBlockCount() {
    ActorId              actorId = m_accountController->mainActor()->id();
    DFSP::RequestDfsSize msg{ actorId.toStdString() };

    m_networkManager->send_message(msg, MessageType::RequestBlockCount, MessageStatus::Request);
}

AccountController* ExtraChainNode::accountController() const {
    return m_accountController;
}

ActorIndex* ExtraChainNode::actorIndex() const {
    return m_actorIndex;
}

DfsController* ExtraChainNode::dfs() const {
    return m_dfs;
}

TransactionManager* ExtraChainNode::txManager() const {
    return m_txManager;
}

DataMiningManager* ExtraChainNode::dataMiningManager() const {
    return m_dmm;
}

ConnectionsManager* ExtraChainNode::connectionsManager() const {
    return m_connectionsManager;
}

bool ExtraChainNode::login(const std::string& login, const std::string& password) {
    return m_accountController->load(Utils::calcHash(login + password));
}

bool ExtraChainNode::login(const std::string& hash) {
    return m_accountController->load(hash);
}

void ExtraChainNode::logout() {
    m_accountController->clear();
    // auto hash remove
    std::exit(0);
}
