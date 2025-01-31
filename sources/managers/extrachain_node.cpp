/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
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

#include <array>

#include <QJsonObject>
#include <sodium/core.h>

#include "blockchain/actor.h"
#include "blockchain/block.h"
#include "blockchain/block_variant.h"
#include "blockchain/blockchain.h"
#include "dfs/dfs_controller.h"
// #include "dfs/permission_manager.h"
#include "blockchain/actor_index.h"
#include "blockchain/transaction.h"
#include "encryption/encryption_tools.h"
#include "managers/account_controller.h"
#include "managers/connections_manager.h"
#include "managers/data_mining_manager.h"
// #include "managers/thread_pool.h"
#include "managers/transaction_manager.h"
#include "managers/token_manager.h"
#include "managers/thread_pool.h"
#include "dfs/collection_template.h"
// #include "managers/restApiServerManager.h"
#include "network/network_manager.h"
#include "chat/chat_manager.h"

#ifdef Q_OS_LINUX
    #include <signal.h>
#endif

struct TokensDataRow {
    TokenId        token_id;
    std::string    name;
    std::string    ticker;
    BigNumberFloat count;
    ActorId        owner;
    std::string    color;
    std::string    smart;
};
BOOST_DESCRIBE_STRUCT(TokensDataRow, (), (token_id, name, ticker, count, owner, name, color, smart))

ExtraChainNodeWrapper::ExtraChainNodeWrapper(QObject* parent,
                                             bool     isClientApp,
                                             bool     allowRunRestApiServer,
                                             bool     isRaccoonCheck)
    : QObject(parent)
    , node(new ExtraChainNode(isClientApp, allowRunRestApiServer, isRaccoonCheck)) {
}

ExtraChainNodeWrapper::~ExtraChainNodeWrapper() {
    eInfo("ExtraChainNodeWrapper::~ExtraChainNodeWrapper");

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        node->deleteLater();
    } else
        delete node;
}

void ExtraChainNodeWrapper::Init(bool makeAsync) {
    if (makeAsync) {
        m_thread = new QThread();
        node->moveToThread(m_thread);
        connect(m_thread, &QThread::started, node, &ExtraChainNode::process);
        connect(m_thread, &QThread::finished, node, &ExtraChainNode::cleanUp);
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        m_thread->start();
    } else
        node->process();
}

ExtraChainNode::ExtraChainNode(bool isClientApp, bool allowRunRestApiServer, bool isRaccoonCheck)
    : isClientApplication(isClientApp)
    , isRaccoon(isRaccoonCheck)
    , allowRunRestApiServer(allowRunRestApiServer) {
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
    Logger::instance().set_debug(true);

#ifdef Q_OS_LINUX
    signal(SIGPIPE, SIG_DFL);
#endif
}

void ExtraChainNode::process() {
    static bool singleton = false;
    if (!singleton)
        singleton = true;
    else
        eFatal("Two instances of Node");

    if (sodium_init() != 0) {
        eLog("Encryption init error");
        eFatal("Encryption init error");
        QCoreApplication::exit(-1000);
    }

    prepareFolders();
    m_actorIndex         = new ActorIndex(this);
    m_accountController  = new AccountController(this);
    m_networkManager     = new NetworkManager(this);
    m_blockchain         = new Blockchain(this);
    m_transactionManager = new TransactionManager(this);
    m_dfs                = new DfsController(this);
    m_dmm                = new DataMiningManager(this);
    auto key             = actorIndex()->firstId().toQByteArray();
    auto address         = "12.12.12.12";
    auto port            = "1212";
    m_connectionsManager = new ConnectionsManager(address, port, key, this);
    m_tokenManager       = new TokenManager(this);
    chat_manager_        = new ChatManager(this);

    auto thread = ThreadPool::addThread(m_blockchain);
    ThreadPool::addThread(m_transactionManager, thread);

    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    timer->start(30000);

    timer_reward = new QTimer(this);
    connect(timer_reward, &QTimer::timeout, this, &ExtraChainNode::timer_reward_request);
    timer_reward->start(60000);

    m_initPublicIPAndCountry = m_networkManager->getPublicIPAndCountry();

    connectSignals();
    emit NodeInitialised();
}

std::uint64_t ExtraChainNode::getBlockCount() const {
    return blockCount;
}

ExtraChainNode::~ExtraChainNode() {
    eInfo("ExtraChainNode::~ExtraChainNode");
    if (m_vpnClearFunc) {
        m_vpnClearFunc();
    }
}

void ExtraChainNode::cleanUp() {
    m_networkManager->deleteLater();
    // m_blockchain->deleteLater();
    // m_transactionManager->deleteLater();
    m_dfs->deleteLater();
    delete chat_manager_;
}

bool ExtraChainNode::create_new_network(const std::string& login, const std::string& password) {
    if (!QDir("keystore/profile").isEmpty()) {
        eInfo("Cannot create a new network: existing profile data found");
        return false;
    }

    eLog("[Node] Create network with login {}", login);
    auto consoleHash = Utils::calculate_hash(login + password);
    auto first       = m_accountController->createProfile(consoleHash, ActorType::DAppMaster);
    m_actorIndex->setFirstId(first.id());
    m_accountController->getProfile(first.id()).rename_wallet(first.id(), "King of the World");

    if (m_blockchain->getRecords() <= 0) {
        auto& first      = m_accountController->mainActor();
        auto  firstBlock = m_blockchain->createFirstBlock(first);
        if (!firstBlock.has_value())
            return false;

        Responder responder(this->m_networkManager);
        auto      block = m_blockchain->addBlock(firstBlock.value());
        // network()->send_message(block.value(), MessageType::BlockchainNewBlock, SendMode::Broadcast);
        blockchain()->status_ = BlockchainStatus::Ready;
    }

    create_network_need_dfs_creation = true;

    eSuccess("[Node] New network created");
    return true;
}

void ExtraChainNode::create_new_network_dfs() {
    // temp while no cached local new store file
    create_network_need_dfs_creation = false;

    auto first_id        = m_actorIndex->firstId();
    auto tokens_template = Dfs::CollectionTemplate::create("Tokens").value().add_fields(
        { Dfs::Field::ActorId("token_id").not_null().unique(),
          Dfs::Field::String("name").not_null().unique().length(3, 20),
          Dfs::Field::String("ticker").not_null().unique().length(2, 5),
          Dfs::Field::String("count").not_null(),
          Dfs::Field::ActorId("owner").not_null(),
          Dfs::Field::String("color").not_null(),
          Dfs::Field::String("smart") });

    auto template_res = m_dfs->store_template(first_id, tokens_template);
    if (!template_res.has_value()) {
        eCritical("Can't create token cache database, because {}", template_res.error());
        return;
    }
    return;

    auto store_res =
        m_dfs->store_collection(first_id, first_id, "Tokens", template_res->actor_id, template_res->file_id);
    if (!store_res.has_value()) {
        eCritical("Can't create token cache database, because {}", store_res.error());
        Utils::wipeDataFiles();
        return;
    }

    auto tokens_row = TokensDataRow { .token_id = ActorId(),
                                      .name     = "ExtraChain",
                                      .ticker   = "EXC",
                                      .count    = BigNumberFloat(0),
                                      .owner    = first_id,
                                      .color    = "#111111",
                                      .smart    = "" };
    m_dfs->add_collection_row(store_res->actor_id, store_res->file_id, tokens_row);

    eSuccess("[Node] New network dfs created");
}

void ExtraChainNode::start() {
    if (!started) {
        QTimer::singleShot(500, this, &ExtraChainNode::ready);
        // emit startNetwork();
        started = true;
    }
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

void ExtraChainNode::connectTransactionManager() {
}

Blockchain* ExtraChainNode::blockchain() {
    return m_blockchain;
}

NetworkManager* ExtraChainNode::network() {
    return m_networkManager;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransaction(Transaction tx) {
    if (tx.amount() <= 0) {
        eWarning("Can not create tx without amount {}", tx);
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (tx.isEmpty() && !tx.isBurn()) {
        eWarning("Can not create: {}. Transaction is empty", tx);
        return std::unexpected(TransactionError::EmptyTransaction);
    }

    auto actor = m_accountController->currentWallet();
    if (actor.empty()) {
        eWarning("Can not create: {}. There no current user", tx);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    eWarning("Attempting to create {} from user {}", tx, actor.id().to_string());

    // 1) set prev block id
    auto lastRealBlock = m_blockchain->getLastRealBlock();
    if (!lastRealBlock.has_value() || (lastRealBlock.has_value() && lastRealBlock->isEmpty())) {
        eWarning("Can not create: {}. There is no last block in blockchain", tx);
        return std::unexpected(TransactionError::NoLastBlock);
    }
    tx.setPrevBlock(lastRealBlock->getIndex());

    // 2) check coin availability
    if (blockchain()->getUserBalance(actor.id(), tx.token()) < tx.amount()) {
        eWarning("Can not create: {}. There is not enough coins/tokens in wallet", tx);
        return std::unexpected(TransactionError::InsufficientFunds);
    }

    // 3) sign transaction
    tx.sign(actor);
    eLog("[Transaction] Send {} to {}", tx.amount().to_string(NumeralBase::Dec), tx.receiver());

    return tx;
}

TokenManager* ExtraChainNode::tokenManager() const {
    return m_tokenManager;
}

ChatManager* ExtraChainNode::chat_manager() {
    return chat_manager_;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransaction(ActorId        receiver,
                                                                               BigNumberFloat amount,
                                                                               ActorId        token) {
    auto actor = m_accountController->currentWallet();

    Transaction tx(actor.id(), receiver, amount, token);
    // add sent tx balances
    tx.setToken(token);

    if (actor.empty()) {
        eWarning("Can not create {}. There no current user", tx);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return this->createTransaction(tx);
}

std::expected<std::string, ImportError> ExtraChainNode::exportUser() {
    const auto& current_profile = m_accountController->currentProfile();
    auto        network_id      = m_actorIndex->firstId();
    if (network_id.is_zero()) {
        return std::unexpected(ImportError::NoNetworkId);
    }

    auto imported_user = ImportedUser { .network      = network_id,
                                        .version      = EXTRACHAIN_VERSION,
                                        .date         = Utils::current_date_ms(),
                                        .system       = current_profile.system().id(),
                                        .actors       = current_profile.actors(),
                                        .imports      = current_profile.imports(),
                                        .wallet_names = current_profile.wallet_names() };

    auto json = Json::serialize(imported_user);

    auto hash      = current_profile.hash();
    auto encrypted = Cryptography::symmetric_encrypt_password(ByteArray(json).toBytes(), hash);
    if (!encrypted.has_value()) {
        return std::unexpected(ImportError::CryptoError);
    }

    return ByteArray(encrypted.value()).toString();
}

std::string ExtraChainNode::importUser(const std::string& data,
                                       const std::string& login,
                                       const std::string& password) {
    if (data.empty()) {
        return std::string(); // unexpected
    }

    auto login_password = login + password;
    if (login_password.empty()) {
        return std::string(); // unexpected
    }

    auto hash = Utils::calculate_hash(login_password);
    auto json = Cryptography::symmetric_decrypt_password(ByteArray(data).toBytes(), hash);
    if (!json.has_value()) {
        return std::string(); // unexpected
    }

    auto imported_user = Json::deserialize<ImportedUser>(json.value());
    if (!imported_user.has_value()) {
        return std::string();
    }

    // TODO: network id check

    m_accountController->import_profile(imported_user.value(), hash);

    return hash;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransactionFrom(ActorId        sender,
                                                                                   ActorId        receiver,
                                                                                   BigNumberFloat amount,
                                                                                   ActorId        token) {
    if (sender == ActorId()) { // TODO: remove hack
        sender = m_accountController->currentWallet().id();
    }

    auto actor = m_accountController->currentProfile().get_actor(sender);
    if (!actor.has_value()) {
        return std::unexpected(TransactionError::NoSender);
    }
    if (amount <= 0) {
        eWarning("Can not create tx without amount");
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (receiver.is_zero() && amount > 0) {
        if (!actor->get().empty()) {
            Transaction tx(actor->get().id(), receiver, amount);
            tx.setToken(token);

            eLog("Attempting to create: {} from user {}", tx, actor->get().id());

            tx.sign(actor.value());
            eLog("[Transaction] Send tx {} to {}", tx.amount().to_string(NumeralBase::Dec), tx.receiver());
            auto createdTx = this->createTransaction(tx);
            return createdTx;
        }

        return std::unexpected(TransactionError::Unknown);
    }

    if (!actor->get().empty()) {
        eLog("{}", actor->get().id());
        Transaction tx(actor->get().id(), receiver, amount);
        // add sent tx balances

        tx.setToken(token);
        //        if (actorIndex->m_firstId != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->m_firstId))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    } else {
        eWarning("Can not create tx to '{}'. There no current user", receiver.toQByteArray());
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return std::unexpected(TransactionError::Unknown);
}

std::expected<Transaction, TransactionError> ExtraChainNode::sendTransaction(Transaction              transaction,
                                                                             const Actor<KeyPrivate>& signer) {
    auto lastRealBlock = m_blockchain->getLastRealBlock();

    if (!lastRealBlock.has_value() || (lastRealBlock.has_value() && lastRealBlock->isEmpty())) {
        return std::unexpected(TransactionError::NoLastBlock);
    }

    BigNumber lastBlockId = lastRealBlock->getIndex();
    transaction.setPrevBlock(lastBlockId);
    transaction.sign(signer);
    // !sign -> the конец

    eLog("[Blockchain] Send {}", transaction);
    network()->send_message(transaction, MessageType::BlockchainTransaction, SendMode::Broadcast);

    return transaction;
}

std::string ExtraChainNode::transactionErrorDescription(const TransactionError& error) {
    switch (error) {
    case TransactionError::Unknown:
        return "Unknown error";
    case TransactionError::ZeroAmount:
        return "Can not create transaction without amount.";
    case TransactionError::EmptyTransaction:
        return "Can not create transaction. Transaction is empty.";
    case TransactionError::NoLastBlock:
        return "There is no last block in blockchain.";
    case TransactionError::InsufficientFunds:
        return "Can not create transaction. There is not enough coins/tokens in wallet.";
    case TransactionError::NoCurrentUser:
        return "Can not create transaction. There no current user.";
    default:
        return "";
    }
}

void ExtraChainNode::getAllActorsTimerCall() {
    if (m_accountController->count() > 0 && m_networkManager->connections()->size() > 0) {
        ActorId actorId = m_accountController->mainActor().id();

        if (!actorId.is_zero())
            m_actorIndex->getAllActors(actorId, true);

        m_dfs->download_manager().check_all_files("");
    }
}

void ExtraChainNode::timer_reward_request() {
    dataMiningManager()->requestCoinReward();
}

void ExtraChainNode::createNetworkIdentifier() {
    QFile file(".settings");
    file.open(QIODevice::WriteOnly | QIODevice::Truncate);
    file.write(Utils::calculate_hash(std::to_string(QDateTime::currentSecsSinceEpoch())
                                     + std::to_string(QRandomGenerator::global()->bounded(100000)))
                   .c_str());
    file.flush();
    file.close();
}

void ExtraChainNode::notificationToken(QString os, QString actorId, QString token) {
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto firstId = m_actorIndex->firstId();
    if (firstId.is_zero())
        return;
    auto first = m_actorIndex->getActor(firstId);
    if (first.empty())
        return;
    auto& mainKey   = m_accountController->mainActor().key();
    auto& publicKey = first.key().public_key();

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
        BigNumber sum = BigNumber(0);
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

        std::string ip   = m_networkManager->localIp().toStdString();
        std::string port = QString::number(m_networkManager->wsPort).toStdString();
        blockCount       = m_connectionsManager->getActivityScore(Connection { ip, port, true })
                     / (std::stoi(middleCount.to_string()) * 2);
    }
}

void ExtraChainNode::connectContractManager() {
}

void ExtraChainNode::connectActorIndex() {
    // connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection() {
    // init dfs for user
    // connect(m_networkManager, &NetworkManager::addFragSignal, m_dfs, &DfsController::threadAddFragment);
    // connect(m_networkManager, &NetworkManager::fetchFragment, m_dfs, &DfsController::fetchFragment);
    connect(this, &ExtraChainNode::ready, m_networkManager, &NetworkManager::startNetwork);
    // connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    // connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    // connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals() {
    connect(this, &ExtraChainNode::ready, []() {
        eInfo("Node: started");
    });
    connectTransactionManager();
    connectContractManager();
    //    connectAccountController();
    connectActorIndex();
    dfsConnection();

    connect(m_networkManager, &NetworkManager::newSocketActivated, this, &ExtraChainNode::getAllActorsTimerCall);

    // temp for tests, maybe only for console
    connect(m_networkManager, &NetworkManager::newSocketActivated, [this]() {
        emit readyInitLocalizationFiles();
        // m_dfs->requestDirFileAllActors();
        // m_dfs->requestSync();
    });

    connect(m_networkManager,
            &NetworkManager::newSocketActivatedWithParams,
            [this](const std::string ip, const std::string identifier) {
                eLog("[WS] Start sync...");

                if (create_network_need_dfs_creation) {
                    create_new_network_dfs();
                }

                Responder responder(m_networkManager);
                responder.add_identifier(identifier);
                m_actorIndex->send_system_actor(responder);

                m_networkManager->sendFromCache();
                m_blockchain->start_check();
                // m_blockchain->sync(BigNumber(), responder);
                m_dfs->sync(identifier);
            });

    connect(m_networkManager, &NetworkManager::newSocketActivated, [this]() {
        m_dfs->sendSizeRequestMsg(m_accountController->mainActor().id());
    });
    connect(m_networkManager, &NetworkManager::newSocketActivated, [this]() {
        m_dfs->sendCountRequestMsg(m_accountController->mainActor().id());
    });

    // connect(m_accountController, &AccountController::loadWallets, m_blockchain,
    //         &Blockchain::updateBlockchain);
    connect(m_tokenManager,
            &TokenManager::sendTransactionCreateToken,
            this,
            [this](const ActorId& actorId, const Transaction& tx) {
                QTimer* timer = new QTimer();
                timer->setSingleShot(true);

                connect(timer, &QTimer::timeout, this, [=]() {
                    auto actor = m_accountController->currentProfile().get_actor(actorId);
                    if (!actor.has_value()) {
                        return;
                    }
                    this->sendTransaction(tx, actor.value());
                    timer->deleteLater();
                });

                timer->start(2000);
            });

    connect(m_tokenManager,
            &TokenManager::sendToken,
            this,
            [=, this](const ActorId& actor_id, const QString& json_path) {
                m_dfs->store_file(actor_id,
                                  actor_id,
                                  json_path.toStdString(),
                                  "contract",
                                  "token-description.json",
                                  Dfs::DataSecurity::Public);
            });
}

void ExtraChainNode::prepareFolders() {
    eLog("Preparing folders");
    eLog("Working directory: {}", QDir::currentPath());

    QDir().mkpath(QString::fromStdString(KeyStore::folder));
    QDir().mkpath(QString::fromStdString(BlockchainConst::TMP_FOLDER));
    QDir().mkpath(QString::fromStdString(BlockchainConst::BLOCKCHAIN_INDEX + "/"
                                         + BlockchainConst::ACTOR_INDEX_FOLDER_NAME));
    QDir().mkpath(QString::fromStdString(BlockchainConst::BLOCKCHAIN_INDEX + "/"
                                         + BlockchainConst::BLOCK_INDEX_FOLDER_NAME));
    // QDir().mkpath(QString::fromStdString(KeyStore::encrypt));
    QDir().mkpath(QString::fromStdString(Token::FOLDER_TOKENS));

    QFile(".settings").remove();
    if (!QFile(".settings").exists())
        createNetworkIdentifier();
}

void ExtraChainNode::calculateBlockCount() {
    ActorId              actorId = m_accountController->mainActor().id();
    DfsP::RequestDfsSize msg { .actorId = actorId };

    m_networkManager->send_message(msg,
                                   MessageType::RequestBlockCount,
                                   SendMode::Neighbours,
                                   MessageStatus::Request);
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

TransactionManager* ExtraChainNode::transactionManager() const {
    return m_transactionManager;
}

DataMiningManager* ExtraChainNode::dataMiningManager() const {
    return m_dmm;
}

ConnectionsManager* ExtraChainNode::connectionsManager() const {
    return m_connectionsManager;
}

bool ExtraChainNode::login(const std::string& login, const std::string& password) {
    return m_accountController->load(Utils::calculate_hash(login + password));
}

bool ExtraChainNode::login(const std::string& hash) {
    return m_accountController->load(hash);
}

void ExtraChainNode::logout() {
    m_accountController->clear();
    // auto hash remove
    QCoreApplication::exit(0);
}

void ExtraChainNode::InitVPN(VpnFunctionClearType vpnClearFunc) {
    m_vpnClearFunc = vpnClearFunc;
}

std::pair<QString, QString> ExtraChainNode::getInitPublicIPAndCountry() const {
    return m_initPublicIPAndCountry;
}
