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

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    #include <malloc.h>
#endif

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
#include "managers/luminance_manager.h"
#include "managers/data_mining_manager.h"
// #include "managers/thread_pool.h"
#include "managers/token_manager.h"
#include "managers/thoth_manager.h"
#include "managers/janus_manager.h"
#include "managers/thread_pool.h"
#include "dfs/collection_template.h"
// #include "managers/restApiServerManager.h"
#include "network/network_manager.h"
#include "chat/chat_manager.h"
#include "utils/thread_pool_boost.h"

std::atomic<bool> node_enabled { true };

ExtraChainNodeWrapper::ExtraChainNodeWrapper(QObject* parent, bool is_client_application, bool is_custom_app, std::uint16_t ws_port)
    : QObject(parent)
    , node(new ExtraChainNode(is_client_application, is_custom_app, ws_port)) {
}

ExtraChainNodeWrapper::~ExtraChainNodeWrapper() {
    eLog("ExtraChainNodeWrapper::~ExtraChainNodeWrapper");
    node_enabled.store(false);
    eLog("Set node_enabled to {}", node_enabled);

    if (m_thread) {
        ThreadPoolBoost::terminate();
        m_thread->quit();
        m_thread->wait();
        node->deleteLater();
    } else {
        delete node;
    }
}

void ExtraChainNodeWrapper::init(bool makeAsync) {
    if (makeAsync) {
        m_thread = new QThread();
        node->moveToThread(m_thread);
        connect(m_thread, &QThread::started, node, &ExtraChainNode::process);
        connect(m_thread, &QThread::finished, node, &ExtraChainNode::cleanUp);
        connect(m_thread, &QThread::finished, m_thread, &QObject::deleteLater);
        m_thread->start();
    } else {
        node->process();
    }
}

ExtraChainNode::ExtraChainNode(bool is_client_application, bool is_custom_app, std::uint16_t port)
    : is_client_application_(is_client_application)
    , is_custom_app_(is_custom_app)
    , ws_port(port) {
    QNetworkInformation::loadBackendByFeatures(QNetworkInformation::Feature::Reachability);
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

    ThreadPoolBoost::instance_dfs(4);
    ThreadPoolBoost::instance(4);

    prepare_folders();
    actor_index_        = new ActorIndex(this);
    account_controller_ = new AccountController(this);
    luminance_manager_  = new LuminanceManager(this);
    network_manager_    = new NetworkManager(this, ws_port);
    dag_                = new Dag(this);
    dfs_                = new DfsController(this);
    dmm_                = new DataMiningManager(this);
    token_manager_      = new TokenManager(this);
    chat_manager_       = new ChatManager(this);
    thoth_manager_      = new ThothManager(this);
    janus_manager_      = new JanusManager(this);

    // auto key             = actorIndex()->network_id().toQByteArray();
    // auto address         = "12.12.12.12";
    // auto port            = "1212";

    // auto thread = ThreadPool::addThread(m_blockchain);

    timer_reward_ = new QTimer(this);
    connect(timer_reward_, &QTimer::timeout, this, &ExtraChainNode::timer_reward_request);
    timer_reward_->start(MINING_TIMER_TICK);

    timer_info_ = new QTimer(this);
    connect(timer_info_, &QTimer::timeout, this, &ExtraChainNode::timer_info_print);
    timer_info_->start(10000);

    timer_luminance_ = new QTimer(this);
    connect(timer_luminance_, &QTimer::timeout, this, &ExtraChainNode::timer_luminance_autoremove);
    timer_luminance_->start(30000);

    init_public_ip_and_country_ = network_manager_->search_public_ip_and_country_();

    connect_signals();

    node_enabled = true;
    emit nodeInitialised();
}

ExtraChainNode::~ExtraChainNode() {
    node_enabled.store(false);
    eLog("ExtraChainNode::~ExtraChainNode");
    if (cleanup_callback_) {
        cleanup_callback_();
    }
    // ThreadPoolBoost::terminate();
}

void ExtraChainNode::cleanUp() {
    delete dag_;
    network_manager_->deleteLater();
    dfs_->deleteLater();
    delete chat_manager_;
}

bool ExtraChainNode::create_new_network(const std::string& login, const std::string& password) {
    if (!AccountController::profiles_list().empty()) {
        eLog("Cannot create a new network: existing profile data found");
        return false;
    }

    eLog("[Node] Create network with login {}", login);
    auto consoleHash = Utils::calculate_hash(login + password);
    auto first       = account_controller_->create_profile(consoleHash, ActorType::DAppMaster);
    actor_index_->set_network_id(first.actors().front().id());
    // m_accountController->getProfile(first.id()).rename_wallet(first.id(), "King of the World");

    this->create_new_dag();

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

    auto prepared_tx = dag_->prepare_transaction(tx, actor, true);
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

    dag_->generate_hash();
    dag_->set_status(DagStatus::Ready);

    actor_index_->set_network_id(actor.id());

    return true;
}

bool ExtraChainNode::create_usernames_vector() {
    auto vector_template =
        Dfs::CollectionTemplate::create("Usernames").value().add_fields({ Dfs::Field::String("name").unique() });

    auto system_actor_id = account_controller()->system_actor().id();
    auto template_res    = dfs()->store_template(system_actor_id, vector_template);
    if (!template_res.has_value()) {
        eCritical("Can't create usernames, because {}", template_res.error());
        return false;
    }

    auto first_id = actor_index()->network_id();
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
    auto system_actor_id   = account_controller()->system_actor().id();
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

bool ExtraChainNode::create_subscription_template() {
    auto subscription_template = Dfs::CollectionTemplate::create("Subscription")
                                     .value()
                                     .add_fields({ Dfs::Field::Integer("type").not_null(),
                                                   Dfs::Field::Integer("date_start").not_null(),
                                                   Dfs::Field::Bool("auto_renew").not_null().between(0, 1),
                                                   Dfs::Field::String("section_id").not_null(),
                                                   Dfs::Field::String("transaction_hash").not_null() });

    auto system_actor_id = account_controller()->system_actor().id();
    auto template_res    = dfs()->store_template(system_actor_id, subscription_template);
    if (!template_res.has_value()) {
        eCritical("Can't create subscription template, because {}", template_res.error());
        return false;
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
    auto network_id = actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          network_id,
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

bool ExtraChainNode::create_token_allocations() {
    auto network_id = actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        dfs_->get_db_instance(), network_id, Dfs::Basic::TEMPLATE_DICTIONARY, "token_allocations");
    if (search_result.has_value()) {
        eLog("[Node] token_allocations dictionary already exists");
        return true;
    }

    auto dict_res = dfs_->store_dictionary(network_id, network_id, "token_allocations");
    if (!dict_res.has_value()) {
        eCritical("[Node] Can't create token_allocations dictionary: {}", dict_res.error());
        return false;
    }

    eSuccess("[Node] token_allocations dictionary created: {}", dict_res->file_id);
    return true;
}

void ExtraChainNode::backfill_token_allocations() {
    QThreadPool::globalInstance()->start([this]() {
        QThread::sleep(10);

        auto network_id = actor_index()->network_id();
        auto alloc_row  = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
            dfs_->get_db_instance(), network_id, Dfs::Basic::TEMPLATE_DICTIONARY, "token_allocations");
        if (!alloc_row.has_value()) {
            eWarning("[Node] token_allocations backfill: dictionary not found");
            return;
        }

        constexpr std::uint64_t       cutoff_ms = 1743458400000ULL; // 2026-04-01 00:00:00 UTC
        std::map<std::string, BigNumberFloat> totals;

        SectionId start_section = dag_->current_section();
        SectionId section_id    = start_section;
        SectionId min_section   = SectionId(BigNumber("a05133", NumeralBase::Hex));
        eLog("[Node] token_allocations backfill: starting from section {}", section_id);

        while (section_id >= min_section) {
            eLog("[Node] token_allocations backfill: section {}", section_id);

            auto section = dag_->read_section(section_id);
            if (!section.has_value()) {
                section_id = section_id - SectionId(1);
                continue;
            }

            if (!section->transactions.empty()) {
                eLog("[Node] token_allocations backfill: section {} middle={} cutoff={}", section_id, section->middle(), cutoff_ms);
                if (section->middle() < cutoff_ms) {
                    eLog("[Node] token_allocations backfill: reached cutoff at section {}", section_id);
                    break;
                }
            }

            for (const auto& tx : section->transactions) {
                if (tx.type() != TransactionType::Minting)
                    continue;
                std::string key = fmt::format("{}:{}", tx.receiver().to_string(), tx.token().to_string());
                totals[key] += tx.amount();
            }

            section_id = section_id - SectionId(1);
        }

        for (const auto& [key, amount] : totals) {
            dfs_->dictionary_set_value(network_id, alloc_row->file_id, key,
                                       amount.to_string(NumeralBase::Dec), network_id);
        }

        eSuccess("[Node] token_allocations backfill complete: {} entries", totals.size());
    });
}

bool ExtraChainNode::create_subscription_vector(const std::string& file_name) {
    auto network_id = actor_index()->network_id();
    if (network_id.is_zero()) {
        return false;
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "Subscription");
    if (!search_result.has_value()) {
        return false;
    }

    auto system_actor_id = account_controller()->system_actor().id();
    auto sub_res =
        dfs()->store_vector(system_actor_id, system_actor_id, file_name, network_id, search_result->file_id);
    if (!sub_res.has_value()) {
        return false;
    }

    return true;
}

bool ExtraChainNode::create_renames_template() {
    auto system_actor_id = account_controller()->system_actor().id();

    auto chat_template = Dfs::CollectionTemplate::create("Renames").value().use_id().add_fields(
        { Dfs::Field::Json("name").not_null() });

    auto chat_result = dfs()->store_template(system_actor_id, chat_template);
    if (!chat_result.has_value()) {
        eCritical("Can't create renames template, because {}", chat_result.error());
        return false;
    }

    eSuccess("Renames template created");
    return true;
}

DfsFileStatus ExtraChainNode::create_renames_vector() {
    auto row = this->dfs()->read_file_status_self("Renames");
    if (row.has_value()) {
        return DfsFileStatus::Existed;
    }

    const auto main_actor_id = this->account_controller()->current_profile().main_id();
    auto       network_id    = this->network_id();
    if (network_id.is_zero()) {
        return DfsFileStatus::CantCreate;
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          "Renames");
    if (!search_result.has_value()) {
        return DfsFileStatus::CantCreate;
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = main_actor_id };
    auto store_chat_res = this->dfs()->store_vector(main_actor_id,
                                                    main_actor_id,
                                                    "Renames",
                                                    network_id,
                                                    search_result->file_id,
                                                    Dfs::DataSecurity::Self,
                                                    security_actor);

    if (!store_chat_res.has_value()) {
        return DfsFileStatus::CantCreate;
    }

    return DfsFileStatus::Created;
}

bool ExtraChainNode::create_file_id_template(Dfs::FileIdState with_state) {
    auto        system_actor_id = account_controller_->system_actor().id();
    std::string template_name   = with_state == Dfs::FileIdState::With ? "FilesListState" : "FilesList";
    auto        template_obj    = Dfs::CollectionTemplate::create(template_name).value().use_id();
    template_obj.add_fields({ Dfs::Field::Blob("owner").not_null(), Dfs::Field::Blob("file_id").not_null() });

    if (with_state == Dfs::FileIdState::With) {
        template_obj.add_fields({ Dfs::Field::Integer("state") });
    }

    auto template_res = dfs_->store_template(system_actor_id, template_obj);
    if (!template_res.has_value()) {
        eCritical("Can't create file id template, because {}", template_res.error());
        return false;
    }

    return true;
}

bool ExtraChainNode::create_file_id_vector(const std::string& vector_name, Dfs::FileIdState with_state) {
    auto network_id = actor_index_->network_id();
    auto system_id  = account_controller_->system_actor().id();

    if (network_id.is_zero() || system_id.is_zero()) {
        return false;
    }

    auto existing_vector =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          system_id,
                                                                          Dfs::Basic::TEMPLATE_VECTOR,
                                                                          vector_name);
    if (existing_vector.has_value()) {
        return true;
    }

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                                                                          with_state == Dfs::FileIdState::With
                                                                              ? "FilesListState"
                                                                              : "FilesList");
    if (!search_result.has_value()) {
        return false;
    }

    auto store_res = dfs_->store_vector(system_id, system_id, vector_name, network_id, search_result->file_id);

    if (!store_res.has_value()) {
        eCritical("Can't create file id '{}' vector, because {}", vector_name, store_res.error());
        return false;
    }

    return true;
}

DfsFileStatus ExtraChainNode::create_channels_vector() {
    auto system_id = account_controller_->system_actor().id();
    if (system_id.is_zero()) {
        return DfsFileStatus::CantCreate;
    }

    auto existing_vector =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          system_id,
                                                                          Dfs::Basic::TEMPLATE_VECTOR,
                                                                          CHANNELS_VECTOR_NAME);
    if (existing_vector.has_value()) {
        return DfsFileStatus::Existed;
    }

    if (!create_file_id_vector(CHANNELS_VECTOR_NAME, Dfs::FileIdState::Without)) {
        return DfsFileStatus::CantCreate;
    }

    return DfsFileStatus::Created;
}

bool ExtraChainNode::write_actor_rename(const ActorId& actor_id, const std::string& name) {
    if (this->account_controller()->profile_type() != ProfileType::New) {
        bool res = this->account_controller()->rename_wallet(this->account_controller()->system_actor().id(),
                                                             actor_id,
                                                             name);
        return res;
    }

    auto row = this->dfs()->read_file_status_self("Renames");
    if (!row.has_value()) {
        auto res = this->create_renames_vector();

        if (res != DfsFileStatus::Created) {
            return false;
        } else {
            return write_actor_rename(actor_id, name);
        }
    }

    if (row->state != Dfs::FileState::Ready) {
        this->dfs()->add_to_waiting_file(actor_id, row->file_id);
        renames_file_id_waiting_ = row->file_id;
    }

    auto main_id = account_controller_->current_profile().main_id();

    if (name.empty()) {
        // TODO: add remove. Need to search for actor, scan and remove
        // this->dfs()->remove_vector_row(main_id, row->file_id);
        emit this->actorRenamed(actor_id, name);
    } else {
        auto  security_actor = Dfs::DataSecuritySelf { .my_actor = main_id };
        DbRow db_row         = { { "id", actor_id.value() }, { "name", name } };

        bool res = this->dfs()->add_vector_row(main_id, row->file_id, db_row, main_id, security_actor);
        if (!res) {
            return false;
        }

        emit this->actorRenamed(actor_id, name);
    }

    return true;
}

std::vector<std::pair<ActorId, std::string>> ExtraChainNode::read_actor_renames() {
    auto row     = this->dfs()->read_file_status_self("Renames");
    auto main_id = account_controller_->current_profile().main_id();

    if (!row.has_value()) {
        return {};
    }

    if (row->state != Dfs::FileState::Ready) {
        this->dfs()->add_to_waiting_file(main_id, row->file_id);
        renames_file_id_waiting_ = row->file_id;
        return {};
    }

    auto security_actor = Dfs::DataSecuritySelf { .my_actor = main_id };
    auto actors         = this->dfs()->read_vector_rows(main_id, row->file_id, "", security_actor);

    if (!actors.has_value()) {
        return {};
    }

    std::vector<std::pair<ActorId, std::string>> renames;
    for (const auto& row : actors.value()) {
        if (row.find("id") == row.end() || row.find("name") == row.end()) {
            continue;
        }

        auto actor = ActorId::create(row.at("id"));
        if (!actor.has_value()) {
            continue;
        }

        renames.push_back(std::make_pair(actor.value(), row.at("name")));
    }

    return renames;
}

void ExtraChainNode::start() {
    if (!started_) {
        QTimer::singleShot(10, this, &ExtraChainNode::ready);
        // emit startNetwork();
        started_ = true;

        // emit m_blockchain->transaction_cache().make_cache();
    }

    // Version compatibility: 0.17.0 (temp)
#ifdef IS_APP_UI_CLIENT
    QThreadPool::globalInstance()->start([this]() {
        auto system_id     = account_controller_->system_actor().id();
        auto main_id       = account_controller_->current_profile().main_id();
        auto data_security = Dfs::DataSecuritySelf { .my_actor = main_id };

        auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(dfs_->get_db_instance(), system_id);

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
#ifdef IS_APP_UI_CLIENT
    QThreadPool::globalInstance()->start([this]() {
        auto main_id       = account_controller_->current_profile().main_id();
        auto data_security = Dfs::DataSecuritySelf { .my_actor = main_id };

        auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(dfs_->get_db_instance(), main_id);

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

bool ExtraChainNode::is_client_application() const {
    return is_client_application_;
}

Dag* ExtraChainNode::dag() const {
    return dag_;
}

NetworkManager* ExtraChainNode::network() const {
    return network_manager_;
}

LuminanceManager* ExtraChainNode::luminance_manager() const {
    return luminance_manager_;
}

std::expected<Transaction, TransactionError> ExtraChainNode::create_transaction(Transaction tx) {
    if (tx.amount() <= 0) {
        eWarning("Can not create tx without amount {}", tx);
        return std::unexpected(TransactionError::ZeroAmount);
    }

    if (tx.is_empty() && !tx.is_burn()) {
        eWarning("Can not create: {}. Transaction is empty", tx);
        return std::unexpected(TransactionError::EmptyTransaction);
    }

    auto actor = account_controller_->current_wallet();
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

ChatManager* ExtraChainNode::chat_manager() {
    return chat_manager_;
}

ThothManager* ExtraChainNode::thoth_manager() {
    return thoth_manager_;
}

JanusManager* ExtraChainNode::janus_manager() {
    return janus_manager_;
}

std::expected<Transaction, TransactionError> ExtraChainNode::add_subscription(const ActorId&     owner_id,
                                      const std::string& file_id,
                                      int                type,
                                      bool               auto_renew,
                                      const TokenId&     token_id) {
    if (subscription_row_.has_value()) {
        return std::unexpected(TransactionError::SubscriptionRowFull);
    }

    ActorId system_id = account_controller_->system_actor().id();

    Transaction transaction;
    transaction.set_sender(system_id);
    transaction.set_receiver(owner_id);
    transaction.set_amount(BigNumberFloat("500", NumeralBase::Dec));
#ifdef QT_DEBUG
    transaction.set_amount(BigNumberFloat("0.112", NumeralBase::Dec));
#endif
    transaction.set_token(token_id); // TODO: get token_id from json
    transaction.set_meta(std::to_string(type));
    transaction.set_type(TransactionType::Repeatable);

    auto res = this->send_transaction(transaction, account_controller_->system_actor());
    if (!res.has_value()) {
        return res;
    }
    // transaction.setHash()

    auto row =
        SubscriptionRow { .owner_id = owner_id, .file_id = file_id, .type = type, .auto_renew = auto_renew };
    subscription_row_ = row;
    return res;
}

void ExtraChainNode::selfTxRepeatableAdded(const Transaction& transaction) {
    if (!subscription_row_.has_value()) {
        return;
    }

    ActorId system_id = account_controller_->system_actor().id();
    if (transaction.sender() != system_id) {
        return;
    }

    auto row = subscription_row_.value();

    row.section_id       = transaction.section();
    row.date_start       = transaction.timestamp();
    row.transaction_hash = transaction.hash();

    auto row_map = Utils::to_dbrow(row);

    // temp for old vector
    auto section = row_map["section_id"];
    row_map.erase("section_id");
    row_map.insert({ "block_id", section });

    auto res = dfs()->add_vector_row(row.owner_id, row.file_id, row_map, system_id);

    if (res) {
        emit subscriptionAdded(row.owner_id, row.file_id);
    }
}

std::expected<Transaction, TransactionError> ExtraChainNode::create_transaction(ActorId        receiver,
                                                                                BigNumberFloat amount,
                                                                                ActorId        token) {
    auto actor = account_controller_->current_wallet();

    Transaction tx;
    tx.set_sender(actor.id());
    tx.set_receiver(receiver);
    tx.set_amount(amount);
    tx.set_token(token);

    if (actor.empty()) {
        eWarning("Can not create {}. There no current user", tx);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return this->create_transaction(tx);
}

std::expected<std::string, ImportError> ExtraChainNode::export_profile() {
    if (account_controller_->profile_type() == ProfileType::New) {
        QFile file(account_controller_->profile_seed.filename().c_str());
        if (!file.open(QFile::ReadOnly)) {
            return std::unexpected(ImportError::FileError);
        }
        return file.readAll().toStdString();
    }

    const auto& current_profile = account_controller_->current_profile();

    auto imported_user = ImportedUser { .version       = extrachain_version,
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

    if (data.size() < 100) {
        auto decrypted = Cryptography::symmetric_decrypt_password(ByteArray(data).toBytes(), hash, true);
        if (!decrypted.has_value()) {
            return std::unexpected(ImportProfileError::DecryptError);
        }

        account_controller_->import_seed(login, password, ByteArray(decrypted.value()).toArray<32>());
        return hash;
    }

    auto json = Cryptography::symmetric_decrypt_password(ByteArray(data).toBytes(), hash, false);
    if (!json.has_value()) {
        return std::unexpected(ImportProfileError::DecryptError);
    }

    auto imported_user = Json::deserialize<ImportedUser>(json.value());
    if (!imported_user.has_value()) {
        return std::unexpected(ImportProfileError::IncorrectJson);
    }

    eLog("imported_user", imported_user.value());

    account_controller_->import_old_profile(imported_user.value(), hash);
    return hash;
}

std::string get_import_error_message(ImportProfileError error) {
    switch (error) {
    case ImportProfileError::DecryptError:
        return "The username or password entered is incorrect. Please try again";
    case ImportProfileError::DataEmpty:
        return "Import data is empty";
    case ImportProfileError::LoginPasswordEmpty:
        return "Login and password is empty";
    case ImportProfileError::IncorrectJson:
        return "Json data is empty";
    default:
        return "Unknown import error";
    }
}

std::expected<std::string, ImportProfileFileError> ExtraChainNode::import_profile_file(
    const std::string& file_path,
    const std::string& login,
    const std::string& password) {
    eLog("Importing profile from file: {}", file_path);

    if (login.empty() || password.empty()) {
        return std::unexpected(ImportProfileFileError::LoginPasswordEmpty);
    }

    QFile file(QString::fromStdString(file_path));
    if (!file.open(QIODevice::ReadOnly)) {
        eInfo("Import operation failed: unable to open file {}", file_path);
        return std::unexpected(ImportProfileFileError::FileNotFound);
    }

    QByteArray file_content = file.readAll();
    file.close();

    if (file_content.isEmpty()) {
        eInfo("Import operation failed: file is empty");
        return std::unexpected(ImportProfileFileError::FileEmpty);
    }

    std::string file_content_str = file_content.toStdString();
    auto        from_base64      = Utils::from_base64(file_content_str);
    if (!from_base64.has_value()) {
        eInfo("Import operation failed: base64 decode error");
        return std::unexpected(ImportProfileFileError::Base64DecodeError);
    }

    auto hash_result = import_profile(from_base64.value(), login, password);
    if (!hash_result.has_value()) {
        eInfo("Import operation failed: {}", get_import_error_message(hash_result.error()));
        return std::unexpected(ImportProfileFileError::ImportError);
    }

    eInfo("Profile successfully imported!");
    return hash_result.value();
}

ActorId ExtraChainNode::network_id() {
    return actor_index_->network_id();
}

std::expected<Transaction, TransactionError> ExtraChainNode::create_transaction_from(ActorId        sender,
                                                                                     ActorId        receiver,
                                                                                     BigNumberFloat amount,
                                                                                     ActorId        token) {
    if (sender == ActorId()) { // TODO: remove hack
        sender = account_controller_->current_wallet().id();
    }

    auto actor = account_controller_->current_profile().get_actor(sender);
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
            auto createdTx = this->create_transaction(tx);
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
        return this->create_transaction(tx);
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

std::string ExtraChainNode::transaction_error_description(const TransactionError& error) {
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

void ExtraChainNode::timer_reward_request() {
    data_mining_manager()->request_reward();
}

void ExtraChainNode::start_mining() {
    dag_->force_full_mode();
    if (timer_reward_ && !timer_reward_->isActive()) {
        timer_reward_->start(MINING_TIMER_TICK);
    }
    eLog("[Mining] Started");
}

void ExtraChainNode::stop_mining() {
    if (timer_reward_ && timer_reward_->isActive()) {
        timer_reward_->stop();
    }
    dag_->force_light_mode();
    eLog("[Mining] Stopped");
}

void ExtraChainNode::timer_luminance_autoremove() {
    luminance_manager_->remove_old();
}

void ExtraChainNode::timer_info_print() {
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    malloc_trim(0);
#endif

    eLog("[Dag] Current: {} (0x{}) section, status: {}, last cache: {} (0x{})", //. Dfs: {:.2f} from {:.2f} KB",
         dag_->current_section().to_string(NumeralBase::Dec),
         dag_->current_section(),
         dag_->status(),
         dag_->cache().section().to_string(NumeralBase::Dec),
         dag_->cache().section()/*,
         m_dfs->sizeTaken() / 1024.0,
         m_dfs->totalDfsSize() / 1024.0*/);

#ifndef IS_APP_CLIENT
#ifdef Q_OS_LINUX
    {
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            long pages = 0;
            statm >> pages; // total
            statm >> pages; // RSS
            long rss_mb = pages * sysconf(_SC_PAGESIZE) / (1024 * 1024);
            long queue_total = 0;
            long bytes_to_write_total = 0;
            {
                auto conns = *network_manager_->connections();
                for (const auto &s : *conns) {
                    queue_total += s->queue_size();
                    bytes_to_write_total += s->pending_bytes();
                }
            }

            eLog("[Mem] RSS: {} MB | msg_hash: {} messages: {} forwarded: {} "
                 "snd_tx: {} fail_tx: {} last_tx: {} cached_tx: {} "
                 "conn: {}/{} queue: {} pending_kb: {} dfs_dl: {}",
                 rss_mb,
                 network_manager_->msg_hash_list_size(),
                 network_manager_->messages_size(),
                 network_manager_->forwarded_messages_size(),
                 dag_->sended_transactions_size(),
                 dag_->failed_transactions_size(),
                 dag_->last_txs_size(),
                 dag_->cached_txs_size(),
                 network_manager_->active_connections_count(),
                 network_manager_->connections_size(),
                 queue_total,
                 bytes_to_write_total / 1024,
                 dfs_->load_manager_downloads_size());
        }
    }
#endif
#endif

    if (dag_->current_section_ >= 0 && dag_->status() == DagStatus::Ready
        && !dag_->read_section(dag_->current_section()).has_value()) {
        eCritical("[Dag] No physical section");
    }
}

void ExtraChainNode::selfTxInitContractAdded(const Transaction& transaction) {
    token_manager_->final_token_creation(transaction);
}

std::string ExtraChainNode::generate_node_identifier() {
    std::string node_identifier =
        Utils::calculate_hash(std::to_string(QDateTime::currentSecsSinceEpoch())
                              + std::to_string(QRandomGenerator::global()->bounded(100000)));

    auto settings            = Utils::read_settings();
    settings.node_identifier = node_identifier;
    Utils::write_settings(settings);
    node_identifier_ = node_identifier;

    return node_identifier;
}

std::string ExtraChainNode::node_identifier() {
    if (!node_identifier_.empty()) {
        return node_identifier_;
    }

    auto settings = Utils::read_settings();

    if (!settings.node_identifier.has_value()) {
        auto new_node_identifier = this->generate_node_identifier();
        return new_node_identifier;
    }

    node_identifier_ = settings.node_identifier.value();
    return node_identifier_;
}

void ExtraChainNode::notificationToken(QString os, QString actorId, QString token) {
    if (os.isEmpty() || actorId.isEmpty() || token.isEmpty())
        return;
    auto network_id = actor_index_->network_id();
    if (network_id.is_zero())
        return;
    auto first = actor_index_->read_actor_old(network_id);
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

void ExtraChainNode::connect_actor_index() {
    // connect(m_actorIndex, &ActorIndex::sendMessage, m_resolveManager, &ResolveManager::registrateMsg);
}

void ExtraChainNode::connect_dfs() {
    // init dfs for user
    // connect(m_networkManager, &NetworkManager::addFragSignal, m_dfs, &DfsController::threadAddFragment);
    // connect(m_networkManager, &NetworkManager::fetchFragment, m_dfs, &DfsController::fetchFragment);
    connect(this, &ExtraChainNode::ready, network_manager_, &NetworkManager::start_network);
    // connect(this, &ExtraChainNode::ready, m_dfs, &Dfs::startDFS);
    // connect(m_accountController, &AccountController::initDfs, m_dfs, &Dfs::initMyLocalStorage);
    // connect(m_actorIndex, &ActorIndex::initDfs, m_dfs, &Dfs::initUser);
    //    connect(chatManger, &ChatManager::sendDataToBlockhainFromChatManager, dfs, &Dfs::savedNewData);
    //    connect(networkManager, &NetworkManager::newDfsSocket, dfsNetworkManager,
    //    &DfsNetworkManager::appendSocket);
}

void ExtraChainNode::connect_signals() {
    connect(this, &ExtraChainNode::ready, [this]() {
        // dag_->start_control(Force::None);
        eInfo("Your node successfully started");
    });

    //    connectAccountController();
    connect_actor_index();
    connect_dfs();

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

                actor_index_->request_actors_hash(responder);

                if (!actor_index_->is_prepare()) {
                    identifiers_after_actors_sync_.insert({ ip, identifier });
                    return;
                }

#ifdef IS_APP_CLIENT
                if (ip == network_manager_->first_node()) {
                    dag_->start_check();
                }
#else
                dag_->start_check();
#endif

                dfs_->sync(identifier);
            });

    connect(network_manager_, &NetworkManager::newSocketActivated, [this]() {
        dfs_->sendSizeRequestMsg(account_controller_->system_actor().id());
    });

    connect(actor_index_, &ActorIndex::firstSyncEnded, [this]() {
        chat_manager_->activate();

        dag_->start_check();

        for (const auto& [ip, identifier] : identifiers_after_actors_sync_) {
            dfs_->sync(identifier);
        }
    });

    // connect(m_blockchain, &Blockchain::selfTxRepeatableAdded, this, &ExtraChainNode::selfTxRepeatableAdded);

    connect(this, &ExtraChainNode::dagTimerStart, this, &ExtraChainNode::dagTimerStarting, Qt::QueuedConnection);
    connect(this, &ExtraChainNode::dagTimerStop, this, &ExtraChainNode::dagTimerStoping, Qt::QueuedConnection);
    connect(dag_->timer_sync_, &QTimer::timeout, this, &ExtraChainNode::dagTimerTick, Qt::QueuedConnection);

    connect(dfs_, &DfsController::waitDownloaded, [this](ActorId actor_id, Dfs::DirRow dir_row) {
        if (dir_row.file_id == renames_file_id_waiting_) {
            emit actorRenamedLoaded();

            for (const auto& [actor_id, name] : renames_todo_) {
                this->write_actor_rename(actor_id, name);
            }
        }
    });
}

void ExtraChainNode::prepare_folders() {
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

    this->generate_node_identifier();
}

void ExtraChainNode::calculateBlockCount() {
    // ActorId              actorId = m_accountController->system_actor().id();
    // DfsP::RequestDfsSize msg { .actorId = actorId };

    // m_networkManager->send_message(msg,
    //                                MessageType::RequestBlockCount,
    //                                SendMode::Neighbours,
    //                                MessageStatus::Request);
}

AccountController* ExtraChainNode::account_controller() const {
    return account_controller_;
}

ActorIndex* ExtraChainNode::actor_index() const {
    return actor_index_;
}

DfsController* ExtraChainNode::dfs() const {
    return dfs_;
}

DataMiningManager* ExtraChainNode::data_mining_manager() const {
    return dmm_;
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

std::pair<QString, QString> ExtraChainNode::init_public_ip_and_country() const {
    return init_public_ip_and_country_;
}

void ExtraChainNode::set_cleanup_callback(std::function<void()> callback) {
    cleanup_callback_ = callback;
}

void ExtraChainNode::dagTimerStarting(int ms) {
    // eLog("[Dag] Timer start, {} ms", ms);
    dag_->timer_sync_->stop();
    dag_->timer_sync_->start(ms);
}

void ExtraChainNode::dagTimerStoping() {
    // eLog("[Dag] Timer stop");
    dag_->timer_sync_->stop();
}

void ExtraChainNode::dagTimerTick() {
    dag_->timer_tick();
}
