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

#include "chain/dag.h"
#include "extrachain_version.h"
#include "chain/actor.h"
#include "dfs/dfs_controller.h"
// #include "dfs/permission_manager.h"
#include "chain/actor_index.h"
#include "chain/transaction.h"
#include "encryption/encryption_tools.h"
#include "managers/account_controller.h"
#include "managers/data_mining_manager.h"
// #include "managers/thread_pool.h"
#include "managers/token_manager.h"
#include "managers/subscription_manager.h"
#include "managers/thread_pool.h"
#include "dfs/collection_template.h"
#include "network/network_manager.h"
#include "chat/chat_manager.h"
#include "utils/thread_pool_boost.h"

#ifdef Q_OS_LINUX
    #include <signal.h>
#endif

ExtraChainNodeWrapper::ExtraChainNodeWrapper(QObject* parent, bool isRaccoonCheck)
    : QObject(parent)
    , node(new ExtraChainNode(isRaccoonCheck)) {
#ifdef Q_OS_LINUX
    signal(SIGPIPE, SIG_IGN);
#endif
}

ExtraChainNodeWrapper::~ExtraChainNodeWrapper() {
    eLog("ExtraChainNodeWrapper::~ExtraChainNodeWrapper");
    node_enabled.store(false);
    eLog("Set node_enabled to {}", node_enabled);

    if (m_thread) {
        m_thread->quit();
        m_thread->wait();
        node->deleteLater();
    } else {
        delete node;
    }
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

ExtraChainNode::ExtraChainNode(bool isRaccoonCheck)
    : isRaccoon(isRaccoonCheck) {
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
#ifndef RACCOON_CLIENT_CONSOLE
    Logger::instance().set_debug(true);
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

#ifdef Q_OS_LINUX
    signal(SIGPIPE, SIG_IGN);
#endif

    ThreadPoolBoost::instance_dfs(4);
    ThreadPoolBoost::instance(4);

    prepareFolders();
    actor_index_          = new ActorIndex(this);
    account_controller_   = new AccountController(this);
    network_manager_      = new NetworkManager(this);
    dag_                  = new Dag(this);
    dfs_                  = new DfsController(this);
    mining_manager_       = new DataMiningManager(this);
    token_manager_        = new TokenManager(this);
    subscription_manager_ = new SubscriptionManager(this);
    chat_manager_         = new ChatManager(this);

    // auto key             = actorIndex()->network_id().toQByteArray();
    // auto address         = "12.12.12.12";
    // auto port            = "1212";

    // auto thread = ThreadPool::addThread(m_blockchain);
    // ThreadPool::addThread(m_transactionManager, thread);

    timer_all_actors_ = new QTimer(this);
    connect(timer_all_actors_, &QTimer::timeout, this, &ExtraChainNode::getAllActorsTimerCall);
    // timer_all_actors_->start(30000);

    timer_reward_ = new QTimer(this);
    connect(timer_reward_, &QTimer::timeout, this, &ExtraChainNode::timer_reward_request);
    timer_reward_->start(MINING_TIMER_TICK);

    timer_info_ = new QTimer(this);
    connect(timer_info_, &QTimer::timeout, this, &ExtraChainNode::timer_info_print);
    timer_info_->start(10000);

    m_initPublicIPAndCountry = network_manager_->getPublicIPAndCountry();

    connectSignals();

    node_enabled = true;
    emit NodeInitialised();
}

ExtraChainNode::~ExtraChainNode() {
    node_enabled.store(false);
    eLog("ExtraChainNode::~ExtraChainNode");
    if (m_vpnClearFunc) {
        m_vpnClearFunc();
    }

    ThreadPoolBoost::terminate();
}

void ExtraChainNode::cleanUp() {
    delete dag_;
    network_manager_->deleteLater();
    // m_blockchain->deleteLater();
    // m_transactionManager->deleteLater();
    dfs_->deleteLater();
    delete chat_manager_;
}

bool ExtraChainNode::create_new_network(const std::string& login, const std::string& password) {
    if (!AccountController::profilesList().empty()) {
        eLog("Cannot create a new network: existing profile data found");
        return false;
    }

    eLog("[Node] Create network with login {}", login);
    auto consoleHash = Utils::calculate_hash(login + password);
    auto first       = account_controller_->createProfile(consoleHash, ActorType::DAppMaster);
    actor_index_->set_network_id(first.id());
    account_controller_->getProfile(first.id()).rename_wallet(first.id(), "King of the World");

    create_new_dag();

    eSuccess("[Node] New network created");
    return true;
}

bool ExtraChainNode::create_new_dag() {
    if (dag_->current_section() >= 0) {
        return false;
    }

    auto actor = account_controller_->system_actor();

    Transaction tx;
    tx.set_sender(actor.id());
    tx.set_receiver(actor.id());
    tx.set_type(TransactionType::Genesis);

    auto prepared_tx = dag_->prepare_transaction(tx, actor);
    if (!prepared_tx.has_value()) {
        eCritical("[Node] Can't prepare transaction for new network");
        std::exit(-10);
    }

    dag_->first_saved_section_ = BigNumber(0);
    auto save_result           = dag_->save_transaction(prepared_tx.value());
    if (!save_result) {
        eCritical("[Node] Can't save transaction for new network");
        std::exit(-11);
    }

    dag_->set_status(DagStatus::Ready);

    actor_index_->set_network_id(actor.id());

    return true;
}

bool ExtraChainNode::create_usernames_vector() {
    auto vector_template =
        Dfs::CollectionTemplate::create("Usernames").value().add_fields({ Dfs::Field::String("name").unique() });

    auto system_actor_id = accountController()->system_actor().id();
    auto template_res    = dfs()->store_template(system_actor_id, vector_template);
    if (!template_res.has_value()) {
        eCritical("Can't create usernames, because {}", template_res.error());
        return false;
    }

    auto first_id = actorIndex()->network_id();
    auto vec_res  = dfs()->store_vector(system_actor_id,
                                       system_actor_id,
                                       "Usernames",
                                       template_res->actor_id,
                                       template_res->file_id);
    if (!vec_res.has_value()) {
        return false;
    }

    return true;
}

bool ExtraChainNode::create_chat_templates() {
    auto system_actor_id   = accountController()->system_actor().id();
    auto my_chats_template = Dfs::CollectionTemplate::create(CHAT_MY_CHATS)
                                 .value()
                                 .use_id()
                                 .add_fields({ Dfs::Field::Json("chat").not_null(),
                                               Dfs::Field::ActorId("owner_id").not_null(),
                                               Dfs::Field::String("file_id").not_null(),
                                               Dfs::Field::String("chat_key").not_null() });

    auto my_chats_result = dfs()->store_template(system_actor_id, my_chats_template);
    if (!my_chats_result.has_value()) {
        eCritical("Can't create my chats template, because {}", my_chats_result.error());
        return false;
    } else {
        eLog("My chats template created");
    }

    auto chat_template = Dfs::CollectionTemplate::create("Chat").value().use_id().add_fields(
        { Dfs::Field::Json("message").not_null() });

    auto chat_result = dfs()->store_template(system_actor_id, chat_template);
    if (!chat_result.has_value()) {
        eCritical("Can't create chat template, because {}", chat_result.error());
        return false;
    } else {
        eLog("Chats template created");
    }

    return true;
}

bool ExtraChainNode::create_token_template() {
    auto network_id      = actor_index_->network_id();
    auto tokens_template = Dfs::CollectionTemplate::create("TokensCache")
                               .value()
                               .add_fields({ Dfs::Field::ActorId("token_id").not_null().unique(),
                                             Dfs::Field::String("name").not_null().unique().length(3, 20),
                                             Dfs::Field::String("ticker").not_null().unique().length(2, 5),
                                             Dfs::Field::String("count").not_null(),
                                             Dfs::Field::ActorId("owner_id").not_null(),
                                             Dfs::Field::String("color").not_null(),
                                             Dfs::Field::String("smart"),
                                             Dfs::Field::String("section_id").not_null(),
                                             Dfs::Field::String("tx_hash").not_null() });

    auto template_res = dfs_->store_template(network_id, tokens_template);
    if (!template_res.has_value()) {
        eCritical("Can't create token cache database, because {}", template_res.error());
        return false;
    }

    return true;
}

bool ExtraChainNode::create_token_vector() {
    auto network_id = actorIndex()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(network_id,
                                                                  Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                  "TokensCache");
    if (!search_result.has_value()) {
        return false;
    }

    auto store_res = dfs_->store_vector(network_id, network_id, "TokensCache", network_id, search_result->file_id);
    if (!store_res.has_value()) {
        eCritical("Can't create token cache database, because {}", store_res.error());
        return false;
    }

    auto tokens_row = TokenData { .token_id   = TokenId(),
                                  .owner_id   = network_id,
                                  .name       = "ExtraCoin",
                                  .ticker     = "EXC",
                                  .count      = BigNumberFloat(0),
                                  .color      = "#808080",
                                  .smart      = "",
                                  .section_id = BigNumber(0),
                                  .tx_hash    = "" };

    auto res = dfs_->add_vector_row(store_res->actor_id, store_res->file_id, tokens_row);
    if (!res) {
        return false;
    }

    return true;
}

void ExtraChainNode::start() {
    if (!started_) {
        QTimer::singleShot(10, this, &ExtraChainNode::ready);
        // emit startNetwork();
        started_ = true;

        // emit m_blockchain->transaction_cache().make_cache();
    }

    // Version compatibility: 0.17.0 (temp)
#ifdef IS_RC
    QThreadPool::globalInstance()->start([this]() {
        auto system_id     = account_controller_->system_actor().id();
        auto main_id       = account_controller_->currentProfile().main_id();
        auto data_security = Dfs::DataSecuritySelf { .my_actor = main_id };

        auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(system_id);

        if (!dir_rows.has_value()) {
            return;
        }

        for (const auto& row : dir_rows.value()) {
            auto file_path = Dfs::Path::file_path(system_id, row.file_id);
            if (!file_path.has_value()) {
                continue;
            }

            auto store = dfs_->store_file(main_id,
                                          main_id,
                                          file_path->native(),
                                          row.folder.has_value() ? row.folder.value() : "",
                                          row.name,
                                          Dfs::DataSecurity::Self,
                                          data_security);

            if (store.has_value()) {
                auto removed_result = dfs_->remove_stored_file(system_id, row.file_id);
                if (!removed_result.has_value()) {
                    eCritical("REMOVE ERROR: {}", removed_result.error());
                }
            }
        }
    });
#endif

    // Version compatibility: 0.19.2 (temp)
#ifdef IS_RC
    QThreadPool::globalInstance()->start([this]() {
        auto main_id       = account_controller_->currentProfile().main_id();
        auto data_security = Dfs::DataSecuritySelf { .my_actor = main_id };

        auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(main_id);

        if (!dir_rows.has_value()) {
            return;
        }

        for (const auto& row : dir_rows.value()) {
            if (row.encryption || row.type != Dfs::FileType::File || row.state != Dfs::FileState::Ready) {
                continue;
            }

            auto file_path = Dfs::Path::file_path(main_id, row.file_id);
            if (!file_path.has_value()) {
                continue;
            }

            auto store = dfs_->store_file(main_id,
                                          main_id,
                                          file_path->native(),
                                          row.folder.has_value() ? row.folder.value() : "",
                                          row.name,
                                          Dfs::DataSecurity::Self,
                                          data_security);

            if (store.has_value()) {
                auto removed_result = dfs_->remove_stored_file(main_id, row.file_id);
                if (!removed_result.has_value()) {
                    eCritical("REMOVE ERROR: {}", removed_result.error());
                }
            }
        }
    });
#endif

    // Version compatibility: 0.20.0
    QThreadPool::globalInstance()->start([this]() {
        QDir("blocks").removeRecursively();
    });
}

Dag* ExtraChainNode::dag() {
    return dag_;
}

NetworkManager* ExtraChainNode::network() {
    return network_manager_;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransaction(Transaction tx) {
    if (tx.amount() <= 0) {
        eWarning("Can not create tx without amount {}", tx);
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (tx.is_empty() && !tx.is_burn()) {
        eWarning("Can not create: {}. Transaction is empty", tx);
        return std::unexpected(TransactionError::EmptyTransaction);
    }

    auto actor = account_controller_->currentWallet();
    if (actor.empty()) {
        eWarning("Can not create: {}. There no current user", tx);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    eLog("Attempting to create {} from user {}", tx, actor.id().to_string());

    // TODO: local check tx
    // // 1) set prev block id
    // auto lastRealBlock = m_blockchain->read_last_block();
    // if (!lastRealBlock.has_value() || (lastRealBlock.has_value() && lastRealBlock->isEmpty())) {
    //     eWarning("Can not create: {}. There is no last block in blockchain", tx);
    //     return std::unexpected(TransactionError::NoLastBlock);
    // }
    // tx.setPrevBlock(lastRealBlock->id());

    // // 2) check coin availability
    // if (blockchain()->calculate_actor_balance(actor.id(), tx.token()) < tx.amount()) {
    //     eWarning("Can not create: {}. There is not enough coins/tokens in wallet", tx);
    //     return std::unexpected(TransactionError::InsufficientFunds);
    // }

    // 3) sign transaction
    tx.sign(actor);
    eLog("[Transaction] Send {} to {}", tx.amount().to_string(NumeralBase::Dec), tx.receiver());

    return tx;
}

TokenManager* ExtraChainNode::token_manager() const {
    return token_manager_;
}

SubscriptionManager* ExtraChainNode::subscription_manager() const {
    return subscription_manager_;
}

ChatManager* ExtraChainNode::chat_manager() {
    return chat_manager_;
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransaction(ActorId        receiver,
                                                                               BigNumberFloat amount,
                                                                               ActorId        token) {
    auto actor = account_controller_->currentWallet();

    Transaction tx;
    tx.set_sender(actor.id());
    tx.set_receiver(receiver);
    tx.set_amount(amount);
    tx.set_token(token);

    if (actor.empty()) {
        eWarning("Can not create {}. There no current user", tx);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return this->createTransaction(tx);
}

std::expected<std::string, ImportError> ExtraChainNode::export_profile() {
    const auto& current_profile = account_controller_->currentProfile();
    auto        network_id      = actor_index_->network_id();
    if (network_id.is_zero()) {
        return std::unexpected(ImportError::NoNetworkId);
    }

    auto imported_user = ImportedUser { .network       = network_id,
                                        .version       = extrachain_version,
                                        .date          = Utils::current_date_ms(),
                                        .system        = current_profile.system().id(),
                                        .main          = current_profile.main()->get().id(),
                                        .actors        = current_profile.actors(),
                                        .imports       = current_profile.imports(),
                                        .wallet_names  = current_profile.wallet_names(),
                                        .creation_date = current_profile.creation_date(),
                                        .modified_date = current_profile.modified_date() };

    auto json = Json::serialize(imported_user);

    auto hash      = current_profile.hash();
    auto encrypted = Cryptography::symmetric_encrypt_password(ByteArray(json).toBytes(), hash);
    if (!encrypted.has_value()) {
        return std::unexpected(ImportError::CryptoError);
    }

    return ByteArray(encrypted.value()).toString();
}

std::expected<std::string, ImportProfileError> ExtraChainNode::import_profile(const std::string& data,
                                                                              const std::string& login,
                                                                              const std::string& password) {
    if (data.empty()) {
        return std::unexpected(ImportProfileError::DataEmpty);
    }

    auto login_password = login + password;
    if (login_password.empty()) {
        return std::unexpected(ImportProfileError::LoginPasswordEmpty);
    }

    auto hash = Utils::calculate_hash(login_password);
    auto json = Cryptography::symmetric_decrypt_password(ByteArray(data).toBytes(), hash);
    if (!json.has_value()) {
        return std::unexpected(ImportProfileError::DecryptError);
    }

    auto imported_user = Json::deserialize<ImportedUser>(json.value());
    if (!imported_user.has_value()) {
        return std::unexpected(ImportProfileError::IncorrectJson);
    }

    // TODO: network id check

    account_controller_->import_profile(imported_user.value(), hash);

    return hash;
}

ActorId ExtraChainNode::network_id() {
    return actor_index_->network_id();
}

std::expected<Transaction, TransactionError> ExtraChainNode::createTransactionFrom(ActorId        sender,
                                                                                   ActorId        receiver,
                                                                                   BigNumberFloat amount,
                                                                                   ActorId        token) {
    if (sender == ActorId()) { // TODO: remove hack
        sender = account_controller_->currentWallet().id();
    }

    auto actor = account_controller_->currentProfile().get_actor(sender);
    if (!actor.has_value()) {
        return std::unexpected(TransactionError::NoSender);
    }
    if (amount <= 0) {
        eWarning("Can not create tx without amount");
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (receiver.is_zero() && amount > 0) {
        if (!actor->get().empty()) {
            Transaction tx;
            tx.set_sender(actor->get().id());
            tx.set_receiver(receiver);
            tx.set_amount(amount);
            tx.set_token(token);

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
        Transaction tx;
        tx.set_sender(actor->get().id());
        tx.set_receiver(receiver);
        tx.set_amount(amount);
        tx.set_token(token);

        //        if (actorIndex->network_id() != nullptr)
        //            if (actor.getId() == BigNumber(*actorIndex->network_id()))
        //                tx.setSenderBalance(BigNumber(0));
        return this->createTransaction(tx);
    } else {
        eWarning("Can not create tx to '{}'. There no current user", receiver.toQByteArray());
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return std::unexpected(TransactionError::Unknown);
}

std::expected<Transaction, TransactionError> ExtraChainNode::send_transaction(const Transaction&       transaction,
                                                                              const Actor<KeyPrivate>& signer) {
    auto transaction_result = dag_->send_transaction(transaction, signer);
    return transaction_result;
}

std::string ExtraChainNode::transactionErrorDescription(const TransactionError& error) {
    switch (error) {
    case TransactionError::Unknown:
        return "Unknown error";
    case TransactionError::ZeroAmount:
        return "Can not create transaction without amount.";
    case TransactionError::EmptyTransaction:
        return "Can not create transaction. Transaction is empty.";
    case TransactionError::NoLastSection:
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
    return;
    if (account_controller_->count() > 0 && network_manager_->connections()->size() > 0) {
        ActorId actorId = account_controller_->system_actor().id();

        if (!actorId.is_zero())
            actor_index_->getAllActors(actorId, true);

        // m_dfs->download_manager().check_all_files("");
    }
}

void ExtraChainNode::timer_reward_request() {
    mining_manager()->requestCoinReward();
}

void ExtraChainNode::timer_info_print() {
    eLog("[Dag] {} (0x{}) sections, status: {}, last cache: {} (0x{})", //. Dfs: {:.2f} from {:.2f} KB",
         dag_->current_section().to_string(NumeralBase::Dec),
         dag_->current_section(),
         dag_->status(),
         dag_->cache().section().to_string(NumeralBase::Dec),
         dag_->cache().section()/*,
         m_dfs->sizeTaken() / 1024.0,
         m_dfs->totalDfsSize() / 1024.0*/);
}

std::string ExtraChainNode::generate_network_identifier() {
    std::string network_identifier =
        Utils::calculate_hash(std::to_string(QDateTime::currentSecsSinceEpoch())
                              + std::to_string(QRandomGenerator::global()->bounded(100000)));

    auto settings               = Utils::read_settings();
    settings.network_identifier = network_identifier;
    Utils::write_settings(settings);

    return network_identifier;
}

std::string ExtraChainNode::network_identifier() {
    auto settings = Utils::read_settings();

    if (!settings.network_identifier.has_value()) {
        auto new_network_identifier = generate_network_identifier();
        return new_network_identifier;
    }

    return settings.network_identifier.value();
}

void ExtraChainNode::notificationToken(QString os, QString actorId, QString token) {
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto network_id = actor_index_->network_id();
    if (network_id.is_zero())
        return;
    auto first = actor_index_->getActor(network_id);
    if (first.empty())
        return;
    auto& mainKey   = account_controller_->system_actor().key();
    auto& publicKey = first.key().public_key();

    // std::map<std::string, std::string> map = { { "actor", actorId.toStdString() },
    //                                            { "token", mainKey.encrypt(token.toStdString(),
    //                                            publicKey)
    //                                            }, { "os", mainKey.encrypt(os.toStdString(), publicKey)
    //                                            } };

    // TODONEW emit sendMsg(Serialization::serializeMap(map), Messages::GeneralRequest::Notification);
}

void ExtraChainNode::connectActorIndex() {
    // connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::dfsConnection() {
    // init dfs for user
    // connect(m_networkManager, &NetworkManager::addFragSignal, m_dfs, &DfsController::threadAddFragment);
    // connect(m_networkManager, &NetworkManager::fetchFragment, m_dfs, &DfsController::fetchFragment);
    connect(this, &ExtraChainNode::ready, network_manager_, &NetworkManager::startNetwork);
    // connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    // connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    // connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connectSignals() {
    connect(this, &ExtraChainNode::ready, []() {
        eInfo("Node successfully started");
    });

    //    connectAccountController();
    connectActorIndex();
    dfsConnection();

    connect(network_manager_, &NetworkManager::newSocketActivated, this, &ExtraChainNode::getAllActorsTimerCall);

    // temp for tests, maybe only for console
    connect(network_manager_, &NetworkManager::newSocketActivated, [this]() {
        emit readyInitLocalizationFiles();
        // m_dfs->requestDirFileAllActors();
        // m_dfs->requestSync();
    });

    connect(network_manager_,
            &NetworkManager::newSocketActivatedWithParams,
            [this](const std::string ip, const std::string identifier) {
                eLog("[WS] Start sync...");

                if (!actors_broadcast_.empty()) {
                    auto actors_broadcast = actors_broadcast_;
                    actors_broadcast_.clear();

                    for (const auto& actor : actors_broadcast) {
                        network()->send_broadcast(actor, MessageType::NewActor);
                    }
                }

                Responder responder(network_manager_);
                responder.add_identifier(identifier);
                actor_index_->send_system_actor(responder);

                network_manager_->sendFromCache();
                dag_->start_check();
                actor_index_->request_actors_hash(responder);
                dfs_->sync(identifier);
            });

    connect(network_manager_, &NetworkManager::newSocketActivated, [this]() {
        dfs_->sendSizeRequestMsg(account_controller_->system_actor().id());
    });

    // connect(m_blockchain, &Blockchain::selfTxRepeatableAdded, this, &ExtraChainNode::selfTxRepeatableAdded);
}

void ExtraChainNode::prepareFolders() {
    eLog("Preparing folders");
    eLog("Working directory: {}", QDir::currentPath());

    // Version compatibility: 0.15.0
    if (QDir("keystore").exists()) {
        QDir().rename("keystore", QString::fromStdString(Profiles::folder));
    }

    QDir().mkpath(QString::fromStdString(Profiles::folder));
    QDir().mkpath(QString::fromStdString(ChainConst::TMP_FOLDER));
    QDir().mkpath(QString::fromStdString(ChainConst::DAG_FOLDER));
    QDir().mkpath(QString::fromStdString(ChainConst::ACTORS_FOLDER));

    generate_network_identifier();
}

void ExtraChainNode::calculateBlockCount() {
    // ActorId              actorId = m_accountController->system_actor().id();
    // DfsP::RequestDfsSize msg { .actorId = actorId };

    // m_networkManager->send_message(msg,
    //                                MessageType::RequestBlockCount,
    //                                SendMode::Neighbours,
    //                                MessageStatus::Request);
}

AccountController* ExtraChainNode::accountController() const {
    return account_controller_;
}

ActorIndex* ExtraChainNode::actorIndex() const {
    return actor_index_;
}

DfsController* ExtraChainNode::dfs() const {
    return dfs_;
}

DataMiningManager* ExtraChainNode::mining_manager() const {
    return mining_manager_;
}

std::expected<void, LoadError> ExtraChainNode::login(const std::string& login, const std::string& password) {
    return account_controller_->load(Utils::calculate_hash(login + password));
}

std::expected<void, LoadError> ExtraChainNode::login(const std::string& hash) {
    return account_controller_->load(hash);
}

void ExtraChainNode::logout() {
    account_controller_->clear();
    // auto hash remove
    QCoreApplication::exit(0);
}

void ExtraChainNode::InitVPN(VpnFunctionClearType vpnClearFunc) {
    m_vpnClearFunc = vpnClearFunc;
}

std::pair<QString, QString> ExtraChainNode::getInitPublicIPAndCountry() const {
    return m_initPublicIPAndCountry;
}
