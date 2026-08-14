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
#include "contracts/contract_manager.h"

#include <boost/asio/post.hpp>
#include <chrono>
#include <cctype>
#include <future>
#include <limits>

#include "dfs/dfs_service.h"
#include "core/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_service.h"
#include "utils/file_io.h"
#include "utils/legacy_compression.h"

static constexpr int  SYNC_SECTIONS_BATCH             = 2100;
static constexpr int  SYNC_SECTIONS_MAX_REQ           = 2500;
static constexpr auto SYNC_LAST_INFO_COLLECTION_DELAY = std::chrono::milliseconds(250);

// Pack files are shipped in fixed-size chunks so neither side holds a whole pack
// in memory and large packs stay well under the socket buffer limit.
static constexpr std::size_t   PACK_SYNC_CHUNK                  = 256 * 1024;
static constexpr std::size_t   FILE_SYNC_MAX_COMPRESSED_BYTES   = 64 * 1024 * 1024;
static constexpr std::uint32_t FILE_SYNC_MAX_UNCOMPRESSED_BYTES = 256 * 1024 * 1024;
static constexpr std::uint64_t PACK_SYNC_MAX_ID =
    (std::numeric_limits<std::uint64_t>::max() - (Pack::SECTIONS_PER_PACK - 1)) / Pack::SECTIONS_PER_PACK;
// The hot section database and pack registry recover exact bounds after a
// crash. The range file is a compact startup hint, so do not replace it for
// every accepted section.
static constexpr long long RANGE_PERSIST_INTERVAL = 256;

Dag::Dag(ExtraChain::Core::ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node)
    , cache_(node, this)
    , pack_registry_(std::make_unique<Pack::Registry>(ChainConst::DAG_PACKS_FOLDER)) {
    bool storage_reset = false;
    std::filesystem::create_directories(ChainConst::DAG_HOT_FOLDER);
    std::filesystem::create_directories(ChainConst::DAG_PACKS_FOLDER);
    pack_registry_->rescan();
    {
        auto packs = pack_registry_->known_packs();
        std::ranges::sort(packs);
        Pack::PackId first_missing = 0;
        for (const auto pack : packs) {
            if (pack < first_missing)
                continue;
            if (pack != first_missing)
                break;
            ++first_missing;
        }
        next_pack_index_ = SectionId(first_missing);
    }

#ifdef IS_APP_CLIENT
    clear_dag_folder();
#endif

    std::filesystem::create_directories(ChainConst::DAG_HOT_FOLDER);
    std::filesystem::create_directories(ChainConst::DAG_PACKS_FOLDER);
    hot_section_store_ =
        std::make_unique<HotSectionStore>(std::filesystem::path(ChainConst::DAG_HOT_FOLDER) / "HotSections.db");

    auto settings = Utils::read_settings();
    if (node->runtime_profile() != RuntimeProfile::FullNode) {
        mode_ = DagMode::Light;
        if (settings.dag_mode != DagMode::Light) {
            settings.dag_mode = DagMode::Light;
            Utils::write_settings(settings);
        }
    } else if (settings.dag_mode.has_value()) {
        mode_ = *settings.dag_mode;
    } else {
        set_mode(DagMode::Full);
    }

    // Light nodes only index local actors. This replaces the larger legacy
    // transaction-history database without indexing the complete chain.
    if (mode_ == DagMode::Light) {
        chain_index_enabled_ = true;
    } else if (settings.chain_index_mode.has_value()) {
        chain_index_enabled_ = (*settings.chain_index_mode == ChainIndexMode::Enabled);
    } else {
        chain_index_enabled_ = true;
    }
    if (chain_index_enabled_) {
        chain_index_ = std::make_unique<ChainIndex>(node);
        eLog("[Dag] ChainIndex enabled");
    } else {
        eLog("[Dag] ChainIndex disabled");
    }

    // Control index is always available — control lookups are on the sync hot
    // path regardless of ChainIndex mode.
    control_index_ = std::make_unique<ControlIndex>(node);

    const auto range_data = FileIo::read_all(ChainConst::DAG_RANGE_PATH);
    if (range_data.has_value()) {
        auto section_range = Json::deserialize<SectionRange>(range_data.value());
        if (section_range.has_value()) {
            persisted_range_        = *section_range;
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
        }
    } else {
        clear_dag();
        storage_reset = true;
    }

    // The section stores are authoritative. Repair a stale or missing range
    // file after a stop between the section commit and the range-file update.
    if (const auto committed = hot_section_store_->committed_range(); committed.has_value()) {
        if (first_saved_section_ == SectionId(-1) || committed->first < first_saved_section_)
            first_saved_section_ = committed->first;
        if (committed->second > current_section_)
            current_section_ = committed->second;
    }
    if (const auto bounds = hot_section_store_->bounds(); bounds.has_value()) {
        if (first_saved_section_ == SectionId(-1) || bounds->first < first_saved_section_)
            first_saved_section_ = bounds->first;
        if (bounds->second > current_section_)
            current_section_ = bounds->second;
    }
    if (const auto coverage = pack_registry_->coverage(); coverage.has_value()) {
        if (first_saved_section_ == SectionId(-1) || coverage->first < first_saved_section_)
            first_saved_section_ = coverage->first;
        if (coverage->last > current_section_)
            current_section_ = coverage->last;
    }
    if (!persisted_range_.has_value()) {
        std::error_code error;
        for (const auto &entry : std::filesystem::directory_iterator(ChainConst::DAG_HOT_FOLDER, error)) {
            if (error || !entry.is_regular_file())
                continue;
            const auto section = SectionId::create(entry.path().filename().string());
            if (!section.has_value() || *section < SectionId(0))
                continue;
            if (first_saved_section_ == SectionId(-1) || *section < first_saved_section_)
                first_saved_section_ = *section;
            if (*section > current_section_)
                current_section_ = *section;
        }
    }
    update_range(true);

    transaction_cache_.make_files();
    cache_.init_db();

    timestamp_bigger_sync_start_ = 0;

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

    // After previous runs the hot/ folder may contain full pack ranges that
    // never got packed (sync was killed mid-flight, or earlier versions
    // didn't pack out-of-order completions). Sweep them on startup.
    try_pack_hot();

    if (node->runtime_profile() == RuntimeProfile::FullNode) {
        admission_state_ = create_admission_state(this);
    }

    // Automatically start so existing callers get the previous default lifecycle.
    // Callers that want explicit control can stop()/start() around migration etc.
    start();
}

Dag::~Dag() {
    stop();
    admission_state_.reset();
    cache_.dag = nullptr;
}

Dag::StatusEvent &Dag::status_event() noexcept {
    return status_event_;
}
Dag::SyncStartEvent &Dag::sync_start_event() noexcept {
    return sync_start_event_;
}
Dag::SectionEvent &Dag::sync_progress_event() noexcept {
    return sync_progress_event_;
}
ExtraChain::Core::Event<> &Dag::sync_finish_event() noexcept {
    return sync_finish_event_;
}
Dag::TimerEvent &Dag::timer_start_event() noexcept {
    return timer_start_event_;
}
ExtraChain::Core::Event<> &Dag::timer_stop_event() noexcept {
    return timer_stop_event_;
}
Dag::TransactionEvent &Dag::transaction_sent_event() noexcept {
    return transaction_sent_event_;
}
Dag::TransactionEvent &Dag::transaction_approved_event() noexcept {
    return transaction_approved_event_;
}
Dag::TransactionEvent &Dag::transaction_rejected_event() noexcept {
    return transaction_rejected_event_;
}
ExtraChain::Core::Event<> &Dag::control_started_event() noexcept {
    return control_started_event_;
}
ExtraChain::Core::Event<> &Dag::control_ended_event() noexcept {
    return control_ended_event_;
}
Dag::SectionEvent &Dag::control_progress_event() noexcept {
    return control_progress_event_;
}
ExtraChain::Core::Event<> &Dag::control_search_started_event() noexcept {
    return control_search_started_event_;
}
ExtraChain::Core::Event<> &Dag::control_search_ended_event() noexcept {
    return control_search_ended_event_;
}

void Dag::start() {
    // Idempotent: once started, later calls are no-ops so callers don't need
    // to track state themselves.
    if (started_.exchange(true)) {
        return;
    }
    accepting_messages_.store(true);
    set_admission_accepting(true);

#ifndef IS_APP_CLIENT
    this->set_status(DagStatus::Ready);
#endif

#ifndef IS_APP_CLIENT
    // Heartbeat that does not depend on the Qt event loop.
    //
    // start_check() otherwise runs only when a peer connects or when the actor
    // first-sync ends. Both are startup events, so a node that falls behind after the
    // mesh has formed never asks again — and asking is the only way it learns, because
    // `last_info_` is filled by answers to a request this node sends.
    //
    // The obvious home for this is the node's 10s status timer, and it is hooked there
    // too. It is not enough: measured on a six-node stand, that timer stopped firing
    // after ~30 seconds on four of six nodes while every other part of the process kept
    // running — network, console and DAG threads all alive and logging. Those four then
    // sat at section 1 while the other two reached 177, and four minutes later their
    // socket queues filled with transactions stamped for a section the network had long
    // passed. A plain thread with a sleep cannot be silenced the same way.
    watchdog_stop_requested_.store(false);
    watchdog_ = std::thread([this]() {
        while (!watchdog_stop_requested_.load()) {
            for (int slept = 0; slept < 15 && !watchdog_stop_requested_.load(); ++slept) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            if (watchdog_stop_requested_.load() || !started_.load()) {
                return;
            }
            schedule_watchdog_tick();
        }
    });
#endif

    eLog("[Dag] start: mode {}, current {}", mode_, current_section_);
}

void Dag::stop() {
    // First close the door on incoming work, then tear down the pieces that
    // would otherwise race against a late callback.
    bool was_started = started_.exchange(false);
    accepting_messages_.store(false);
    set_admission_accepting(false);

    // Before anything else it might touch: the watchdog calls start_check().
    if (watchdog_.joinable()) {
        watchdog_stop_requested_.store(true);
        watchdog_.join();
    }
    watchdog_tick_pending_.store(false);
    sync_check_pending_.store(false);

    if (!was_started) {
        return;
    }

    flush_admission();

    pack_hot_generation_.fetch_add(1);
    {
        std::unique_lock completion_lock(pack_hot_completion_mutex_);
        pack_hot_completion_.wait(completion_lock, [this]() {
            return !pack_hot_running_.load();
        });
    }

    if (chain_index_enabled_ && chain_index_)
        chain_index_->flush();

    update_range(true);

    timer_stop_event_.publish();

    eLog("[Dag] stop");
}

void Dag::schedule_watchdog_tick() {
    if (watchdog_tick_pending_.exchange(true)) {
        return;
    }
    boost::asio::post(node->runtime_executor(), [node = node] {
        node->dagWatchdogTick();
    });
}

void Dag::watchdog_tick() {
    watchdog_tick_pending_.store(false);
    if (!started_.load()) {
        return;
    }

    eLog("[Dag] Watchdog: info timer active={}", node->info_timer_active());

    if (mode_ != DagMode::Full) {
        return;
    }
    if (status_ == DagStatus::Ready) {
        stalled_sync_rounds_ = 0;
        if (current_section_ != last_watchdog_section_) {
            last_watchdog_section_ = current_section_;
            return;
        }
        start_check();
        return;
    }
    if (status_ != DagStatus::Sync && status_ != DagStatus::Maybe && status_ != DagStatus::Timered) {
        stalled_sync_rounds_ = 0;
        return;
    }
    if (current_section_ != last_watchdog_section_) {
        last_watchdog_section_ = current_section_;
        stalled_sync_rounds_   = 0;
        return;
    }
    if (++stalled_sync_rounds_ < 2) {
        return;
    }

    eWarning("[Dag] Sync stalled at section {} for {} rounds — restarting the sync attempt",
             current_section_.to_string(),
             stalled_sync_rounds_);
    stalled_sync_rounds_ = 0;
    timer_tick();
}

void Dag::schedule_sync_check() {
    if (sync_check_pending_.exchange(true)) {
        return;
    }
    boost::asio::post(node->runtime_executor(), [node = node] {
        node->dagSyncCheck();
    });
}

void Dag::sync_check() {
    sync_check_pending_.store(false);
    if (started_.load() && mode_ == DagMode::Full && status_ == DagStatus::Ready) {
        start_check();
    }
}

void Dag::clear_pending_sync_responses() {
    std::lock_guard lock(sync_response_request_mutex_);
    pending_section_response_.reset();
    pending_file_response_.reset();
}

std::optional<std::pair<SectionId, SectionId>> Dag::pending_sync_range(const Responder &responder,
                                                                       const SectionId &to,
                                                                       bool             file_response) const {
    std::lock_guard lock(sync_response_request_mutex_);
    const auto     &pending = file_response ? pending_file_response_ : pending_section_response_;
    if (!pending.has_value() || responder.message_id() != pending->message_id || to != pending->to) {
        return std::nullopt;
    }
    return std::pair { pending->from, pending->to };
}

void Dag::consume_pending_sync_response(const Responder &responder, bool file_response) {
    std::lock_guard lock(sync_response_request_mutex_);
    auto           &pending = file_response ? pending_file_response_ : pending_section_response_;
    if (pending.has_value() && responder.message_id() == pending->message_id) {
        pending.reset();
    }
}

bool Dag::is_accepting_messages() const {
    return accepting_messages_.load();
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
    status_event_.publish(status_);

    if (status == DagStatus::Ready) {
        clear_pending_sync_responses();
        timer_stop_event_.publish();
        min_req_count_ = 5;
    }
}

TransactionCache &Dag::transaction_cache() {
    return transaction_cache_;
}

bool Dag::should_queue_network_transaction() {
    return status_ == DagStatus::Ready || cached_txs_size() < node->runtime_limits().sync_transactions;
}

DagCache &Dag::cache() {
    return cache_;
}

ChainIndex *Dag::chain_index() {
    return chain_index_.get();
}

const ChainIndex *Dag::chain_index() const {
    return chain_index_.get();
}

bool Dag::chain_index_enabled() const {
    return chain_index_enabled_;
}

SectionId Dag::first_saved_section() {
    return first_saved_section_;
}

SectionId Dag::file_section(const SectionId &section) const {
    return section / Config::DataStorage::SECTION_SIZE;
}

std::string Dag::file_folder(const SectionId &) const {
    // Hot sections live in a flat directory until they're packed.
    return ChainConst::DAG_HOT_FOLDER;
}

std::string Dag::file_path(const SectionId &section) const {
    return fmt::format("{}/{}", ChainConst::DAG_HOT_FOLDER, section.to_string());
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
    // A node that is still syncing does not know the current section, and
    // prepare_transaction stamps `current_section_ + 1` — a number the network passed
    // long ago. Every such transaction is rejected by every peer as TooSectionDiff and
    // is then lost outright: a locally created transaction is not written to our chain
    // when sent, it waits in `sended_transactions_` for an approval that will never
    // come. Measured on a six-node stand: a node stuck at section 1942 while the
    // network reached 5157 emitted 3189 doomed transactions, and its own user saw them
    // simply vanish.
    //
    // Refusing here is not a lost transaction — it is an error the caller can act on,
    // and the only honest answer while our view of the chain is stale.
    if (status_ != DagStatus::Ready) {
        eWarning("[Dag] Refusing to send a transaction while {}: our section {} is stale",
                 status_,
                 current_section_.to_string());
        return std::unexpected(TransactionError::NotReady);
    }

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

std::expected<void, TransactionProveError> Dag::network_transaction_immediate(const Transaction &transaction,
                                                                              const Responder   &responder) {
    if (status_ != DagStatus::Final) {
        /*
        bool sync_timeout = false;
        if (timestamp_bigger_sync_start_ != 0) {
            sync_timeout = (Utils::current_date_ms() - timestamp_bigger_sync_start_) > 10000;
        }
        */

        if (status_ != DagStatus::Ready) {
            // A live transaction arriving mid-sync would otherwise be lost — the
            // peer we sync from may not have it in a sealed section yet. Park it
            // in the pending set (a cheap, deduplicated insert — no prove/save on
            // the sync path) and replay it via process_cached_transactions() once
            // sync finishes. Capped so a flood during a long sync can't grow
            // memory without bound (the per-sender rate limit above also helps).
            const auto cache_limit = node->runtime_limits().sync_transactions;
            if (cached_txs_size() < cache_limit) {
                this->add_to_cached_tx(transaction);
            } else {
                eWarning("[Dag] Pending tx cache full ({}), dropping {} during sync",
                         cache_limit,
                         transaction.hash());
            }

            // Update sync target if transaction section is ahead but within reasonable range
            if (transaction.section() > sync_last_index_
                && transaction.section() <= sync_last_index_ + SectionId(CACHE_LAG_SECTIONS)) {
                sync_last_index_ = transaction.section();
                sync_start_event_.publish(current_section_, sync_last_index_);
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

    if (transaction.type() == TransactionType::Regular) {
        const auto      sender       = NodeId { .actor_id = transaction.sender(), .node_identifier = "" };
        const auto      current_time = Utils::current_date_ms();
        std::lock_guard lock(last_txs_mutex_);
        const auto      last = last_txs_.find(sender);
        if (last != last_txs_.end() && current_time - last->second < 4500) {
            return std::unexpected(TransactionProveError::TooOften);
        }
    }

    auto                  section = read_section(transaction.section());
    TransactionProveError res =
        this->prove_transaction(transaction,
                                section.has_value() ? section->transactions : std::set<Transaction> {});
    if (res == TransactionProveError::NoError && transaction.type() == TransactionType::Regular) {
        const auto      sender       = NodeId { .actor_id = transaction.sender(), .node_identifier = "" };
        const auto      current_time = Utils::current_date_ms();
        std::lock_guard lock(last_txs_mutex_);
        auto            last = last_txs_.find(sender);
        if (last != last_txs_.end() && current_time - last->second < 4500) {
            eLog("[Dag] Ignore verified transaction from {}, diff: {} ms", sender, current_time - last->second);
            res = TransactionProveError::TooOften;
        } else {
            last_txs_.insert_or_assign(sender, current_time);
        }
    }
    TransactionResult transaction_result { .section_id = transaction.section(),
                                           .hash       = transaction.hash(),
                                           .result     = res };

    if (res == TransactionProveError::ContractDependencyMissing) {
        std::scoped_lock lock(deferred_contracts_mutex_);
        if (deferred_contracts_.size() >= MaxDeferredContractTransactions
            && !deferred_contracts_.contains(transaction.hash())) {
            deferred_contracts_.erase(deferred_contracts_.begin());
        }
        deferred_contracts_.insert_or_assign(transaction.hash(),
                                             DeferredContractTransaction {
                                                 transaction,
                                                 std::make_shared<Responder>(responder) });
        return {};
    }

    if (res != TransactionProveError::NoError) {
        auto tx = transaction;
        tx.set_prev_hashs({ "hashs" });
        eLog("[Dag] Transaction not approved: {} {}", tx, res);

        if (res == TransactionProveError::TooSectionDiff) {
            eLog("[Dag] Current: {} section (status: {}), but TooSectionDiff!: {}",
                 this->current_section().to_string(),
                 this->status(),
                 transaction.section().to_string());

            if (tx.section() > this->current_section()) {
                if (cached_txs_size() < node->runtime_limits().sync_transactions) {
                    add_to_cached_tx(transaction);
                }
                schedule_sync_check();
            }
        }
    } else {
        eLog("[Dag] Transaction from network approved: {}", transaction);
    }

    if (res == TransactionProveError::NoError) {
        auto save_result = this->save_transaction(transaction);
        if (!save_result) {
            transaction_result.result = TransactionProveError::NoSectionAdded;
            if (is_contract_transaction(transaction.type())) {
                node->finalize_contract_change(transaction.hash(), false);
            }
            // send response
            return std::unexpected(transaction_result.result);
        }

        this->set_current_section(transaction.section());
        if (is_contract_transaction(transaction.type())) {
            node->finalize_contract_change(transaction.hash(), true);
        }
    }

    if (!responder.empty()) {
        responder.send_response(transaction_result,
                                MessageType::DagTransactionResult,
                                SendMode::Focused,
                                MessageStatus::Response);
    }

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
        if (is_contract_transaction(transaction.type())) {
            node->finalize_contract_change(transaction.hash(), false);
        }
        eLog("[Dag] Our transaction not approved: {} / {}, {}",
             transaction.section().to_string(),
             transaction.hash(),
             tx_result.result);

        // if not approved > min (connections, 5)
        this->sended_transactions_.erase(tx_result.hash);
        this->failed_transactions_.insert({ tx_result.hash, transaction });
        transaction_rejected_event_.publish(transaction.section(), tx_result.hash);
        return;
    } else {
        eLog("[Dag] Our transaction approved: {} / {}", transaction.section(), transaction.hash());
        this->sended_transactions_.erase(tx_result.hash);
        transaction_approved_event_.publish(transaction.section(), tx_result.hash);
    }

    auto save_result = this->save_transaction(transaction);
    if (!save_result) {
        eLog("[Dag] Can't save our approved transaction {} in section {}",
             transaction.hash(),
             transaction.section());
        if (is_contract_transaction(transaction.type())) {
            node->finalize_contract_change(transaction.hash(), false);
        }
        return;
    }

    this->set_current_section(transaction.section());

    if (is_contract_transaction(transaction.type())) {
        node->finalize_contract_change(transaction.hash(), true);
    }

    this->check_self(transaction);
}

void Dag::check_self(const Transaction &transaction) {
    const auto my_actors = node->account_controller()->accounts_ids();
    const auto is_local  = std::ranges::any_of(my_actors, [&](const ActorId &actor) {
        return transaction.sender() == actor || transaction.receiver() == actor;
    });
    if (!is_local) {
        return;
    }

    transaction_cache_.add(transaction);

    if (transaction.type() == TransactionType::InitContract
        || transaction.type() == TransactionType::ContractDeploy) {
        node->selfTxInitContractAdded(transaction);
    }

    if (transaction.type() == TransactionType::Repeatable) {
        node->selfTxRepeatableAdded(transaction);
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

    const auto batch_limit = std::min<std::size_t>(64, node->runtime_limits().derived_sections);
    while (true) {
        std::vector<Transaction> txs_to_process;
        txs_to_process.reserve(batch_limit);
        {
            try {
                auto guard_mut = cached_txs_.lock_mut();
                if (guard_mut->empty()) {
                    break;
                }
                while (!guard_mut->empty() && txs_to_process.size() < batch_limit) {
                    auto transaction = guard_mut->extract(guard_mut->begin());
                    txs_to_process.push_back(std::move(transaction.value()));
                }
            } catch (const std::system_error &e) {
                std::cerr << "[Dag] Caught system_error in process cached 2: " << e.what() << std::endl;
            }
        }

        std::map<SectionId, std::optional<Section>> sections;
        for (const auto &tx : txs_to_process) {
            auto section = sections.find(tx.section());
            if (section == sections.end())
                section = sections.emplace(tx.section(), read_section(tx.section())).first;
            if (section->second.has_value()) {
                const auto exists =
                    std::ranges::any_of(section->second.value().transactions, [&](const auto &stored) {
                        return stored.hash() == tx.hash();
                    });
                if (exists)
                    continue;
            }
            Responder responder(node->network());
            (void)network_transaction(tx, responder);
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

void Dag::retry_contract_transactions() {
    DeferredContractMap transactions;
    {
        std::scoped_lock lock(deferred_contracts_mutex_);
        transactions.swap(deferred_contracts_);
    }
    for (const auto &[hash, deferred] : transactions) {
        static_cast<void>(hash);
        const auto &[transaction, responder] = deferred;
        static_cast<void>(network_transaction(transaction, responder ? *responder : Responder(node->network())));
    }
}

void Dag::request_contract_section(const SectionId &section_id) {
    request_sections(section_id, section_id, Responder(node->network()));
}

std::optional<Section> Dag::read_section(const SectionId &section_id) const {
    try {
        std::shared_lock<std::shared_mutex> lock(section_mutex_);

        // On-disk sections are always canonical (decimal), regardless of any
        // wire-format scope a network handler may have left active on this thread.
        WireFormat::Scope disk_scope(WireFormat::Mode::Canonical);

        // Current storage: one WAL database for the mutable tail. This avoids
        // one filesystem create operation for every accepted transaction.
        if (hot_section_store_) {
            auto content = hot_section_store_->get(section_id);
            if (content.has_value()) {
                auto section = Json::deserialize<Section>(*content);
                if (section.has_value()) {
                    section->id = section_id;
                    return section.value();
                }
            }
        }

        // Migration fallback: section files written by earlier storage code.
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

        // Cold path: look up in packs
        if (pack_registry_) {
            auto packed = pack_registry_->read_section(section_id);
            if (packed.has_value()) {
                auto section = Json::deserialize<Section>(*packed);
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

std::map<SectionId, Section> Dag::read_hot_sections(const SectionId &from, const SectionId &to) const {
    std::map<SectionId, Section> result;
    if (!hot_section_store_ || from > to)
        return result;

    try {
        std::shared_lock<std::shared_mutex> lock(section_mutex_);
        WireFormat::Scope                   disk_scope(WireFormat::Mode::Canonical);
        for (auto &[section_id, payload] : hot_section_store_->read_range(from, to)) {
            auto section = Json::deserialize<Section>(payload);
            if (!section.has_value())
                continue;
            section.value().id = section_id;
            result.emplace(section_id, std::move(section.value()));
        }
    } catch (const std::system_error &) {
        return {};
    }
    return result;
}

bool Dag::exists_section_file(const SectionId &section_id) const {
    if (hot_section_store_ && hot_section_store_->contains(section_id)) {
        return true;
    }
    auto p    = this->file_path(section_id);
    auto path = FsPath::create(p);
    if (path.has_value()) {
        return path->exists();
    }

    return false;
}

std::optional<bool> Dag::write_section(const Section &section) {
    // Deliberately no flush_admission() here: this is called with save_mutex_ already
    // held (save_transaction takes it around the whole read-insert-write cycle), and
    // waiting for the admission queue to drain while holding it is a deadlock.
    //
    // Both halves were caught in one sample: the node thread sat in
    // network_transaction_result -> save_transaction -> write_section ->
    // flush_admission, waiting for the queue; the admission worker sat in
    // network_transaction_immediate -> save_transaction, waiting for save_mutex_. Each
    // held what the other needed. Because the node thread owned ordered node work,
    // the whole node froze with it: timers stopped (3 ticks instead of 49),
    // inbound messages stopped being read, and every peer writing to it filled its send
    // queue until locally created transactions were dropped — 200+ per node.
    //
    // Callers that need the pipeline quiet before writing must flush before taking the
    // lock; see save_transaction.
    try {
        {
            std::unique_lock<std::shared_mutex> lock(section_mutex_);

            // Persist sections in canonical (decimal) form regardless of any
            // wire-format scope left active by a network handler on this thread.
            WireFormat::Scope disk_scope(WireFormat::Mode::Canonical);

            auto serialized = Json::serialize(section);
            if (hot_section_store_ && hot_section_store_->is_open()) {
                std::optional<std::pair<SectionId, SectionId>> committed_range;
                if (first_saved_section_ >= SectionId(0) && section.id >= SectionId(0)) {
                    committed_range = std::pair { std::min(first_saved_section_, section.id),
                                                  std::max(current_section_, section.id) };
                }
                if (!hot_section_store_->commit_batch({ { section.id, serialized } }, committed_range)) {
                    return std::nullopt;
                }
            } else {
                // Keep a safe fallback for an unavailable SQLite database.
                auto path = FsPath::create(this->file_path(section.id));
                if (!path.has_value() || !Utils::write_file_content(path.value(), serialized).has_value()) {
                    return std::nullopt;
                }
            }
            if (status_ != DagStatus::Sync) {
                std::lock_guard cache_lock(pack_hot_cache_mutex_);
                pack_hot_cache_.insert_or_assign(section.id, std::move(serialized));
                while (pack_hot_cache_.size() > PACK_HOT_CACHE_LIMIT) {
                    // Preserve the oldest range because it is the next range
                    // that try_pack_hot() must seal after a previous failure.
                    pack_hot_cache_.erase(std::prev(pack_hot_cache_.end()));
                }
            }
        }

        update_range();
        // Full nodes rebuild the complete index after sync. Light nodes index
        // their small local subset here and avoid a full DAG scan on a phone.
        if ((status_ != DagStatus::Sync || mode_ == DagMode::Light) && chain_index_enabled_ && chain_index_) {
            chain_index_->on_section_written(section);
        }
        // Keep the control index in step with the section's control field. Only
        // control-bearing sections touch it, so this is cheap even during sync,
        // where control hashes are read back for verification.
        if (control_index_) {
            if (section.control.has_value()) {
                control_index_->put(section.id, section.control.value());
            } else if (is_aligned20(section.id)) {
                // Only aligned sections can contain a control hash. Limit the
                // stale-row cleanup to those slots instead of writing an empty
                // DELETE transaction for every ordinary section.
                control_index_->erase(section.id);
            }
        }
        // try_pack_hot() is a no-op during sync (it checks status_ itself).
        try_pack_hot();
        return true;
    } catch (const std::system_error &e) {
        return std::nullopt;
    }
}

std::optional<std::pair<WriteResult, std::optional<SectionDiff>>> Dag::write_section_diff(const Section &section) {
    // Same-section RMW race: a sync write must not clobber a concurrent tx insert.
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    std::optional<SectionDiff>            section_diff;
    auto                                  existing_section = this->read_section(section.id);

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
    auto                                  section = this->read_section(section_id);
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

    if (control_index_)
        control_index_->put(section_id, hash);

    // A peer may have claimed this boundary before we sealed it. Now that we have our
    // own control, that claim is finally comparable — this is the point of having
    // remembered it in network_hash_interval.
    {
        std::optional<PendingIntervalClaim> claim;
        {
            std::lock_guard lock(pending_intervals_mutex_);
            if (auto it = pending_intervals_.find(section_id); it != pending_intervals_.end()) {
                claim = it->second;
                pending_intervals_.erase(it);
            }
        }

        if (claim.has_value()) {
            if (claim->hash != hash) {
                // No responder here — this runs from write_control, not a network
                // handler, so we cannot ask that peer directly. Queue the boundary;
                // the next live interval exchange drains the queue and refetches.
                eCritical("[Dag] Control mismatch at section {} (deferred): ours {}, peer {}",
                          section_id,
                          hash,
                          claim->hash);
                std::lock_guard lock(mismatched_intervals_mutex_);
                mismatched_intervals_[section_id] = claim->from;
                while (mismatched_intervals_.size() > 8) {
                    mismatched_intervals_.erase(mismatched_intervals_.begin());
                }
            } else {
                eLog("[Dag] Deferred interval check at {}: match", section_id);
            }
        }
    }

    return WriteResult::Write;
}

std::optional<WriteResult> Dag::remove_control(const SectionId &section_id) {
    std::lock_guard<std::recursive_mutex> save_lock(save_mutex_);
    auto                                  section = this->read_section(section_id);
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

    if (control_index_)
        control_index_->erase(section_id);
    return WriteResult::Write;
}

void Dag::timer_tick() {
    eLog("[Dag] Timer tick");
    timer_stop_event_.publish();
    clear_pending_sync_responses();
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
    // Quiet the admission pipeline BEFORE taking save_mutex_, never while holding it:
    // the admission worker itself calls save_transaction and so waits for this very
    // mutex, and waiting for it to drain from inside the lock deadlocks both. This used
    // to live in write_section, which runs with the lock already held.
    if (!is_admission_worker())
        flush_admission();

    // Hold save_mutex_ across the whole read-insert-write cycle: without it two
    // concurrent transactions for the same section both read the old set and one
    // insert is lost, so sections diverge between nodes and ControlIndex fails.
    //
    // unique_lock, not lock_guard: the cache update at the end must run AFTER the
    // lock is released. check_and_update_cache_thread takes the cache mutex and can
    // seal a control (write_control -> save_mutex_); another thread runs the same
    // cache update from the sync path holding the cache mutex first. Calling it from
    // under save_mutex_ is the ABBA half of a deadlock observed on the stand: four
    // threads (admission, file-sections response, sync retry, WS read) parked
    // forever, node silent for 20 minutes until SIGTERM.
    std::unique_lock<std::recursive_mutex> save_lock(save_mutex_);

    auto section = this->read_section(transaction.section());

    if (!section.has_value()) {
        // Create new section
        Section section { .id = transaction.section(), .transactions = { transaction } };

        set_current_section(section.id);

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

        const bool written = write_section(section).has_value();
        if (written) {
            cache_.apply_live_transaction(transaction);
            cache_.index_contract_transaction(transaction);
        }
        save_lock.unlock();
        if (written) {
            cache_.check_and_update_cache_thread(current_section_);
        }
        return written;
    }

    // if (section->id > current_section_) {
    //     current_section_ = section->id;
    // }

    // Add transaction to existing section
    section->transactions.insert(transaction);

    // Invalidate control if section had one - transactions changed
    if (section->control.has_value()) {
        section->control = std::nullopt;
        if (control_index_)
            control_index_->erase(section->id);
    }

    // Update first_saved_section_ if this is the first section or has a lower ID
    if (first_saved_section_ == SectionId(-1) && transaction.section() >= SectionId(0)) {
        if (mode_ == DagMode::Full || (mode_ == DagMode::Light && transaction.section() != SectionId(0))) {
            first_saved_section_ = transaction.section();
        }

        eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
    }

    const bool written = write_section(section.value()).has_value();
    if (written) {
        cache_.apply_live_transaction(transaction);
        cache_.index_contract_transaction(transaction);
    }
    save_lock.unlock();
    if (written) {
        cache_.check_and_update_cache_thread(current_section_);
    }
    return written;
}

bool Dag::local_remove_transaction(const SectionId &section_id, const std::string &hash) {
    cache_.invalidate_live_balances();
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
    if (section->control.has_value()) {
        section->control = std::nullopt;
        if (control_index_)
            control_index_->erase(section_id);
    }
    this->write_section(section.value());

    return true;
}

std::optional<std::pair<SectionId, SectionId>> Dag::save_transactions(const std::set<Transaction> &transactions) {
    cache_.invalidate_live_balances();
    if (transactions.empty()) {
        return std::nullopt;
    }
    // Same section RMW race as save_transaction — serialize the batch too.
    // unique_lock for the same reason as save_transaction: the cache update below
    // must not run under save_mutex_ (ABBA with the sync-path cache update).
    std::unique_lock<std::recursive_mutex> save_lock(save_mutex_);

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
            if (control_index_)
                control_index_->erase(section.id);
        }

        set_current_section(section_id);
        if (created) {
            if (mode_ == DagMode::Light && section_id == SectionId(0)) {
                node->actor_index()->set_network_id(first->sender());
                all_saved &= write_section(section).has_value();
                it = last;
                continue;
            }
        } else {
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

        const bool section_saved = write_section(section).has_value();
        all_saved &= section_saved;
        if (section_saved && changed) {
            for (auto transaction = first; transaction != last; ++transaction) {
                cache_.index_contract_transaction(*transaction);
            }
        }
        it = last;
    }

    if (!all_saved)
        return std::nullopt;

    save_lock.unlock();
    if (has_changes)
        cache_.check_and_update_cache_thread(current_section_);

    // if (has_changes)
    //     eTemp("[Dag] Saved sections from {} to {} with changes", min_section, max_section);
    // else
    //     eTemp("[Dag] Saved sections from {} to {} - no changes", min_section, max_section);

    return std::make_pair(min_section, max_section);
}

TransactionProveError Dag::prove_transaction(const Transaction           &tx,
                                             const std::set<Transaction> &transactions,
                                             const std::set<Transaction> *pending_transactions,
                                             const SectionId             *validation_frontier) {
    return prove_transaction_with_facts(tx,
                                        transactions,
                                        pending_transactions,
                                        nullptr,
                                        validation_frontier,
                                        nullptr);
}

TransactionProveError Dag::prove_transaction_with_facts(const Transaction           &tx,
                                                        const std::set<Transaction> &transactions,
                                                        const std::set<Transaction> *pending_transactions,
                                                        const std::unordered_set<std::string> *pending_hashes,
                                                        const SectionId                       *validation_frontier,
                                                        const TransactionValidationFacts      *facts) {
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

    // Keep the same bounded admission window for the stored or staged frontier.
    const auto current = validation_frontier != nullptr ? *validation_frontier : current_section_;
    if (!transaction_section_is_open(current, tx.section())) {
        return TransactionProveError::TooSectionDiff;
    }

    // Validate transaction amount
    if (tx.amount() == BigNumberFloat(0) && !is_contract_transaction(tx.type())) {
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

    // Verify transaction hash integrity.
    // A transaction from a legacy peer will carry a hash computed in the old hex
    // form — accept either the new canonical decimal hash or the legacy one.
    const auto  stored_hash      = facts == nullptr ? tx.hash() : std::string();
    const auto &transaction_hash = facts == nullptr ? stored_hash : facts->hash;
    const auto  hash_valid       = facts != nullptr ? facts->hash_valid
                                                    : transaction_hash == tx.calculate_hash()
                                                          || transaction_hash == tx.calculate_hash_hex();
    if (!hash_valid) {
        return TransactionProveError::WrongHash;
    }

    const bool  pending_duplicate =
        pending_hashes != nullptr ? pending_hashes->contains(transaction_hash)
                                   : pending_transactions != nullptr
                                        && std::ranges::any_of(*pending_transactions, [&](const auto &pending) {
                                               return pending.hash() == transaction_hash;
                                           });
    const bool stored_duplicate = std::ranges::any_of(transactions, [&](const auto &existing) {
        return existing.hash() == transaction_hash;
    });
    if (pending_duplicate || stored_duplicate) {
        return TransactionProveError::Duplicate;
    }

    // Validate sender
    if (targetSender.is_zero()) {
        return TransactionProveError::SenderZero;
    }

    Actor<KeyPublic> senderActor;
    const auto       sender_exists = [&]() {
        if (facts != nullptr && facts->sender_exists.has_value())
            return facts->sender_exists.value();
        senderActor = node->actor_index()->read_actor_old(targetSender);
        return !senderActor.empty();
    }();
    if (!sender_exists) {
        return TransactionProveError::SenderNotExists;
    }

    // Hash integrity was checked above against both supported encodings. Verify
    // the signature over the stored hash once. Transaction::verify() must
    // derive both hashes because it is also a standalone API; doing that here
    // repeated hashing and made every legacy-signed transaction pay for a
    // failed canonical Ed25519 verification before the successful legacy one.
    const auto verify_stored_hash = [&]() {
        if (facts != nullptr && facts->signature_valid.has_value())
            return facts->signature_valid.value();
        const auto result = senderActor.key().verify(transaction_hash, tx.signature());
        return result.has_value() && *result;
    };

    if (is_contract_transaction(tx.type())) {
        if (tx.amount() != 0 || tx.meta().value_or("").empty() || tx.meta()->size() > 1024 * 1024
            || targetReceiver.is_zero() || targetSender == targetReceiver || !tx.token().is_zero()) {
            return TransactionProveError::InvalidContractPayload;
        }

        auto receiver_actor = node->actor_index()->read_actor_old(targetReceiver);
        if (receiver_actor.empty()) {
            return tx.type() == TransactionType::ContractDeploy ? TransactionProveError::ContractDependencyMissing
                                                                : TransactionProveError::ReceiverNotExists;
        }
        if (tx.signature().empty()) {
            return TransactionProveError::MissingSignature;
        }
        if (!verify_stored_hash()) {
            return TransactionProveError::InvalidSignature;
        }
        return node->validate_contract_transaction(tx);
    }

    if (tx.type() == TransactionType::Regular && !tx.token().is_zero()) {
        auto token_contract = node->contract_manager()->inspect(tx.token().to_string());
        if (token_contract.has_value() && token_contract->kind == "fungible-token") {
            return TransactionProveError::InvalidContractPayload;
        }
    }

    // Special handling for Burn transactions
    if (tx.type() == TransactionType::Burn) {
        if (!tx.receiver().is_zero()) {
            return TransactionProveError::BurnIncorrectReceiver;
        }

        bool verify = verify_stored_hash();
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
    const auto       receiver_exists = [&]() {
        if (facts != nullptr && facts->receiver_exists.has_value())
            return facts->receiver_exists.value();
        receiverActor = node->actor_index()->read_actor_old(targetReceiver);
        return !receiverActor.empty();
    }();
    if (!receiver_exists) {
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

        bool verify = verify_stored_hash();
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

    bool verify = verify_stored_hash();
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
    if (pending_transactions != nullptr && !pending_transactions->empty()) {
        Balances balances { { std::pair { targetSender, token }, senderBalance } };
        for (const auto &pending : *pending_transactions) {
            if (pending.section() <= tx.section())
                cache_.apply_transaction_delta(pending, balances);
        }
        senderBalance = balances[std::pair { targetSender, token }];
    }
    BigNumberFloat transactionAmount = tx.amount();

    // Check if the sender has sufficient balance
    if (senderBalance < transactionAmount) {
        return TransactionProveError::SenderBalanceBelowZero;
    }

    // Freeze check: block spending of minted amount (Regular only)
    if (tx.type() == TransactionType::Regular) {
        auto network_id = node->actor_index()->network_id();
        if (!network_id.is_zero()) {
            const auto minted_amount = frozen_token_allocation(targetSender, token);
            if (minted_amount.has_value() && senderBalance - *minted_amount < transactionAmount) {
                return TransactionProveError::SenderBalanceBelowZero;
            }
        }
    }

    return TransactionProveError::NoError;
}

std::optional<BigNumberFloat> Dag::frozen_token_allocation(const ActorId &actor, const TokenId &token) {
    std::lock_guard lock(token_allocations_mutex_);
    const auto      network_id = node->actor_index()->network_id();
    if (network_id.is_zero())
        return std::nullopt;

    if (token_allocations_owner_ != network_id) {
        token_allocations_owner_ = network_id;
        token_allocations_file_id_.reset();
        token_allocations_cache_.clear();
        token_allocations_cache_loaded_ = false;
    }

    const auto key = std::pair { actor, token };
    if (!token_allocations_file_id_.has_value()) {
        const auto row =
            Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                              network_id,
                                                                              Dfs::Basic::TEMPLATE_DICTIONARY,
                                                                              "token_allocations");
        if (!row.has_value())
            return std::nullopt;
        token_allocations_file_id_ = row->file_id;
    }

    if (!token_allocations_cache_loaded_) {
        const auto rows = node->dfs()->read_dictionary_rows(network_id, *token_allocations_file_id_);
        if (!rows.has_value()) {
            token_allocations_file_id_.reset();
            return std::nullopt;
        }
        for (const auto &[stored_key, stored_value] : *rows) {
            const auto separator = stored_key.find(':');
            if (separator == std::string::npos)
                continue;
            const auto stored_actor = ActorId::create(stored_key.substr(0, separator));
            const auto stored_token = TokenId::create(stored_key.substr(separator + 1));
            const auto amount       = BigNumberFloat::create(stored_value);
            if (stored_actor.has_value() && stored_token.has_value() && amount.has_value()) {
                token_allocations_cache_.insert_or_assign(std::pair { *stored_actor, *stored_token }, *amount);
            }
        }
        token_allocations_cache_loaded_ = true;
    }
    const auto cached = token_allocations_cache_.find(key);
    return cached != token_allocations_cache_.end() ? std::optional<BigNumberFloat>(cached->second) : std::nullopt;
}

void Dag::invalidate_token_allocations() {
    std::lock_guard lock(token_allocations_mutex_);
    token_allocations_cache_.clear();
    token_allocations_cache_loaded_ = false;
}

void Dag::add_transaction_sended(const Transaction &transaction) {
    // eLog("[Dag] Add to sended: {}", transaction.hash());
    sended_transactions_.insert({ transaction.hash(), transaction });
    transaction_sent_event_.publish(transaction.section(), transaction.hash());
}

void Dag::update_range(bool allow_lower_first) {
    try {
        std::lock_guard<std::mutex> lock(range_mutex_);

        // Calls that permit a lower first section are explicit lifecycle or
        // recovery checkpoints. They also force an immediate range write.
        const bool force = allow_lower_first;

        auto new_first = first_saved_section_;
        auto new_last  = current_section_;

        if (persisted_range_.has_value()) {
            auto existing_first = SectionId::create(persisted_range_->first);
            if (!allow_lower_first && existing_first.has_value() && existing_first.value() != SectionId(-1)
                && new_first != SectionId(-1) && new_first < existing_first.value()) {
                eLog("[Dag] update_range blocked: new first {} < existing {}", new_first, existing_first.value());
                return;
            }

            const auto existing_last   = SectionId::create(persisted_range_->last);
            const auto first_unchanged = existing_first.has_value() && existing_first.value() == new_first;
            if (!force && first_unchanged && existing_last.has_value() && new_last >= existing_last.value()) {
                const auto distance = (new_last - existing_last.value()).to_int();
                if (distance.has_value() && *distance < RANGE_PERSIST_INTERVAL) {
                    return;
                }
            }
        }

        SectionRange next { .first       = new_first.to_string(),
                            .last        = new_last.to_string(),
                            .last_cached = cache_.section().to_string() };
        if (persisted_range_.has_value() && persisted_range_->first == next.first
            && persisted_range_->last == next.last && persisted_range_->last_cached == next.last_cached) {
            return;
        }

        const std::string json = Json::serialize(next);
        if (!FileIo::write_atomic(ChainConst::DAG_RANGE_PATH, json).has_value()) {
            eLog("[Dag] Failed to write range file");
            return;
        }
        persisted_range_ = std::move(next);
    } catch (const std::system_error &e) {
        // eCritical("[Dag] Caught system_error in update range: {}", e.what());
        return;
    }
}

std::optional<Transaction> Dag::find_transaction(const SectionId &section_id, const std::string &hash) const {
    // Direct read: section is part of the natural identity of a tx, so we
    // jump straight to its file/pack and scan its small (set-sized) tx list.
    auto section = this->read_section(section_id);
    if (!section.has_value())
        return std::nullopt;
    for (const auto &tx : section->transactions) {
        if (tx.hash() == hash)
            return tx;
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
    std::lock_guard sync_lock(sync_last_info_mutex_);
    if (status_ == DagStatus::Sync) {
        return;
    }

    // Cancel a background pack sweep before waiting for its storage lock. The
    // worker checks this generation between packs, so the Qt event thread waits
    // for at most the pack currently being sealed instead of the whole history.
    pack_hot_generation_.fetch_add(1);

    // start timer, after end -> again request
    {
        std::lock_guard pack_lock(pack_mutex_);
        if (status_ == DagStatus::Sync) {
            return;
        }
        status_ = DagStatus::Sync;
    }
    clear_pending_sync_responses();
    status_event_.publish(DagStatus::Sync);

    // if (mode_ == DagMode::Light) {
    timer_start_event_.publish(15001);
    // eLog("Timer start");
    // }

    last_info_.clear();
    set_sync_status(DagSyncStatus::LastInfo);
    requests_count_ = std::max(1, std::min(node->network()->active_connections_count(), min_req_count_));
    node->network()->send_message(true,
                                  MessageType::DagSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);
}

void Dag::start_check() {
    std::lock_guard sync_lock(sync_last_info_mutex_);
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
            // Holding genesis is not the same as being level with the network. This
            // used to return unconditionally, so a Ready node that owned section 0 —
            // which includes the node that created it — never reached
            // handle_sync_request at all: `last_info_` stayed empty, no peer height was
            // ever compared, and the node sat at its own current_section_ forever.
            // Measured on a six-node stand: the seed node stopped at section 0 while
            // the network reached 141, still reporting DagStatus::Ready.
            //
            // The height cannot be checked here: `last_info_` is only ever filled by
            // network_status_sync_response, which answers a request that start_sync
            // sends — so returning early is what keeps it empty. Reading it before
            // asking is a closed loop.
            //
            // So ask. start_sync collects peer heights, and handle_sync_request decides
            // from them whether anything needs fetching: with everyone level it logs
            // "Not need sync" and goes straight back to Ready, which is the same
            // outcome this early return produced, only now it is a decision rather than
            // an assumption. See docs/TODO.md 1.1 and 0.75.
            eLog("[Dag] start_check: at section {}, asking peers whether we are behind", current_section_);
        } else {
            eLog("[Dag] start_check: no genesis section yet — running initial sync");
        }
    }
#endif

    if (status_ != DagStatus::Ready || status_ == DagStatus::Maybe) {
        start_sync();
        // eLog("BC 12 start_check return");
        return;
    }

    last_info_.clear();
    check_status_   = DagSyncStatus::LastInfo;
    requests_count_ = std::max(1, std::min(node->network()->active_connections_count(), min_req_count_));
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

    eLog("[Dag] Peer asked for our height; we are at {}", current_section_.to_string());

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
    }

    std::uint64_t zero_timestamp =
        zero_section.has_value() ? (zero_section->transactions.size() == 1 ? zero_section->middle() : 0) : 0;

    auto last_info = DagLastInfo {
        .last_section_id         = section_id,
        .last_control_section_id = last_control.has_value() ? last_control.value().section_id : SectionId(-1),
        .last_control_hash       = last_control.has_value() ? last_control.value().control : std::string(),
        .zero_date               = zero_section.has_value() ? zero_timestamp : 0,
        .status                  = status_,
    };
    // eLog("network_status_sync_request, send: {}", last_info);,
    responder.send_response(last_info, MessageType::DagSyncLastInfo, SendMode::Focused, MessageStatus::Response);
}

void Dag::network_status_sync_response(const DagLastInfo &last_info, const Responder &responder) {
    std::lock_guard sync_lock(sync_last_info_mutex_);
    if (responder.identifiers().empty() || responder.luminance() < 2) {
        eLog("[Dag] Sync responce dropped: identifiers={}, luminance={}",
             responder.identifiers().size(),
             responder.luminance());
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

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    // A dense network must not synchronize from whichever peer answers first.
    // Give the other active peers a short bounded window, then let
    // handle_sync_request() select the responder with the highest section.
    if (last_info_.size() == 1) {
        node->schedule_dag_peer_info_collection(SYNC_LAST_INFO_COLLECTION_DELAY);
    }
    if (last_info_.size() >= static_cast<std::size_t>(requests_count_)) {
        node->cancel_dag_peer_info_collection();
        continue_with_collected_peer_info();
    }
}

void Dag::continue_with_collected_peer_info() {
    std::lock_guard sync_lock(sync_last_info_mutex_);
    if (last_info_.empty()) {
        return;
    }
    if (sync_status_ == DagSyncStatus::LastInfo) {
        set_sync_status(DagSyncStatus::Sections);
        check_status_ = DagSyncStatus::None;
        eLog("BC 6 sync status");
        handle_sync_request();
        return;
    }
    if (check_status_ == DagSyncStatus::LastInfo) {
        check_status_ = DagSyncStatus::Sections;
        eLog("BC 7 check status");
        handle_sync_request();
    }
}

void Dag::request_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    // TODO: auto add sync_last_index = to, also auto from, from + 100

    auto range         = SectionRange { .first = from == -1 ? "0" : from.to_string(), .last = to.to_string() };
    auto responder_new = responder.with_new_message_id();
    {
        std::lock_guard lock(sync_response_request_mutex_);
        pending_section_response_ = PendingSyncResponse { responder_new.message_id(), from, to };
    }
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

    const auto serialized = MessagePack::serialize(section_sync);
    const auto compressed = LegacyCompression::compress(serialized);
    if (!compressed.has_value()) {
        eWarning("[Dag] Failed to compress section response");
        return;
    }
    responder.send_response(compressed.value(),
                            MessageType::DagSections,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_request_sections_response(const std::string &compressed, const Responder &responder) {
    // Same reasoning as network_file_sections_response: the retry clock stays running
    // until the answer proves usable, so a corrupt or undecodable one does not silently
    // cancel the retry it should have caused.
    node->post_storage([this, compressed, responder]() {
        const auto uncompressed = LegacyCompression::decompress(compressed, FILE_SYNC_MAX_UNCOMPRESSED_BYTES);
        if (!uncompressed.has_value()) {
            eWarning("[Dag] Failed to decompress section response");
            return;
        }
        const auto section_sync = MessagePack::deserialize<SectionSync>(uncompressed.value());

        if (!section_sync.has_value()) {
            // eLog("network_request_sections_response 1");
            // eLog("sysync 2");
            return;
        }

        const auto expected_range = pending_sync_range(responder, section_sync->to, false);
        if (!expected_range.has_value()
            || std::ranges::any_of(section_sync->txs,
                                   [&](const auto &transaction) {
                                       return transaction.section() < expected_range.value().first
                                              || transaction.section() > expected_range.value().second;
                                   })
            || std::ranges::any_of(section_sync->controls, [&](const auto &control) {
                   return control.section_id < expected_range.value().first
                          || control.section_id > expected_range.value().second;
               })) {
            eWarning("[Dag] Reject stale or out-of-range section response {}", responder.message_id());
            return;
        }

        if (!section_sync->txs.empty()) {
            auto res = this->save_transactions(section_sync->txs);
            if (!res.has_value()) {
                // eLog("network_request_sections_response 2");
                // eLog("sysync 3");
                return;
            }

            boost::asio::post(node->runtime_executor(), [this] {
                retry_contract_transactions();
            });

            const auto &[min, max] = res.value();
        }

        consume_pending_sync_response(responder, false);
        timer_stop_event_.publish();

        if (section_sync->last_section > sync_last_index_) {
            sync_last_index_ = section_sync->last_section;
            sync_start_event_.publish(current_section_, sync_last_index_);
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

        sync_progress_event_.publish(section_sync->to);
        this->set_current_section(section_sync->to);
        // eLog("curr: {}, sync last: {}, curr + 100 {}", current_section_, sync_last_index, current_section_ +
        // 100);

        // timer_sync->start();
        timer_start_event_.publish(15002);
        this->request_file_sections(section_sync->to,
                                    std::min(sync_last_index_, section_sync->to + SYNC_SECTIONS_BATCH),
                                    responder);
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

    // Pick the section file format the peer stores on disk: legacy peers expect
    // hex-serialized sections, post-0.26 peers decimal. A legacy peer has no
    // PeerMeta entry or advertises a pre-0.26 dag, so default to hex when unknown.
    auto peer_id     = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    auto meta        = node->network()->peer_meta_for(peer_id);
    bool peer_legacy = !meta.has_value() || meta->is_legacy_dag();

    node->post_storage([this, from, to, responder, peer_legacy]() {
        std::vector<SectionFileData> sections;

        for (SectionId i = from; i <= to; i++) {
            // read_section falls back to packs, so cold (packed) history is served
            // to file-sync peers that can't speak pack-sync. file_bytes is the
            // peer's on-disk section format: legacy stores hex, post-0.26 decimal.
            auto section = this->read_section(i);
            if (!section.has_value()) {
                continue;
            }

            WireFormat::Scope disk_scope(peer_legacy ? WireFormat::Mode::Legacy : WireFormat::Mode::Canonical);
            sections.push_back(
                SectionFileData { .section_id = i, .file_bytes = Json::serialize(section.value()) });
        }

        auto file_sync = FileSectionsSync { .to = to, .sections = sections, .last_section = current_section_ };

        // The message envelope (section ids in SectionFileData/FileSectionsSync)
        // travels in the wire format, symmetric with the request and response
        // decode, independent of the peer's on-disk file_bytes format above.
        WireFormat::Scope wire_scope(WireFormat::wire());
        const auto        serialized = MessagePack::serialize(file_sync);
        const auto        compressed = LegacyCompression::compress(serialized);
        if (!compressed.has_value()) {
            eWarning("[Dag] Failed to compress file section response");
            return;
        }
        responder.send_response(compressed.value(),
                                MessageType::DagFileSections,
                                SendMode::Focused,
                                MessageStatus::Response);
    });
}

void Dag::network_file_sections_response(const std::string &compressed, const Responder &responder) {
    // The sync timeout is deliberately NOT cancelled here.
    //
    // It used to be, as the first statement of this function — before any of the
    // fourteen validation checks below, each of which can `return`. A malformed,
    // oversized or unnegotiated answer therefore disarmed the timeout and left with the
    // sync still in progress: nothing rearmed the timer, so `timer_tick` never fired,
    // and the retry it exists to trigger never happened. Measured on a six-node stand:
    // zero timer ticks across every node, and a node frozen in DagStatus::Sync at
    // section 1942 while the network reached 5157 — for an hour, emitting 3189
    // transactions that every peer rejected as TooSectionDiff.
    //
    // The timeout is now cancelled only once the answer is known to be usable, just
    // before this batch is applied. A rejected answer leaves the clock running, which
    // is exactly what a rejected answer should do.
    if (compressed.size() < sizeof(std::uint32_t) || compressed.size() > FILE_SYNC_MAX_COMPRESSED_BYTES) {
        eWarning("[Dag] Reject file sections with invalid compressed size {}", compressed.size());
        return;
    }
    const auto declared_size = LegacyCompression::declared_size(compressed);
    if (!declared_size.has_value() || declared_size.value() > FILE_SYNC_MAX_UNCOMPRESSED_BYTES) {
        eWarning("[Dag] Reject file sections with invalid declared size");
        return;
    }

    // A legacy server sends section file_bytes in hex; our disk is canonical
    // (decimal), so such sections need re-serializing before they're written.
    auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    auto meta    = node->network()->peer_meta_for(peer_id);
    if (!meta.has_value()) {
        eWarning("[Dag] Reject file sections from a peer without negotiated metadata");
        return;
    }
    bool peer_legacy = meta->is_legacy_dag();

    node->post_storage([this, compressed, responder, peer_legacy]() {
        // Dense paths can deliver the same response more than once. Serialize
        // state mutation so two workers cannot write sections and finish the
        // same sync at the same time.
        std::lock_guard response_lock(file_sync_response_mutex_);
        if (mode_ == DagMode::Light) {
            eLog("[Dag] Skip file sections response: light mode");
            return;
        }

        // Runs on a worker thread, so the dispatch-layer wire scope doesn't apply
        // here — set it explicitly so section ids decode in the wire format.
        WireFormat::Scope wire_scope(WireFormat::wire());

        const auto uncompressed = LegacyCompression::decompress(compressed, FILE_SYNC_MAX_UNCOMPRESSED_BYTES);
        if (!uncompressed.has_value()) {
            eWarning("[Dag] File sections exceed the uncompressed size limit");
            return;
        }
        const auto file_sync = MessagePack::deserialize<FileSectionsSync>(uncompressed.value());

        if (!file_sync.has_value()) {
            eLog("[Dag] File sections sync: failed to deserialize");
            return;
        }

        const auto expected_range = pending_sync_range(responder, file_sync->to, true);
        if (!expected_range.has_value() || std::ranges::any_of(file_sync->sections, [&](const auto &section) {
                return section.section_id < expected_range.value().first
                       || section.section_id > expected_range.value().second;
            })) {
            eWarning("[Dag] Reject stale or out-of-range file section response {}", responder.message_id());
            return;
        }

        const bool valid_hot_gap_response =
            hot_gap_request_.has_value() && file_sync->to == hot_gap_request_->second
            && std::ranges::any_of(file_sync->sections,
                                   [&](const auto &section) {
                                       return section.section_id == hot_gap_request_->first;
                                   })
            && std::ranges::all_of(file_sync->sections, [&](const auto &section) {
                   return section.section_id >= hot_gap_request_->first
                          && section.section_id <= hot_gap_request_->second;
               });
        // No height gate here. `if (to <= current_section_) return;` used to silently
        // discard every answer that did not raise our height — which is exactly the
        // shape of a divergence repair: same height, different section content.
        // Measured: a server stuck in Sync re-requested [24970..25039] every 30
        // seconds, five clients answered 19 times, and every answer died on that line
        // without a log. The pending-response correlation above already rejects
        // unsolicited or stale answers, so a response that reaches this point is one
        // we asked for. (valid_hot_gap_response stays computed for the reset below.)
        static_cast<void>(valid_hot_gap_response);

        std::map<SectionId, std::string> received_sections;
        bool                             cache_below_watermark_changed = false;
        for (const auto &section_data : file_sync->sections) {
            if (mode_ == DagMode::Light) {
                eLog("[Dag] Skip file section write: light mode");
                return;
            }

            std::string disk_bytes = section_data.file_bytes;
            if (peer_legacy) {
                // Normalize hex section bytes from a legacy peer into our canonical
                // (decimal) on-disk format, so later read_section parses them.
                WireFormat::Scope legacy_scope(WireFormat::Mode::Legacy);
                auto              section = Json::deserialize<Section>(section_data.file_bytes);
                if (section.has_value()) {
                    WireFormat::Scope canon_scope(WireFormat::Mode::Canonical);
                    disk_bytes = Json::serialize(section.value());
                }
            }

            {
                WireFormat::Scope canonical_scope(WireFormat::Mode::Canonical);
                auto              section = Json::deserialize<Section>(disk_bytes);
                const bool        wrong_section =
                    section.has_value() && std::ranges::any_of(section->transactions, [&](const auto &tx) {
                        return tx.section() != section_data.section_id;
                    });
                if (!section.has_value() || wrong_section) {
                    eWarning("[Dag] Reject invalid section {} in a sync batch", section_data.section_id);
                    return;
                }

                // Merge with what we already hold instead of replacing it. commit_batch
                // overwrites the row wholesale, so applying a peer's copy of a section
                // we also have would DROP every transaction only we know about — the
                // sync path would trade one divergence for another. Union the two sets;
                // if that changes the peer's set, its control no longer matches the
                // content and must not be stored as if it did.
                auto       local       = this->read_section(section_data.section_id);
                const auto local_count = local.has_value() ? local->transactions.size() : 0;
                if (local.has_value() && !local->transactions.empty()) {
                    const auto peer_count = section->transactions.size();
                    section->transactions.insert(local->transactions.begin(), local->transactions.end());
                    if (section->transactions.size() != peer_count) {
                        section->control = std::nullopt;
                        if (control_index_)
                            control_index_->erase(section_data.section_id);
                    }
                    disk_bytes = Json::serialize(section.value());
                }
                // A repair that changes a section at or below the cache watermark
                // invalidates the cached balance prefix: the cache summed the OLD
                // content of this section and no later delta will ever correct it.
                // (Observed: a healed fund transaction present in every chain but
                // absent from two nodes' balance caches forever.) Full rebuild —
                // reset_db() now also drops the watermark, so the next cache update
                // replays from the start of the chain.
                if (section_data.section_id <= cache_.section()
                    && section->transactions.size() != local_count) {
                    cache_below_watermark_changed = true;
                }
            }

            received_sections.insert_or_assign(section_data.section_id, std::move(disk_bytes));
        }

        if (!received_sections.empty() && hot_section_store_ && hot_section_store_->is_open()) {
            const auto received_first  = received_sections.begin()->first;
            const auto received_last   = received_sections.rbegin()->first;
            const auto committed_first = first_saved_section_ < SectionId(0)
                                             ? received_first
                                             : std::min(first_saved_section_, received_first);
            const auto committed_last  = std::max(current_section_, received_last);
            if (!hot_section_store_->commit_batch(received_sections,
                                                  std::pair { committed_first, committed_last })) {
                eWarning("[Dag] Failed to store a section sync batch");
                return;
            }
        }
        // The answer survived every check and is about to be applied — only now is it
        // safe to stop the retry clock. See the note at the top of this function.
        consume_pending_sync_response(responder, true);
        timer_stop_event_.publish();

        if (cache_below_watermark_changed) {
            eLog("[Dag] Sync repaired sections below the cache watermark — rebuilding the balance cache");
            cache_.reset_db();
            cache_.init_db();
        }

        if (!received_sections.empty() && (!hot_section_store_ || !hot_section_store_->is_open())) {
            for (const auto &[section_id, disk_bytes] : received_sections) {
                auto path = FsPath::create(this->file_path(section_id));
                if (path.has_value()) {
                    static_cast<void>(Utils::write_file_content(path.value(), disk_bytes));
                }
            }
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

            // ChainIndex is intentionally NOT fed per-section here: during sync
            // that would be thousands of wasted SQLite writes. It is rebuilt once
            // in bulk when sync completes (see below).
        }

        update_range();

        if (file_sync->last_section > sync_last_index_) {
            sync_last_index_ = file_sync->last_section;
            sync_start_event_.publish(current_section_, sync_last_index_);
        }

        if (file_sync->to >= sync_last_index_ - 1) {
            hot_gap_request_.reset();
            eLog("[Dag] File sync completed");

            // A sync can replace existing control slots. Drop the derived index
            // once and rebuild it from the received chain on the next lookup.
            if (control_index_) {
                control_index_->clear();
                control_index_ready_.store(false);
            }

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
                set_status(DagStatus::Ready);
                set_sync_status(DagSyncStatus::None);
                sync_finish_event_.publish();

                // The balance cache is derived state. Rebuild it from the
                // verified local sections instead of trusting a peer snapshot.
                cache_.reset_db();
                cache_.init_db();
                cache_.check_and_update_cache_thread(current_section_);
                repair_control_chain();
#endif
            }

            // Sync is done — now do the deferred bookkeeping once: seal the cold
            // tail into packs and bulk-rebuild the tx index off the network thread.
            try_pack_hot();
            if (mode_ == DagMode::Full && chain_index_enabled_ && chain_index_) {
                auto *index = chain_index_.get();
                node->post_storage([index]() {
                    index->rebuild_from_disk();
                });
            }
            return;
        }

        sync_progress_event_.publish(file_sync->to);
        timer_start_event_.publish(15002);
        this->request_file_sections(file_sync->to + 1,
                                    std::min(sync_last_index_, file_sync->to + SYNC_SECTIONS_BATCH),
                                    responder);
    });
}

void Dag::request_file_sections(const SectionId &from, const SectionId &to, const Responder &responder) {
    if (mode_ == DagMode::Light) {
        eLog("[Dag] Skip file sections request: light mode");
        return;
    }

    // SectionRange carries ids as bare strings outside the BigNumber wire path,
    // so encode them in the wire format (hex during the legacy transition) to
    // match what legacy peers send and parse. Decode mirrors this in the handler.
    bool wire_hex = WireFormat::wire() == WireFormat::Mode::Legacy;
    auto encode   = [wire_hex](const SectionId &s) {
        return wire_hex ? s.to_hex_string() : s.to_string();
    };
    auto range = SectionRange { .first = from == -1 ? std::string("0") : encode(from), .last = encode(to) };
    auto responder_new = responder.with_new_message_id();
    {
        // One in-flight request at a time. The pending slot is single: overwriting an
        // active request re-keys the expected message id, so the answer to the
        // previous request — possibly megabytes already on the wire — is then thrown
        // away as "stale or out-of-range". With a deep re-check, live mismatch
        // refetches and periodic sync all issuing requests, last-writer-wins kept
        // discarding almost every response (measured: 5 rejects on one node in one
        // quiet phase, zero applied). Let the active request finish or time out.
        std::lock_guard lock(sync_response_request_mutex_);
        const auto      now = Utils::current_date_ms();
        if (pending_file_response_.has_value() && pending_file_started_ms_ != 0
            && now - pending_file_started_ms_ < 30'000) {
            eLog("[Dag] File sections request already in flight — skipping [{}..{}]", from, to);
            return;
        }
        pending_file_started_ms_ = now;
        pending_file_response_   = PendingSyncResponse { responder_new.message_id(), from, to };
    }
    responder_new.send_response(range, MessageType::DagFileSections, SendMode::Focused, MessageStatus::Request);

    eTemp("[Dag] Request file sections from {} to {}", range.first, range.last);
}

void Dag::network_request_light(const Responder &responder) {
    node->post_storage([this, responder]() {
        const auto                                     started_at = std::chrono::steady_clock::now();
        std::set<Transaction>                          txs;
        std::vector<std::pair<SectionId, std::string>> controls;

        if (cache().section() == SectionId(-1) && current_section_ > 100) {
            return;
        }

        auto [cache_section, cached_balances] = this->cache().read_cached_balances();
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

        ExtraChain::Contracts::ContractCatalogFilter catalog_filter;
        catalog_filter.limit = 100;
        do {
            const auto catalog_page = cache().list_contracts(catalog_filter);
            for (const auto &contract : catalog_page.items) {
                const auto add_evidence = [this, &txs](std::uint64_t      section_number,
                                                       const std::string &transaction_hash) {
                    const auto evidence_section = read_section(SectionId(section_number));
                    if (!evidence_section.has_value()) {
                        return;
                    }
                    const auto evidence =
                        std::ranges::find_if(evidence_section->transactions,
                                             [&transaction_hash](const Transaction &transaction) {
                                                 return transaction.hash() == transaction_hash;
                                             });
                    if (evidence != evidence_section->transactions.end()) {
                        txs.insert(*evidence);
                    }
                };
                add_evidence(contract.deploy_section, contract.deploy_transaction_hash);
                add_evidence(contract.section, contract.transaction_hash);
            }
            catalog_filter.cursor = catalog_page.next_cursor;
        } while (catalog_filter.cursor.has_value());

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

        auto dag_light = DagLightPackage { .cache         = cached_balances,
                                           .cache_section = cache_section,
                                           .txs           = txs,
                                           .controls      = controls };

        node->network()->send_message(dag_light,
                                      MessageType::DagLightData,
                                      SendMode::Focused,
                                      MessageStatus::Response,
                                      responder);

        eLog("[Dag] Sent light data: cache section {}, transactions count: {}, time: {}",
             cache_section,
             txs.size(),
             std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at)
                 .count());
    });
}

void Dag::network_response_light(const DagLightPackage &dag_light, const Responder &responder) {
    // eLog("network_response_light {}", dag_light);

    node->post_storage([this, responder, dag_light]() {
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

        this->update_range(true);

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

        set_status(DagStatus::Ready);
        set_sync_status(DagSyncStatus::None);
        sync_finish_event_.publish();
        // start check hash
        // TIMER_END(network_response_light)
    });
}

void Dag::network_hash_interval(const HashInterval &hash_interval, const Responder &responder) {
    if (responder.luminance() < 2) {
        return;
    }

    if (status_ != DagStatus::Ready) {
        // Dropping the claim here silenced the whole verification whenever a node was
        // syncing — which is exactly when its content is most likely to diverge.
        // Measured on a four-node stand: 16 of 17 interval exchanges died on this
        // gate, and a section that both sides sealed at the same height but with
        // different content was never re-fetched. Remember the claim instead; it is
        // compared when our own control for that boundary is sealed.
        std::lock_guard lock(pending_intervals_mutex_);
        pending_intervals_[hash_interval.to] =
            PendingIntervalClaim { .from = hash_interval.from, .hash = hash_interval.hash };
        while (pending_intervals_.size() > 8) {
            pending_intervals_.erase(pending_intervals_.begin());
        }
        eLog("[Dag] Hash interval check: not ready — remembered claim at {}", hash_interval.to);
        return;
    }

    // A live exchange gives us what write_control lacks: a peer to ask. Drain the
    // boundaries whose deferred comparison found a mismatch before handling the new
    // claim (the refetched_intervals_ guard below still applies to each).
    std::map<SectionId, SectionId> to_refetch;
    {
        std::lock_guard lock(mismatched_intervals_mutex_);
        to_refetch.swap(mismatched_intervals_);
    }
    for (const auto &[to, from] : to_refetch) {
        {
            // Only the bookkeeping is under the lock — request_file_sections talks to
            // the network and must not run while holding it.
            std::lock_guard lock(refetched_intervals_mutex_);
            const auto      now = Utils::current_date_ms();
            if (auto it = refetched_intervals_.find(to);
                it != refetched_intervals_.end() && now - it->second < 60'000) {
                continue;
            }
            refetched_intervals_[to] = now;
        }
        eCritical("[Dag] Deferred control mismatch at {} — refetching interval {}..{}", to, from, to);
        this->request_file_sections(from, to, responder);
    }

    if (hash_interval.to > current_section_) {
        eLog("[Dag] Hash interval check: ignore #2");
        return;
    }

    if (hash_interval.to + 100 < current_section_) {
        eLog("[Dag] Hash interval check: ignore #3");
        return;
    }

    // Read the control AT the boundary the peer names. This used to call
    // find_last_control(to - 1), which returns our *latest* control, then compare its
    // section_id with `to` — so the moment we were one interval ahead the check bailed
    // out as "ignore #4". Measured on a six-node stand: half of all interval exchanges
    // died there (6 of 12), i.e. the live verification mostly did not run.
    // With ControlIndex this lookup is O(1), so there is no cost argument for the walk.
    auto last_control = this->read_control(hash_interval.to);

    if (!last_control.has_value()) {
        // The peer sealed this boundary before us. Keep the claim instead of discarding
        // it — we are Ready and only slightly behind, so our control is minutes away
        // and the comparison becomes possible then. Bounded to the newest few.
        std::lock_guard lock(pending_intervals_mutex_);
        pending_intervals_[hash_interval.to] =
            PendingIntervalClaim { .from = hash_interval.from, .hash = hash_interval.hash };
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

        // Was `return;` since the feature was written, so a node has never acted on a
        // control mismatch — it logged "Need sync" and carried on. Only the refetch
        // branch is enabled: the other one flips status_ to Maybe from a network
        // handler, which is the kind of side effect that likely got this disabled in
        // the first place. Re-fetching is idempotent (write_section merges), so the
        // worst case is redundant traffic, not corruption.
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
    timer_stop_event_.publish();

    // if (search_control_) {
    //     eLog("[Dag] Ignore sync, because search control");
    //     return;
    // }

    auto section = this->read_section(current_section_);

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    bool                     need_sync              = false;
    bool                     need_recontrol         = false;
    bool                     current_section_exists = false;
    std::optional<SectionId> hot_gap_from;

    // eLog("[Dag] current: {}; send_sync_request, last_info_: {}", current_section_, last_info_);

    auto last_control = this->find_last_control();
    if (mode_ == DagMode::Full && current_section_ >= SectionId(0)) {
        const auto hot_floor = current_section_ >= SectionId(HOT_PACK_LAG - 1)
                                   ? current_section_ - SectionId(HOT_PACK_LAG - 1)
                                   : SectionId(0);
        const auto scan_from =
            first_saved_section_ == SectionId(-1) ? hot_floor : std::max(hot_floor, first_saved_section_);
        for (auto id = scan_from; id <= current_section_; id += SectionId(1)) {
            if (!read_section(id).has_value()) {
                hot_gap_from = id;
                need_sync    = true;
                break;
            }
        }
    }

    if (!section.has_value()) {
        for (const auto &[_, info] : last_info_) {
            // eLog("----- {}", info);
            if (info.last_section_id >= 0 || (info.last_section_id == SectionId(0))) {
                need_sync = true;
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
                // Falling behind by more than the acceptance window is a missing-section
                // problem, not a control problem. `need_recontrol` alone ends in
                // request_control_section() followed by `return` — no section is ever
                // fetched — so a node that came back from a restart 65 sections behind
                // kept drifting (measured: 65 -> 137 in two minutes) while rejecting
                // every incoming transaction as TooSectionDiff: 606 rejections against
                // ~130 on healthy peers. See docs/TODO.md 0.75.
                //
                // The hot-tail gap scan above does not cover this: it walks up to
                // `current_section_`, this node's *own* height, and a node that is
                // merely behind has no hole below it — its chain is contiguous, just
                // short. Only a comparison against the peers' height sees it.
                //
                // 15 = the acceptance window enforced in prove_transaction. Beyond it we
                // are not merely lagging, we are rejecting live traffic.
                if (info.last_section_id > my_index + SectionId(15)) {
                    need_sync = true;
                } else if (info.last_control_section_id < SectionId(0)) {
                    need_sync = true;
                } else {
                    need_recontrol = true;
                }
                break;
            }

            if (!last_control.has_value()) {
                if (info.last_control_section_id < SectionId(0)) {
                    continue;
                }
                control_started_event_.publish();
                this->start_control(Force::Active);
                last_control = this->find_last_control();
            }

            if (!last_control.has_value()) {
                need_recontrol = true;
                break;
            }

            eLog("____ {} {} {} {}",
                 last_control.value().section_id,
                 info.last_control_section_id,
                 last_control.value().control,
                 info.last_control_hash);
            eLog("____ {} {} {} {}",
                 last_control.value().section_id.to_string(),
                 info.last_control_section_id.to_string(),
                 last_control.value().control,
                 info.last_control_hash);

            if (last_control.value().section_id < info.last_control_section_id
                && info.last_control_section_id <= current_section_) {
                // Build the missing controls from local sections before a peer request.
                // The local result must still match the control reported by the peer.
                this->start_control(Force::Active);
                last_control = this->find_last_control();

                if (!last_control.has_value() || last_control.value().section_id < info.last_control_section_id
                    || (last_control.value().section_id == info.last_control_section_id
                        && last_control.value().control != info.last_control_hash)) {
                    need_recontrol = true;
                    break;
                }
            }

            if (last_control.value().section_id == info.last_control_section_id
                && last_control.value().control != info.last_control_hash) {
                need_recontrol = true;
                break;
            }
        }
    }

    if (!need_sync && !need_recontrol && mode_ == DagMode::Full) {
        std::string peers;
        for (const auto &[id, info] : last_info_) {
            peers += fmt::format("{}={} ", id.substr(0, 6), info.last_section_id.to_string());
        }
        eLog("[Dag] Sync decision: nothing to do at section {} (peers: {})",
             current_section_.to_string(),
             peers.empty() ? "none" : peers);
        this->set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        this->process_cached_transactions();
        this->try_pack_hot();
        // timer_sync->stop();

        // Controls are per-interval, not chained: a section that closed with different
        // content on two nodes only differs in ITS boundary's hash, and this decision
        // path compares the latest boundary alone — once the network moves one interval
        // on, the divergence becomes permanently invisible to every trigger we have.
        // Measured on a six-node stand: section 25001 lost transactions on two joiners
        // and 5 minutes of idle start_checks never looked back at boundary 25020.
        // So, on a quiet network, occasionally re-verify the recent boundary window
        // (request_control_section covers the last 16 = 300 sections; its response path
        // Direct-requests any mismatching interval, and section merge is idempotent).
        if (last_control.has_value() && !last_info_.empty()) {
            const auto now = Utils::current_date_ms();
            if (now - last_deep_control_check_ms_ >= 60'000) {
                last_deep_control_check_ms_ = now;
                Responder deep_responder(node->network());
                deep_responder.add_identifier(last_info_.begin()->first);
                eLog("[Dag] Deep control re-check at {}", last_control->section_id);
                this->request_control_section(last_control->section_id, deep_responder);
            }
        }

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
        this->try_pack_hot();
        // timer_sync->stop();

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
        if (check_status_ != DagSyncStatus::Sections) {
            start_sync();
            eLog("BC 1");
            return;
        }

        // The check already collected the same signed peer state that sync
        // needs. Keep it so live traffic cannot delay a duplicate round trip.
        pack_hot_generation_.fetch_add(1);
        {
            std::lock_guard pack_lock(pack_mutex_);
            status_ = DagStatus::Sync;
        }
        status_event_.publish(DagStatus::Sync);
        timer_start_event_.publish(15001);
        set_sync_status(DagSyncStatus::Sections);
        check_status_ = DagSyncStatus::None;
    }

    eLog("BC 2");
    Responder responder(node->network());
    for (const auto &[id, _] : nodes_by_block) {
        // TODO
        responder.add_identifier(id);
    }

    auto last_block = this->read_section(current_section_);
    auto sync_index = hot_gap_from.value_or(last_block.has_value() ? last_block->id + 1 : SectionId(0));
    // Section 0 is the sync base (genesis tx carries the network id). If we never
    // received it — even though live traffic already advanced current_section_ —
    // pull the chain from the very beginning; section sync merges idempotently.
    {
        auto zero = this->read_section(SectionId(0));
        if (!zero.has_value() || zero->transactions.empty()) {
            eLog("[Dag] handle_sync_request: genesis section missing — syncing from 0");
            sync_index = SectionId(0);
        }
    }

    sync_last_index_ = nodes_by_block.front().second;

    // A control mismatch can be a consequence of a missing section. Fetch the
    // section first; accepting a peer control before its source data would hide
    // the gap and leave this node unable to reproduce the control hash.
    if (need_recontrol && !need_sync && !hot_gap_from.has_value() && mode_ == DagMode::Full) {
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

    if (!hot_gap_from.has_value() && current_section_exists && current_section_ >= sync_last_index_) {
        eLog("[Dag] Not need sync");

        // Drain any transactions parked while we were deciding whether to sync,
        // then go Ready (process_cached_transactions sets Ready + sync None).
        check_status_ = DagSyncStatus::None;
        this->process_cached_transactions();
        this->try_pack_hot();
        // start_check();
        return;
    }

    eLog("[Dag] sync_last_index: {} sections", sync_last_index_.to_string());
    // sync(sync_index, responder);
    if (mode_ == DagMode::Full) {
        if (sync_index == SectionId(0) && current_section_ >= SectionId(0)) {
            request_file_sections(SectionId(0),
                                  std::min(sync_last_index_, SectionId(SYNC_SECTIONS_BATCH)),
                                  responder);
        } else {
            // Pack-capable peer: fetch cold history wholesale, then file-sync only the
            // hot tail once pack-sync finishes (see issue_next_pack_request) so the
            // two paths never overlap. Legacy peers fall straight through to file-sync.
            auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
            auto meta    = node->network()->peer_meta_for(peer_id);
            if (!hot_gap_from.has_value() && meta.has_value() && meta->supports_pack_sync()) {
                start_pack_sync(responder);
            } else {
                const auto request_to = std::min(sync_last_index_, sync_index + SYNC_SECTIONS_BATCH);
                if (hot_gap_from.has_value()) {
                    std::lock_guard response_lock(file_sync_response_mutex_);
                    hot_gap_request_ = std::pair { sync_index, request_to };
                }
                request_file_sections(sync_index, request_to, responder);
            }
        }
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
    sync_start_event_.publish(sync_index, sync_last_index_);
    timer_start_event_.publish(30000);
    // eLog("Timer start");
    eLog("syncStart, timer 30 secs");
}

void Dag::clear_dag_folder() {
#ifdef IS_APP_CLIENT
    const std::filesystem::path dag_path      = ChainConst::DAG_FOLDER;
    const std::filesystem::path remove_path   = ChainConst::DAG_FOLDER + "_to_remove";
    const std::filesystem::path migrated_path = ChainConst::DAG_FOLDER + "_migrated";
    std::error_code             error;

    if (std::filesystem::exists(remove_path, error)) {
        node->post_storage([remove_path]() {
            std::error_code remove_error;
            std::filesystem::remove_all(remove_path, remove_error);
        });
    }

    error.clear();
    if (std::filesystem::exists(dag_path, error) && !std::filesystem::exists(migrated_path, error)
        && FileIo::write_atomic(migrated_path, {}).has_value()) {
        std::filesystem::rename(dag_path, remove_path, error);
        if (!error) {
            node->post_storage([remove_path]() {
                std::error_code remove_error;
                std::filesystem::remove_all(remove_path, remove_error);
            });
        }
        std::filesystem::remove(ChainConst::BALANCE_CACHE, error);
        std::filesystem::remove(ChainConst::DAG_RANGE_PATH, error);

        current_section_     = SectionId(-1);
        first_saved_section_ = SectionId(-1);
    }
#endif
}

void Dag::clear_dag() {
#ifdef IS_APP_CLIENT
    eLog("[Dag] Clearing...");
    pack_hot_generation_.fetch_add(1);
    std::lock_guard pack_lock(pack_mutex_);
    auto            max_section = file_section(current_section_);

    std::error_code error;
    std::filesystem::remove(ChainConst::BALANCE_CACHE, error);
    std::filesystem::remove(ChainConst::DAG_RANGE_PATH, error);

    #if defined(__ANDROID__) || defined(__APPLE__)
    for (SectionId i = SectionId(0); i <= max_section; ++i) {
        std::filesystem::remove_all(std::filesystem::path(ChainConst::DAG_FOLDER) / i.to_string(), error);
    }
    #else
    std::vector<std::filesystem::path> to_delete;
    const std::filesystem::path        parent_dir = ChainConst::DAG_FOLDER;
    const auto                         suffix     = Utils::generate_random_hex(4);

    for (SectionId i = SectionId(0); i <= max_section; ++i) {
        const auto old_path = parent_dir / i.to_string();
        if (!std::filesystem::exists(old_path, error)) {
            continue;
        }
        const auto new_path = parent_dir / (i.to_string() + "_del_" + suffix);
        std::filesystem::rename(old_path, new_path, error);
        if (!error) {
            to_delete.push_back(new_path);
        }
        error.clear();
    }

    if (!to_delete.empty()) {
        node->post_storage([paths = std::move(to_delete)]() {
            for (const auto &path : paths) {
                std::error_code remove_error;
                std::filesystem::remove_all(path, remove_error);
            }
        });
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

    // Also clear hot and packed storage. Keep the open SQLite files in place;
    // Windows does not allow removal of an open database.
    std::error_code ec;
    if (hot_section_store_)
        hot_section_store_->clear();
    for (const auto &entry : std::filesystem::directory_iterator(ChainConst::DAG_HOT_FOLDER, ec)) {
        if (ec)
            break;
        const auto name = entry.path().filename().string();
        if (name == "HotSections.db" || name == "HotSections.db-wal" || name == "HotSections.db-shm")
            continue;
        std::filesystem::remove_all(entry.path(), ec);
        if (ec)
            break;
    }
    std::filesystem::remove_all(ChainConst::DAG_PACKS_FOLDER, ec);
    std::filesystem::create_directories(ChainConst::DAG_HOT_FOLDER);
    std::filesystem::create_directories(ChainConst::DAG_PACKS_FOLDER);
    if (pack_registry_)
        pack_registry_->rescan();
    next_pack_index_ = SectionId(0);
    {
        std::lock_guard lock(pack_hot_cache_mutex_);
        pack_hot_cache_.clear();
    }
    if (chain_index_enabled_ && chain_index_)
        chain_index_->clear();
    if (control_index_)
        control_index_->clear();

    eLog("[Dag] Cleared");
#endif
}

// ---- Pack-level sync (peers with dag_version >= 100) ------------------------

void Dag::network_pack_list_request(const Responder &responder) {
    if (!pack_registry_)
        return;

    const auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    const auto meta    = node->network()->peer_meta_for(peer_id);
    if (!meta.has_value() || !meta->supports_pack_sync()) {
        eWarning("[Dag] Reject pack list request without negotiated pack support");
        return;
    }

    PackList list;
    auto     spans = pack_registry_->spans(); // in-memory metadata, no file I/O
    list.packs.reserve(spans.size());
    for (const auto &s : spans) {
        list.packs.push_back(PackInfo {
            .pack_id       = s.id,
            .first_section = s.first,
            .last_section  = s.last,
        });
    }

    node->network()->send_message(list,
                                  MessageType::DagPackList,
                                  SendMode::Focused,
                                  MessageStatus::Response,
                                  responder);
}

void Dag::network_pack_request(const PackRequest &req, const Responder &responder) {
    if (!pack_registry_)
        return;

    const auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    const auto meta    = node->network()->peer_meta_for(peer_id);
    if (!meta.has_value() || !meta->supports_pack_sync()) {
        eWarning("[Dag] Reject pack request without negotiated pack support");
        return;
    }

    auto total = pack_registry_->pack_byte_size(req.pack_id);
    if (!total.has_value() || *total == 0 || *total > PACK_SYNC_MAX_PACK_BYTES) {
        eWarning("[Dag] Pack {} requested but missing locally", req.pack_id);
        return; // peer will time out / move on
    }

    auto chunk = pack_registry_->read_chunk(req.pack_id, req.offset, PACK_SYNC_CHUNK);
    if (!chunk.has_value()) {
        eWarning("[Dag] Pack {} chunk at {} read failed", req.pack_id, req.offset);
        return;
    }

    PackData out {
        .pack_id    = req.pack_id,
        .offset     = req.offset,
        .total_size = *total,
        .bytes      = std::move(*chunk),
    };
    node->network()->send_message(out,
                                  MessageType::DagPackData,
                                  SendMode::Focused,
                                  MessageStatus::Response,
                                  responder);
}

void Dag::start_pack_sync(const Responder &responder) {
    // Fresh message id: the core dedups Requests by message_id, so reusing the shared sync responder's id gets
    // dropped.
    node->network()->send_message(PackList {},
                                  MessageType::DagPackList,
                                  SendMode::Focused,
                                  MessageStatus::Request,
                                  responder.with_new_message_id());
}

void Dag::network_pack_list_response(const PackList &list, const Responder &responder) {
    if (!pack_registry_)
        return;

    const auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    const auto meta    = node->network()->peer_meta_for(peer_id);
    if (!meta.has_value() || !meta->supports_pack_sync() || list.packs.size() > PACK_SYNC_MAX_PACKS) {
        eWarning("[Dag] Reject invalid or unnegotiated pack list");
        return;
    }

    auto local_ids = pack_registry_->known_packs();
    std::sort(local_ids.begin(), local_ids.end());

    std::vector<Pack::PackId> missing;
    missing.reserve(list.packs.size());
    std::unordered_set<Pack::PackId> advertised;
    for (const auto &p : list.packs) {
        if (p.pack_id > PACK_SYNC_MAX_ID) {
            eWarning("[Dag] Reject pack list with overflowing id {}", p.pack_id);
            return;
        }
        const SectionId expected_first(p.pack_id * Pack::SECTIONS_PER_PACK);
        const SectionId expected_last = expected_first + Pack::SECTIONS_PER_PACK - 1;
        if (p.first_section != expected_first || p.last_section != expected_last
            || !advertised.insert(p.pack_id).second) {
            eWarning("[Dag] Reject pack list with invalid range for pack {}", p.pack_id);
            return;
        }
        if (!std::binary_search(local_ids.begin(), local_ids.end(), p.pack_id)) {
            missing.push_back(p.pack_id);
        }
    }
    std::sort(missing.begin(), missing.end()); // ascending — sync history forward

    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        pack_sync_pending_       = std::move(missing);
        pack_sync_in_flight_     = false;
        pack_sync_installed_any_ = false;
        pack_sync_peer_          = peer_id;
        pack_sync_fallback_from_ = std::nullopt;
    }

    eLog("[Dag] Pack sync: {} packs missing locally", pack_sync_pending_.size());
    issue_next_pack_request(responder);
}

void Dag::issue_next_pack_request(const Responder &responder) {
    Pack::PackId             next_id;
    bool                     has_next      = false;
    bool                     finished      = false;
    bool                     installed_any = false;
    std::optional<SectionId> fallback_from;
    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        if (pack_sync_in_flight_)
            return;
        if (pack_sync_pending_.empty()) {
            eLog("[Dag] Pack sync: all packs received");
            finished                 = true;
            fallback_from            = pack_sync_fallback_from_;
            installed_any            = pack_sync_installed_any_;
            pack_sync_installed_any_ = false;
        } else {
            next_id = pack_sync_pending_.front();
            pack_sync_pending_.erase(pack_sync_pending_.begin());
            pack_sync_in_flight_   = true;
            pack_sync_current_id_  = next_id;
            pack_sync_next_offset_ = PACK_SYNC_CHUNK;
            pack_sync_total_size_  = 0;
            pack_sync_outstanding_offsets_.clear();
            pack_sync_received_offsets_.clear();
            pack_sync_outstanding_offsets_.insert(0);
            has_next = true;
        }
    }

    if (finished) {
        // Installed packs extend history backwards, so pull first_saved_section_ down to their coverage.
        SectionId tail_from = current_section_ + 1;
        if (pack_registry_) {
            auto known_packs = pack_registry_->known_packs();
            std::sort(known_packs.begin(), known_packs.end());
            Pack::PackId contiguous_count = 0;
            for (const auto pack_id : known_packs) {
                if (pack_id != contiguous_count)
                    break;
                ++contiguous_count;
            }
            if (contiguous_count > 0) {
                const SectionId packed_last(contiguous_count * Pack::SECTIONS_PER_PACK - 1);
                if (first_saved_section_ == SectionId(-1) || SectionId(0) < first_saved_section_) {
                    first_saved_section_ = SectionId(0);
                }
                if (!fallback_from.has_value() && packed_last > current_section_) {
                    set_current_section(packed_last);
                }
                // Only a contiguous prefix can move file sync past cold history.
                if (packed_last + 1 > tail_from) {
                    tail_from = packed_last + 1;
                }
            }
        }
        if (fallback_from.has_value() && *fallback_from < tail_from) {
            tail_from = *fallback_from;
        }
        update_range(/*allow_lower_first*/ true);

        // Cold history is in place; now pull the hot tail per-section.
        if (mode_ == DagMode::Full && tail_from <= sync_last_index_) {
            request_file_sections(tail_from,
                                  std::min(sync_last_index_, tail_from + SYNC_SECTIONS_BATCH),
                                  responder);
            return;
        }

        // A chain can end exactly at a pack boundary. There is no hot tail in
        // that case, so finish the same derived-state work as file sync.
        if (mode_ == DagMode::Full) {
            if (control_index_) {
                control_index_->clear();
                control_index_ready_.store(false);
            }
            start_control();
            process_cached_transactions();
            sync_finish_event_.publish();

            cache_.reset_db();
            cache_.init_db();
            cache_.check_and_update_cache_thread(current_section_);
            repair_control_chain();
            try_pack_hot();

            if (installed_any && chain_index_enabled_ && chain_index_) {
                auto *index = chain_index_.get();
                node->post_storage([index]() {
                    index->rebuild_from_disk();
                });
            }
        }
    }
    if (!has_next)
        return;

    // Fresh message id per pack request: the core dedups Requests by message_id, dropping reuses of the sync
    // responder's id.
    PackRequest req { .pack_id = next_id, .offset = 0 };
    node->network()->send_message(req,
                                  MessageType::DagPackRequest,
                                  SendMode::Focused,
                                  MessageStatus::Request,
                                  responder.with_new_message_id());
}

void Dag::issue_pack_window(const Responder &responder) {
    std::vector<PackRequest> requests;
    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        if (!pack_sync_in_flight_ || pack_sync_total_size_ == 0)
            return;
        const auto sync_window = node->runtime_limits().pack_sync_window;
        while (pack_sync_outstanding_offsets_.size() < sync_window
               && pack_sync_next_offset_ < pack_sync_total_size_) {
            const auto offset = pack_sync_next_offset_;
            pack_sync_next_offset_ += PACK_SYNC_CHUNK;
            if (pack_sync_received_offsets_.contains(offset)
                || !pack_sync_outstanding_offsets_.insert(offset).second) {
                continue;
            }
            requests.push_back(PackRequest { .pack_id = pack_sync_current_id_, .offset = offset });
        }
    }
    for (const auto &request : requests) {
        node->network()->send_message(request,
                                      MessageType::DagPackRequest,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder.with_new_message_id());
    }
}

void Dag::network_pack_data_response(const PackData &data, const Responder &responder) {
    if (!pack_registry_)
        return;

    const auto peer_id = responder.identifiers().empty() ? std::string() : *responder.identifiers().begin();
    const auto meta    = node->network()->peer_meta_for(peer_id);
    if (!meta.has_value() || !meta->supports_pack_sync()) {
        eWarning("[Dag] Reject pack data without negotiated pack support");
        return;
    }

    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        if (!pack_sync_in_flight_ || peer_id != pack_sync_peer_ || data.pack_id != pack_sync_current_id_
            || !pack_sync_outstanding_offsets_.contains(data.offset) || data.total_size == 0
            || data.total_size > PACK_SYNC_MAX_PACK_BYTES || data.offset >= data.total_size || data.bytes.empty()
            || data.bytes.size() > PACK_SYNC_CHUNK || data.offset + data.bytes.size() > data.total_size
            || (pack_sync_total_size_ != 0 && pack_sync_total_size_ != data.total_size)) {
            eWarning("[Dag] Unexpected pack {} chunk at {}", data.pack_id, data.offset);
            return;
        }
        pack_sync_total_size_ = data.total_size;
    }

    auto res = pack_registry_->install_chunk(data.pack_id, data.offset, data.bytes, false);
    if (!res.has_value()) {
        eWarning("[Dag] Pack {} chunk at {} install failed: error {}",
                 data.pack_id,
                 data.offset,
                 static_cast<int>(res.error()));
        // Abort this pack; clear in_flight so the next pack can proceed.
        {
            std::lock_guard<std::mutex> lock(pack_sync_mutex_);
            pack_sync_in_flight_ = false;
            pack_sync_outstanding_offsets_.clear();
            pack_sync_received_offsets_.clear();
            const SectionId failed_from(data.pack_id * Pack::SECTIONS_PER_PACK);
            if (!pack_sync_fallback_from_.has_value() || failed_from < *pack_sync_fallback_from_)
                pack_sync_fallback_from_ = failed_from;
        }
        issue_next_pack_request(responder);
        return;
    }

    bool complete = false;
    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        pack_sync_outstanding_offsets_.erase(data.offset);
        pack_sync_received_offsets_.insert(data.offset);
        const auto expected = static_cast<std::size_t>((data.total_size + PACK_SYNC_CHUNK - 1) / PACK_SYNC_CHUNK);
        complete = pack_sync_received_offsets_.size() == expected && pack_sync_outstanding_offsets_.empty();
    }

    if (!complete) {
        issue_pack_window(responder);
        return;
    }

    // All offsets are present. Finalization verifies the complete pack before
    // it becomes visible in the registry.
    auto finalized = pack_registry_->install_chunk(data.pack_id,
                                                   data.total_size,
                                                   {},
                                                   true,
                                                   [this, id = data.pack_id](const Pack::Reader &reader) {
                                                       return validate_received_pack(id, reader);
                                                   });
    if (!finalized.has_value()) {
        eWarning("[Dag] Pack {} finalization failed: error {}", data.pack_id, static_cast<int>(finalized.error()));
        {
            std::lock_guard<std::mutex> lock(pack_sync_mutex_);
            pack_sync_in_flight_ = false;
            pack_sync_outstanding_offsets_.clear();
            pack_sync_received_offsets_.clear();
            const SectionId failed_from(data.pack_id * Pack::SECTIONS_PER_PACK);
            if (!pack_sync_fallback_from_.has_value() || failed_from < *pack_sync_fallback_from_)
                pack_sync_fallback_from_ = failed_from;
        }
        issue_next_pack_request(responder);
        return;
    }

    // Pack fully received and installed.
    {
        std::lock_guard<std::mutex> lock(pack_sync_mutex_);
        pack_sync_in_flight_     = false;
        pack_sync_installed_any_ = true;
        pack_sync_outstanding_offsets_.clear();
        pack_sync_received_offsets_.clear();
    }
    eLog("[Dag] Pack {} installed ({} bytes)", data.pack_id, data.total_size);

    issue_next_pack_request(responder);
}

bool Dag::validate_pack_controls(Pack::PackId id, const std::map<SectionId, Section> &sections) const {
    const auto reject = [id](std::string_view reason) {
        eWarning("[Dag] Reject pack {}: {}", id, reason);
        return false;
    };
    if (id > PACK_SYNC_MAX_ID)
        return reject("pack id overflow");

    const SectionId expected_first(id * Pack::SECTIONS_PER_PACK);
    const SectionId expected_last = expected_first + Pack::SECTIONS_PER_PACK - 1;
    if (sections.size() != Pack::SECTIONS_PER_PACK || sections.begin()->first != expected_first
        || sections.rbegin()->first != expected_last) {
        return reject("incomplete section range");
    }

    const auto read_for_control = [&](const SectionId &section_id) -> std::optional<Section> {
        if (const auto it = sections.find(section_id); it != sections.end())
            return it->second;
        return read_section(section_id);
    };

    SectionId  control_id = expected_first;
    const auto remainder  = control_id % CONTROL_INTERVAL;
    if (remainder != 0)
        control_id = control_id + (CONTROL_INTERVAL - remainder);

    for (; control_id <= expected_last; control_id = control_id + CONTROL_INTERVAL) {
        const auto control_section = read_for_control(control_id);
        if (!control_section.has_value() || !control_section.value().control.has_value())
            return reject("missing control section");

        const SectionId interval_first =
            control_id == SectionId(0) ? SectionId(0) : control_id - CONTROL_INTERVAL_DIFF;
        std::string section_hashes;
        for (SectionId section_id = interval_first; section_id <= control_id; section_id = section_id + 1) {
            const auto section = read_for_control(section_id);
            if (!section.has_value())
                return reject("incomplete control interval");
            const auto input = section.value().transactions.empty()
                                   ? section_id.to_string()
                                   : section_id.to_string() + section.value().calculate_hash();
            section_hashes += Utils::calculate_hash(input);
        }

        auto expected_control = Utils::calculate_hash(section_hashes);
        if (control_id != SectionId(0)) {
            const auto previous = read_for_control(control_id - CONTROL_INTERVAL);
            if (!previous.has_value() || !previous.value().control.has_value())
                return reject("missing previous control");
            expected_control = Utils::calculate_hash(previous.value().control.value() + expected_control);
        }
        if (control_section.value().control.value() != expected_control)
            return reject("control hash mismatch");
    }

    return true;
}

bool Dag::validate_received_pack(Pack::PackId id, const Pack::Reader &reader) const {
    const auto reject = [id](std::string_view reason) {
        eWarning("[Dag] Reject pack {}: {}", id, reason);
        return false;
    };
    if (id > PACK_SYNC_MAX_ID)
        return reject("pack id overflow");

    const SectionId expected_first(id * Pack::SECTIONS_PER_PACK);
    const SectionId expected_last = expected_first + Pack::SECTIONS_PER_PACK - 1;
    if (reader.id() != id || reader.first_section() != expected_first || reader.last_section() != expected_last
        || reader.count() != Pack::SECTIONS_PER_PACK) {
        return reject("header range mismatch");
    }

    const auto rows = reader.read_range(expected_first, expected_last);
    if (rows.size() != Pack::SECTIONS_PER_PACK)
        return reject("incomplete section range");

    std::map<SectionId, Section>                      sections;
    std::unordered_map<std::string, Actor<KeyPublic>> actor_cache;

    const auto valid_control = [](const std::optional<std::string> &control) {
        if (!control.has_value())
            return true;
        return control->size() == 64 && std::ranges::all_of(*control, [](unsigned char value) {
                   return std::isxdigit(value) != 0;
               });
    };

    for (const auto &[section_id, payload] : rows) {
        WireFormat::Scope canonical(WireFormat::Mode::Canonical);
        auto              section = Json::deserialize<Section>(payload);
        if (!section.has_value())
            return reject("section parse failed");
        // The pack frame index is the authoritative section location. Existing
        // readers apply the same normalization to older packed payloads.
        section->id = section_id;
        if (!valid_control(section->control))
            return reject("invalid control encoding");

        for (const auto &tx : section->transactions) {
            if (tx.section() != section_id)
                return reject("transaction section mismatch");
            if (tx.hash() != tx.calculate_hash() && tx.hash() != tx.calculate_hash_hex())
                return reject("transaction hash mismatch");
            if (tx.type() == TransactionType::Genesis || tx.type() == TransactionType::Balance)
                continue;
            if (tx.signature().empty())
                return reject("missing transaction signature");

            const auto sender_id = tx.sender().to_string();
            auto       sender    = actor_cache.find(sender_id);
            if (sender == actor_cache.end()) {
                auto loaded = node->actor_index()->read_actor_old(tx.sender());
                if (loaded.empty())
                    return reject("unknown transaction sender");
                sender = actor_cache.emplace(sender_id, std::move(loaded)).first;
            }

            // The content hash was checked above. Verify the signature against
            // that exact stored hash, instead of recalculating and checking both
            // canonical and legacy preimages for every transaction.
            const auto signature_valid = sender->second.key().verify(tx.hash(), tx.signature());
            if (!signature_valid.has_value() || !signature_valid.value())
                return reject("invalid transaction signature");
        }
        sections.emplace(section_id, std::move(*section));
    }

    return validate_pack_controls(id, sections);
}

// ---- Balance-cache snapshot (peers with dag_version >= 100) ------------------

void Dag::network_cache_snapshot_request(const Responder &responder) {
    (void)responder;
    eWarning("[Dag] Ignore cache snapshot request: network snapshots are not trusted");
}

void Dag::network_cache_snapshot_response(const std::string &compressed, const Responder &responder) {
    (void)compressed;
    (void)responder;
    eWarning("[Dag] Reject cache snapshot: rebuild balances from verified sections");
}

void Dag::request_cache_snapshot(const Responder &responder) {
    (void)responder;
}

// -----------------------------------------------------------------------------

void Dag::try_pack_hot() {
    if (!pack_registry_)
        return;

    // Sealing a pack is irreversible. Never do it while a sync is in flight:
    // set_current_section() only moves forward, so a high section arriving first
    // can push current_section_ ahead of sections that are still downloading, and
    // we'd seal their slots as empty. Packing only the at-rest cold tail (status
    // Ready, or at startup before any sync starts) avoids that race.
    if (status_ == DagStatus::Sync)
        return;

    // Sync can complete several pack ranges at once. Start at the first range
    // that is not sealed. Do not scan every old pack on every live write.
    auto      section_size = Config::DataStorage::SECTION_SIZE;
    SectionId lag(HOT_PACK_LAG);
    // We pack [N*size .. N*size+size-1] only after `current` is HOT_PACK_LAG
    // sections past pack_last, so reorgs and out-of-order delivery can still
    // mutate the trailing window without rewriting an immutable pack.
    if (current_section_ < section_size + lag)
        return;

    SectionId max_pack_idx = ((current_section_ - lag) / section_size) - SectionId(1);
    if (max_pack_idx < SectionId(0))
        return;

    const auto first_saved = first_saved_section_;
    const auto generation  = pack_hot_generation_.load();
    if (!started_.load()) {
        pack_hot_sections(max_pack_idx, first_saved, generation);
        return;
    }

    bool expected = false;
    if (!pack_hot_running_.compare_exchange_strong(expected, true))
        return;

    try {
        node->post_storage([this, max_pack_idx, first_saved, generation]() {
            try {
                pack_hot_sections(max_pack_idx, first_saved, generation);
            } catch (const std::exception &error) {
                eWarning("[Dag] Pack worker failed: {}", error.what());
            } catch (...) {
                eWarning("[Dag] Pack worker failed");
            }
            finish_pack_hot();
        });
    } catch (const std::exception &error) {
        finish_pack_hot();
        eWarning("[Dag] Failed to schedule pack worker: {}", error.what());
    } catch (...) {
        finish_pack_hot();
        eWarning("[Dag] Failed to schedule pack worker");
    }
}

void Dag::finish_pack_hot() {
    {
        std::lock_guard completion_lock(pack_hot_completion_mutex_);
        pack_hot_running_.store(false);
    }
    pack_hot_completion_.notify_all();
}

void Dag::pack_hot_sections(const SectionId    &max_pack_idx,
                            const SectionId    &first_saved_section,
                            const std::uint64_t generation) {
    std::lock_guard pack_lock(pack_mutex_);
    if (generation != pack_hot_generation_.load() || status_ == DagStatus::Sync)
        return;
    const auto section_size = Config::DataStorage::SECTION_SIZE;

    // On-disk pack payloads are canonical (decimal) — the empty placeholder and
    // any re-read content must not pick up an ambient wire (hex) scope.
    WireFormat::Scope disk_scope(WireFormat::Mode::Canonical);

    while (next_pack_index_ <= max_pack_idx) {
        if (generation != pack_hot_generation_.load() || status_ == DagStatus::Sync)
            return;

        const auto pack_idx   = next_pack_index_;
        SectionId  pack_first = pack_idx * section_size;

        if (pack_registry_->find_pack_for_section(pack_first).has_value()) {
            if (hot_section_store_)
                hot_section_store_->erase_range(pack_first, pack_first + section_size - 1);
            next_pack_index_ += 1;
            continue;
        }

        SectionId pack_last = pack_first + section_size - 1;

        // A genuinely-empty section legitimately has no hot file. But a section
        // missing because it hasn't been downloaded yet must NOT be sealed as
        // empty — that would silently corrupt control hashes and fork the node.
        // We only treat absence as "empty" for ranges that sit entirely above
        // first_saved_section_ (i.e. fully within our synced history). A range
        // that dips below first_saved_section_ is incompletely synced; skip it.
        if (first_saved_section != SectionId(-1) && pack_first < first_saved_section) {
            next_pack_index_ += 1;
            continue;
        }

        // Gather all section files in this range. Missing file == empty section
        // (sync skips sections with no transactions). With HOT_PACK_LAG already
        // guarding the moment, every id in this range has been "passed by" sync
        // — so absence is an empty section, not pending data.
        std::map<SectionId, std::string> sections = hot_section_store_
                                                        ? hot_section_store_->read_range(pack_first, pack_last)
                                                        : std::map<SectionId, std::string> {};
        {
            // Copy the complete candidate range under one lock. Do not lock
            // once per section and do not keep the lock during disk I/O.
            std::lock_guard cache_lock(pack_hot_cache_mutex_);
            auto            cached = pack_hot_cache_.lower_bound(pack_first);
            const auto      end    = pack_hot_cache_.upper_bound(pack_last);
            while (cached != end) {
                sections.emplace(cached->first, cached->second);
                ++cached;
            }
        }
        const std::string empty_serialized = Json::serialize(Section { .id = SectionId(0) });
        for (SectionId s = pack_first; s <= pack_last; s = s + 1) {
            if (sections.contains(s))
                continue;
            auto p   = this->file_path(s);
            auto fsp = FsPath::create(p);
            if (fsp.has_value() && fsp->exists()) {
                auto content = Utils::read_file_content(fsp.value());
                if (content.has_value()) {
                    sections.emplace(s, std::string(content->begin(), content->end()));
                    continue;
                }
            }
            // Missing or unreadable -> empty placeholder section.
            sections.emplace(s, empty_serialized);
        }

        Pack::PackId pid;
        {
            auto candidate_int = pack_idx.to_int();
            if (!candidate_int.has_value() || *candidate_int < 0) {
                eCritical("[Dag] Invalid pack cursor: {}", pack_idx.to_string());
                return;
            }
            pid = static_cast<Pack::PackId>(*candidate_int);
        }

        std::map<SectionId, Section> parsed_sections;
        for (const auto &[section_id, payload] : sections) {
            auto section = Json::deserialize<Section>(payload);
            if (!section.has_value()) {
                eWarning("[Dag] Defer pack {}: section {} cannot be parsed", pid, section_id);
                return;
            }
            section.value().id = section_id;
            parsed_sections.emplace(section_id, std::move(section.value()));
        }
        if (!validate_pack_controls(pid, parsed_sections)) {
            eWarning("[Dag] Defer pack {} until its control intervals are complete", pid);
            return;
        }

        auto res = pack_registry_->create_pack(pid, sections);
        if (!res.has_value()) {
            eWarning("[Dag] Failed to pack sections {}..{} (error {})",
                     pack_first,
                     pack_last,
                     static_cast<int>(res.error()));
            return;
        }

        // The immutable pack is valid and visible. The corresponding mutable
        // rows can now be removed in one database transaction.
        if (hot_section_store_ && !hot_section_store_->erase_range(pack_first, pack_last)) {
            eWarning("[Dag] Failed to remove packed hot rows {}..{}", pack_first, pack_last);
        }

        // Remove hot files that are now safely packed.
        for (SectionId s = pack_first; s <= pack_last; s = s + 1) {
            std::error_code ec;
            std::filesystem::remove(this->file_path(s), ec);
        }
        {
            std::lock_guard cache_lock(pack_hot_cache_mutex_);
            const auto      first = pack_hot_cache_.lower_bound(pack_first);
            const auto      last  = pack_hot_cache_.upper_bound(pack_last);
            pack_hot_cache_.erase(first, last);
        }

        eLog("[Dag] Packed sections {}..{} into pack {}", pack_first, pack_last, pid);
        next_pack_index_ += 1;
    }
}

void Dag::remove_sections(const SectionId &from) {
#ifndef IS_APP_CLIENT
    return;
#endif
    pack_hot_generation_.fetch_add(1);
    std::lock_guard pack_lock(pack_mutex_);

    cache_.set_section(align_down20(from), Force::Active);
    auto to           = current_section_;
    auto correct_from = std::max(SectionId(0), from);
    current_section_  = correct_from;
    this->update_range(true);

    if (hot_section_store_)
        hot_section_store_->erase_from(correct_from);

    {
        std::lock_guard cache_lock(pack_hot_cache_mutex_);
        const auto      first = pack_hot_cache_.lower_bound(correct_from);
        pack_hot_cache_.erase(first, pack_hot_cache_.end());
    }

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

        std::error_code error;
        std::filesystem::remove(path_str.value(), error);

        if (i % Config::DataStorage::SECTION_SIZE == 0) {
            std::filesystem::remove_all(this->file_folder(i), error);
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
                                tx.amount().to_string(),
                                tx.timestamp(),
                                balances[{ actor_id, tx.token() }].to_string()));
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

    auto alloc_row =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(node->dfs()->get_db_instance(),
                                                                          network_id,
                                                                          Dfs::Basic::TEMPLATE_DICTIONARY,
                                                                          "token_allocations");
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
    for (const auto &[key, val] : alloc_map.value()) {
        auto sep = key.find(':');
        if (sep == std::string::npos)
            continue;
        auto actor = ActorId::create(key.substr(0, sep));
        auto token = TokenId::create(key.substr(sep + 1));
        if (!actor.has_value() || !token.has_value())
            continue;
        auto parsed = BigNumberFloat::create(val);
        if (!parsed.has_value())
            continue;
        minted[{ actor.value(), token.value() }] = parsed.value();
    }

    // Build minted pairs set and per-token minted actors set for taint tracking
    std::set<ActorTokenKey>              minted_pairs;
    std::map<TokenId, std::set<ActorId>> tainted; // token -> set of tainted actors
    for (const auto &[key, _] : minted) {
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
    std::vector<Transfer>                   all_transfers; // all Regular txs (for taint chain)
    std::vector<MintTx>                     mint_txs;

    // Scan chain from min_section to current
    static const SectionId min_section = SectionId(BigNumber::from_hex("a05133"));
    eLog("[Dag] mint_analysis_log: scanning {} .. {}", min_section, current_section_);

    SectionId     section_id    = min_section;
    std::uint64_t sections_read = 0, sections_missing = 0;
    while (section_id <= current_section_) {
        auto section = read_section(section_id);
        if (!section.has_value()) {
            section_id = section_id + SectionId(1);
            ++sections_missing;
            continue;
        }
        ++sections_read;

        for (const auto &tx : section->transactions) {
            if (tx.type() == TransactionType::Minting) {
                mint_txs.push_back({ tx.receiver(), tx.token(), tx.amount(), section_id, tx.timestamp() });
            } else if (tx.type() == TransactionType::Regular) {
                auto key = ActorTokenKey { tx.sender(), tx.token() };
                if (minted_pairs.count(key)) {
                    spent[key] += tx.amount();
                }
                all_transfers.push_back(
                    { tx.sender(), tx.receiver(), tx.token(), tx.amount(), section_id, tx.timestamp() });
            }
        }

        section_id = section_id + SectionId(1);
    }
    eLog("[Dag] mint_analysis_log: read={} missing={}", sections_read, sections_missing);

    // Build tainted chain via BFS (max depth 10)
    // transfers index: (sender, token) -> list of receivers
    std::map<ActorTokenKey, std::vector<std::pair<ActorId, SectionId>>> transfers_by_sender;
    for (const auto &t : all_transfers)
        transfers_by_sender[{ t.from, t.token }].push_back({ t.to, t.section_id });

    for (auto &[token, actors] : tainted) {
        std::vector<ActorId> queue(actors.begin(), actors.end());
        for (int depth = 0; depth < 10 && !queue.empty(); ++depth) {
            std::vector<ActorId> next;
            for (const auto &actor : queue) {
                auto it = transfers_by_sender.find({ actor, token });
                if (it == transfers_by_sender.end())
                    continue;
                for (const auto &[receiver, _] : it->second) {
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
    for (const auto &t : all_transfers) {
        auto it = tainted.find(t.token);
        if (it == tainted.end())
            continue;
        const auto &tainted_set = it->second;
        if (tainted_set.count(t.from) || tainted_set.count(t.to))
            chain_transfers.push_back(t);
    }

    // Collector stats: non-minted actors that received tainted tokens
    std::map<ActorTokenKey, BigNumberFloat> collector_received;
    for (const auto &t : chain_transfers) {
        if (!minted_pairs.count({ t.to, t.token }))
            collector_received[{ t.to, t.token }] += t.amount;
    }

    // ── Output ──────────────────────────────────────────────────────────────

    eLog("[Dag] mint_analysis_log: === MINT TRANSACTIONS ===");
    for (const auto &m : mint_txs) {
        eLog("[Dag] mint_analysis_log: section={} actor={} token={} amount={}",
             m.section_id,
             m.actor,
             m.token,
             m.amount.to_string());
    }

    eLog("[Dag] mint_analysis_log: === SUMMARY PER ACTOR+TOKEN ===");
    int abuse_count = 0;
    for (const auto &[key, mint_amount] : minted) {
        const auto &[actor, token]  = key;
        BigNumberFloat spent_amount = spent.count(key) ? spent.at(key) : BigNumberFloat(0);
        BigNumberFloat frozen       = mint_amount - spent_amount;
        if (frozen < BigNumberFloat(0))
            frozen = BigNumberFloat(0);
        BigNumberFloat overspend = spent_amount - mint_amount;
        bool           abused    = spent_amount > BigNumberFloat(0);
        if (abused)
            ++abuse_count;

        eLog("[Dag] mint_analysis_log: actor={} token={} minted={} spent={} frozen={} {}{}",
             actor,
             token,
             mint_amount.to_string(),
             spent_amount.to_string(),
             frozen.to_string(),
             abused ? "USED_BEFORE_FREEZE " : "",
             overspend > BigNumberFloat(0) ? fmt::format("OVERSPEND={}", overspend.to_string()) : "");
    }

    eLog("[Dag] mint_analysis_log: === COLLECTORS (received tainted, no direct mint) ===");
    for (const auto &[key, total] : collector_received) {
        const auto &[actor, token] = key;
        eLog("[Dag] mint_analysis_log: collector actor={} token={} received={}", actor, token, total.to_string());
    }

    eLog("[Dag] mint_analysis_log: === TAINTED CHAIN TRANSFERS ===");
    for (const auto &t : chain_transfers) {
        bool        from_minted = minted_pairs.count({ t.from, t.token }) > 0;
        bool        to_minted   = minted_pairs.count({ t.to, t.token }) > 0;
        std::string tag;
        if (from_minted && !to_minted)
            tag = "MINT->COLLECTOR";
        else if (from_minted && to_minted)
            tag = "MINT->MINT";
        else
            tag = "CHAIN";
        eLog("[Dag] mint_analysis_log: [{}] section={} from={} to={} amount={}",
             tag,
             t.section_id,
             t.from,
             t.to,
             t.amount.to_string());
    }

    eLog("[Dag] mint_analysis_log: done. minted_pairs={} abused={} chain_transfers={}",
         minted.size(),
         abuse_count,
         chain_transfers.size());
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
             balance.to_string(),
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
            eLog("Processing section {} from {}", i.to_string(), current_section_);
        }

        for (const auto &tx : section->transactions) {
            if (tx.type() == TransactionType::Reward) {
                total_rewards += BigNumberFloat(tx.amount());
            }
        }
    }

    eLog("Total rewards sum: {}", total_rewards.to_string());
    return total_rewards;
}

void Dag::ensure_control_index() {
    if (control_index_ready_.load())
        return;
    if (!control_index_) {
        control_index_ready_.store(true);
        return;
    }
    // Nothing to populate yet (chain not loaded) — do NOT latch ready, so a later
    // call still rebuilds once the chain is present.
    if (current_section_ <= SectionId(0)) {
        return;
    }
    // First control lookup with a chain present: populate the index once from disk
    // if it is cold, then latch ready so this runs only once.
    if (control_index_->row_count() == 0 || control_index_->rebuild_required()) {
        control_index_->rebuild_from_dag();
    }
    control_index_ready_.store(true);
}

void Dag::repair_control_chain() {
    if (mode_ != DagMode::Full || !control_index_) {
        return;
    }

    const auto closed_tip = align_down20(std::min(current_section_, cache_.section()));
    if (closed_tip < SectionId(0)) {
        return;
    }

    ensure_control_index();
    const auto closed_value = closed_tip.to_int();
    if (!closed_value.has_value()) {
        return;
    }
    const auto expected_controls = static_cast<std::uint64_t>(closed_value.value() / CONTROL_INTERVAL_MOD + 1);
    if (control_index_->row_count_at_or_below(closed_tip) == expected_controls) {
        return;
    }

    std::optional<SectionId> missing_control;
    for (SectionId section_id(0); section_id <= closed_tip; section_id += CONTROL_INTERVAL) {
        const auto section = read_section(section_id);
        if (!section.has_value() || !section->control.has_value()) {
            missing_control = section_id;
            break;
        }
    }
    if (!missing_control.has_value()) {
        control_index_->rebuild_from_dag();
        control_index_ready_.store(true);
        return;
    }

    eWarning("[Dag] Repair control chain from section {}", missing_control.value());
    clear_controls(missing_control.value());
    control_index_->clear();
    control_index_ready_.store(true);
    const auto generation_start =
        missing_control.value() == SectionId(0) ? SectionId(0) : missing_control.value() - CONTROL_INTERVAL_DIFF;
    if (!generate_hash_from_section(generation_start, Force::Active, Force::None).has_value()) {
        eWarning("[Dag] Control chain repair deferred from section {}", missing_control.value());
    }
}

std::optional<DagControl> Dag::find_last_control(const SectionId from, bool disable_break) {
    // `skips`: sections scanned since we last saw a control-aligned slot.
    // `missing_aligned`: control-aligned slots with no section file at all.
    // The `// jj++` in the missing-section branch is intentionally disabled:
    // a missing aligned section currently resets `skips` only — preserve the existing
    // consensus behaviour, but keep the variable as a guard-rail for future tuning.
    int skips           = 0;
    int missing_aligned = 0;

    // Lazy one-time population: the index may be cold (fresh file, or built before
    // the chain range was loaded). Build it on first use so the fast path works
    // regardless of init order. Cheap once warm (flag short-circuits).
    ensure_control_index();

    // Fast path: the control index answers the common "highest control <= from"
    // query in O(1) instead of walking and decompressing section frames. Only
    // for the normal (skip-limited) call; disable_break has special section-0
    // semantics handled by the scan below. The result must be within the kept
    // range; otherwise fall through to the authoritative scan.
    if (!disable_break && control_index_) {
        SectionId top = from < SectionId(0) ? current_section_ : from;
        if (auto hit = control_index_->last_at_or_below(top); hit.has_value()) {
            if (hit->first >= first_saved_section_ && hit->first <= top) {
                return DagControl { .section_id = hit->first, .control = hit->second };
            }
        }
    }

    if (disable_break) {
        auto section = this->read_section(SectionId(0));
        if (section.has_value()) {
            if (!section->control.has_value()) {
                return std::nullopt;
            }
        }
    }

    for (SectionId i = from < 0 ? current_section_ : from; i >= SectionId(0); i--) {
        if (i < first_saved_section_) {
            eCritical("[Dag] Try to find section < current first");
            break;
        }

        auto section = this->read_section(i);
        if (!section.has_value()) {
            if (i % CONTROL_INTERVAL_MOD == 0) {
                eLog("[Dag] No section: {}", i);
                skips = 0;
                // missing_aligned++;  // kept intentionally disabled, see note above
            }
            continue;
        }

        if (section->control.has_value()) {
            if (section->id % CONTROL_INTERVAL_MOD != 0) {
                eCritical("[Dag] Control for section {}", section->id.to_string());
                continue;
            }

            return DagControl { .section_id = i, .control = section->control.value() };
        }

        skips += 1;
        if (!disable_break && (skips > CONTROL_SEARCH_SKIP_LIMIT || missing_aligned > CONTROL_SEARCH_MISS_LIMIT)) {
            break;
        }
    }

    return std::nullopt;
}

std::optional<DagControl> Dag::read_control(const SectionId &section_id) {
    ensure_control_index();
    // Fast path: the control index answers without reading/decompressing a section.
    if (control_index_) {
        if (auto h = control_index_->get(section_id); h.has_value()) {
            return DagControl { .section_id = section_id, .control = h.value() };
        }
    }

    // Fallback: read from the section (index cold/missing). Backfill the index.
    auto section = read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    if (!section->control.has_value()) {
        return std::nullopt;
    }

    if (control_index_)
        control_index_->put(section_id, section->control.value());
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
    const SectionId interval_end = control_interval_end(start);

    if (!control_interval_is_closed(start, current_section_, cache_.section())) {
        return std::nullopt;
    }

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
    std::lock_guard generation_lock(controls_generation_mutex_);

    std::string last_hash;
    SectionId   current_start = start;

    if (start > SectionId(0)) {
        auto last_control = this->find_last_control(start - SectionId(1));
        if (last_control.has_value()) {
            last_hash              = last_control.value().control;
            const auto resume_from = last_control.value().section_id + SectionId(1);
            if (resume_from < current_start) {
                // A previous control pass can be skipped when another pass is
                // still active. The balance cache can advance meanwhile. Resume
                // after the last control that is actually stored, so no interval
                // is omitted from the control chain.
                current_start = resume_from;
            }
        } else {
            current_start = SectionId(0);
        }
    }

    if (current_start == SectionId(0)) {
        if (!this->generate_hash_for_interval(SectionId(0), last_hash).has_value()) {
            return std::nullopt;
        }
        if (full_generation == Force::None) {
            return last_hash;
        }
        current_start = SectionId(1);
    }

    for (; control_interval_is_closed(current_start, current_section_, cache_.section());
         current_start += CONTROL_INTERVAL) {
        if (!this->generate_hash_for_interval(current_start, last_hash).has_value()) {
            return std::nullopt;
        }

        if (current_start % 600 == 1) {
            if (!node_enabled.load()) {
                return std::nullopt;
            }

            if (qt_signals == Force::Active) {
                control_progress_event_.publish(current_start);
            }
        }
    }

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
        control_started_event_.publish();
    }

    if (start_section > cache_.section() && start_section != SectionId(0)) {
        control_ended_event_.publish();
        return true;
    }

    auto result = this->generate_hash_from_section(start_section, Force::Active, qt_signals);

    if (qt_signals == Force::Active) {
        control_ended_event_.publish();
    }

    return result.has_value();
}

std::optional<std::string> Dag::hash_interval(const SectionId &from, const SectionId &to) {
    std::string section_hashs;

    // TODO: if first < from or to

    if (status_ != DagStatus::Sync) {
        eLog("[Dag] Hash interval from {} to {}", from.to_string(), to.to_string());
    }

    // current or to?
    if (to > current_section_) {
        eCritical("[Dag] Section to ({}) > current ({})", to.to_string(), current_section_.to_string());
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
            auto hash = Utils::calculate_hash(i.to_string());
            section_hashs += hash;
            // eTemp("[Dag] section_hashs: no section +{} {}, {}", i, i.to_string(), hash);
            continue;
        }

        auto hash = Utils::calculate_hash(i.to_string() + section->calculate_hash());
        section_hashs += hash;
        // eTemp("[Dag] section_hashs: section +{} {}, {}", i, i.to_string(), hash);
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
        // eTemp("[Dag] Find control in section 0x{} / {}", section_id, section_id.to_string());

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
    auto       section_id = from;
    const auto remainder  = section_id % CONTROL_INTERVAL;
    if (remainder != SectionId(0)) {
        section_id += CONTROL_INTERVAL - remainder;
    }
    for (; section_id <= current_section_; section_id += CONTROL_INTERVAL) {
        auto section = read_section(section_id);
        if (!section.has_value()) {
            continue;
        }

        if (section->control.has_value()) {
            this->remove_control(section_id);
        }
    }
}

void Dag::clear_controls_async(const SectionId &from) {
    node->post_storage([this, from] {
        clear_controls(from);
    });
}

void Dag::request_control_section(const SectionId &from_top, const Responder &responder) {
    // The in-flight flag must expire: it is cleared only by a response, and a peer
    // that cannot serve the range (or dies) never sends one. Measured on a six-node
    // stand: the flag stuck for 9 minutes, every later need_recontrol logged
    // "No need request control search", and the node sat in DagStatus::Sync forever —
    // which also made every peer drop ITS sync responses ("last info not ready").
    if (search_control_) {
        const auto now = Utils::current_date_ms();
        if (search_control_started_ms_ != 0 && now - search_control_started_ms_ < 30'000) {
            eTemp("[Dag] No need request control search");
            return;
        }
        eWarning("[Dag] Control search response never arrived — resetting the in-flight flag");
        search_control_ = false;
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

    search_control_            = true;
    search_control_started_ms_ = Utils::current_date_ms();
    control_search_started_event_.publish();
    sync_start_event_.publish(current_section_, current_section_);

    DagControlRangeRequest req { .from = lo, .to = hi };
    node->network()->send_message(req,
                                  MessageType::DagControlRangeRequest,
                                  responder.empty() ? SendMode::Neighbours : SendMode::Focused,
                                  MessageStatus::Request,
                                  responder.with_new_message_id());
}

void Dag::network_request_control_section(const DagControlRangeRequest &control_request,
                                          const Responder              &responder,
                                          int                           regen_depth) {
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
        // One regeneration attempt only. A node that has no cache yet (fresh joiner)
        // cannot build these controls no matter how many times it clears and retries —
        // the unbounded recursion here overflowed the stack and killed the process
        // the moment a peer asked it for a control range it did not have.
        if (regen_depth >= 1) {
            // Answer with whatever was collected rather than staying silent: the
            // requester set its in-flight flag and entered Sync — silence leaves it
            // waiting for a response that never comes.
            eWarning("[Dag] Serving partial control range [{}..{}]: controls not rebuildable yet",
                     control_request.from,
                     control_request.to);
            responder.send_response(control_response,
                                    MessageType::DagControlRangeResponse,
                                    SendMode::Focused,
                                    MessageStatus::Response);
            return;
        }
        this->clear_controls(from);
        this->start_control(Force::Active);
        this->network_request_control_section(control_request, responder, regen_depth + 1);
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
        control_search_ended_event_.publish();
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
            control_search_ended_event_.publish();
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
            control_progress_event_.publish(next_lo);
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
        sync_start_event_.publish(correct_from, sync_last_index_);
        search_control_ = false;
        control_search_ended_event_.publish();
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
