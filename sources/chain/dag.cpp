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

#include "chain/dag.h"

#include "dfs/dfs_controller.h"
#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"
#include "utils/thread_pool_boost.h"

static constexpr int SYNC_SECTIONS_BATCH   = 2100;
static constexpr int SYNC_SECTIONS_MAX_REQ = 2500;

Dag::Dag(ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node, node)
    , cache_(node, this) {
    timer_sync_ = new QTimer();

    bool storage_reset = false;

    auto settings = Utils::read_settings();
    if (settings.dag_mode.has_value()) {
        mode_ = settings.dag_mode.value();
    }

    if (!settings.dag_mode.has_value()) {
#ifdef IS_APP_UI_CLIENT
        set_mode(DagMode::Light);
#else
        set_mode(DagMode::Full);
#endif
    }

    QFile file(QString::fromStdString(ChainConst::DAG_RANGE_PATH));
    if (file.open(QFile::ReadOnly)) {
        auto last_id_content = file.readAll();

        auto section_range = Json::deserialize<SectionRange>(last_id_content.toStdString());
        if (section_range.has_value()) {
            auto first_id_result    = SectionId::create(section_range->first);
            auto current_id_result  = SectionId::create(section_range->last);
            auto last_cached_result = SectionId::create(section_range->last_cached);

            if (!first_id_result.has_value() || !current_id_result.has_value()) {
                return;
            }

            if (mode_ == DagMode::Full && first_id_result != SectionId("0")) {
                set_current_section(current_id_result.value());

                first_saved_section_ = first_id_result.value();
                clear_dag();
                cache_.reset_db();
                cache_.init_db();
            } else {
                set_current_section(current_id_result.value());
                first_saved_section_ = first_id_result.value();

                if (last_cached_result.has_value()) {
                    cache_.set_section(last_cached_result.value());
                }
            }

            // For Full mode, cache will be requested via DagLightData after sync
            // if (mode_ == DagMode::Full && cache_.section() == -1) {
            //     cache_.reset_db();
            //     cache_.init_db();
            //     cache_.check_and_update_cache(current_section_);
            // }

            eLog("[Dag] Loaded: {}, first: {}, last cached: {}",
                 current_section_,
                 first_saved_section_,
                 cache_.section());
            file.close();
        }
    } else {
        // QDir(QString::fromStdString(ChainConst::CHAIN_FOLDER)).removeRecursively();
        clear_dag();
        storage_reset = true;
    }

    transaction_cache_.make_files();
    cache_.init_db();

    // if (!QDir(QString::fromStdString(ChainConst::CHAIN_FOLDER)).exists()) {
    //     QDir().mkdir(QString::fromStdString(ChainConst::CHAIN_FOLDER));
    //     transaction_cache_.make_files();
    // }

    timestamp_bigger_sync_start_ = 0;

#ifndef IS_APP_CLIENT
    this->set_status(DagStatus::Ready);
#endif

    auto section = this->read_section(SectionId(0));
    if (section.has_value() && section->transactions.size() == 1) {
        // prove_transaction()
        auto network_id = section->transactions.begin()->sender();
        node->actor_index()->set_network_id(network_id);
    }

    if (mode_ == DagMode::Light && cache_.section() == SectionId(-1) && !storage_reset) {
        clear_dag();
        cache_.reset_db();
        cache_.init_db();
    }

    eLog("[Dag] Started. Mode: {}", mode_);
}

Dag::~Dag() {
    cache_.dag = nullptr;
    timer_sync_->deleteLater();
}

SectionId Dag::current_section() const {
    return current_section_;
}

void Dag::set_current_section(const SectionId &new_current_section) {
    if (current_section_ < new_current_section) {
        current_section_ = new_current_section;
    }
}

DagMode Dag::mode() const {
    return mode_;
}

DagStatus Dag::status() const {
    return status_;
}

void Dag::set_mode(DagMode mode) {
    // if (this->mode_ == mode) {
    //     return;
    // }

    this->mode_ = mode;

    auto settings     = Utils::read_settings();
    settings.dag_mode = this->mode_;
    Utils::write_settings(settings);
}

void Dag::force_full_mode() {
    if (mode_ == DagMode::Full) {
        return;
    }

    set_mode(DagMode::Full);
    clear_dag();
    start_sync();
}

void Dag::force_light_mode() {
    if (mode_ == DagMode::Light) {
        return;
    }

    set_mode(DagMode::Light);
    clear_dag();
    start_sync();
}

void Dag::set_status(DagStatus status) {
    this->status_ = status;
    emit node->dagStatus(status_);

    if (status == DagStatus::Ready) {
        emit node->dagTimerStop();
        min_req_count_ = 5;
    }
}

TransactionCache &Dag::transaction_cache() {
    return transaction_cache_;
}

DagCache &Dag::cache() {
    return cache_;
}

SectionId Dag::first_saved_section() {
    return first_saved_section_;
}

SectionId Dag::file_section(const SectionId &section) const {
    return section / Config::DataStorage::SECTION_SIZE;
}

std::string Dag::file_folder(const SectionId &section) const {
    auto file_section = this->file_section(section);
    auto path         = fmt::format("{}/{}", ChainConst::DAG_FOLDER, file_section.to_string());
    return path;
}

std::string Dag::file_path(const SectionId &section) const {
    auto path = fmt::format("{}/{}", this->file_folder(section), section.to_string());
    return path;
}

std::expected<Transaction, TransactionError> Dag::prepare_transaction(const Transaction       &transaction,
                                                                      const Actor<KeyPrivate> &signer,
                                                                      bool                     ignore_zero) {
    auto tx = transaction;
    tx.set_section(current_section_ + 1);

    if (tx.section() == 0 && !ignore_zero) {
        return std::unexpected(TransactionError::NoLastSection);
    }

    auto section = this->read_section(tx.section() - 1);
    if (!section.has_value() && transaction.type() != TransactionType::Genesis) {
        return std::unexpected(TransactionError::NoLastSection);
    }

    if (section.has_value()) {
        tx.set_prev_hashs(section->hashs());
    }

    tx.set_timestamp(Utils::current_date_ms());

    auto sign_res = tx.sign(signer);
    if (!sign_res) {
        return std::unexpected(TransactionError::Unknown);
    }

    return tx;
}

std::expected<Transaction, TransactionError> Dag::send_transaction(const Transaction       &transaction,
                                                                   const Actor<KeyPrivate> &signer) {
    auto tx = this->prepare_transaction(transaction, signer);
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }

    //

    eLog("[Dag] Send {}", tx.value());
    this->add_transaction_sended(tx.value());
    node->network()->send_message(tx.value(), MessageType::DagTransaction, SendMode::Broadcast);

    return tx;
}

std::expected<void, TransactionProveError> Dag::network_transaction(const Transaction &transaction,
                                                                    const Responder   &responder) {
    if (transaction.type() == TransactionType::Regular) {
        auto sender  = NodeId { .actor_id = transaction.sender(), .node_identifier = "" };
        auto last_it = last_txs_.find(sender);

        if (last_it != last_txs_.end()) {
            auto current_time = Utils::current_date_ms();
            auto time_diff_ms = current_time - last_it->second;

            if (time_diff_ms < 4500) {
                eLog("[Dag] Ignore transaction from {}, diff: {} ms", sender, time_diff_ms);
                return std::unexpected(TransactionProveError::TooOften);
            }
        }

        last_txs_[sender] = transaction.timestamp();
    }

    if (status_ != DagStatus::Final) {
        /*
        bool sync_timeout = false;
        if (timestamp_bigger_sync_start_ != 0) {
            sync_timeout = (Utils::current_date_ms() - timestamp_bigger_sync_start_) > 10000;
        }
        */

        if (/* !sync_timeout && */ status_ != DagStatus::Ready) {
            if (mode_ == DagMode::Light || light_requested_) {
                this->add_to_cached_tx(transaction);
            }
        }

        if (status_ != DagStatus::Ready) {
            // Update sync target if transaction section is ahead but within reasonable range
            if (transaction.section() > sync_last_index_
                && transaction.section() <= sync_last_index_ + 15) {
                sync_last_index_ = transaction.section();
                emit node->dagSyncStart(current_section_, sync_last_index_);
            }
            return {};
        }

        /*
        if (sync_timeout && transaction.section() > current_section_ + 5) {
            if (!sync_timeout)
                this->add_to_cached_tx(transaction);
            this->set_status(DagStatus::Sync);
            sync_last_index_             = transaction.section();
            timestamp_bigger_sync_start_ = Utils::current_date_ms();
            eLog("[Dag] Section bigger: {}", sync_last_index_);
            this->request_sections(current_section_,
                                   std::min(sync_last_index_, current_section_ + 100),
                                   responder);
            return {};
        }
        */
    }

    auto tx   = transaction;
    auto resp = responder;
    // ThreadPoolBoost::instance()->post([this, transaction = tx, responder = resp]() {
    auto                  section = read_section(transaction.section());
    TransactionProveError res =
        this->prove_transaction(transaction,
                                section.has_value() ? section->transactions : std::set<Transaction> {});
    TransactionResult transaction_result { .section_id = transaction.section(),
                                           .hash       = transaction.hash(),
                                           .result     = res };

    if (res != TransactionProveError::NoError) {
        auto tx = transaction;
        tx.set_prev_hashs({ "hashs" });
        eLog("[Dag] Transaction not approved: {} {}", tx, res);

        if (res == TransactionProveError::TooSectionDiff) {
            eLog("[Dag] Current: {} (0x{}) section (status: {}), but TooSectionDiff!: {} (0x{})",

                 this->current_section().to_string(NumeralBase::Dec),
                 this->current_section(),
                 this->status(),
                 transaction.section().to_string(NumeralBase::Dec),
                 transaction.section());

            if (tx.section() < this->current_section()) {
                // need sync?
            }
        }
    } else {
        eLog("[Dag] Transaction from network approved: {}", transaction);
    }

    if (res == TransactionProveError::NoError) {
        auto save_result = this->save_transaction(transaction);
        if (!save_result) {
            transaction_result.result = TransactionProveError::NoSectionAdded;
            // send response
            return std::unexpected(transaction_result.result);
        }

        this->set_current_section(transaction.section());
        this->update_range();
    }

    // send broadcast to network with tx result
    node->network()->send_broadcast(transaction_result,
                                    MessageType::DagTransactionResult,
                                    MessageStatus::NoStatus);

    if (res != TransactionProveError::NoError) {
        return std::unexpected(res);
    }

    this->check_self(transaction);
    return {};
}

void Dag::network_transaction_result(const TransactionResult &tx_result, const Responder &responder) {
    if (sended_transactions_.find(tx_result.hash) == sended_transactions_.end()) {
        // eLog("[Dag] Ignore transaction result: {} / {}", hash, result);
        return;
    }

    // map of

    auto transaction = this->sended_transactions_[tx_result.hash];
    // this->sended_transactions.erase(hash);

    if (tx_result.result != TransactionProveError::NoError) {
        eLog("[Dag] Our transaction not approved: 0x{} ({}) / {}, {}",
             transaction.section(),
             transaction.section().to_string(NumeralBase::Dec),
             transaction.hash(),
             tx_result.result);

        // if not approved > min (connections, 5)
        this->sended_transactions_.erase(tx_result.hash);
        this->failed_transactions_.insert({ tx_result.hash, transaction });
        emit node->dagTxNotApproved(transaction.section(), tx_result.hash);
        return;
    } else {
        eLog("[Dag] Our transaction approved: {} / {}", transaction.section(), transaction.hash());
        this->sended_transactions_.erase(tx_result.hash);
        emit node->dagTxApproved(transaction.section(), tx_result.hash);
    }

    auto save_result = this->save_transaction(transaction);
    if (!save_result) {
        eLog("[Dag] Can't save our approved transaction {} in section {}",
             transaction.hash(),
             transaction.section());
        return;
    }

    this->check_self(transaction);
}

void Dag::check_self(const Transaction &transaction) {
    const auto my_actors = node->account_controller()->accounts_ids();

    for (const auto &my_actor : my_actors) {
        if (transaction.sender() == my_actor || transaction.receiver() == my_actor) {
            auto section = read_section(transaction.section());
            if (!section.has_value()) {
                continue;
            }

            emit transaction_cache_.add(transaction);

            if (transaction.type() == TransactionType::InitContract) {
                node->selfTxInitContractAdded(transaction);
            }

            if (transaction.type() == TransactionType::Repeatable) {
                node->selfTxRepeatableAdded(transaction);
            }

            // if (transaction.type() == TransactionType::Reward
            //     && accountId == node->accountController()->system_actor().id()) {
            //     Transaction tx;
            //     tx.setSender(accountId);
            //     tx.setReceiver(accountId);
            //     tx.setType(TransactionType::Conversion);
            //     tx.setData(ActorId().to_string());
            //     tx.setAmount(transaction.amount());
            //     tx.setToken(
            //         ActorId("468faf2f1be6504a9a26f7f027"
            //                 "f7e43380b0d77d"));
            //     eLog("[Reward] Send conversion: {} coins", tx.amount());
            //     node->sendTransaction(tx, node->accountController()->system_actor());
            // }
        }
    }
}

void Dag::network_section(const Section &section) {
    //
}

Balances Dag::calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                       std::optional<SectionId>    to_section) {
    // Use DagCache to calculate balances
    return cache_.calculate_balances(actor_ids, current_section_, first_saved_section_, to_section);
}

void Dag::process_cached_transactions(bool not_ready) {
    // TODO: check controls

    {
        try {
            auto guard = cached_txs_.lock();
            eLog("[Dag] Processing {} cached transactions after sync", guard->size());
        } catch (const std::system_error &e) {
            std::cerr << "[Dag] Caught system_error in process cached: " << e.what() << std::endl;
        }
    }

    auto current_status = status_;
    status_             = DagStatus::Final;

    timestamp_bigger_sync_start_ = 0;

    while (true) {
        std::set<Transaction> txs_to_process;
        {
            try {
                auto guard_mut = cached_txs_.lock_mut();
                if (guard_mut->empty()) {
                    break;
                }

                txs_to_process = std::move(*guard_mut);
                guard_mut->clear();
            } catch (const std::system_error &e) {
                std::cerr << "[Dag] Caught system_error in process cached 2: " << e.what() << std::endl;
            }
        }

        for (const auto &tx : txs_to_process) {
            Responder responder(node->network());
            network_transaction(tx, responder);
        }
    }

    timestamp_bigger_sync_start_ = 0;

    if (not_ready) {
        status_ = current_status;
    }

    if (!not_ready) {
        set_status(DagStatus::Ready);
        set_sync_status(DagSyncStatus::None);
    }
}

void Dag::add_to_cached_tx(const Transaction &transaction) {
    bool exists = false;
    {
        try {
            auto guard = cached_txs_.lock();
            exists     = guard->find(transaction) != guard->end();
        } catch (const std::system_error &e) {
            std::cerr << "[Dag] Caught system_error in add_to_cached_tx: " << e.what() << std::endl;
        }
    }

    if (!exists) {
        try {
            auto guard_mut = cached_txs_.lock_mut();
            guard_mut->insert(transaction);
        } catch (const std::system_error &e) {
            std::cerr << "[Dag] Caught system_error in add_to_cached_tx 2: " << e.what() << std::endl;
        }

        // eLog("[Dag] Add to cached transaction: {} / {}", transaction.section(), transaction.hash());
    }
}

std::optional<Section> Dag::read_section(const SectionId &section_id) const {
    try {
        std::shared_lock<std::shared_mutex> lock(section_mutex_);

        auto p    = this->file_path(section_id);
        auto path = FsPath::create(p);
        if (path.has_value()) {
            auto content = Utils::read_file_content(path.value());
            if (content.has_value()) {
                auto section = Json::deserialize<Section>(content.value());
                if (section.has_value()) {
                    section->id = section_id;
                    return section.value();
                }
            }
        }

        return std::nullopt;
    } catch (const std::system_error &e) {
        return std::nullopt;
    }
}

bool Dag::exists_section_file(const SectionId &section_id) const {
    auto p    = this->file_path(section_id);
    auto path = FsPath::create(p);
    if (path.has_value()) {
        return path->exists();
    }

    return false;
}

std::optional<bool> Dag::write_section(const Section &section) {
    try {
        std::unique_lock<std::shared_mutex> lock(section_mutex_);

        auto folder = this->file_folder(section.id);
        if (!std::filesystem::exists(folder)) {
            std::filesystem::create_directory(folder);
        }

        auto p    = this->file_path(section.id);
        auto path = FsPath::create(p);
        if (!path.has_value()) {
            return std::nullopt;
        }

        auto res = Utils::write_file_content(path.value(), Json::serialize(section));
        if (!res.has_value()) {
            return std::nullopt;
        }

        update_range();
        return true;
    } catch (const std::system_error &e) {
        return std::nullopt;
    }
}

std::optional<std::pair<WriteResult, std::optional<SectionDiff>>> Dag::write_section_diff(const Section &section) {
    // Same-section RMW race: a sync write must not clobber a concurrent tx insert.
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    std::optional<SectionDiff> section_diff;
    auto                       existing_section = this->read_section(section.id);

    if (existing_section.has_value()) {
        if (existing_section->transactions.size() == section.transactions.size()
            && existing_section->calculate_hash() == section.calculate_hash()) {
            return std::pair { WriteResult::NoChanges, section_diff };
        }
        section_diff = this->calculate_section_diff(*existing_section, section);
    } else {
        SectionDiff diff;
        diff.added_transactions.reserve(section.transactions.size());
        for (const auto &transaction : section.transactions) {
            diff.added_transactions.push_back(transaction);
        }
        section_diff = std::move(diff);
    }

    auto write_result = write_section(section);
    if (!write_result.has_value()) {
        return std::nullopt;
    }

    return std::pair { WriteResult::Write, section_diff };
}

std::optional<WriteResult> Dag::write_control(const SectionId &section_id, const std::string &hash) {
    if (section_id % CONTROL_INTERVAL_MOD != 0) {
        return std::nullopt;
    }

    // Serialize against save_transaction: a concurrent tx insert into this section
    // must not race with writing its control hash.
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        section = Section { .id = section_id };
    }

    if (section->control.has_value()) {
        if (section->control == hash) {
            // eTemp("[Dag] No need writing control to {}", section_id);
            return WriteResult::NoChanges;
        }
    }

    // eTemp("[Dag] Write control to {}", section_id);
    section->control = hash;
    auto res         = this->write_section(section.value());
    if (!res.has_value()) {
        return std::nullopt;
    }

    // A peer may have claimed this boundary before we sealed it. Now that we have our own
    // control, that claim is finally comparable — this is the point of remembering it.
    {
        std::string peer_hash;
        {
            std::lock_guard lock(pending_intervals_mutex_);
            if (auto it = pending_intervals_.find(section_id); it != pending_intervals_.end()) {
                peer_hash = it->second;
                pending_intervals_.erase(it);
            }
        }

        if (!peer_hash.empty()) {
            if (peer_hash != hash) {
                // No responder here — this runs from write_control, not a network
                // handler, so we cannot ask that peer directly. The next interval
                // exchange for this boundary will hit the live path and refetch.
                eCritical("[Dag] Control mismatch at section {} (deferred): ours {}, peer {}",
                          section_id,
                          hash,
                          peer_hash);
            } else {
                eLog("[Dag] Deferred interval check at {}: match", section_id);
            }
        }
    }

    return WriteResult::Write;
}

std::optional<WriteResult> Dag::remove_control(const SectionId &section_id) {
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    if (!section->control.has_value()) {
        return WriteResult::Write;
    }

    if (section_id == SectionId(0)) {
        return WriteResult::NoChanges;
    }

    section->control = std::nullopt;
    auto res         = this->write_section(section.value());
    if (!res.has_value()) {
        return std::nullopt;
    }

    return WriteResult::Write;
}

void Dag::timer_tick() {
    eLog("[Dag] Timer tick");
    this->timer_sync_->stop(); // no need emit?
    this->set_status(DagStatus::Timered);
    this->sync_status_ = DagSyncStatus::None;

    if (min_req_count_ > 1) {
        min_req_count_ -= 1;
    }

    this->start_sync();
}

SectionDiff Dag::calculate_section_diff(const Section &old_section, const Section &new_section) {
    SectionDiff diff;

    auto old_it  = old_section.transactions.begin();
    auto new_it  = new_section.transactions.begin();
    auto old_end = old_section.transactions.end();
    auto new_end = new_section.transactions.end();

    // O(n + m)
    while (old_it != old_end && new_it != new_end) {
        if (*old_it < *new_it) {
            diff.removed_transactions.push_back(*old_it);
            ++old_it;
        } else if (*new_it < *old_it) {
            diff.added_transactions.push_back(*new_it);
            ++new_it;
        } else {
            ++old_it;
            ++new_it;
        }
    }

    while (old_it != old_end) {
        diff.removed_transactions.push_back(*old_it);
        ++old_it;
    }

    while (new_it != new_end) {
        diff.added_transactions.push_back(*new_it);
        ++new_it;
    }

    return diff;
}

bool Dag::save_transaction(const Transaction &transaction) {
    // Hold save_mutex_ across the whole read-insert-write cycle: without it two
    // concurrent transactions for the same section both read the old set and one
    // insert is lost, so sections diverge between nodes and ControlIndex fails.
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);

    auto section = this->read_section(transaction.section());

    if (!section.has_value()) {
        // Create new section
        Section section { .id = transaction.section(), .transactions = { transaction } };

        set_current_section(section.id);

        // Check if cache needs updating
        cache_.check_and_update_cache_thread(current_section_);

        // Update range file
        update_range();

        if (mode_ == DagMode::Light && transaction.section() == SectionId(0)) {
            auto network_id = transaction.sender();
            node->actor_index()->set_network_id(network_id);
        }

        // Update first_saved_section_ if this is the first section or has a lower ID
        if (first_saved_section_ == SectionId(-1) && transaction.section() >= SectionId(0)) {
            if (mode_ == DagMode::Light && transaction.section() == SectionId(0)) {
                return write_section(section).has_value();
            }

            first_saved_section_ = transaction.section();
            eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
        }

        return write_section(section).has_value();
    }

    // if (section->id > current_section_) {
    //     current_section_ = section->id;
    // }

    // Add transaction to existing section
    section->transactions.insert(transaction);

    // Invalidate control if section had one - transactions changed
    if (section->control.has_value()) {
        section->control = std::nullopt;
    }

    // Check if cache needs updating
    cache_.check_and_update_cache_thread(current_section_);

    // Update first_saved_section_ if this is the first section or has a lower ID
    if (first_saved_section_ == SectionId(-1) && transaction.section() >= SectionId(0)) {
        if (mode_ == DagMode::Full || (mode_ == DagMode::Light && transaction.section() != SectionId(0))) {
            first_saved_section_ = transaction.section();
        }

        eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
    }

    // Update range file
    update_range();

    return write_section(section.value()).has_value();
}

bool Dag::local_remove_transaction(const SectionId &section_id, const std::string &hash) {
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        return false;
    }

    auto it =
        std::find_if(section->transactions.begin(), section->transactions.end(), [&hash](const Transaction &tx) {
            return tx.hash() == hash;
        });

    if (it == section->transactions.end()) {
        // eFatal("WTF?!");
        return true;
    }

    section->transactions.erase(it);
    this->write_section(section.value());

    return true;
}

std::optional<std::pair<SectionId, SectionId>> Dag::save_transactions(const std::set<Transaction> &transactions) {
    if (transactions.empty()) {
        return std::nullopt;
    }
    // Same section RMW race as save_transaction — serialize the batch too.
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);

    bool      all_saved   = true;
    bool      has_changes = false;
    SectionId min_section = SectionId(-1);
    SectionId max_section;

    auto it = transactions.begin();
    while (it != transactions.end()) {
        const SectionId section_id = it->section();

        if (min_section == SectionId(-1)) {
            min_section = section_id;
            max_section = section_id;
        } else {
            if (section_id < min_section)
                min_section = section_id;
            if (section_id > max_section)
                max_section = section_id;
        }

        // from first to last
        const auto first = it;
        auto       last  = it;
        while (last != transactions.end() && last->section() == section_id)
            ++last;

        // create or load
        auto    section_local = this->read_section(section_id);
        bool    created       = !section_local.has_value();
        Section section       = created ? Section { .id = section_id, .transactions = {} } : *section_local;

        const size_t old_size = section.transactions.size();
        section.transactions.insert(first, last);
        const size_t new_size = section.transactions.size();

        const bool changed = (new_size != old_size);
        if (changed)
            has_changes = true;

        // Invalidate control if section changed (new transactions added)
        if (changed && section.control.has_value()) {
            section.control = std::nullopt;
        }

        set_current_section(section_id);
        if (created) {
            cache_.check_and_update_cache_thread(current_section_);
            this->update_range();
            if (mode_ == DagMode::Light && section_id == SectionId(0)) {
                node->actor_index()->set_network_id(first->sender());
                all_saved &= write_section(section).has_value();
                it = last;
                continue;
            }
        } else {
            cache_.check_and_update_cache(current_section_);
            if (first_saved_section_ == SectionId(-1) && section_id >= SectionId(0)) {
                if (mode_ == DagMode::Full || (mode_ == DagMode::Light && section_id != SectionId(0))) {
                    first_saved_section_ = section_id;
                    eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
                }
            }
        }

        //
        if (created && first_saved_section_ == SectionId(-1) && section_id >= SectionId(0)) {
            if (mode_ == DagMode::Full || (mode_ == DagMode::Light && section_id != SectionId(0))) {
                first_saved_section_ = section_id;
                eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
            }
        }

        all_saved &= write_section(section).has_value();
        it = last;
    }

    this->update_range();

    if (!all_saved)
        return std::nullopt;

    // if (has_changes)
    //     eTemp("[Dag] Saved sections from {} to {} with changes", min_section, max_section);
    // else
    //     eTemp("[Dag] Saved sections from {} to {} - no changes", min_section, max_section);

    return std::make_pair(min_section, max_section);
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions) {
    // Check Genesis transactions
    if (tx.type() == TransactionType::Genesis) {
        if (tx.section() != SectionId(0)) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        if (tx.amount() != 0) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        if (!node->network_id().is_zero() && tx.sender() != tx.receiver() && tx.sender() != node->network_id()) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        return TransactionProveError::NoError;
    }

    if (tx.type() == TransactionType::Balance) {
        if (tx.section() != SectionId(1)) {
            return TransactionProveError::BalanceOnlyFirstSection;
        }

        if (!node->network_id().is_zero() && tx.sender() != tx.receiver() && tx.sender() != node->network_id()) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        return TransactionProveError::NoError;
    }

    // Verify previous section exists
    auto section = this->read_section(SectionId(tx.section() - 1));
    if (section.has_value()) {
        // TODO: Additional section validation could be added here
    }

    if ((current_section_ - tx.section()).abs() > 15) {
        return TransactionProveError::TooSectionDiff;
    }

    // Validate transaction amount
    if (tx.amount() == BigNumberFloat(0)) {
        return TransactionProveError::AmountZero;
    }

    if (tx.amount() < 0) {
        return TransactionProveError::AmountLessZero;
    }

    // Get sender and receiver IDs
    ActorId        targetSender   = tx.sender();
    ActorId        targetReceiver = tx.receiver();
    const ActorId &mainActorId    = node->account_controller()->system_actor().id();

    // Check if transaction involves the node's own accounts
    // if (tx.type() != TransactionType::Repeatable) {
    //     const auto accounts = node->accountController()->accountsIds();
    //     for (const auto &accountId : accounts) {
    //         if (targetSender == accountId || targetReceiver == accountId) {
    //             return TransactionProveError::SelfPleasure;
    //         }
    //     }
    // }

    // Verify transaction hash integrity
    auto tx_copy = tx;
    tx_copy.update_hash();
    if (tx.hash() != tx_copy.hash()) {
        return TransactionProveError::WrongHash;
    }

    // Check for duplicate transaction. The section id is part of the tx hash, so a
    // duplicate can only live in its own section — and that section's transactions
    // are already loaded and passed in here. No chain scan needed (the old
    // search_duplicate_by_hash walked and JSON-parsed up to 100 sections per tx).
    for (const auto &existing : transactions) {
        if (existing.hash() == tx_copy.hash()) {
            return TransactionProveError::Duplicate;
        }
    }

    // Validate sender
    if (targetSender.is_zero()) {
        return TransactionProveError::SenderZero;
    }

    Actor<KeyPublic> senderActor;
    senderActor = node->actor_index()->read_actor_old(targetSender);
    if (senderActor.empty()) {
        return TransactionProveError::SenderNotExists;
    }

    // Special handling for Burn transactions
    if (tx.type() == TransactionType::Burn) {
        if (!tx.receiver().is_zero()) {
            return TransactionProveError::BurnIncorrectReceiver;
        }

        bool verify = tx.verify(senderActor);
        if (!verify) {
            return TransactionProveError::InvalidSignature;
        }

        return TransactionProveError::NoError;
    }

    // Validate receiver
    if (targetReceiver.is_zero()) {
        return TransactionProveError::ReceiverZero;
    }

    Actor<KeyPublic> receiverActor;
    receiverActor = node->actor_index()->read_actor_old(targetReceiver);
    if (receiverActor.empty()) {
        return TransactionProveError::ReceiverNotExists;
    }

    // Validate Minting transactions (owner-only)
    if (tx.type() == TransactionType::Minting) {
        static const ActorId minting_owner("46710a2d823c23db9fc2ac01e0f84212a8128373");
        static const TokenId minting_token("468faf2f1be6504a9a26f7f027f7e43380b0d77d");

        if (targetSender != minting_owner) {
            return TransactionProveError::InvalidSignature;
        }
        if (tx.token() != minting_token) {
            return TransactionProveError::RewardInvalidToken;
        }
        if (targetSender == targetReceiver) {
            return TransactionProveError::IdenticalSenderReceiver;
        }

        bool verify = tx.verify(senderActor);
        if (!verify) {
            return TransactionProveError::InvalidSignature;
        }

        return TransactionProveError::NoError;
    }

    // Check sender-receiver relationship based on transaction type
    if (tx.type() == TransactionType::Reward || tx.type() == TransactionType::Conversion) {
        // These transaction types require sender and receiver to be the same
        if (targetSender != targetReceiver) {
            return TransactionProveError::NotIdenticalSenderReceiver;
        }
    } else {
        // Regular transactions require different sender and receiver
        if (targetSender == targetReceiver) {
            return TransactionProveError::IdenticalSenderReceiver;
        }
    }

    // Verify signature
    if (tx.signature().empty()) {
        return TransactionProveError::MissingSignature;
    }

    bool verify = tx.verify(senderActor);
    if (!verify) {
        return TransactionProveError::InvalidSignature;
    }

    // Special transaction types that don't require balance check
    if (tx.type() == TransactionType::Reward) {
        if (tx.amount() > 3) {
            return TransactionProveError::BigReward;
        }

        return TransactionProveError::NoError;
    }

    // special conditions: receiver is null - coins burning,
    // contract creation
    // TODO: InitContract: check duplicate
    // Validate InitContract transactions
    if (tx.type() == TransactionType::InitContract) {
        auto count = tx.amount();
        if (count < 0 || count >= ChainConst::MAX_TOKEN_COUNT) {
            return TransactionProveError::InvalidTokenCount;
        }
        return TransactionProveError::NoError;
    }

    // Validate Conversion transactions
    if (tx.type() == TransactionType::Conversion) {
        // Check conversion token information
        if (!tx.meta().has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }
        auto from_token = TokenId::create(tx.meta().value());
        if (!from_token.has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }

        TokenId token = from_token.value();

        if (from_token == tx.token()) {
            return TransactionProveError::ConversionEqualToken;
        }

        return TransactionProveError::NoError;
    }

    // Balance validation for regular transactions
    TokenId token = tx.token();

    // Calculate sender's current balance from all previous sections
    std::vector<ActorId> actor_ids = { targetSender };
    BigNumberFloat       senderBalance =
        calculate_actors_balance(actor_ids, tx.section())[std::pair { targetSender, token }];
    BigNumberFloat transactionAmount = tx.amount();

    // Apply all transactions in the current section to the balance
    for (const Transaction &tx_check : std::as_const(transactions)) {
        if (tx.hash() == tx_check.hash()) {
            continue; // Skip the current transaction itself
        }

        if (tx_check.token() != token) {
            continue; // Skip transactions with different tokens
        }

        // Rewards and contract initializations increase balance
        if (tx_check.type() == TransactionType::Reward && tx_check.sender() == targetSender) {
            senderBalance += tx_check.amount();
            continue;
        }

        if (tx_check.type() == TransactionType::InitContract && tx_check.sender() == targetSender) {
            senderBalance += tx_check.amount();
            continue;
        }

        // Conversions can both increase and decrease balance
        if (tx_check.type() == TransactionType::Conversion && tx_check.sender() == targetSender) {
            if (tx_check.meta().has_value()) {
                auto from_token = TokenId::create(tx_check.meta().value());
                if (from_token.has_value() && from_token.value() == token) {
                    senderBalance -= tx_check.amount();
                }
                if (tx_check.token() == token) {
                    senderBalance += tx_check.amount();
                }
            }
            continue;
        }

        // Regular transactions
        if (tx_check.sender() == targetSender && tx_check.token() == token) {
            senderBalance -= tx_check.amount();
        }
        if (tx_check.receiver() == targetSender && tx_check.token() == token) {
            senderBalance += tx_check.amount();
        }
    }

    // Check if the sender has sufficient balance
    if (senderBalance < transactionAmount) {
        return TransactionProveError::SenderBalanceBelowZero;
    }

    // Freeze check: block spending of minted amount (Regular only)
    if (tx.type() == TransactionType::Regular) {
        auto network_id = node->actor_index()->network_id();
        if (!network_id.is_zero()) {
            auto alloc_row = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
                node->dfs()->get_db_instance(), network_id, Dfs::Basic::TEMPLATE_DICTIONARY, "token_allocations");
            if (alloc_row.has_value()) {
                auto minted_str = node->dfs()->read_dictionary(
                    network_id, alloc_row->file_id,
                    fmt::format("{}:{}", targetSender.to_string(), token.to_string()));
                if (minted_str.has_value() && !minted_str->empty()) {
                    auto minted_amount = BigNumberFloat::create(*minted_str, NumeralBase::Dec);
                    if (minted_amount.has_value() && senderBalance - minted_amount.value() < transactionAmount) {
                        return TransactionProveError::SenderBalanceBelowZero;
                    }
                }
            }
        }
    }

    return TransactionProveError::NoError;
}

void Dag::add_transaction_sended(const Transaction &transaction) {
    // eLog("[Dag] Add to sended: {}", transaction.hash());
    sended_transactions_.insert({ transaction.hash(), transaction });
    emit node->dagTxSended(transaction.section(), transaction.hash());
}

void Dag::update_range() {
    try {
        std::lock_guard<std::mutex> lock(range_mutex_);

        auto new_first = first_saved_section_;
        auto new_last  = current_section_;

        QFile read_file(QString::fromStdString(ChainConst::DAG_RANGE_PATH));
        if (read_file.open(QFile::ReadOnly)) {
            auto existing = Json::deserialize<SectionRange>(read_file.readAll().toStdString());
            read_file.close();

            if (existing.has_value()) {
                auto existing_first = SectionId::create(existing->first);
                if (existing_first.has_value() && existing_first.value() != SectionId(-1)
                    && new_first != SectionId(-1) && new_first < existing_first.value()) {
                    eLog("[Dag] update_range blocked: new first {} < existing {}", new_first, existing_first.value());
                    return;
                }
            }
        }

        std::string json = Json::serialize(SectionRange { .first       = new_first.to_string(),
                                                          .last        = new_last.to_string(),
                                                          .last_cached = cache_.section().to_string() });

        QFile file(QString::fromStdString(ChainConst::DAG_RANGE_PATH));
        if (file.open(QFile::WriteOnly)) {
            file.write(json.data());
            file.close();
        } else {
            eLog("[Dag] Failed to open range file for writing");
        }
    } catch (const std::system_error &e) {
        // eCritical("[Dag] Caught system_error in update range: {}", e.what());
        return;
    }
}

std::optional<SectionId> Dag::find_first_gap(std::size_t limit) const {
    if (current_section_ == SectionId(-1)) {
        return std::nullopt;
    }

    // Full nodes only: a Light node does not store section 0 at all, so a contiguity
    // scan is meaningless for it. Every caller checks the mode; this is the backstop.
    if (mode_ != DagMode::Full) {
        return std::nullopt;
    }

    // Scan from 0, not from first_saved_section_: that field is set from whatever
    // section happened to arrive first (live traffic can set it to 5 on a node that
    // joined late — dag.cpp:764/792), which is exactly the case this check exists to
    // catch. Starting there would step over the very gap we are looking for.
    auto from = SectionId(0);

    // Fast path: count the section files and compare with how many the range implies.
    // A healthy chain (the overwhelming majority of calls) costs a few directory reads
    // instead of one stat per section — the per-section scan below is ~2.6 us/section,
    // i.e. a quarter of a second once the chain reaches 100k sections.
    if (limit == 0) {
        try {
            std::uintmax_t counted = 0;
            for (SectionId folder = file_section(from); folder <= file_section(current_section_);
                 folder++) {
                auto dir = fmt::format("{}/{}", ChainConst::DAG_FOLDER, folder.to_string());
                std::error_code ec;
                for (const auto &entry : std::filesystem::directory_iterator(dir, ec)) {
                    (void)entry;
                    counted++;
                }
            }

            // Sections known to be empty network-wide have no file anywhere, so they are
            // not expected on disk. Only the fast "definitely no gap" answer is given
            // here; a shortfall falls through to the exact scan rather than being
            // reported as a gap.
            std::size_t known_empty = 0;
            {
                std::lock_guard lock(known_empty_sections_mutex_);
                known_empty = known_empty_sections_.size();
            }

            auto expected = current_section_ - from + SectionId(1) - SectionId(static_cast<long long>(known_empty));
            if (SectionId(static_cast<long long>(counted)) >= expected) {
                return std::nullopt;
            }
        } catch (const std::exception &) {
            // Fall through to the exact scan: a counting failure must never be read
            // as "no gap".
        }
    }

    std::set<SectionId> empty_snapshot;
    {
        std::lock_guard lock(known_empty_sections_mutex_);
        empty_snapshot = known_empty_sections_;
    }

    std::size_t seen = 0;
    for (SectionId i = from; i <= current_section_; i++) {
        if (limit != 0 && seen++ >= limit) {
            break;
        }

        if (!std::filesystem::exists(this->file_path(i)) && !empty_snapshot.contains(i)) {
            return i;
        }
    }

    return std::nullopt;
}

std::optional<Transaction> Dag::search_duplicate_by_hash(const std::string &hash, int deep) const {
    int count = 0;

    for (SectionId i = current_section_ + 1; i >= first_saved_section_; i--) {
        auto section = this->read_section(i);

        if (!section.has_value()) {
            continue;
        }

        if (section.has_value() && (section->transactions.empty() || section->id < 0)) {
            continue;
        }

        for (auto &tx : section->transactions) {
            if (tx.hash() == hash) {
                return tx;
            }
        }

        count += 1;
        if (deep != 0 && count > deep) {
            return std::nullopt;
        }
    }

    return std::nullopt;
}

std::optional<std::pair<SectionId, std::string>> Dag::search_duplicate_by_sender(const ActorId &actor_id,
                                                                                 std::uint64_t  latest_timestamp,
                                                                                 std::uint64_t  time) const {

    std::uint64_t threshold = latest_timestamp - time;

    for (SectionId i = current_section_ + 1; i >= first_saved_section_; i--) {
        auto section = this->read_section(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty() || section->id < 0) {
            continue;
        }

        for (auto &tx : section->transactions) {
            if (tx.timestamp() < threshold) {
                break; // continue
            }

            if (tx.sender() == actor_id) {
                return std::pair { tx.section(), tx.hash() };
            }
        }
    }

    return std::nullopt;
}

//
//
// Sync
//
//

void Dag::start_sync() {
    // start timer, after end -> again request
    if (status_ == DagStatus::Sync) {
        // eLog("BC 11 start_sync return");
        return;
    }

    // if (mode_ == DagMode::Light) {
    emit node->dagTimerStart(15001);
    // eLog("Timer start");
    // }

    if (status_ != DagStatus::Sync) {
        this->set_status(DagStatus::Sync);
    }

    last_info_.clear();
    set_sync_status(DagSyncStatus::LastInfo);
    requests_count_ = 1; // std::max(1, node->network()->active_connections_count() - 1);
    node->network()->send_message(true,
                                  MessageType::DagSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);
}

void Dag::start_check() {
#ifndef IS_APP_CLIENT
    if (status_ == DagStatus::Ready) {
        // Section 0 is the sync base and must exist on every FULL node (the
        // genesis tx also carries the network id). A fresh full node joins
        // already-Ready (set unconditionally at init), so without this check it
        // would never run the initial chain sync: it would miss the genesis
        // section, write_control would later materialize an empty stub and its
        // control chain would diverge from the network forever. Light nodes keep
        // the old behavior (they sync via the light package, not full sections).
        if (mode_ != DagMode::Full) {
            return;
        }
        auto zero = this->read_section(SectionId(0));
        if (zero.has_value() && !zero->transactions.empty()) {
            // Holding genesis is not the same as holding the chain. A node that joined
            // while the network was still near height 0 asks for a single section (the
            // request range is capped by the peers' height at handshake time), declares
            // the sync complete, and afterwards only ever accepts live traffic — leaving
            // a permanent hole that `range` cannot even express, since it stores just
            // first/last. Seen on a six-node run: one node missing sections 1-4 forever
            // while reporting a range identical to the healthy nodes'. See TODO.md 1.1.
            auto gap = this->find_first_gap();
            if (!gap.has_value()) {
                return;
            }
            // eCritical, not eLog: this is missing history, and if no peer can supply
            // the section it will repeat on every connection. It must be visible in a
            // log rather than blend into routine sync chatter.
            eCritical("[Dag] start_check: chain is missing section {} (current {}) — syncing",
                      gap.value(),
                      current_section_);
        } else {
            eLog("[Dag] start_check: no genesis section yet — running initial sync");
        }
    }
#endif

    if (status_ != DagStatus::Ready || status_ == DagStatus::Maybe) {
        start_sync();
        // QTimer::singleShot(3000, [this]() {
        //     this->start_sync();
        // });
        // eLog("BC 12 start_check return");
        return;
    }

    last_info_.clear();
    check_status_   = DagSyncStatus::LastInfo;
    requests_count_ = 1; // std::max(1, node->network()->active_connections_count() - 1);
    node->network()->send_message(true,
                                  MessageType::DagSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 9 start_check");
}

void Dag::network_status_sync_request(const Responder &responder) {
#ifdef IS_APP_UI_CLIENT
    return;
#endif

    if (mode_ == DagMode::Light) {
        return;
    }

    // if (status_ != DagStatus::Ready) {
    //     return;
    // }

    auto      section      = this->read_section(current_section_);
    SectionId section_id   = section.has_value() ? section->id : SectionId(-1);
    auto      hashs        = section.has_value() ? section->hashs() : std::set<std::string> {};
    auto      zero_section = this->read_section(SectionId(0));

    auto last_control = this->find_last_control();
    if (!last_control.has_value()) {
        this->start_control(Force::Active);
        last_control = this->find_last_control();
        // return;
    }

    if (!last_control.has_value()) {
        eCritical("[Dag] Sync response problem: no last control");
        return;
    }

    std::uint64_t zero_timestamp =
        zero_section.has_value() ? (zero_section->transactions.size() == 1 ? zero_section->middle() : 0) : 0;

    auto last_info = DagLastInfo { .last_section_id         = section_id,
                                   .last_control_section_id = last_control->section_id,
                                   .last_control_hash       = last_control->control,
                                   .zero_date               = zero_section.has_value() ? zero_timestamp : 0,
                                   .status                  = status_ };
    // eLog("network_status_sync_request, send: {}", last_info);,
    responder.send_response(last_info, MessageType::DagSyncLastInfo, SendMode::Focused, MessageStatus::Response);
}

void Dag::network_status_sync_response(const DagLastInfo &last_info, const Responder &responder) {
    if (responder.luminance() < 2) {
        return;
    }

    if (sync_status_ != DagSyncStatus::LastInfo && check_status_ != DagSyncStatus::LastInfo) {
        eWarning("[Dag] Sync responce: not last info status");
        return;
    }
    // min(connections size, 5)

    if (last_info.status != DagStatus::Ready) {
        eWarning("[Dag] Sync responce: last info not ready");
        return;
    }

    auto zero_section = this->read_section(SectionId(0));
    if (zero_section.has_value() && last_info.last_section_id != SectionId(-1)
        && zero_section->middle() < last_info.zero_date) {
        // TODO: clear dag?
        // this->clear_dag();
    }

    int count = 1; // std::min(requests_count_, min_req_count_);

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    if (sync_status_ == DagSyncStatus::LastInfo && last_info_.size() >= count) {
        set_sync_status(DagSyncStatus::Sections);
        check_status_ = DagSyncStatus::None;
        eLog("BC 6 sync status");
        this->handle_sync_request();
        return;
    }

    if (check_status_ == DagSyncStatus::LastInfo && last_info_.size() >= count) {
        check_status_ = DagSyncStatus::Sections;
        eLog("BC 7 check status");
        this->handle_sync_request();
    }
}

void Dag::request_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    // TODO: auto add sync_last_index = to, also auto from, from + 100

    auto range         = SectionRange { .first = from == -1 ? "0" : from.to_string(), .last = to.to_string() };
    auto responder_new = responder.with_new_message_id();
    responder_new.send_response(range, MessageType::DagSections, SendMode::Focused, MessageStatus::Request);

    // if (status_ != DagStatus::Sync) {
    eTemp("[Dag] Request sections from {} to {}", range.first, range.last);
    // }
}

void Dag::network_request_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    if (current_section_ < from) { // to
        eLog("[Dag] Send sections error: {} < {}", current_section_, from);
        return;
    }

    if (from < first_saved_section_) {
        // eLog("sysync 1 {} {}", from, first_saved_section_);
        return;
    }

    if (to < from) {
        eLog("[Dag] Send sections error: {} < {}", to, from);
        return;
    }

    if (to - from >= SYNC_SECTIONS_MAX_REQ) {
        // return;
    }

    std::set<Transaction>   txs;
    std::vector<DagControl> controls;

    for (SectionId i = from; i <= to; i++) {
        auto section = this->read_section(i);
        if (!section.has_value()) {
            continue;
        }

        if (section->control.has_value()) {
            controls.push_back(DagControl { .section_id = section->id, .control = section->control.value() });
        }

        if (section->transactions.empty()) {
            continue;
        }

        for (const auto &tx : section->transactions) {
            txs.insert(tx);
        }
    }

    // if (txs.empty()) {
    //     return;
    // }

    // eLog("[Dag] Send sections from {} to {}", from, to);

    auto section_sync =
        SectionSync { .to = to, .txs = txs, .controls = controls, .last_section = current_section_ };

    auto ser      = MessagePack::serialize(section_sync);
    auto compress = qCompress(QByteArray::fromStdString(ser));
    responder.send_response(compress.toStdString(),
                            MessageType::DagSections,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_request_sections_response(const std::string &compressed, const Responder &responder) {
    emit node->dagTimerStop();
    // eLog("Timer stop");

    ThreadPoolBoost::instance_dag_sync()->post([this, compressed, responder]() {
        const auto section_sync = MessagePack::deserialize<SectionSync>(
            qUncompress(QByteArray::fromStdString(compressed)).toStdString());

        if (!section_sync.has_value()) {
            // eLog("network_request_sections_response 1");
            // eLog("sysync 2");
            return;
        }

        if (!section_sync->txs.empty()) {
            auto res = this->save_transactions(section_sync->txs);
            if (!res.has_value()) {
                // eLog("network_request_sections_response 2");
                // eLog("sysync 3");
                return;
            }

            const auto &[min, max] = res.value();
        }

        if (section_sync->last_section > sync_last_index_) {
            sync_last_index_ = section_sync->last_section;
            emit node->dagSyncStart(current_section_, sync_last_index_);
        }

        // for (const auto &[section_id, control] : section_sync->controls) {
        //     if (section_id % 20 == 0) {
        //         auto existing_section = this->read_section(section_id);
        //         if (existing_section.has_value()) {
        //             if (existing_section->control.has_value()) {
        //                 continue;
        //             }

        //             if (!existing_section->control.has_value()) {
        //                 // eTemp("[Dag] Control changed for section {}: {} -> {}",
        //                 //      section_id,
        //                 //      existing_section->control.value_or("none"),
        //                 //      control);

        //                 // auto removed = this->remove_control(section_id);
        //                 // if (removed.has_value()) {
        //                 // if (removed.value() == WriteResult::Write) {
        //                 this->start_control(Force::Active, false);
        //                 // }
        //                 // }
        //             }
        //         }
        //     }
        //     // this->write_control(section_id, control);
        // }

        if (section_sync->to >= sync_last_index_ - 1) {
            eLog("[Dag] Sync completed, processing cached transactions");

            if (this->status_ != DagStatus::Ready) {
                this->start_control();

                if (mode_ == DagMode::Light) {
                    this->process_cached_transactions();
                    return;
                }

#ifdef IS_APP_CLIENT // only for clients for first correction and integration
                     // emit node->dagSyncFinish();
                this->process_cached_transactions(true);
                cache_.reset_db();
                auto responder_new = responder.with_new_message_id();
                node->network()->send_message(true,
                                              MessageType::DagLightData,
                                              SendMode::Focused,
                                              MessageStatus::Request,
                                              responder_new);
                light_requested_ = true;
#else
                this->process_cached_transactions();
#endif
            }
            return;
        }

        emit node->dagSyncProgress(section_sync->to);
        this->set_current_section(section_sync->to);
        // eLog("curr: {}, sync last: {}, curr + 100 {}", current_section_, sync_last_index, current_section_ +
        // 100);

        // timer_sync->start();
        emit node->dagTimerStart(15002);
        this->request_file_sections(section_sync->to, std::min(sync_last_index_, section_sync->to + SYNC_SECTIONS_BATCH), responder);
    });
}

void Dag::network_request_file_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    if (current_section_ < from) {
        eLog("[Dag] Send file sections error: {} < {}", current_section_, from);
        return;
    }

    if (from < first_saved_section_) {
        eLog("[Dag] File sections: from {} < first_saved {}", from, first_saved_section_);
        return;
    }

    if (to < from) {
        eLog("[Dag] Send file sections error: {} < {}", to, from);
        return;
    }

    ThreadPoolBoost::instance_dag_sync()->post([this, from, to, responder]() {
        std::vector<SectionFileData> sections;

        for (SectionId i = from; i <= to; i++) {
            auto p    = this->file_path(i);
            auto path = FsPath::create(p);
            if (!path.has_value() || !path->exists()) {
                continue;
            }

            auto content = Utils::read_file_content(path.value());
            if (!content.has_value()) {
                continue;
            }

            auto &bytes = content.value();
            sections.push_back(SectionFileData {
                .section_id = i,
                .file_bytes = std::string(bytes.begin(), bytes.end())
            });
        }

        auto file_sync = FileSectionsSync { .to = to, .sections = sections, .last_section = current_section_ };

        auto ser      = MessagePack::serialize(file_sync);
        auto compress = qCompress(QByteArray::fromStdString(ser));
        responder.send_response(compress.toStdString(),
                                MessageType::DagFileSections,
                                SendMode::Focused,
                                MessageStatus::Response);
    });
}

void Dag::network_file_sections_response(const std::string &compressed, const Responder &responder) {
    emit node->dagTimerStop();

    ThreadPoolBoost::instance_dag_sync()->post([this, compressed, responder]() {
        if (mode_ == DagMode::Light) {
            eLog("[Dag] Skip file sections response: light mode");
            return;
        }

        const auto file_sync = MessagePack::deserialize<FileSectionsSync>(
            qUncompress(QByteArray::fromStdString(compressed)).toStdString());

        if (!file_sync.has_value()) {
            eLog("[Dag] File sections sync: failed to deserialize");
            return;
        }

        for (const auto &section_data : file_sync->sections) {
            if (mode_ == DagMode::Light) {
                eLog("[Dag] Skip file section write: light mode");
                return;
            }

            auto folder = this->file_folder(section_data.section_id);
            if (!std::filesystem::exists(folder)) {
                std::filesystem::create_directory(folder);
            }

            auto p    = this->file_path(section_data.section_id);
            auto path = FsPath::create(p);
            if (!path.has_value()) {
                continue;
            }

            Utils::write_file_content(path.value(), section_data.file_bytes);
        }

        if (mode_ == DagMode::Light) {
            return;
        }

        for (const auto &section_data : file_sync->sections) {
            if (first_saved_section_ == SectionId(-1) || section_data.section_id < first_saved_section_) {
                first_saved_section_ = section_data.section_id;
            }

            if (section_data.section_id > current_section_) {
                this->set_current_section(section_data.section_id);
            }
        }

        update_range();

        // Anything at or below the answered boundary that the peer did not send, it does
        // not have either — on a real chain that means a section with no transactions,
        // which never gets a file anywhere. Remember those so the contiguity scan stops
        // reporting them as gaps and the sync stops asking for them forever.
        {
            std::set<SectionId> delivered;
            for (const auto &section_data : file_sync->sections) {
                delivered.insert(section_data.section_id);
            }

            std::lock_guard lock(known_empty_sections_mutex_);
            auto            from = first_saved_section_ == SectionId(-1) ? SectionId(0) : first_saved_section_;
            for (SectionId i = from; i <= file_sync->to; i++) {
                if (!delivered.contains(i) && !std::filesystem::exists(this->file_path(i))) {
                    known_empty_sections_.insert(i);
                }
            }
        }

        if (file_sync->last_section > sync_last_index_) {
            sync_last_index_ = file_sync->last_section;
            emit node->dagSyncStart(current_section_, sync_last_index_);
        }

        // Reaching the target index is not the same as having a contiguous chain: the
        // target is the peers' height from handshake time, and on a network that was
        // near zero it is satisfied immediately (0 >= -1) without a single section
        // being fetched. Keep pulling while a hole remains. See TODO.md 1.1.
        if (auto gap = this->find_first_gap();
            gap.has_value() && gap.value() <= sync_last_index_ && file_sync->to >= gap.value()) {
            constexpr int max_gap_retries = 5;

            if (gap.value() != gap_retry_section_) {
                gap_retry_section_ = gap.value();
                gap_retry_count_   = 0;
            }

            if (++gap_retry_count_ > max_gap_retries) {
                // The peer answers but never delivers this section. Stop asking rather
                // than loop: the gap stays visible to start_check, which will retry on
                // the next sync round, possibly against a different peer.
                eCritical("[Dag] Gap at section {} not delivered after {} attempts — giving up this round",
                          gap.value(),
                          max_gap_retries);
                // Clear the counter here, not on the next differing gap: leaving it
                // spent would make every later round give up immediately on this same
                // section, turning a loop guard into a permanent refusal to repair.
                gap_retry_section_ = SectionId(-1);
                gap_retry_count_   = 0;
            } else {
                eLog("[Dag] File sync: gap still at section {}, continuing from there", gap.value());
                emit node->dagTimerStart(15002);
                this->request_file_sections(gap.value(),
                                            std::min(sync_last_index_, gap.value() + SYNC_SECTIONS_BATCH),
                                            responder);
                return;
            }
        } else {
            gap_retry_section_ = SectionId(-1);
            gap_retry_count_   = 0;
        }

        if (file_sync->to >= sync_last_index_ - 1) {
            eLog("[Dag] File sync completed");

            if (this->status_ != DagStatus::Ready) {
                this->start_control();

#ifdef IS_APP_CLIENT
                this->process_cached_transactions(true);
                cache_.reset_db();
                auto responder_new = responder.with_new_message_id();
                node->network()->send_message(true,
                                              MessageType::DagLightData,
                                              SendMode::Focused,
                                              MessageStatus::Request,
                                              responder_new);
                light_requested_ = true;
#else
                this->process_cached_transactions();
#endif
            }
            return;
        }

        emit node->dagSyncProgress(file_sync->to);
        emit node->dagTimerStart(15002);
        this->request_file_sections(file_sync->to + 1, std::min(sync_last_index_, file_sync->to + SYNC_SECTIONS_BATCH), responder);
    });
}

void Dag::request_file_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    if (mode_ == DagMode::Light) {
        eLog("[Dag] Skip file sections request: light mode");
        return;
    }

    auto range         = SectionRange { .first = from == -1 ? "0" : from.to_string(), .last = to.to_string() };
    auto responder_new = responder.with_new_message_id();
    responder_new.send_response(range, MessageType::DagFileSections, SendMode::Focused, MessageStatus::Request);

    eTemp("[Dag] Request file sections from {} to {}", range.first, range.last);
}

void Dag::network_request_light(const Responder &responder) {
    ThreadPoolBoost::instance_dag_sync()->post([this, responder]() {
        QElapsedTimer timer;
        timer.start();
        std::set<Transaction>                          txs;
        std::vector<std::pair<SectionId, std::string>> controls;

        if (cache().section() == SectionId(-1) && current_section_ > 100) {
            return;
        }

        auto [cache_section, cache] = this->cache().read_cached_balances();
        // txs.reserve(20);

        auto section = this->read_section(SectionId(0));
        if (section.has_value()) {
            if (section->control.has_value()) {
                controls.push_back({ SectionId(0), section->control.value() });
            }

            for (const auto &tx : section->transactions) {
                txs.insert(tx);
            }
        }

        for (SectionId i = cache_section; i <= current_section_; i++) {
            auto section = this->read_section(i);
            if (!section.has_value()) {
                continue;
            }

            if (section->control.has_value()) {
                controls.push_back({ i, section->control.value() });
            }

            for (const auto &tx : section->transactions) {
                txs.insert(tx);
            }
        }

        auto section_before = this->read_section(cache_section - CONTROL_INTERVAL);
        if (section_before.has_value()) {
            if (section_before->control.has_value()) {
                controls.push_back({ cache_section - CONTROL_INTERVAL, section_before->control.value() });
            }
        }

        if (txs.empty()) {
            eLog("[Dag] No transactions to send in light mode");
            return;
        }

        auto dag_light =
            DagLightPackage { .cache = cache, .cache_section = cache_section, .txs = txs, .controls = controls };

        node->network()->send_message(dag_light,
                                      MessageType::DagLightData,
                                      SendMode::Focused,
                                      MessageStatus::Response,
                                      responder);

        eLog("[Dag] Sent light data: cache section {}, transactions count: {}, time: {}",
             cache_section,
             txs.size(),
             timer.elapsed());
    });
}

void Dag::network_response_light(const DagLightPackage &dag_light, const Responder &responder) {
    // eLog("network_response_light {}", dag_light);

    ThreadPoolBoost::instance()->post([this, responder, dag_light]() {
        // TIMER_START(network_response_light)
        cache_.reset_db();
        cache_.init_db();

        cache_.write_cached_balances(dag_light.cache, dag_light.cache_section);

        // auto min = SectionId(-1), max = SectionId(-1);
        // for (const auto &tx : std::as_const(dag_light.txs)) {
        //     min = min != -1 ? std::min(tx.section(), min) : tx.section();
        //     max = std::max(tx.section(), max);
        //     save_transaction(tx);
        // }
        this->save_transactions(dag_light.txs);

        // if (first_saved_section_ == SectionId(-1) && min >= SectionId(0)) {
        //     first_saved_section_ = min;
        //     eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
        // }

        if (dag_light.cache_section == -1 || dag_light.cache_section == 0) {
            this->first_saved_section_ = 0;
        }

        if (mode_ == DagMode::Light) {
            for (const auto &[section_id, control] : dag_light.controls) {
                this->write_control(section_id, control);
            }
        }

        this->update_range();

        if (mode_ == DagMode::Light) {
            eLog("[Dag] Light sync completed: cache section {}, saved sections from {} to {}",
                 dag_light.cache_section,
                 this->first_saved_section_,
                 this->current_section_);
        } else {
            eLog("[Dag] Balances updated");
        }

        if (mode_ == DagMode::Full) {
            this->start_control(Force::Active);
        }

        light_requested_ = false;
        this->process_cached_transactions();
        // start check hash
        // TIMER_END(network_response_light)
    });
}

void Dag::network_hash_interval(const HashInterval &hash_interval, const Responder &responder) {
    if (status_ != DagStatus::Ready) {
        eLog("[Dag] Hash interval check: ignore", hash_interval);
        return;
    }

    if (responder.luminance() < 2) {
        return;
    }

    if (hash_interval.to > current_section_) {
        eLog("[Dag] Hash interval check: ignore #2");
        return;
    }

    if (hash_interval.to + 100 < current_section_) {
        eLog("[Dag] Hash interval check: ignore #3");
        return;
    }

    // Read the control AT the boundary the peer is talking about. This used to call
    // find_last_control(to - 1), which returns our *latest* control — so once we were a
    // single interval ahead, section_id != to and the check was skipped as "ignore #4".
    // Measured on a two-node stand: 4 of 6 interval exchanges died there, i.e. the live
    // verification mostly did not run at all.
    auto last_control = this->read_control(hash_interval.to);

    if (!last_control.has_value()) {
        // The peer sealed this boundary before us. Keep the claim instead of discarding
        // it — we are Ready and only slightly behind, so the control is minutes away and
        // the comparison becomes possible then. Bounded: only the newest few boundaries
        // are worth holding, older ones are past the point of acting on.
        std::lock_guard lock(pending_intervals_mutex_);
        pending_intervals_[hash_interval.to] = hash_interval.hash;
        while (pending_intervals_.size() > 8) {
            pending_intervals_.erase(pending_intervals_.begin());
        }
        eLog("[Dag] Hash interval check: no control at {} yet — remembered", hash_interval.to);
        return;
    }

    if (last_control->control != hash_interval.hash) {
        eCritical("[Dag] Control mismatch at {}: ours {}, peer {} — refetching interval {}..{}",
                  hash_interval.to,
                  last_control->control,
                  hash_interval.hash,
                  hash_interval.from,
                  hash_interval.to);

        // Was `return;` since the feature was written (9608d71b), so a node has never
        // acted on a control mismatch — it logged "Need sync" and carried on for the
        // whole run. Only the refetch branch is enabled: the other one flips status_ to
        // Maybe from a network handler, which is the kind of side effect that likely got
        // this disabled in the first place. Re-fetching the interval is idempotent —
        // write_section merges — so the worst case is redundant traffic, not corruption.
        //
        // Guard against a stampede: several peers reporting the same boundary would
        // otherwise each trigger their own refetch of the same range.
        {
            std::lock_guard lock(refetched_intervals_mutex_);
            const auto      now = Utils::current_date_ms();
            if (auto it = refetched_intervals_.find(hash_interval.to);
                it != refetched_intervals_.end() && now - it->second < 60'000) {
                eLog("[Dag] Interval {} already refetched recently — skipping", hash_interval.to);
                return;
            }
            refetched_intervals_[hash_interval.to] = now;
            while (refetched_intervals_.size() > 16) {
                refetched_intervals_.erase(refetched_intervals_.begin());
            }
        }

        this->request_file_sections(hash_interval.from, hash_interval.to, responder);
    } else {
        eLog("[Dag] Hash interval check: true. {}", hash_interval);
    }
}

void Dag::set_sync_status(DagSyncStatus status) {
    sync_status_ = status;
}

void Dag::handle_sync_request() {
    emit node->dagTimerStop();

    // if (search_control_) {
    //     eLog("[Dag] Ignore sync, because search control");
    //     return;
    // }

    auto section = this->read_section(current_section_);

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    bool need_sync              = false;
    bool need_recontrol         = false;
    bool current_section_exists = false;

    // eLog("[Dag] current: {}; send_sync_request, last_info_: {}", current_section_, last_info_);

    auto last_control = this->find_last_control();

    if (!section.has_value()) {
        for (const auto &[_, info] : last_info_) {
            // eLog("----- {}", info);
            if (info.last_section_id >= 0 || (info.last_section_id == SectionId(0))) {
                if (mode_ == DagMode::Light) {
                    need_sync = true;
                } else {
                    need_recontrol = true;
                }
                break;
            }
        }
    } else {
        current_section_exists = true;
        const auto my_index    = section->id;
        const auto my_hash     = section->prev_hashs();

        // TODO: better cons
        for (const auto &[_, info] : last_info_) {
            if (info.last_section_id > my_index) {
                // Falling behind by more than the acceptance window is not a control
                // problem, it is a missing-sections problem: `need_recontrol` alone sends
                // us to compare control hashes and return, so a node that came back from a
                // restart 65 sections behind never fetched anything and drifted further
                // (measured: 65 → 137 in two minutes, while it rejected every incoming
                // transaction as TooSectionDiff — 467 rejections against ~120 on healthy
                // peers). See docs/TODO.md 0.75.
                // 15 = the acceptance window enforced in prove_transaction (dag.cpp:987).
                // Beyond it we are not merely lagging, we are rejecting live traffic.
                if (info.last_section_id > my_index + SectionId(15)) {
                    need_sync = true;
                } else {
                    need_recontrol = true;
                }
                break;
            }

            if (!last_control.has_value()) {
                // need_sync = true;
                emit node->dagControlStarted();
                this->start_control(Force::Active);
                last_control = this->find_last_control();
            }

            if (!last_control.has_value()) {
                need_recontrol = true;
                break;
            }

            eLog("____ {} {} {} {}",
                 last_control->section_id,
                 info.last_control_section_id,
                 last_control->control,
                 info.last_control_hash);
            eLog("____ {} {} {} {}",
                 last_control->section_id.to_string(NumeralBase::Dec),
                 info.last_control_section_id.to_string(NumeralBase::Dec),
                 last_control->control,
                 info.last_control_hash);

            if (last_control->section_id < info.last_control_section_id
                && info.last_control_section_id <= current_section_) {
                // this->start_control(true);
                need_recontrol = true;
                break;
            }

            if (last_control->section_id == info.last_control_section_id
                && last_control->control != info.last_control_hash) {
                need_recontrol = true;
                break;
            }
        }
    }

    // A hole in the middle never sets need_sync: that flag is derived purely from
    // heights (`info.last_section_id > my_index`), and a gapped node sits at the same
    // height as everyone else. Without this check start_check detects the gap, calls
    // into the sync, and the sync immediately answers "nothing to do" — measured: the
    // node logged `chain is missing section f` and then `BC 4`, leaving the hole
    // permanent. See docs/TODO.md 1.1.
    if (!need_sync && mode_ == DagMode::Full && this->find_first_gap().has_value()) {
        need_sync = true;
    }

    if (!need_sync && !need_recontrol && mode_ == DagMode::Full) {
        this->set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        this->process_cached_transactions();
        // timer_sync->stop();

        // emit syncEnd();

        eLog("BC 4");
        return; // end sync
    }

    int connections = requests_count_;
    int max_nodes   = std::min(connections, 1);

    std::vector<std::pair<std::string, SectionId>> nodes_by_block;
    for (const auto &[id, info] : last_info_) {
        if (info.last_section_id >= 0 || (info.last_section_id == SectionId(0))) {
            nodes_by_block.emplace_back(id, info.last_section_id);
        }
    }

    // TODO: recheck
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        this->set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        this->process_cached_transactions();
        // timer_sync->stop();

        // emit syncEnd();

        return;
    }

    if (nodes_by_block.size() > max_nodes) {
        std::partial_sort(nodes_by_block.begin(),
                          nodes_by_block.begin() + max_nodes,
                          nodes_by_block.end(),
                          [](const auto &a, const auto &b) {
                              return a.second > b.second;
                          });
        nodes_by_block.resize(max_nodes);
    } else {
        std::sort(nodes_by_block.begin(), nodes_by_block.end(), [](const auto &a, const auto &b) {
            return a.second > b.second;
        });
    }

    if (sync_status_ != DagSyncStatus::Sections) {
        start_sync();
        eLog("BC 1");
        return;
    }

    eLog("BC 2");
    Responder responder(node->network());
    for (const auto &[id, _] : nodes_by_block) {
        // TODO
        responder.add_identifier(id);
    }

    auto last_block  = this->read_section(current_section_);
    auto sync_index  = last_block.has_value() ? last_block->id + 1 : SectionId(0);
    // Section 0 is the sync base (genesis tx carries the network id). If we never
    // received it — even though live traffic already advanced current_section_ —
    // pull the chain from the very beginning; section sync merges idempotently.
    {
        auto zero = this->read_section(SectionId(0));
        if (!zero.has_value() || zero->transactions.empty()) {
            eLog("[Dag] handle_sync_request: genesis section missing — syncing from 0");
            sync_index = SectionId(0);
        } else if (mode_ == DagMode::Full) {
            // Full only: sync_index drives request_file_sections, which a Light node
            // never reaches (it syncs through the light package instead), and Light
            // deliberately does not store section 0, so a contiguity scan would report
            // a permanent phantom gap at the very start.
            if (auto gap = this->find_first_gap(); gap.has_value()) {
                // Resume from the hole, not from current+1: live traffic keeps advancing
                // current_section_, so a node that skipped sections while joining would
                // otherwise never ask for them again. Section sync merges idempotently.
                eLog("[Dag] handle_sync_request: gap at section {} — syncing from there", gap.value());
                sync_index = gap.value();
            }
        }
    }

    sync_last_index_ = nodes_by_block.front().second;
    // The peers' height is a snapshot from handshake time and can lag our own view;
    // never let it shrink the sync target below what we already know exists, or the
    // request range collapses (min(sync_last_index_, ...)) and the sync "completes"
    // without fetching anything. See TODO.md 1.1.
    if (current_section_exists && current_section_ > sync_last_index_) {
        sync_last_index_ = current_section_;
    }

    if (need_recontrol && mode_ == DagMode::Full) {
        if (!last_control.has_value()) {
            last_control = this->find_last_control(current_section_, true);
        }
        if (!last_control.has_value()) {
            if (current_section_ != SectionId(-1)) {
                eCritical("[Dag] Sync fatal error");
                return;
            }
        }

        if (current_section_ != SectionId(-1)) {
            this->request_control_section(last_control->section_id, responder);
            return;
        } else {
            need_sync = true;
        }
    }

    // A gap means work remains even when our height already matches the network's:
    // being level with the tip says nothing about the middle of the chain. Full only —
    // a Light node stores no section 0 by design, so the scan would report a phantom
    // gap and it could never reach Ready.
    if (current_section_exists && current_section_ >= sync_last_index_
        && (mode_ != DagMode::Full || !this->find_first_gap().has_value())) {
        eLog("[Dag] Not need sync");

        set_status(DagStatus::Ready);
        set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        // start_check();
        return;
    }

    eLog("[Dag] sync_last_index: 0x{} / {} sections",
         sync_last_index_,
         sync_last_index_.to_string(NumeralBase::Dec));
    // sync(sync_index, responder);
    if (mode_ == DagMode::Full) {
        // Start from sync_index, not current: when the genesis section is missing
        // sync_index is forced to 0 above, otherwise it equals current+1 anyway.
        auto sync_from = sync_index < current_section_ ? sync_index : current_section_;
        request_file_sections(sync_from, std::min(sync_last_index_, current_section_ + SYNC_SECTIONS_BATCH), responder);
    } else {
        auto responder_new = responder.with_new_message_id();
        node->network()->send_message(true,
                                      MessageType::DagLightData,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder_new);
        light_requested_ = true;
    }

    // request from to
    check_status_ = DagSyncStatus::None;
    emit node->dagSyncStart(sync_index, sync_last_index_);
    emit node->dagTimerStart(30000);
    // eLog("Timer start");
    eLog("syncStart, timer 30 secs");
}

void Dag::clear_dag_folder() {
#ifdef IS_APP_CLIENT
    auto dag_path      = QString::fromStdString(ChainConst::DAG_FOLDER);
    auto remove_path   = dag_path + "_to_remove";
    auto migrated_path = dag_path + "_migrated";

    // Clean up leftover from previous interrupted deletion
    if (QDir(remove_path).exists()) {
        std::thread([path = remove_path.toStdString()]() {
            QDir(QString::fromStdString(path)).removeRecursively();
        }).detach();
    }

    // One-time migration: if dag exists and not yet migrated
    if (QDir(dag_path).exists() && !QFile::exists(migrated_path)) {
        (void)QFile(migrated_path).open(QFile::WriteOnly);
        QDir().rename(dag_path, remove_path);
        std::thread([path = remove_path.toStdString()]() {
            QDir(QString::fromStdString(path)).removeRecursively();
        }).detach();

        QFile(QString::fromStdString(ChainConst::BALANCE_CACHE)).remove();
        QFile(QString::fromStdString(ChainConst::DAG_RANGE_PATH)).remove();

        current_section_     = SectionId(-1);
        first_saved_section_ = SectionId(-1);
    }
#endif
}

void Dag::clear_dag() {
#ifdef IS_APP_CLIENT
    eLog("[Dag] Clearing...");
    auto max_section = file_section(current_section_);

    QFile(QString::fromStdString(ChainConst::BALANCE_CACHE)).remove();
    QFile(QString::fromStdString(ChainConst::DAG_RANGE_PATH)).remove();

    #if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    for (SectionId i = SectionId(0); i <= max_section; ++i) {
        QString path = QString::fromStdString(ChainConst::DAG_FOLDER + "/" + i.to_string());
        QDir    dir(path);
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }
    #else
    QStringList to_delete;
    QDir        parent_dir(QString::fromStdString(ChainConst::DAG_FOLDER));
    auto        suffix = QString::fromStdString(Utils::generate_random_hex(4));

    for (SectionId i = SectionId(0); i <= max_section; ++i) {
        QString old_name = QString::fromStdString(i.to_string());
        if (!parent_dir.exists(old_name)) {
            continue;
        }
        QString new_name = old_name + "_del_" + suffix;
        if (parent_dir.rename(old_name, new_name)) {
            to_delete << QString::fromStdString(ChainConst::DAG_FOLDER) + "/" + new_name;
        }
    }

    if (!to_delete.isEmpty()) {
        #ifdef Q_OS_WIN
        QString cmd = "cmd /C \"";
        for (const QString &path : to_delete) {
            cmd += "rmdir /S /Q \"" + QDir::toNativeSeparators(path) + "\" & ";
        }
        cmd.chop(3); // remove last " & "
        cmd += "\"";
        if (!QProcess::startDetached(cmd)) {
            for (const QString &path : to_delete) {
                QDir(path).removeRecursively();
            }
        }
        #else
        if (!QProcess::startDetached("rm", QStringList() << "-rf" << to_delete)) {
            for (const QString &path : to_delete) {
                QDir(path).removeRecursively();
            }
        }
        #endif
    }
    #endif

    auto guard = cached_txs_.lock_mut();
    guard->clear();
    // sended_transactions.clear();
    current_section_     = SectionId(-1);
    first_saved_section_ = SectionId(-1);
    status_              = DagStatus::Started;

    cache_.reset_db();
    cache_.init_db();
    eLog("[Dag] Cleared");
#endif
}

void Dag::remove_sections(const SectionId &from) {
#ifndef IS_APP_CLIENT
    return;
#endif

    cache_.set_section(align_down20(from), Force::Active);
    auto to           = current_section_;
    auto correct_from = std::max(SectionId(0), from);
    current_section_  = correct_from;
    this->update_range();

    eLog("[Dag] Clear from {}", correct_from);

    for (SectionId i = to; i >= correct_from; --i) {
        auto p    = this->file_path(i);
        auto path = FsPath::create(p);

        if (!path.has_value()) {
            continue;
        }

        auto path_str = path.value().string();
        if (!path_str.has_value()) {
            continue;
        }

        QFile::remove(QString::fromStdString(path_str.value()));

        if (i % Config::DataStorage::SECTION_SIZE == 0) {
            QDir dir(QString::fromStdString(this->file_folder(i)));
            dir.removeRecursively();
        }
    }
}

void Dag::tx_list_log(const ActorId &actor_id, bool ignore_reward) {
    eLog("Start tx_list_log");
    Balances                 balances;
    std::vector<std::string> logs;

    for (SectionId i = SectionId(1); i <= current_section_; i++) {
        auto section = read_section(i);
        if (!section.has_value()) {
            continue;
        }
        if (section->transactions.empty()) {
            continue;
        }

        if (i % SectionId(10000) == 0) {
            eLog("tx_list_log on {} / {}", i.to_printable_string(), current_section_.to_printable_string());
        }

        // Process each transaction
        for (const auto &tx : section->transactions) {
            if (ignore_reward && tx.type() == TransactionType::Reward) {
                continue;
            }

            cache_.process_transaction(tx, balances);

            if (tx.sender() == ActorId(actor_id) || tx.receiver() == ActorId(actor_id)) {
                logs.push_back(
                    fmt::format("[TX] Section: {}, sender: {}, receiver: {}, type: {}, token: {}, amount: {}, "
                                "timestamp: {}, balance: {}",
                                tx.section(),
                                tx.sender(),
                                tx.receiver(),
                                tx.type(),
                                tx.token(),
                                tx.amount().to_string(NumeralBase::Dec),
                                tx.timestamp(),
                                balances[{ actor_id, tx.token() }].to_string(NumeralBase::Dec)));
            }
        }
    }

    for (const auto &log : logs) {
        eInfo("{}", log);
    }

    eLog("End tx_list_log");
}

void Dag::mint_analysis_log() {
    eLog("[Dag] mint_analysis_log: start");

    auto network_id = node->actor_index()->network_id();
    if (network_id.is_zero()) {
        eWarning("[Dag] mint_analysis_log: network_id is zero");
        return;
    }

    auto alloc_row = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(
        node->dfs()->get_db_instance(), network_id, Dfs::Basic::TEMPLATE_DICTIONARY, "token_allocations");
    if (!alloc_row.has_value()) {
        eWarning("[Dag] mint_analysis_log: token_allocations not found");
        return;
    }

    auto alloc_map = node->dfs()->read_dictionary_rows(network_id, alloc_row->file_id);
    if (!alloc_map.has_value() || alloc_map->empty()) {
        eWarning("[Dag] mint_analysis_log: token_allocations is empty");
        return;
    }

    // Parse actor:token -> minted amount from token_allocations
    using ActorTokenKey = std::pair<ActorId, TokenId>;
    std::map<ActorTokenKey, BigNumberFloat> minted;
    for (const auto& [key, val] : alloc_map.value()) {
        auto sep = key.find(':');
        if (sep == std::string::npos)
            continue;
        auto actor = ActorId::create(key.substr(0, sep));
        auto token = TokenId::create(key.substr(sep + 1));
        if (!actor.has_value() || !token.has_value())
            continue;
        auto parsed = BigNumberFloat::create(val, NumeralBase::Dec);
        if (!parsed.has_value())
            continue;
        minted[{ actor.value(), token.value() }] = parsed.value();
    }

    // Build minted pairs set and per-token minted actors set for taint tracking
    std::set<ActorTokenKey> minted_pairs;
    std::map<TokenId, std::set<ActorId>> tainted; // token -> set of tainted actors
    for (const auto& [key, _] : minted) {
        minted_pairs.insert(key);
        tainted[key.second].insert(key.first);
    }

    struct Transfer {
        ActorId        from;
        ActorId        to;
        TokenId        token;
        BigNumberFloat amount;
        SectionId      section_id;
        std::uint64_t  timestamp;
    };

    struct MintTx {
        ActorId        actor;
        TokenId        token;
        BigNumberFloat amount;
        SectionId      section_id;
        std::uint64_t  timestamp;
    };

    std::map<ActorTokenKey, BigNumberFloat> spent;
    std::vector<Transfer>  all_transfers; // all Regular txs (for taint chain)
    std::vector<MintTx>    mint_txs;

    // Scan chain from min_section to current
    static const SectionId min_section = SectionId(BigNumber("a05133", NumeralBase::Hex));
    eLog("[Dag] mint_analysis_log: scanning {} .. {}", min_section, current_section_);

    SectionId section_id = min_section;
    std::uint64_t sections_read = 0, sections_missing = 0;
    while (section_id <= current_section_) {
        auto section = read_section(section_id);
        if (!section.has_value()) {
            section_id = section_id + SectionId(1);
            ++sections_missing;
            continue;
        }
        ++sections_read;

        for (const auto& tx : section->transactions) {
            if (tx.type() == TransactionType::Minting) {
                mint_txs.push_back({ tx.receiver(), tx.token(), tx.amount(), section_id, tx.timestamp() });
            } else if (tx.type() == TransactionType::Regular) {
                auto key = ActorTokenKey { tx.sender(), tx.token() };
                if (minted_pairs.count(key)) {
                    spent[key] += tx.amount();
                }
                all_transfers.push_back({ tx.sender(), tx.receiver(), tx.token(), tx.amount(), section_id, tx.timestamp() });
            }
        }

        section_id = section_id + SectionId(1);
    }
    eLog("[Dag] mint_analysis_log: read={} missing={}", sections_read, sections_missing);

    // Build tainted chain via BFS (max depth 10)
    // transfers index: (sender, token) -> list of receivers
    std::map<ActorTokenKey, std::vector<std::pair<ActorId, SectionId>>> transfers_by_sender;
    for (const auto& t : all_transfers)
        transfers_by_sender[{ t.from, t.token }].push_back({ t.to, t.section_id });

    for (auto& [token, actors] : tainted) {
        std::vector<ActorId> queue(actors.begin(), actors.end());
        for (int depth = 0; depth < 10 && !queue.empty(); ++depth) {
            std::vector<ActorId> next;
            for (const auto& actor : queue) {
                auto it = transfers_by_sender.find({ actor, token });
                if (it == transfers_by_sender.end())
                    continue;
                for (const auto& [receiver, _] : it->second) {
                    if (!actors.count(receiver)) {
                        actors.insert(receiver);
                        next.push_back(receiver);
                    }
                }
            }
            queue = std::move(next);
        }
    }

    // Collect chain transfers (any tx involving tainted actors)
    std::vector<Transfer> chain_transfers;
    for (const auto& t : all_transfers) {
        auto it = tainted.find(t.token);
        if (it == tainted.end())
            continue;
        const auto& tainted_set = it->second;
        if (tainted_set.count(t.from) || tainted_set.count(t.to))
            chain_transfers.push_back(t);
    }

    // Collector stats: non-minted actors that received tainted tokens
    std::map<ActorTokenKey, BigNumberFloat> collector_received;
    for (const auto& t : chain_transfers) {
        if (!minted_pairs.count({ t.to, t.token }))
            collector_received[{ t.to, t.token }] += t.amount;
    }

    // ── Output ──────────────────────────────────────────────────────────────

    eLog("[Dag] mint_analysis_log: === MINT TRANSACTIONS ===");
    for (const auto& m : mint_txs) {
        eLog("[Dag] mint_analysis_log: section={} actor={} token={} amount={}",
             m.section_id, m.actor, m.token, m.amount.to_string(NumeralBase::Dec));
    }

    eLog("[Dag] mint_analysis_log: === SUMMARY PER ACTOR+TOKEN ===");
    int abuse_count = 0;
    for (const auto& [key, mint_amount] : minted) {
        const auto& [actor, token] = key;
        BigNumberFloat spent_amount = spent.count(key) ? spent.at(key) : BigNumberFloat(0);
        BigNumberFloat frozen       = mint_amount - spent_amount;
        if (frozen < BigNumberFloat(0))
            frozen = BigNumberFloat(0);
        BigNumberFloat overspend = spent_amount - mint_amount;
        bool abused = spent_amount > BigNumberFloat(0);
        if (abused)
            ++abuse_count;

        eLog("[Dag] mint_analysis_log: actor={} token={} minted={} spent={} frozen={} {}{}",
             actor, token,
             mint_amount.to_string(NumeralBase::Dec),
             spent_amount.to_string(NumeralBase::Dec),
             frozen.to_string(NumeralBase::Dec),
             abused ? "USED_BEFORE_FREEZE " : "",
             overspend > BigNumberFloat(0)
                 ? fmt::format("OVERSPEND={}", overspend.to_string(NumeralBase::Dec))
                 : "");
    }

    eLog("[Dag] mint_analysis_log: === COLLECTORS (received tainted, no direct mint) ===");
    for (const auto& [key, total] : collector_received) {
        const auto& [actor, token] = key;
        eLog("[Dag] mint_analysis_log: collector actor={} token={} received={}",
             actor, token, total.to_string(NumeralBase::Dec));
    }

    eLog("[Dag] mint_analysis_log: === TAINTED CHAIN TRANSFERS ===");
    for (const auto& t : chain_transfers) {
        bool from_minted = minted_pairs.count({ t.from, t.token }) > 0;
        bool to_minted   = minted_pairs.count({ t.to,   t.token }) > 0;
        std::string tag;
        if (from_minted && !to_minted)
            tag = "MINT->COLLECTOR";
        else if (from_minted && to_minted)
            tag = "MINT->MINT";
        else
            tag = "CHAIN";
        eLog("[Dag] mint_analysis_log: [{}] section={} from={} to={} amount={}",
             tag, t.section_id, t.from, t.to, t.amount.to_string(NumeralBase::Dec));
    }

    eLog("[Dag] mint_analysis_log: done. minted_pairs={} abused={} chain_transfers={}",
         minted.size(), abuse_count, chain_transfers.size());
}

void Dag::cache_log() {
    const auto [cached_section, balances] = cache_.read_cached_balances();

    std::vector<std::pair<std::pair<ActorId, TokenId>, BigNumberFloat>> sorted_balances(balances.begin(),
                                                                                        balances.end());

    std::sort(sorted_balances.begin(), sorted_balances.end(), [](const auto &a, const auto &b) {
        return a.second > b.second;
    });

    eLog("=== Cached Balances (sorted by amount, descending) ===");
    eLog("Total entries: {}", sorted_balances.size());

    for (const auto &[key, balance] : sorted_balances) {
        const auto &[actor_id, token_id] = key;

        eLog("ActorId: {}, TokenId: {}, Balance: {} (hex: {})",
             actor_id,
             token_id,
             balance.to_string(NumeralBase::Dec),
             balance.to_string());
    }

    eLog("=== End of cached balances ===");
}

std::map<TokenId, BigNumberFloat> Dag::sum() {
    std::map<TokenId, BigNumberFloat> token_sums;
    ActorId                           network_id = node->network_id();
    const auto                        balances   = cache_.read_cached_balances();

    for (const auto &balance_entry : balances.second) {
        const auto &balance_key   = balance_entry.first;
        const auto &balance_value = balance_entry.second;

        const ActorId &actor_id = balance_key.first;
        const TokenId &token_id = balance_key.second;

        if (actor_id == network_id) {
            continue;
        }

        token_sums[token_id] += balance_value;
    }

    return token_sums;
}

std::set<ActorId> Dag::last_month() {
    eLog("Start reward month scanning...");
    std::set<ActorId> actors;

    auto now       = Utils::current_date_ms();
    auto month_ago = now - (30LL * 24 * 60 * 60 * 1000);

    for (SectionId i = current_section_; i >= SectionId(0); i--) {
        auto section = read_section(i);
        if (!section.has_value()) {
            continue;
        }

        bool found_older = false;

        for (const auto &tx : section->transactions) {
            if (tx.type() != TransactionType::Reward) {
                continue;
            }

            auto timestamp = tx.timestamp();

            if (timestamp < month_ago) {
                found_older = true;
                break;
            }

            ActorId sender = tx.sender();
            actors.insert(sender);
        }

        if (found_older) {
            eLog("Stop at section {}", i);
            break;
        }
    }

    eLog("Last month reward actors summary:");
    eLog("  Total count: {}", actors.size());
    if (!actors.empty()) {
        eLog("  Actor ids: {}", fmt::join(actors, ", "));
    } else {
        eLog("  No reward actors found in the last 30 days");
    }

    return actors;
}

BigNumberFloat Dag::sum_all_rewards() {
    eLog("Start summing all rewards...");
    BigNumberFloat total_rewards = BigNumberFloat(0);

    for (SectionId i = SectionId(1); i <= current_section_; i++) {
        auto section = read_section(i);
        if (!section.has_value()) {
            continue;
        }

        if (i % SectionId(1000) == 0) {
            eLog("Processing section 0x{} / {} from {}", i, i.to_string(NumeralBase::Dec), current_section_);
        }

        for (const auto &tx : section->transactions) {
            if (tx.type() == TransactionType::Reward) {
                total_rewards += BigNumberFloat(tx.amount());
            }
        }
    }

    eLog("Total rewards sum: {}", total_rewards.to_string(NumeralBase::Dec));
    return total_rewards;
}

std::optional<DagControl> Dag::find_last_control(const SectionId from, bool disable_break) {
    int j  = 0;
    int jj = 0;
    // eTemp("[Dag] find_last_control: search from {}, current section: {}",
    //       from < 0 ? current_section_ : from,
    //       current_section_);
    // emit checking local?

    if (disable_break) {
        auto section = this->read_section(SectionId(0));
        if (section.has_value()) {
            if (!section->control.has_value()) {
                return std::nullopt;
            }
        }
    }

    for (SectionId i = from < 0 /*|| from > current_section_*/ ? current_section_ : from; i >= SectionId(0); i--) {
        if (i < first_saved_section_) {
            eCritical("[Dag] Try to find section < current first");
            break;
        }

        auto section = this->read_section(i);
        if (!section.has_value()) {
            if (i % CONTROL_INTERVAL_MOD == 0) {
                eLog("[Dag] No section: {}", i);
                j = 0;
                // jj++;
            }
            continue;
        }

        if (section->control.has_value()) {
            if (section->id % CONTROL_INTERVAL_MOD != 0) {
                eCritical("[Dag] Control for section {}", section->id.to_string(NumeralBase::Dec));
                continue;
            }

            return DagControl { .section_id = i, .control = section->control.value() };
        }

        j += 1;
        if (!disable_break && (j > 37 || jj > 10)) {
            break;
        }
    }

    return std::nullopt;
}

std::optional<DagControl> Dag::read_control(const SectionId &section_id) {
    auto section = read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    if (!section->control.has_value()) {
        return std::nullopt;
    }

    return DagControl { .section_id = section_id, .control = section->control.value() };
}

std::optional<DagControl> Dag::read_control_prev(const SectionId &section_id) {
    for (SectionId i = section_id; i >= SectionId(0); i--) {
        if (i % CONTROL_INTERVAL_MOD == 0) {
            return read_control(i);
        }
    }

    return std::nullopt;
}

std::optional<DagControl> Dag::read_control_next(const SectionId &section_id) {
    for (SectionId i = section_id; i <= current_section_; i++) {
        if (i % CONTROL_INTERVAL_MOD == 0) {
            return read_control(i);
        }
    }

    return std::nullopt;
}

std::optional<std::string> Dag::generate_hash_for_interval(const SectionId &start, std::string &last_hash) {
    SectionId interval_end = (start == SectionId(0) /*&& current_section_ < 20*/)
                                 ? SectionId(0)
                                 : start + (start == 0 ? CONTROL_INTERVAL_DIFF + 1 : CONTROL_INTERVAL_DIFF);

    if (start != 0 && start % 20 == 0) {
#ifdef IS_APP_UI_CLIENT
        eCritical("DAG ERROR 1: {} {}", start, interval_end);
        return std::nullopt;
#endif

        eFatal("DAG ERROR 1: {} {}", start, interval_end);
    }
    if (start != 0 && (start - 1) % 20 != 0) {
#ifdef IS_APP_UI_CLIENT
        eCritical("DAG ERROR 2: {} {}", start, interval_end);
        return std::nullopt;
#endif

        eFatal("DAG ERROR 2: {} {}", start, interval_end);
    }

    // SectionId interval_end;
    // if (start == 0) {
    //     interval_end = 0;
    // } else {
    //     // start = 1,21,41,...
    //     interval_end = start + 19; // => 1..20, 21..40, ...
    // }

    auto interval_hash = this->hash_interval(start, interval_end);
    if (!interval_hash.has_value()) {
        return std::nullopt;
    }

    if (start != SectionId(0)) {
        last_hash = Utils::calculate_hash(last_hash + interval_hash.value());
        // eTemp("----- {},  {}", last_hash, interval_hash.value());
    } else {
        last_hash = interval_hash.value();
    }

    if (last_hash.empty()) {
        return std::nullopt;
    }

    auto res = this->write_control(interval_end, last_hash);
    if (!res.has_value()) {
        return std::nullopt;
    }

    return last_hash;
}

std::optional<std::string> Dag::generate_hash_from_section(const SectionId &start,
                                                           Force            full_generation,
                                                           Force            qt_signals) {
    static bool generating = false;

    if (generating) {
        return std::nullopt;
    }

    std::string last_hash = "";

    if (start > SectionId(0)) {
        auto last_control = this->find_last_control(start - SectionId(1));
        // eLog("LL 1 {}", last_control);
        if (last_control.has_value()) {
            last_hash = last_control.value().control;
        } else {
            generating = false;
            return std::nullopt;
        }
    }

    if (/*full_generation || */ start == SectionId(0)) {
        this->generate_hash_for_interval(SectionId(0), last_hash);
        if (full_generation == Force::None) {
            generating = false;
            return last_hash;
        }
    }

    generating = true;

    SectionId current_start = start == SectionId(0) ? SectionId(1) : start;
    for (; current_start <= current_section_ && current_start < current_section_;
         current_start += CONTROL_INTERVAL) {
        if (current_start + CONTROL_INTERVAL > current_section_) {
            break;
        }

        if (current_start > cache_.section()) {
            break;
        }

        this->generate_hash_for_interval(current_start, last_hash);

        if (current_start % 600 == 1) {
            if (!node_enabled.load()) {
                return std::nullopt;
            }

            if (qt_signals == Force::Active) {
                emit node->dagControlProgress(current_start);
            }
        }
    }

    generating = false;
    return last_hash;
}

bool Dag::generate_hash(const SectionId &start_section, Force qt_signals) {
#ifndef IS_APP_CLIENT
    eTemp("[Dag] Generate AcyclicChain controls from {}...", start_section);
#endif

    eTemp("[Dag] Generate hash from {}", start_section);

    if (start_section == BigNumber(0) && current_section_ > 20 && mode_ == DagMode::Light) {
        return false;
    }

    if (qt_signals == Force::Active) {
        emit node->dagControlStarted();
    }

    if (start_section > cache_.section() && start_section != SectionId(0)) {
        emit node->dagControlEnded();
        return true;
    }

    auto result = this->generate_hash_from_section(start_section, Force::Active, qt_signals);

    if (qt_signals == Force::Active) {
        emit node->dagControlEnded();
    }

    return result.has_value();
}

std::optional<std::string> Dag::hash_interval(const SectionId &from, const SectionId &to) {
    std::string section_hashs;

    // TODO: if first < from or to

    if (status_ != DagStatus::Sync) {
        eLog("[Dag] Hash interval from {} to {}, from 0x{} to 0x{}",
             from.to_string(NumeralBase::Dec),
             to.to_string(NumeralBase::Dec),
             from,
             to);
    }

    // current or to?
    if (to > current_section_) {
        eCritical("[Dag] Section to (0x{}) > current (0x{})", to, current_section_);
        return std::nullopt;
    }

    for (SectionId i = from; i <= to; i++) {
        auto section = this->read_section(i);

        bool is_empty = false;
        if (!section.has_value()) {
            is_empty = true;
        }
        if (section.has_value() && section->transactions.empty()) {
            is_empty = true;
        }

        if (is_empty) {
            auto hash = Utils::calculate_hash(i.to_string(NumeralBase::Dec));
            section_hashs += hash;
            // eTemp("[Dag] section_hashs: no section +{} {}, {}", i, i.to_string(NumeralBase::Dec), hash);
            continue;
        }

        auto hash = Utils::calculate_hash(i.to_string(NumeralBase::Dec) + section->calculate_hash());
        section_hashs += hash;
        // eTemp("[Dag] section_hashs: section +{} {}, {}", i, i.to_string(NumeralBase::Dec), hash);
    }

    return Utils::calculate_hash(section_hashs);
}

void Dag::start_control(Force force, Force qt_signals) {
    // for tests
    // generate_hash();
    // return;

    // for tests 2
    // this->clear_controls();

#ifndef IS_APP_CLIENT
    eLog("[Dag] Check controls...");
#endif
    // TODO: make signal about do something?

    auto find_result = this->find_last_control();
    if (find_result.has_value()) {
        auto section_id = find_result->section_id;
        // write last control?
        // eTemp("[Dag] Find control in section 0x{} / {}", section_id, section_id.to_string(NumeralBase::Dec));

        if (section_id % 20 != 0) {
            eCritical("[Dag] Incorrect control section % 20 != 0: {}, remove wrong control", section_id);
            this->remove_control(section_id);
            this->start_control(Force::Active);
            return;
        }

        if (force == Force::None) {
            return;
        }
    }

    auto find_result_full = this->find_last_control(current_section_, true);

    SectionId start_from = SectionId(0);
    if (find_result_full) {
        if (find_result_full->section_id % 20 == 0) {
            start_from = find_result_full->section_id + 1;
        } else if (find_result) {
            start_from = find_result->section_id;
        }
    }

    this->generate_hash(start_from, qt_signals);
}

void Dag::clear_controls(const SectionId &from) {
    eLog("[Dag] Clear controls from {}...", from);
    for (SectionId i = from; i <= current_section_; i++) {
        auto section = read_section(i);
        if (!section.has_value()) {
            continue;
        }

        if (section->control.has_value()) {
            this->remove_control(i);
        }
    }
}

void Dag::clear_controls_async(const SectionId &from) {
    eLog("[Dag] Clear controls from {}...", from);

    const size_t    num_threads = std::thread::hardware_concurrency();
    const SectionId total       = current_section_ - from + 1;
    const SectionId chunk       = total / num_threads;

    std::vector<std::future<void>> futures;

    for (size_t t = 0; t < num_threads; ++t) {
        SectionId start = from + SectionId(t) * chunk;
        SectionId end   = (t == num_threads - 1) ? current_section_ : start + chunk - 1;

        futures.emplace_back(std::async(std::launch::async, [this, start, end]() {
            for (SectionId i = start; i <= end; i++) {
                auto section = read_section(i);
                if (section.has_value() && section->control.has_value()) {
                    remove_control(i);
                }
            }
        }));
    }

    for (auto &f : futures) {
        f.wait();
    }
}

void Dag::request_control_section(const SectionId &from_top, const Responder &responder) {
    if (search_control_) {
        eTemp("[Dag] No need request control search");
        return;
    }

    SectionId hi = align_down20(from_top < current_section_ ? from_top : current_section_);

    const int       COUNT = 16;                             // temp?
    const SectionId TOTAL = CONTROL_INTERVAL * (COUNT - 1); // 15*20

    SectionId lo;
    if (hi >= TOTAL) {
        lo = hi - TOTAL;
    } else {
        lo = SectionId(0);
    }

    search_control_ = true;
    emit node->dagSearchControlStarted();
    emit node->dagSyncStart(current_section_, current_section_);

    DagControlRangeRequest req { .from = lo, .to = hi };
    node->network()->send_message(req,
                                  MessageType::DagControlRangeRequest,
                                  responder.empty() ? SendMode::Neighbours : SendMode::Focused,
                                  MessageStatus::Request,
                                  responder.with_new_message_id());
}

void Dag::network_request_control_section(const DagControlRangeRequest &control_request,
                                          const Responder              &responder) {
    if (mode_ == DagMode::Light) {
        return;
    }

    // TODO: to thread? with status generated controls
    if (!is_aligned20(control_request.from) || !is_aligned20(control_request.to)
        || control_request.to < control_request.from) {
        eLog("[Dag] network_request_control_section Can't send control from {} to {}",
             control_request.to,
             control_request.from);
        return;
    }

    DagControlRangeResponse control_response { .from = control_request.from, .to = control_request.to };
    SectionId               from = SectionId(-1);

    for (SectionId s = control_request.from; s <= control_request.to; s += CONTROL_INTERVAL_MOD) {
        auto dag_control = this->read_control(s);
        if (!dag_control.has_value()) {
            eLog("[Dag] network_request_control_section Can't send control {}, try to regen...", s);

            if (s % 20 == 0) {
                from = s;
            }

            continue;
        }

        control_response.controls.emplace_back(dag_control.value());
    }

    if (from != -1) {
        this->clear_controls(from);
        this->start_control(Force::Active);
        this->network_request_control_section(control_request, responder);
        return;
    }

    // eTemp("[Dag] Sended control response: {}, {}", control_response.from, control_response.to);
    responder.send_response(control_response,
                            MessageType::DagControlRangeResponse,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_control_range_response(const DagControlRangeResponse &control_response,
                                         const Responder               &responder) {
    if (!is_aligned20(control_response.from) || !is_aligned20(control_response.to)
        || control_response.to < control_response.from) {
        eLog("[Dag] network_request_control_section Can't read control from {} to {}",
             control_response.to,
             control_response.from);
        search_control_ = false;
        emit node->dagSearchControlEnded();
        return;
    }

    if (responder.luminance() < 2) {
        return;
    }

    SectionId sync_from  = SectionId(-1);
    bool      force_next = false;

    for (int i = 0; i != control_response.controls.size(); i++) {
        auto section_id    = control_response.controls[i].section_id;
        auto control       = control_response.controls[i].control;
        auto local_control = this->read_control(section_id);

        // if (section_id > current_section_) {
        //     sync_from = current_section_;
        //     break;
        // }

        if (!local_control.has_value() && i == 0) {
            force_next = true;
            break;
        }

        if (!local_control.has_value() && i != 0) {
            sync_from = section_id;
            continue;
            break;
        }

        if (local_control.has_value()) {
            if (local_control->control != control) {
                sync_from = section_id;

                if (i == 0) {
                    force_next = true;
                } else {
                    force_next = false;
                }

                break;
            }
        }
    }

    if (sync_from == SectionId(-1) && !force_next) {
        // eFatal("[Dag] Sync complete!");

        if (sync_last_index_ <= current_section_) {
            search_control_ = false;
            emit node->dagSearchControlEnded();
            this->process_cached_transactions();
            return;
        } else {
            sync_from = current_section_;
        }
    }

    if (force_next) { // TODO: better search? counter of requests?
        if (control_response.from > 0) {
            const int       COUNT   = 16;
            SectionId       next_hi = (control_response.from >= CONTROL_INTERVAL_MOD)
                                          ? (control_response.from - CONTROL_INTERVAL_MOD)
                                          : SectionId(0);
            const SectionId step    = SectionId(CONTROL_INTERVAL_MOD);
            const SectionId total   = step * (COUNT - 1);
            SectionId       next_lo = (next_hi >= total) ? (next_hi - total) : SectionId(0);

            DagControlRangeRequest req { .from = next_lo, .to = next_hi };
            // eTemp("[Dag] Request controls: {}", req);
            emit node->dagControlProgress(next_lo);
            node->network()->send_message(req,
                                          MessageType::DagControlRangeRequest,
                                          SendMode::Neighbours,
                                          MessageStatus::Request,
                                          responder.with_new_message_id());
        }

        return;
    }

    if (sync_from != SectionId(-1)) {
        SectionId sync_end = control_response.to;

        eLog("[Dag] Direct request: requesting sections [{}, {}]", sync_from, sync_end);
        // sync_last_index_ = std::max(current_section_, sync_end);

        auto correct_from = std::max(SectionId(0), sync_from - 50);
        this->remove_sections(correct_from);
        check_status_ = DagSyncStatus::None;
        emit node->dagSyncStart(correct_from, sync_last_index_);
        search_control_ = false;
        emit node->dagSearchControlEnded();
        this->request_file_sections(correct_from,
                                    std::min(sync_from + SYNC_SECTIONS_BATCH, sync_last_index_),
                                    responder.with_new_message_id());
    }
}

std::set<std::string> Section::prev_hashs() const {
    std::set<std::string> hashs;

    for (const auto &tx : transactions) {
        const auto &prev_hashes = tx.prev_hashs();
        hashs.insert(prev_hashes.begin(), prev_hashes.end());
    }

    return hashs;
}

std::set<std::string> Section::hashs() const {
    std::set<std::string> hashs;

    for (const auto &tx : transactions) {
        hashs.insert(tx.hash());
    }

    return hashs;
}

std::uint64_t Section::middle() const {
    if (transactions.empty()) {
        return 0;
    }

    std::uint64_t sum = 0;

    for (const auto &tx : transactions) {
        sum += tx.timestamp();
    }

    return sum / transactions.size();
}

std::string Section::calculate_hash() const {
    std::string tx_hashs;
    for (const auto &transaction : transactions) {
        tx_hashs += transaction.hash();
    }
    return Utils::calculate_hash(tx_hashs);
}
