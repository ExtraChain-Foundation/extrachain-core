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

#include <algorithm>
#include <array>
#include <chrono>

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    #include <malloc.h>
#endif

#include <QJsonObject>
#include <QFile>
#include <QThread>
#include <msgpack.hpp>
#include <sodium/core.h>

#include "chain/dag.h"
#include "chain/dag_migration.h"
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
#include "dfs/collection_template.h"
// #include "managers/restApiServerManager.h"
#include "network/network_manager.h"
#include "chat/chat_manager.h"
#include "utils/thread_pool_boost.h"
#include "contracts/contract_manager.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_hash.h"
#include "contracts/contract_module.h"
#include "contracts/standard_token.h"
#include "contracts/contract_transaction.h"
#include "contracts/dfs_contract_storage.h"
#include "contracts/toolchain_registry.h"
#include "utils/exc_utils.h"

std::atomic<bool> node_enabled { true };

static void initialize_contract_resources() {
    Q_INIT_RESOURCE(contracts);
}

namespace {
    std::string contract_hash_prefix(std::string_view value) {
        return std::string(value.substr(0, std::min<std::size_t>(value.size(), 12)));
    }

    std::vector<ContractTransitionData> contract_transitions(
        const ExtraChain::Contracts::PreparedContractChange& change) {
        std::vector<ContractTransitionData> result;
        const auto                          append = [&](const auto&                                          self,
                                const ExtraChain::Contracts::PreparedContractChange& parent) -> void {
            std::size_t child_index = 0;
            for (const auto& effect : parent.output.effects) {
                if (effect.kind != ExtraChain::Contracts::ContractEffectKind::ContractCall) {
                    continue;
                }
                if (child_index >= parent.children.size()) {
                    return;
                }
                const auto& child = parent.children[child_index++];
                const auto& version = child.record.versions.at(child.record.active_version - 1);
                const auto& revision = version.revisions.back();
                result.push_back(ContractTransitionData {
                                             .contract_id         = child.record.contract_id,
                                             .caller_contract_id  = parent.record.contract_id,
                                             .kind                = child.record.kind,
                                             .language            = child.record.language,
                                             .method              = effect.operation,
                                             .arguments_base64    = Utils::to_base64(effect.arguments),
                                             .module_hash         = version.module_hash,
                                             .previous_state_hash = revision.previous_hash,
                                             .state_hash          = revision.state_hash,
                                             .effects_hash = ExtraChain::Contracts::Codec::effect_hash(child.output.effects),
                                             .effects_base64 =
                        Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(child.output.effects)),
                                             .version             = version.version,
                                             .revision            = revision.revision,
                                             .checkpoint          = child.checkpoint,
                                             .checkpoint_revision = revision.checkpoint_revision,
                });
                self(self, child);
            }
        };
        append(append, change);
        return result;
    }

    std::expected<ExtraChain::Contracts::VerifiedInputs, ExtraChain::Contracts::ContractFailure>
    verify_contract_inputs(Dag*                                         dag,
                           DfsController*                               dfs,
                           const ExtraChain::Contracts::VerifiedInputs& requested,
                           std::uint64_t                                block) {
        using namespace ExtraChain::Contracts;
        if (dag == nullptr || dfs == nullptr
            || requested.dag.size() + requested.dfs.size() > ContractMaximumProofs) {
            return std::unexpected(ContractFailure {
                .error  = ContractError::TooManyProofs,
                .detail = "Contract proof input is not valid",
            });
        }
        VerifiedInputs                  result;
        std::unordered_set<std::string> dag_ids;
        std::unordered_set<std::string> dfs_ids;
        for (const auto& proof : requested.dag) {
            if (proof.transaction_hash.empty() || proof.section > block
                || !dag_ids.insert(proof.transaction_hash).second
                || !dag->find_transaction(SectionId(proof.section), proof.transaction_hash).has_value()) {
                return std::unexpected(ContractFailure {
                    .error  = ContractError::InvalidProof,
                    .detail = "A DAG proof is not valid",
                });
            }
            const auto confirmations = block - proof.section + 1;
            if (confirmations < std::max<std::uint64_t>(1, proof.confirmations)) {
                return std::unexpected(ContractFailure {
                    .error  = ContractError::InvalidProof,
                    .detail = "A DAG proof does not have enough confirmations",
                });
            }
            result.dag.push_back(DagProof {
                .transaction_hash = proof.transaction_hash,
                .section          = proof.section,
                .confirmations    = confirmations,
            });
        }
        for (const auto& proof : requested.dfs) {
            const auto owner = ActorId::create(proof.owner_id);
            const auto key   = proof.owner_id + ':' + proof.file_id;
            if (!owner.has_value() || proof.file_id.empty() || !dfs_ids.insert(key).second) {
                return std::unexpected(ContractFailure {
                    .error  = ContractError::InvalidProof,
                    .detail = "A DFS proof is not valid",
                });
            }
            const auto row =
                Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dfs->get_db_instance(), *owner, proof.file_id);
            if (!row.has_value() || row->state != Dfs::FileState::Ready
                || (!proof.content_hash.empty() && proof.content_hash != row->hash)) {
                return std::unexpected(ContractFailure {
                    .error  = ContractError::InvalidProof,
                    .detail = "A DFS proof is not ready or has a different hash",
                });
            }
            result.dfs.push_back(DfsProof {
                .file_id      = row->file_id,
                .owner_id     = row->owner_id.to_string(),
                .content_hash = row->hash,
            });
        }
        return result;
    }

    bool verify_dfs_effect_authors(DfsController*                                       dfs,
                                   const ExtraChain::Contracts::PreparedContractChange& change,
                                   const ActorId&                                       sender) {
        for (const auto& effect : change.output.effects) {
            if (effect.kind != ExtraChain::Contracts::ContractEffectKind::DfsWrite
                || effect.operation == "tombstone") {
                continue;
            }
            try {
                if (dfs == nullptr) {
                    return false;
                }
                std::size_t offset = 0;
                auto        handle = msgpack::unpack(reinterpret_cast<const char*>(effect.arguments.data()),
                                              effect.arguments.size(),
                                              offset);
                std::tuple<std::string, std::string, std::string, std::string> binding;
                handle.get().convert(binding);
                const auto  owner   = ActorId::create(effect.target);
                const auto& file_id = std::get<1>(binding);
                if (!owner.has_value()) {
                    return false;
                }
                const auto row =
                    Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dfs->get_db_instance(), owner.value(), file_id);
                if (offset != effect.arguments.size() || !row.has_value() || row->actor_id != sender
                    || row->owner_id != owner.value() || row->state != Dfs::FileState::Ready
                    || row->hash != std::get<2>(binding)) {
                    return false;
                }
            } catch (const std::exception&) {
                return false;
            }
        }
        return std::ranges::all_of(change.children, [&](const auto& child) {
            return verify_dfs_effect_authors(dfs, child, sender);
        });
    }
} // namespace

ExtraChainNodeWrapper::ExtraChainNodeWrapper(QObject*                      parent,
                                             bool                          is_client_application,
                                             bool                          is_custom_app,
                                             std::uint16_t                 ws_port,
                                             std::optional<RuntimeProfile> runtime_profile)
    : QObject(parent)
    , node(new ExtraChainNode(is_client_application, is_custom_app, ws_port, runtime_profile)) {
}

ExtraChainNodeWrapper::~ExtraChainNodeWrapper() {
    eLog("ExtraChainNodeWrapper::~ExtraChainNodeWrapper");
    node_enabled.store(false);
    eLog("Set node_enabled to {}", node_enabled);
    const auto stop_node = [this] {
        if (node != nullptr && node->dag() != nullptr) {
            node->dag()->stop();
        }
        if (node != nullptr && node->network() != nullptr) {
            node->network()->go_offline();
        }
    };
    if (node != nullptr && m_thread != nullptr && m_thread->isRunning()
        && node->thread() != QThread::currentThread()) {
        QMetaObject::invokeMethod(node, stop_node, Qt::BlockingQueuedConnection);
    } else {
        stop_node();
    }
    ThreadPoolBoost::terminate();

    if (m_thread) {
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

ExtraChainNode::ExtraChainNode(bool                          is_client_application,
                               bool                          is_custom_app,
                               std::uint16_t                 port,
                               std::optional<RuntimeProfile> runtime_profile)
    : is_client_application_(is_client_application)
    , is_custom_app_(is_custom_app)
    , ws_port(port) {
    initialize_contract_resources();
    if (runtime_profile.has_value()) {
        runtime_profile_ = runtime_profile.value();
    } else {
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
        runtime_profile_ = is_client_application_ ? RuntimeProfile::MobileLight : RuntimeProfile::FullNode;
#else
        runtime_profile_ = is_client_application_ ? RuntimeProfile::DesktopLight : RuntimeProfile::FullNode;
#endif
    }
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

    const auto limits = runtime_limits();
    ThreadPoolBoost::instance_dfs(limits.dfs_workers);
    ThreadPoolBoost::instance(limits.general_workers);
    ThreadPoolBoost::instance_dag_sync(limits.dag_sync_workers);

    prepare_folders();

    // Auto-migrate legacy hex-shard DAG layout into decimal hot/ + packs/.
    // No-op when storage is already up to date. Must run before `new Dag(...)`
    // so the constructor reads range/sections in the new layout.
    if (DagMigration::needs_migration()) {
        eLog("[Node] Legacy DAG layout detected — migrating");
        auto res = DagMigration::migrate([](const DagMigration::Progress& p) {
            if (p.processed % 10000 == 0 || p.stage == "done") {
                eLog("[Node] Migration {}: {}/{}", p.stage, p.processed, p.total);
            }
        });
        if (!res.has_value()) {
            eCritical("[Node] DAG migration failed: error {}", static_cast<int>(res.error()));
            node_enabled.store(false);
            QCoreApplication::exit(-1001);
            return;
        } else {
            eSuccess("[Node] DAG migration complete");
        }
    }

    actor_index_        = new ActorIndex(this);
    account_controller_ = new AccountController(this);
    luminance_manager_  = new LuminanceManager(this);
    network_manager_    = new NetworkManager(this, ws_port);
    dag_                = new Dag(this);
    dfs_                = new DfsController(this);
    contract_manager_   = std::make_unique<
          ExtraChain::Contracts::ContractManager>(std::make_unique<ExtraChain::Contracts::DfsContractStorage>(dfs_,
                                                                                                            dag_),
                                                ExtraChain::Contracts::ExecutionLimits {},
                                                ExtraChain::Contracts::RuntimeTuning {
                                                      .max_concurrent_executions = limits.wasm_concurrency,
                                                      .module_cache_entries =
                                                        runtime_profile_ == RuntimeProfile::MobileLight ? 2U : 8U,
                                                      .module_cache_bytes = limits.wasm_cache_bytes_per_thread,
                                                });
    toolchain_registry_  = std::make_unique<ExtraChain::Contracts::ToolchainRegistry>(this);
    auto retry_contracts = [this](ActorId, Dfs::DirRow row) {
        if (row.folder == Dfs::Basic::TEMPLATE_CONTRACTS) {
            QTimer::singleShot(0, this, [this]() {
                dag_->retry_contract_transactions();
            });
        }
    };
    connect(dfs_, &DfsController::added, this, retry_contracts);
    connect(dfs_, &DfsController::downloaded, this, retry_contracts);
    connect(actor_index_, &ActorIndex::newActorSaved, this, [this](ActorId) {
        QTimer::singleShot(0, this, [this]() {
            dag_->retry_contract_transactions();
        });
    });
    connect(actor_index_, &ActorIndex::actorSaved, this, [this](ActorId) {
        QTimer::singleShot(0, this, [this]() {
            dag_->retry_contract_transactions();
        });
    });
    dmm_           = new DataMiningManager(this);
    token_manager_ = new TokenManager(this);
    chat_manager_  = new ChatManager(this);
    thoth_manager_ = new ThothManager(this);
    janus_manager_ = new JanusManager(this);

    // auto address         = "12.12.12.12";
    // auto port            = "1212";

    // auto thread = ThreadPool::addThread(m_blockchain);

    timer_reward_ = new QTimer(this);
    connect(timer_reward_, &QTimer::timeout, this, &ExtraChainNode::timer_reward_request);
    timer_info_ = new QTimer(this);
    connect(timer_info_, &QTimer::timeout, this, &ExtraChainNode::timer_info_print);

    timer_luminance_ = new QTimer(this);
    connect(timer_luminance_, &QTimer::timeout, this, &ExtraChainNode::timer_luminance_autoremove);
    if (runtime_profile_ == RuntimeProfile::FullNode) {
        timer_reward_->start(MINING_TIMER_TICK);
        timer_info_->start(10000);
        timer_luminance_->start(30000);
    }

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
    auto* dag = std::exchange(dag_, nullptr);
    delete dag;
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

    // Freshly created network starts at the current storage schema.
    auto settings        = Utils::read_settings();
    settings.dag_version = CURRENT_DAG_VERSION;
    settings.dfs_version = CURRENT_DFS_VERSION;
    Utils::write_settings(settings);

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
    auto system_actor_id = account_controller()->system_actor().id();
    auto chat_template   = Dfs::CollectionTemplate::create("Chat").value().use_id().add_fields(
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
    auto tokens_template = Dfs::CollectionTemplate::create("TokensRegistry")
                               .value()
                               .add_fields({ Dfs::Field::String("name").not_null().unique().length(3, 20),
                                             Dfs::Field::String("ticker").not_null().unique().length(2, 5),
                                             Dfs::Field::String("count").not_null(),
                                             Dfs::Field::ActorId("owner_id").not_null(),
                                             Dfs::Field::String("color").not_null(),
                                             Dfs::Field::String("smart"),
                                             Dfs::Field::Integer("decimals").not_null().between(0, 18),
                                             Dfs::Field::String("section_id").not_null(),
                                             Dfs::Field::String("tx_hash").not_null() });
    tokens_template.primary = Dfs::Field::ActorId("token_id").not_null().unique();

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
                                                                          "TokensRegistry");
    if (!search_result.has_value()) {
        return false;
    }

    auto store_res =
        dfs_->store_vector(network_id, network_id, "TokensRegistry", network_id, search_result.value().file_id);
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
                                  .decimals   = 8,
                                  .section_id = BigNumber(0),
                                  .tx_hash    = "" };

    auto res = dfs_->add_vector_row(store_res.value().actor_id, store_res.value().file_id, tokens_row, network_id);
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

    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_DICTIONARY,
                                                                          "token_allocations");
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
    QTimer::singleShot(std::chrono::seconds(10), this, [this]() {
        ThreadPoolBoost::instance()->post([this]() {
            auto network_id = actor_index()->network_id();
            auto alloc_row =
                Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dfs_->get_db_instance(),
                                                                                  network_id,
                                                                                  Dfs::Basic::TEMPLATE_DICTIONARY,
                                                                                  "token_allocations");
            if (!alloc_row.has_value()) {
                eWarning("[Node] token_allocations backfill: dictionary not found");
                return;
            }

            constexpr std::uint64_t               cutoff_ms = 1743458400000ULL; // 2026-04-01 00:00:00 UTC
            std::map<std::string, BigNumberFloat> totals;

            SectionId start_section = dag_->current_section();
            SectionId section_id    = start_section;
            SectionId min_section   = SectionId(BigNumber::from_hex("a05133"));
            eLog("[Node] token_allocations backfill: starting from section {}", section_id);

            while (section_id >= min_section) {
                eLog("[Node] token_allocations backfill: section {}", section_id);

                auto section = dag_->read_section(section_id);
                if (!section.has_value()) {
                    section_id = section_id - SectionId(1);
                    continue;
                }

                const auto& section_value = section.value();
                if (!section_value.transactions.empty()) {
                    eLog("[Node] token_allocations backfill: section {} middle={} cutoff={}",
                         section_id,
                         section_value.middle(),
                         cutoff_ms);
                    if (section_value.middle() < cutoff_ms) {
                        eLog("[Node] token_allocations backfill: reached cutoff at section {}", section_id);
                        break;
                    }
                }

                for (const auto& tx : section_value.transactions) {
                    if (tx.type() != TransactionType::Minting)
                        continue;
                    std::string key = fmt::format("{}:{}", tx.receiver().to_string(), tx.token().to_string());
                    totals[key] += tx.amount();
                }

                section_id = section_id - SectionId(1);
            }

            for (const auto& [key, amount] : totals) {
                if (dfs_->dictionary_set_value(network_id,
                                               alloc_row.value().file_id,
                                               key,
                                               amount.to_string(),
                                               network_id)) {
                    dag_->invalidate_token_allocations();
                }
            }

            eSuccess("[Node] token_allocations backfill complete: {} entries", totals.size());
        });
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

    auto vector_template = Dfs::CollectionTemplate::create(CHANNELS_VECTOR_NAME)
                               .value()
                               .add_fields({ Dfs::Field::String("name"),
                                             Dfs::Field::String("owner_id").not_null(),
                                             Dfs::Field::String("file_id").unique().not_null() });

    auto template_res = dfs()->store_template(system_id, vector_template);
    if (!template_res.has_value()) {
        eCritical("Can't create channels template, because {}", template_res.error());
        return DfsFileStatus::CantCreate;
    }

    auto vec_res = dfs()->store_vector(system_id,
                                       system_id,
                                       CHANNELS_VECTOR_NAME,
                                       template_res->actor_id,
                                       template_res->file_id);
    if (!vec_res.has_value()) {
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

    // DFS download ranks (see DfsController::download_rank): chat-actor vectors -> 2,
    // main-actor vectors -> 3. The account is already loaded at this point.
    if (!account_controller_->empty()) {
        if (auto chat_actor = account_controller_->chat_actor(); chat_actor.has_value()) {
            dfs_->set_download_rank(chat_actor->get().id(), 2, -1);
        }
        dfs_->set_download_rank(account_controller_->current_profile().main_id(), 3, -1);
    }

    // Priority overrides: large non-critical vectors must not delay the critical path
    // (Thoth/MyChats/chats) — demote them to the tail of the vector phase.
    dfs_->set_download_rank_by_name(network_id(), "Usernames", DfsController::RANK_OTHER_VECTORS);
    dfs_->set_download_rank_by_name(ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373"),
                                    "RaccoonSubscription",
                                    DfsController::RANK_OTHER_VECTORS);

    // Version compatibility: 0.17.0 (temp)
#ifdef IS_APP_UI_CLIENT
    ThreadPoolBoost::instance()->post([this]() {
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
    ThreadPoolBoost::instance()->post([this]() {
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
    ThreadPoolBoost::instance()->post([this]() {
        QDir("blocks").removeRecursively();
    });
}

bool ExtraChainNode::is_client_application() const {
    return is_client_application_;
}

RuntimeProfile ExtraChainNode::runtime_profile() const {
    return runtime_profile_;
}

RuntimeActivity ExtraChainNode::runtime_activity() const {
    return runtime_activity_.load();
}

RuntimeLimits ExtraChainNode::runtime_limits() const {
    const auto activity = runtime_activity_.load();
    switch (runtime_profile_) {
    case RuntimeProfile::MobileLight:
        return RuntimeLimits {
            .dfs_workers                     = 1,
            .general_workers                 = 1,
            .dag_sync_workers                = 2,
            .peer_limit                      = activity == RuntimeActivity::Background ? 1U : 3U,
            .dfs_downloads                   = activity == RuntimeActivity::Background ? 0U : 3U,
            .pack_sync_window                = 2,
            .cached_transactions             = 1024,
            .sync_transactions               = 64,
            .derived_sections                = 64,
            .admission_prevalidation_workers = 0,
            .wasm_concurrency                = 1,
            .wasm_cache_bytes_per_thread     = 4 * 1024 * 1024,
        };
    case RuntimeProfile::DesktopLight:
        return RuntimeLimits {
            .dfs_workers                     = 2,
            .general_workers                 = 2,
            .dag_sync_workers                = 4,
            .peer_limit                      = activity == RuntimeActivity::Background ? 2U : 5U,
            .dfs_downloads                   = activity == RuntimeActivity::Background ? 1U : 5U,
            .pack_sync_window                = activity == RuntimeActivity::Background ? 2U : 4U,
            .cached_transactions             = 4096,
            .sync_transactions               = 128,
            .derived_sections                = 128,
            .admission_prevalidation_workers = 0,
            .wasm_concurrency                = 2,
            .wasm_cache_bytes_per_thread     = 8 * 1024 * 1024,
        };
    case RuntimeProfile::FullNode:
        return RuntimeLimits {
            .dfs_workers                     = 4,
            .general_workers                 = 4,
            .dag_sync_workers                = 8,
            .peer_limit                      = 0,
            .dfs_downloads                   = 5,
            .pack_sync_window                = 8,
            .cached_transactions             = 16384,
            .sync_transactions               = 256,
            .derived_sections                = 256,
            .admission_prevalidation_workers = 2,
            .wasm_concurrency                = 4,
            .wasm_cache_bytes_per_thread     = 16 * 1024 * 1024,
        };
    }
    return {};
}

void ExtraChainNode::set_runtime_activity(RuntimeActivity activity) {
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, activity]() {
                set_runtime_activity(activity);
            },
            Qt::QueuedConnection);
        return;
    }

    if (runtime_activity_.load() == activity) {
        return;
    }

    runtime_activity_.store(activity);
    if (runtime_profile_ == RuntimeProfile::FullNode) {
        if (activity == RuntimeActivity::Background) {
            if (timer_info_) {
                timer_info_->stop();
            }
        } else if (timer_info_ && !timer_info_->isActive()) {
            timer_info_->start(10000);
        }
    }
    if (activity == RuntimeActivity::Background) {
        emit dagTimerStop();
    } else if (started_ && dag_) {
        emit dagTimerStart(15000);
    }
    emit runtimeActivityChanged(activity);
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

    if (tx.type() == TransactionType::Regular && token_manager_->is_contract_token(tx.token())) {
        auto arguments = token_manager_->transfer_arguments(tx.token(), tx.receiver(), tx.amount());
        if (!arguments.has_value()) {
            return std::unexpected(TransactionError::Unknown);
        }
        auto contract_transaction = submit_contract_call(tx.token(), "transfer", arguments.value());
        if (!contract_transaction.has_value()) {
            eWarning("[TokenManager] Contract token transfer failed: {}", contract_transaction.error().detail);
            return std::unexpected(TransactionError::Unknown);
        }
        return contract_transaction.value();
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
    eLog("[Transaction] Send {} to {}", tx.amount().to_string(), tx.receiver());

    return tx;
}

TokenManager* ExtraChainNode::token_manager() const {
    return token_manager_;
}

ExtraChain::Contracts::ContractManager* ExtraChainNode::contract_manager() const {
    return contract_manager_.get();
}

ExtraChain::Contracts::ToolchainRegistry* ExtraChainNode::toolchain_registry() const {
    return toolchain_registry_.get();
}

bool ExtraChainNode::stage_contract_change(std::string                                   transaction_hash,
                                           ExtraChain::Contracts::PreparedContractChange change) {
    std::scoped_lock lock(pending_contracts_mutex_);
    const auto       contract_ids = [](const ExtraChain::Contracts::PreparedContractChange& root) {
        std::unordered_set<std::string> result;
        const auto                      collect = [&](const auto&                                          self,
                                 const ExtraChain::Contracts::PreparedContractChange& current) -> void {
            result.insert(current.record.contract_id);
            for (const auto& child : current.children) {
                self(self, child);
            }
        };
        collect(collect, root);
        return result;
    };
    const auto changed_contracts = contract_ids(change);
    const auto conflict          = std::ranges::any_of(pending_contracts_, [&](const auto& pending) {
        if (pending.first == transaction_hash) {
            return false;
        }
        const auto pending_contracts = contract_ids(pending.second);
        return std::ranges::any_of(changed_contracts, [&](const auto& contract_id) {
            return pending_contracts.contains(contract_id);
        });
    });
    if (conflict) {
        return false;
    }
    pending_contracts_.insert_or_assign(std::move(transaction_hash), std::move(change));
    return true;
}

std::expected<Transaction, TransactionError> ExtraChainNode::send_contract_transaction(
    Transaction                                   transaction,
    const Actor<KeyPrivate>&                      signer,
    ExtraChain::Contracts::PreparedContractChange change) {
    auto prepared = dag_->prepare_transaction(transaction, signer);
    if (!prepared.has_value()) {
        eWarning("[Contract] Cannot prepare transaction: {}", prepared.error());
        return std::unexpected(prepared.error());
    }
    if (!stage_contract_change(prepared->hash(), change)) {
        eWarning("[Contract] Another pending transaction changes the same contract");
        return std::unexpected(TransactionError::Unknown);
    }
    auto staged = contract_manager_->stage(change);
    if (!staged.has_value()) {
        finalize_contract_change(prepared->hash(), false);
        eWarning("[Contract] Cannot stage transaction artifacts: {}", staged.error().detail);
        return std::unexpected(TransactionError::Unknown);
    }
    dag_->add_transaction_sended(*prepared);
    network_manager_->send_message(*prepared, MessageType::DagTransaction, SendMode::Broadcast);
    return *prepared;
}

void ExtraChainNode::finalize_contract_change(std::string_view transaction_hash, bool approved) {
    std::optional<ExtraChain::Contracts::PreparedContractChange> change;
    {
        std::scoped_lock lock(pending_contracts_mutex_);
        auto             iterator = pending_contracts_.find(std::string(transaction_hash));
        if (iterator == pending_contracts_.end()) {
            return;
        }
        if (approved) {
            change = std::move(iterator->second);
        }
        pending_contracts_.erase(iterator);
    }
    if (!change.has_value()) {
        return;
    }
    auto committed = contract_manager_->commit(std::move(*change), std::string(transaction_hash));
    if (!committed.has_value()) {
        eCritical("[Contract] Cannot commit approved transaction {}: {}",
                  transaction_hash,
                  committed.error().detail);
    }
}

TransactionProveError ExtraChainNode::validate_contract_transaction(const Transaction& transaction) {
    if (!transaction.meta().has_value()) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto metadata = Json::deserialize<ContractTransactionData>(*transaction.meta());
    if (!metadata.has_value() || metadata->schema != 4 || metadata->kind.empty() || metadata->kind.size() > 64
        || metadata->method.empty() || metadata->method.size() > 64 || metadata->module_hash.size() != 64
        || metadata->state_hash.size() != 64 || metadata->effects_hash.size() != 64 || metadata->version == 0
        || metadata->revision == 0 || metadata->transitions.size() >= ExtraChain::Contracts::ContractMaximumCalls
        || std::ranges::any_of(metadata->transitions, [](const ContractTransitionData& transition) {
               return transition.contract_id.empty() || transition.caller_contract_id.empty()
                      || transition.kind.empty() || transition.method.empty() || transition.method.size() > 64
                      || transition.module_hash.size() != 64 || transition.state_hash.size() != 64
                      || transition.effects_hash.size() != 64 || transition.effects_base64.empty()
                      || transition.version == 0 || transition.revision == 0;
           })) {
        return TransactionProveError::InvalidContractPayload;
    }
    if (metadata->checkpoint != (metadata->checkpoint_revision == metadata->revision)
        || metadata->checkpoint_revision == 0 || metadata->checkpoint_revision > metadata->revision
        || metadata->revision - metadata->checkpoint_revision
               >= ExtraChain::Contracts::ContractCheckpointInterval) {
        return TransactionProveError::InvalidContractPayload;
    }

    auto arguments       = Utils::from_base64<std::vector<std::uint8_t>>(metadata->arguments_base64);
    auto encoded_effects = Utils::from_base64<std::vector<std::uint8_t>>(metadata->effects_base64);
    if (!arguments.has_value() || arguments.value().size() > 512 * 1024 || !encoded_effects.has_value()) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto declared_effects = ExtraChain::Contracts::Codec::decode_effects(encoded_effects.value());
    if (!declared_effects.has_value()
        || ExtraChain::Contracts::Codec::effect_hash(declared_effects.value()) != metadata->effects_hash) {
        return TransactionProveError::InvalidContractPayload;
    }

    const auto contract_id = transaction.receiver();
    const auto verified_inputs =
        verify_contract_inputs(dag_,
                               dfs_,
                               metadata->verified_inputs,
                               static_cast<std::uint64_t>(transaction.section().to_int().value_or(0)));
    if (!verified_inputs.has_value()
        || Json::serialize(*verified_inputs) != Json::serialize(metadata->verified_inputs)) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto read_artifact = [this, &contract_id](const std::string& name, const ActorId& expected_author)
        -> std::expected<std::vector<std::uint8_t>, TransactionProveError> {
        auto row = dfs_->read_file_status(contract_id, name, Dfs::Basic::TEMPLATE_CONTRACTS);
        if (!row.has_value() || row->state != Dfs::FileState::Ready) {
            return std::unexpected(TransactionProveError::ContractDependencyMissing);
        }
        if (row->actor_id != expected_author) {
            return std::unexpected(TransactionProveError::InvalidContractPayload);
        }
        auto content = Dfs::Tables::DirsFile::ActorSpace::get_file_content(contract_id, row->file_id);
        if (!content.has_value()) {
            dfs_->request_file(contract_id, row->file_id);
            return std::unexpected(TransactionProveError::ContractDependencyMissing);
        }
        return *content;
    };

    auto verify_output = [&](const ExtraChain::Contracts::ContractOutput& output) -> TransactionProveError {
        if (ExtraChain::Contracts::content_hash(output.state) != metadata->state_hash
            || ExtraChain::Contracts::Codec::encode_effects(output.effects) != encoded_effects.value()) {
            return TransactionProveError::InvalidContractPayload;
        }
        if (!metadata->checkpoint) {
            return TransactionProveError::NoError;
        }
        auto state_name = fmt::format("contract-checkpoint-v{:06}-r{:012}-{}.msgpack",
                                      metadata->version,
                                      metadata->revision,
                                      contract_hash_prefix(metadata->state_hash));
        auto state      = read_artifact(state_name, transaction.sender());
        return !state.has_value() ? state.error()
                                  : (*state == output.state ? TransactionProveError::NoError
                                                            : TransactionProveError::InvalidContractPayload);
    };

    const auto block = static_cast<std::uint64_t>(transaction.section().to_int().value_or(0));
    if (transaction.type() == TransactionType::ContractDeploy) {
        if (metadata->method != "init" || metadata->version != 1 || metadata->revision != 1
            || !metadata->previous_state_hash.empty() || !metadata->checkpoint
            || metadata->checkpoint_revision != 1 || !metadata->transitions.empty()) {
            return TransactionProveError::InvalidContractPayload;
        }
        if (contract_manager_->inspect(contract_id.to_string()).has_value()) {
            return TransactionProveError::InvalidContractPayload;
        }
        auto module_name = fmt::format("contract-module-v{:06}-{}.wasm",
                                       metadata->version,
                                       contract_hash_prefix(metadata->module_hash));
        auto module      = read_artifact(module_name, transaction.sender());
        if (!module.has_value()) {
            return module.error();
        }
        if (ExtraChain::Contracts::content_hash(*module) != metadata->module_hash) {
            return TransactionProveError::InvalidContractPayload;
        }
        const auto language = ExtraChain::Contracts::module_language(*module);
        if ((metadata->kind == "fungible-token" || metadata->kind == "non-fungible-token")
            && (!language.has_value() || *language != metadata->language)) {
            return TransactionProveError::InvalidContractPayload;
        }
        if (ExtraChain::Contracts::is_system_token_kind(metadata->kind)) {
            if (!ExtraChain::Contracts::is_standard_token_module(metadata->kind, metadata->module_hash)) {
                return TransactionProveError::InvalidContractPayload;
            }
        }
        auto change = contract_manager_->prepare_deploy(contract_id.to_string(),
                                                        transaction.sender().to_string(),
                                                        metadata->kind,
                                                        *module,
                                                        *arguments,
                                                        block);
        if (!change.has_value() || !change->checkpoint || !metadata->verified_inputs.dag.empty()
            || !metadata->verified_inputs.dfs.empty()) {
            return TransactionProveError::InvalidContractPayload;
        }
        const auto verified = verify_output(change->output);
        if (verified == TransactionProveError::NoError) {
            if (!stage_contract_change(transaction.hash(), std::move(*change))) {
                return TransactionProveError::InvalidContractPayload;
            }
        }
        return verified;
    }

    auto record = contract_manager_->inspect(contract_id.to_string());
    if (!record.has_value() || record->kind != metadata->kind || record->language != metadata->language
        || record->versions.empty()) {
        return TransactionProveError::ContractDependencyMissing;
    }
    const auto& current  = record->versions.at(record->active_version - 1);
    const auto& previous = current.revisions.back();

    if (transaction.type() == TransactionType::ContractCall) {
        const bool checkpoint_due = previous.revision + 1 - previous.checkpoint_revision
                                    >= ExtraChain::Contracts::ContractCheckpointInterval;
        if (metadata->method == "init" || metadata->method == "migrate" || metadata->method == "authorize_upgrade"
            || metadata->version != current.version || metadata->revision != previous.revision + 1
            || metadata->module_hash != current.module_hash || metadata->previous_state_hash != previous.state_hash
            || metadata->checkpoint != checkpoint_due
            || metadata->checkpoint_revision
                   != (checkpoint_due ? metadata->revision : previous.checkpoint_revision)) {
            return TransactionProveError::InvalidContractPayload;
        }
        auto change = contract_manager_->prepare_call(contract_id.to_string(),
                                                      transaction.sender().to_string(),
                                                      metadata->method,
                                                      *arguments,
                                                      block,
                                                      *verified_inputs);
        if (!change.has_value() || change->kind == ExtraChain::Contracts::ContractChangeKind::ReadOnly
            || !verify_dfs_effect_authors(dfs_, *change, transaction.sender())
            || change->checkpoint != metadata->checkpoint
            || Json::serialize(contract_transitions(*change)) != Json::serialize(metadata->transitions)) {
            return TransactionProveError::InvalidContractPayload;
        }
        const auto verified = verify_output(change->output);
        if (verified == TransactionProveError::NoError) {
            if (!stage_contract_change(transaction.hash(), std::move(*change))) {
                return TransactionProveError::InvalidContractPayload;
            }
        }
        return verified;
    }

    if (transaction.type() != TransactionType::ContractUpgrade || metadata->method != "migrate"
        || transaction.sender().to_string() != record->owner_id || metadata->version != current.version + 1
        || metadata->revision != previous.revision + 1 || metadata->previous_state_hash != previous.state_hash
        || !metadata->checkpoint || metadata->checkpoint_revision != metadata->revision
        || !metadata->transitions.empty() || !metadata->verified_inputs.dag.empty()
        || !metadata->verified_inputs.dfs.empty()) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto module_name = fmt::format("contract-module-v{:06}-{}.wasm",
                                   metadata->version,
                                   contract_hash_prefix(metadata->module_hash));
    auto module      = read_artifact(module_name, transaction.sender());
    if (!module.has_value()) {
        return module.error();
    }
    if (ExtraChain::Contracts::content_hash(*module) != metadata->module_hash) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto authorization_arguments = ExtraChain::Contracts::Codec::encode_string_argument(metadata->module_hash);
    auto authorization           = contract_manager_->evaluate(current.module,
                                                     transaction.sender().to_string(),
                                                     "authorize_upgrade",
                                                     authorization_arguments,
                                                     previous.state,
                                                     block);
    if (!authorization.has_value()) {
        return TransactionProveError::InvalidContractPayload;
    }
    auto change = contract_manager_->prepare_upgrade(contract_id.to_string(),
                                                     transaction.sender().to_string(),
                                                     *module,
                                                     *arguments,
                                                     block);
    if (!change.has_value() || !change->checkpoint) {
        return TransactionProveError::InvalidContractPayload;
    }
    const auto verified = verify_output(change->output);
    if (verified == TransactionProveError::NoError) {
        if (!stage_contract_change(transaction.hash(), std::move(*change))) {
            return TransactionProveError::InvalidContractPayload;
        }
    }
    return verified;
}

std::expected<Transaction, ExtraChain::Contracts::ContractFailure> ExtraChainNode::submit_contract_deploy(
    std::string                   kind,
    std::span<const std::uint8_t> module,
    std::span<const std::uint8_t> init_arguments) {
    auto signer = account_controller_->current_wallet();
    if (signer.empty()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidOwner,
            .detail = "No current wallet",
        });
    }
    if (ExtraChain::Contracts::is_system_token_kind(kind)) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidArguments,
            .detail = "The token kind is reserved for a standard token contract",
        });
    }
    auto block = static_cast<std::uint64_t>(dag_->current_section().to_int().value_or(0)) + 1;
    auto validation =
        contract_manager_->evaluate(module, signer.id().to_string(), "init", init_arguments, {}, block);
    if (!validation.has_value()) {
        return std::unexpected(validation.error());
    }
    auto contract_actor = account_controller_->create_service();
    auto change         = contract_manager_->prepare_deploy(contract_actor.id().to_string(),
                                                    signer.id().to_string(),
                                                    std::move(kind),
                                                    module,
                                                    init_arguments,
                                                    block);
    if (!change.has_value()) {
        return std::unexpected(change.error());
    }
    const auto&             version  = change->record.versions.back();
    const auto&             revision = version.revisions.back();
    ContractTransactionData metadata {
        .kind                = change->record.kind,
        .language            = change->record.language,
        .method              = "init",
        .arguments_base64    = Utils::to_base64(init_arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(change->output.effects),
        .effects_base64 = Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(change->output.effects)),
        .version        = version.version,
        .revision       = revision.revision,
        .checkpoint     = change->checkpoint,
        .checkpoint_revision = revision.checkpoint_revision,
        .transitions         = contract_transitions(*change),
    };
    Transaction transaction;
    transaction.set_sender(signer.id());
    transaction.set_receiver(contract_actor.id());
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractDeploy);
    transaction.set_meta(Json::serialize(metadata));
    auto sent = send_contract_transaction(transaction, signer, std::move(*change));
    if (!sent.has_value()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::StorageError,
            .detail = transaction_error_description(sent.error()),
        });
    }
    return *sent;
}

std::expected<Transaction, ExtraChain::Contracts::ContractFailure> ExtraChainNode::submit_contract_call(
    const ActorId&                               contract_id,
    std::string_view                             method,
    std::span<const std::uint8_t>                arguments,
    const ExtraChain::Contracts::VerifiedInputs& verified_inputs) {
    auto signer = account_controller_->current_wallet();
    return submit_contract_call(signer, contract_id, method, arguments, verified_inputs);
}

std::expected<Transaction, ExtraChain::Contracts::ContractFailure> ExtraChainNode::submit_contract_call(
    const Actor<KeyPrivate>&                     signer,
    const ActorId&                               contract_id,
    std::string_view                             method,
    std::span<const std::uint8_t>                arguments,
    const ExtraChain::Contracts::VerifiedInputs& verified_inputs) {
    if (signer.empty()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidOwner,
            .detail = "No current wallet",
        });
    }
    auto block    = static_cast<std::uint64_t>(dag_->current_section().to_int().value_or(0)) + 1;
    auto verified = verify_contract_inputs(dag_, dfs_, verified_inputs, block);
    if (!verified.has_value()) {
        return std::unexpected(verified.error());
    }
    auto change = contract_manager_->prepare_call(contract_id.to_string(),
                                                  signer.id().to_string(),
                                                  method,
                                                  arguments,
                                                  block,
                                                  *verified);
    if (!change.has_value()) {
        return std::unexpected(change.error());
    }
    if (!verify_dfs_effect_authors(dfs_, *change, signer.id())) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidProof,
            .detail = "A DFS effect references data from another author",
        });
    }
    if (change->kind == ExtraChain::Contracts::ContractChangeKind::ReadOnly) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidArguments,
            .detail = "The method is read-only; use a contract query",
        });
    }

    const auto&             version  = change->record.versions.at(change->record.active_version - 1);
    const auto&             revision = version.revisions.back();
    ContractTransactionData metadata {
        .kind                = change->record.kind,
        .language            = change->record.language,
        .method              = std::string(method),
        .arguments_base64    = Utils::to_base64(arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(change->output.effects),
        .effects_base64 = Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(change->output.effects)),
        .version        = version.version,
        .revision       = revision.revision,
        .checkpoint     = change->checkpoint,
        .checkpoint_revision = revision.checkpoint_revision,
        .transitions         = contract_transitions(*change),
        .verified_inputs     = *verified,
    };
    Transaction transaction;
    transaction.set_sender(signer.id());
    transaction.set_receiver(contract_id);
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractCall);
    transaction.set_meta(Json::serialize(metadata));
    auto sent = send_contract_transaction(transaction, signer, std::move(*change));
    if (!sent.has_value()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::StorageError,
            .detail = transaction_error_description(sent.error()),
        });
    }
    return *sent;
}

std::expected<Transaction, ExtraChain::Contracts::ContractFailure> ExtraChainNode::submit_contract_upgrade(
    const ActorId&                contract_id,
    std::span<const std::uint8_t> module,
    std::span<const std::uint8_t> migration_arguments) {
    auto signer = account_controller_->current_wallet();
    if (signer.empty()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidOwner,
            .detail = "No current wallet",
        });
    }
    auto block  = static_cast<std::uint64_t>(dag_->current_section().to_int().value_or(0)) + 1;
    auto change = contract_manager_->prepare_upgrade(contract_id.to_string(),
                                                     signer.id().to_string(),
                                                     module,
                                                     migration_arguments,
                                                     block);
    if (!change.has_value()) {
        return std::unexpected(change.error());
    }
    const auto&             version  = change->record.versions.at(change->record.active_version - 1);
    const auto&             revision = version.revisions.back();
    ContractTransactionData metadata {
        .kind                = change->record.kind,
        .language            = change->record.language,
        .method              = "migrate",
        .arguments_base64    = Utils::to_base64(migration_arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = ExtraChain::Contracts::Codec::effect_hash(change->output.effects),
        .effects_base64 = Utils::to_base64(ExtraChain::Contracts::Codec::encode_effects(change->output.effects)),
        .version        = version.version,
        .revision       = revision.revision,
        .checkpoint     = change->checkpoint,
        .checkpoint_revision = revision.checkpoint_revision,
        .transitions         = contract_transitions(*change),
    };
    Transaction transaction;
    transaction.set_sender(signer.id());
    transaction.set_receiver(contract_id);
    transaction.set_amount(BigNumberFloat(0));
    transaction.set_token(TokenId());
    transaction.set_type(TransactionType::ContractUpgrade);
    transaction.set_meta(Json::serialize(metadata));
    auto sent = send_contract_transaction(transaction, signer, std::move(*change));
    if (!sent.has_value()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::StorageError,
            .detail = transaction_error_description(sent.error()),
        });
    }
    return *sent;
}

std::expected<ExtraChain::Contracts::ContractReceipt, ExtraChain::Contracts::ContractFailure> ExtraChainNode::
    query_contract(const ActorId& contract_id, std::string_view method, std::span<const std::uint8_t> arguments) {
    auto signer = account_controller_->current_wallet();
    if (signer.empty()) {
        return std::unexpected(ExtraChain::Contracts::ContractFailure {
            .error  = ExtraChain::Contracts::ContractError::InvalidOwner,
            .detail = "No current wallet",
        });
    }
    auto block = static_cast<std::uint64_t>(dag_->current_section().to_int().value_or(0));
    return contract_manager_->query(contract_id.to_string(), signer.id().to_string(), method, arguments, block);
}

ExtraChain::Contracts::ContractCatalogPage ExtraChainNode::list_contracts(
    const ExtraChain::Contracts::ContractCatalogFilter& filter) {
    return dag_ == nullptr ? ExtraChain::Contracts::ContractCatalogPage {} : dag_->cache().list_contracts(filter);
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
    transaction.set_amount(BigNumberFloat("500"));
#ifdef QT_DEBUG
    transaction.set_amount(BigNumberFloat("0.112"));
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

    auto imported_user = ImportedUser { .version       = extrachain_node_version,
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
            eLog("[Transaction] Send tx {} to {}", tx.amount().to_string(), tx.receiver());
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
        eWarning("Can not create tx to '{}'. There no current user", receiver);
        return std::unexpected(TransactionError::NoCurrentUser);
    }

    return std::unexpected(TransactionError::Unknown);
}

std::expected<Transaction, TransactionError> ExtraChainNode::send_transaction(const Transaction&       transaction,
                                                                              const Actor<KeyPrivate>& signer) {
    if (transaction.type() == TransactionType::Regular && token_manager_->is_contract_token(transaction.token())) {
        auto arguments =
            token_manager_->transfer_arguments(transaction.token(), transaction.receiver(), transaction.amount());
        if (!arguments.has_value()) {
            return std::unexpected(TransactionError::Unknown);
        }
        auto result = submit_contract_call(signer, transaction.token(), "transfer", arguments.value());
        if (!result.has_value()) {
            return std::unexpected(TransactionError::Unknown);
        }
        return result.value();
    }
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

    eLog("[Dag] Current: {} section, status: {}, last cache: {}",
         dag_->current_section().to_printable_string(),
         dag_->status(),
         dag_->cache().section().to_printable_string());

#ifndef IS_APP_CLIENT
    // Periodically re-check where we stand against the network. start_check otherwise
    // runs only when a peer connects (or when the actor first-sync ends), so a node
    // that falls behind *after* the mesh has formed never asks again.
    //
    // NOTE: this timer is not a reliable heartbeat on its own — see the DAG-side
    // watchdog in Dag::start(). Measured on a six-node stand: this tick stopped firing
    // after ~30s on four of six nodes while the node itself kept working (network,
    // console and DAG threads all live), and those four then sat at section 1 while
    // the other two reached 177. The watchdog covers that case; this stays because
    // when it does fire it is the cheapest place to check.
    if (dag_->status() == DagStatus::Ready) {
        dag_->start_check();
    }
#endif

#ifndef IS_APP_CLIENT
    #ifdef Q_OS_LINUX
    {
        std::ifstream statm("/proc/self/statm");
        if (statm.is_open()) {
            long pages = 0;
            statm >> pages; // total
            statm >> pages; // RSS
            long rss_mb               = pages * sysconf(_SC_PAGESIZE) / (1024 * 1024);
            long queue_total          = 0;
            long bytes_to_write_total = 0;
            {
                auto conns = *network_manager_->connections();
                for (const auto& s : *conns) {
                    queue_total += s->queue_size();
                    bytes_to_write_total += s->pending_bytes();
                }
            }

            eLog(
                "[Mem] RSS: {} MB | msg_hash: {} messages: {} forwarded: {} "
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

    (void)connect(actor_index_, &ActorIndex::firstSyncEnded, [this]() {
        (void)chat_manager_->activate();

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
    if (runtime_activity_.load() == RuntimeActivity::Background) {
        return;
    }
    dag_->timer_sync_->stop();
    dag_->timer_sync_->start(ms);
}

void ExtraChainNode::dagTimerStoping() {
    // eLog("[Dag] Timer stop");
    dag_->timer_sync_->stop();
}

void ExtraChainNode::dagTimerTick() {
    if (dag_ != nullptr) {
        dag_->timer_tick();
    }
}

void ExtraChainNode::dagWatchdogTick() {
    if (dag_ != nullptr) {
        dag_->watchdog_tick();
    }
}

void ExtraChainNode::dagSyncCheck() {
    if (dag_ != nullptr) {
        dag_->sync_check();
    }
}
