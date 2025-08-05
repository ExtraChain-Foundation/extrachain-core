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

#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"
#include "utils/thread_pool_boost.h"

Dag::Dag(ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node, node)
    , cache_(node, this) {
    timer_sync = new QTimer();

    auto settings = Utils::read_settings();
    if (settings.dag_mode.has_value()) {
        mode_ = settings.dag_mode.value();
    }

    if (!settings.dag_mode.has_value()) {
#ifdef IS_RC
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
            auto first_id_result    = BigNumber::create(section_range->first);
            auto current_id_result  = BigNumber::create(section_range->last);
            auto last_cached_result = BigNumber::create(section_range->last_cached);

            if (!first_id_result.has_value() || !current_id_result.has_value()) {
                return;
            }

            if (mode_ == DagMode::Full && first_id_result != BigNumber("0")) {
                current_section_     = current_id_result.value();
                first_saved_section_ = first_id_result.value();
                clear_dag();
                cache_.reset_db();
                cache_.init_db();
            } else {
                current_section_     = current_id_result.value();
                first_saved_section_ = first_id_result.value();

                if (last_cached_result.has_value()) {
                    cache_.set_section(last_cached_result.value());
                }
            }

            if (mode_ == DagMode::Full && cache_.section() == -1) { // TODO: and have all 0-current
                cache_.reset_db();
                cache_.init_db();
                cache_.check_and_update_cache(current_section_);
            }

            eLog("[Dag] Loaded: {}, first: {}, last cached: {}",
                 current_section_,
                 first_saved_section_,
                 cache_.section());
            file.close();
        }
    } else {
        // QDir(QString::fromStdString(ChainConst::CHAIN_FOLDER)).removeRecursively();
        clear_dag();
    }

    transaction_cache_.make_files();
    cache_.init_db();

    // if (!QDir(QString::fromStdString(ChainConst::CHAIN_FOLDER)).exists()) {
    //     QDir().mkdir(QString::fromStdString(ChainConst::CHAIN_FOLDER));
    //     transaction_cache_.make_files();
    // }

    timestamp_bigger_sync_start_ = 0;

#ifndef IS_R
    this->set_status(DagStatus::Ready);
#endif

    auto section = this->read_section(BigNumber(0));
    if (section.has_value() && section->transactions.size() == 1) {
        // prove_transaction()
        auto network_id = section->transactions.begin()->sender();
        node->actorIndex()->set_network_id(network_id);
    }

    if (mode_ == DagMode::Light) {
        clear_dag();
        cache_.reset_db();
        cache_.init_db();
    }

    eLog("[Dag] Done. Mode: {}", mode_);
}

Dag::~Dag() {
    cache_.dag = nullptr;
    timer_sync->deleteLater();
}

BigNumber Dag::current_section() const {
    return current_section_;
}

void Dag::set_current_section(const BigNumber &new_current_section) {
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

void Dag::set_status(DagStatus status) {
    this->status_ = status;
    emit node->dagStatus(status_);

    if (status == DagStatus::Ready) {
        emit node->dagTimerStop();
        min_req_count = 5;
    }
}

TransactionCache &Dag::transaction_cache() {
    return transaction_cache_;
}

DagCache &Dag::cache() {
    return cache_;
}

BigNumber Dag::first_saved_section() {
    return first_saved_section_;
}

SectionId Dag::file_section(const SectionId &section) const {
    return section / Config::DataStorage::SECTION_SIZE;
}

std::string Dag::file_folder(const BigNumber &section) const {
    auto file_section = this->file_section(section);
    auto path         = fmt::format("{}/{}", ChainConst::DAG_FOLDER, file_section.to_string());
    return path;
}

std::string Dag::file_path(const BigNumber &section) const {
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

    auto section = read_section(tx.section() - 1);
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
    auto tx = prepare_transaction(transaction, signer);
    if (!tx.has_value()) {
        return std::unexpected(tx.error());
    }

    eLog("[Dag] Send {}", tx.value());
    add_transaction_sended(tx.value());
    node->network()->send_message(tx.value(), MessageType::DagTransaction, SendMode::Broadcast);

    return tx;
}

std::expected<void, bool> Dag::network_transaction(const Transaction &transaction, const Responder &responder) {
    if (status_ != DagStatus::Final) {
        bool sync_timeout = false;
        if (timestamp_bigger_sync_start_ != 0) {
            sync_timeout = (Utils::current_date_ms() - timestamp_bigger_sync_start_) > 10000;
        }

        if (!sync_timeout && status_ != DagStatus::Ready) {
            add_to_cached_tx(transaction);
            return {};
        }

        if (sync_timeout && transaction.section() > current_section_ + 5) {
            if (!sync_timeout)
                add_to_cached_tx(transaction);
            set_status(DagStatus::Sync);
            sync_last_index              = transaction.section();
            timestamp_bigger_sync_start_ = Utils::current_date_ms();
            eLog("[Dag] Section bigger: {}", sync_last_index);
            request_sections(current_section_, std::min(sync_last_index, current_section_ + 100), responder);
            return {};
        }
    }

    auto tx   = transaction;
    auto resp = responder;
    // ThreadPoolBoost::instance()->post([this, transaction = tx, responder = resp]() {
    auto                  section = read_section(transaction.section());
    TransactionProveError res =
        this->prove_transaction(transaction,
                                section.has_value() ? section->transactions : std::set<Transaction> {});
    TransactionResult transaction_result { .hash = transaction.hash(), .result = res };

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
                // sync
            }
        }
    } else {
        eLog("[Dag] Transaction from network approved: {}", transaction);
    }

    if (res == TransactionProveError::NoError) {
        auto save_result = save_transaction(transaction);
        if (!save_result) {
            transaction_result.result = TransactionProveError::NoSectionAdded;
            // send response
            return std::unexpected(false);
        }

        set_current_section(transaction.section());
        update_range();
    }

    if (!responder.identifiers().empty()) {
        responder.send_response(transaction_result,
                                MessageType::DagTransactionResult,
                                SendMode::Focused,
                                MessageStatus::Response);
    }

    if (res != TransactionProveError::NoError) {
        return std::unexpected(false);
    }

    check_self(transaction);
    // });

    return {};
}

void Dag::network_transaction_result(const std::string hash, TransactionProveError result) {
    if (sended_transactions_.find(hash) == sended_transactions_.end()) {
        // eLog("[Dag] Ignore transaction result: {} / {}", hash, result);
        return;
    }

    auto transaction = this->sended_transactions_[hash];
    // this->sended_transactions.erase(hash);

    if (result != TransactionProveError::NoError) {
        eLog("[Dag] Our transaction not approved: 0x{} ({}) / {}, {}",
             transaction.section(),
             transaction.section().to_string(NumeralBase::Dec),
             transaction.hash(),
             result);
        // this->sended_transactions.erase(hash);
        return;
    } else {
        eLog("[Dag] Our transaction approved: {} / {}", transaction.section(), transaction.hash());
        this->sended_transactions_.erase(hash);
    }

    auto save_result = this->save_transaction(transaction);
    if (!save_result) {
        eLog("[Dag] Can't save our approved transaction {} in section {}",
             transaction.hash(),
             transaction.section());
        return;
    }

    check_self(transaction);
}

void Dag::check_self(const Transaction &transaction) {
    const auto my_actors = node->accountController()->accounts_ids();

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

void Dag::process_cached_transactions() {
    {
        try {
            auto guard = cached_txs_.lock();
            eLog("[Dag] Processing {} cached transactions after sync", guard->size());
        } catch (const std::system_error &e) {
            std::cerr << "[Dag] Caught system_error in process cached: " << e.what() << std::endl;
        }
    }

    status_                      = DagStatus::Final;
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
    set_status(DagStatus::Ready);
    set_sync_status(DagSyncStatus::None);
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

std::optional<Section> Dag::read_section(const BigNumber &section_id) const {
    try {
        std::shared_lock<std::shared_mutex> lock(section_mutex_);
    } catch (const std::system_error &e) {
        return std::nullopt;
    }

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
}

std::optional<bool> Dag::write_section(const Section &section) {
    try {
        std::unique_lock<std::shared_mutex> lock(section_mutex_);
    } catch (const std::system_error &e) {
        return std::nullopt;
    }

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
}

std::optional<bool> Dag::write_control(const SectionId &section_id, const std::string &hash) {
    // return std::nullopt;
    auto section = this->read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    // if not genesis -> return

    if (section->control == hash) {
        eLog("[Dag] No need writing control to {}", section_id);
        return true;
    }

    eLog("[Dag] Write control to {}", section_id);
    section->control = hash;
    return this->write_section(section.value());
}

void Dag::timer_tick() {
    eLog("[Dag] Timer tick");
    this->timer_sync->stop(); // no need emit?
    this->set_status(DagStatus::Timered);
    this->sync_status_ = DagSyncStatus::None;

    if (min_req_count > 1) {
        min_req_count -= 1;
    }

    this->start_sync();
}

bool Dag::save_transaction(const Transaction &transaction) {
    auto section = this->read_section(transaction.section());

    if (!section.has_value()) {
        // Create new section
        Section section { .id = transaction.section(), .transactions = { transaction } };

        set_current_section(section.id);

        // Check if cache needs updating
        cache_.check_and_update_cache_thread(current_section_);

        // Update range file
        update_range();

        if (mode_ == DagMode::Light && transaction.section() == BigNumber(0)) {
            auto network_id = transaction.sender();
            node->actorIndex()->set_network_id(network_id);
        }

        // Update first_saved_section_ if this is the first section or has a lower ID
        if (first_saved_section_ == BigNumber(-1) && transaction.section() >= BigNumber(0)) {
            if (mode_ == DagMode::Light && transaction.section() == BigNumber(0)) {
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

    // Check if cache needs updating
    cache_.check_and_update_cache_thread(current_section_);

    // Update first_saved_section_ if this is the first section or has a lower ID
    if (first_saved_section_ == BigNumber(-1) && transaction.section() >= BigNumber(0)) {
        if (mode_ == DagMode::Full || (mode_ == DagMode::Light && transaction.section() != BigNumber(0))) {
            first_saved_section_ = transaction.section();
        }

        eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
    }

    // Update range file
    update_range();

    return write_section(section.value()).has_value();
}

bool Dag::local_remove_transaction(const BigNumber &section_id, const std::string &hash) {
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

std::optional<std::pair<BigNumber, BigNumber>> Dag::save_transactions(
    const std::vector<Transaction> &transactions) {
    if (transactions.empty()) {
        return std::nullopt;
    }

    // TODO: optimize
    std::map<BigNumber, std::vector<Transaction>> section_transactions;
    for (const auto &transaction : transactions) {
        section_transactions[transaction.section()].push_back(transaction);
    }

    bool      all_saved   = true;
    BigNumber min_section = BigNumber(-1);
    BigNumber max_section;
    bool      first_iteration = true;

    for (const auto &[section_id, section_txs] : section_transactions) {
        if (min_section == BigNumber(-1)) {
            min_section = section_id;
            max_section = section_id;
        } else {
            min_section = std::min(section_id, min_section);
            max_section = std::max(section_id, max_section);
        }

        auto section = this->read_section(section_id);

        if (!section.has_value()) {
            Section new_section { .id = section_id, .transactions = {} };

            for (const auto &tx : section_txs) {
                new_section.transactions.insert(tx);
            }

            set_current_section(section_id);
            cache_.check_and_update_cache_thread(current_section_);

            update_range();

            if (mode_ == DagMode::Light && section_id == BigNumber(0)) {
                auto network_id = section_txs[0].sender();
                node->actorIndex()->set_network_id(network_id);
            }

            if (first_saved_section_ == BigNumber(-1) && section_id >= BigNumber(0)) {
                if (mode_ == DagMode::Light && section_id == BigNumber(0)) {
                    all_saved &= write_section(new_section).has_value();
                    continue;
                }
                first_saved_section_ = section_id;
                eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
            }

            all_saved &= write_section(new_section).has_value();
        } else {
            for (const auto &tx : section_txs) {
                section->transactions.insert(tx);
            }

            set_current_section(section_id);
            cache_.check_and_update_cache(current_section_);

            if (first_saved_section_ == BigNumber(-1) && section_id >= BigNumber(0)) {
                if (mode_ == DagMode::Full || (mode_ == DagMode::Light && section_id != BigNumber(0))) {
                    first_saved_section_ = section_id;
                }
                eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
            }

            update_range();

            all_saved &= write_section(section.value()).has_value();
        }
    }

    if (!all_saved) {
        return std::nullopt;
    }

    return std::make_pair(min_section, max_section);
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions) {
    // Check Genesis transactions
    if (tx.type() == TransactionType::Genesis) {
        if (tx.section() != BigNumber(0)) {
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
        if (tx.section() != BigNumber(1)) {
            return TransactionProveError::BalanceOnlyFirstSection;
        }

        if (!node->network_id().is_zero() && tx.sender() != tx.receiver() && tx.sender() != node->network_id()) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        return TransactionProveError::NoError;
    }

    // Verify previous section exists
    auto section = this->read_section(BigNumber(tx.section() - 1));
    if (section.has_value()) {
        // TODO: Additional section validation could be added here
    }

    if ((current_section_ - tx.section()).abs() > 15) {
        return TransactionProveError::TooSectionDiff;
    }

    // Validate transaction amount
    if (tx.amount() == 0) {
        return TransactionProveError::AmountZero;
    }

    if (tx.amount() < 0) {
        return TransactionProveError::AmountLessZero;
    }

    // Get sender and receiver IDs
    ActorId        targetSender   = tx.sender();
    ActorId        targetReceiver = tx.receiver();
    const ActorId &mainActorId    = node->accountController()->system_actor().id();

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

    // Check for duplicate transaction
    auto tx_result = search_transaction(tx_copy.hash());
    if (tx_result.has_value()) {
        return TransactionProveError::Duplicate;
    }

    // Validate sender
    if (targetSender.is_zero()) {
        return TransactionProveError::SenderZero;
    }

    Actor<KeyPublic> senderActor;
    senderActor = node->actorIndex()->getActor(targetSender);
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
    receiverActor = node->actorIndex()->getActor(targetReceiver);
    if (receiverActor.empty()) {
        return TransactionProveError::ReceiverNotExists;
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

    return TransactionProveError::NoError;
}

void Dag::add_transaction_sended(const Transaction &transaction) {
    // eLog("[Dag] Add to sended: {}", transaction.hash());
    sended_transactions_.insert({ transaction.hash(), transaction });
}

void Dag::update_range() {
    try {
        std::lock_guard<std::mutex> lock(range_mutex_);
    } catch (const std::system_error &e) {
        // eCritical("[Dag] Caught system_error in update range: {}", e.what());
        return;
    }

    std::string json = Json::serialize(SectionRange { .first       = first_saved_section_.to_string(),
                                                      .last        = current_section_.to_string(),
                                                      .last_cached = cache_.section().to_string() });

    // eLog("[Dag] Updating range: first={}, last={}, last_cached={}",
    //      first_saved_section_,
    //      current_section_,
    //      cache_.section());

    QFile file(QString::fromStdString(ChainConst::DAG_RANGE_PATH));
    if (file.open(QFile::WriteOnly)) {
        file.write(json.data());
        file.close();

        QFile check_file(QString::fromStdString(ChainConst::DAG_RANGE_PATH));
        if (check_file.open(QFile::ReadOnly)) {
            auto content = check_file.readAll();
            // eLog("[Dag] Range file written: {}", content.toStdString());
            check_file.close();
        }
    } else {
        eLog("[Dag] Failed to open range file for writing");
    }
}

std::optional<Transaction> Dag::search_transaction(const std::string &hash, int deep) const {
    int count = 0;

    for (BigNumber i = current_section_ + 1; i >= first_saved_section_; i--) {
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
    requests_count = std::max(1, node->network()->active_connections_count() - 1);
    node->network()->send_message(true,
                                  MessageType::DagSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);
}

void Dag::start_check() {
    // temp
#ifndef IS_R
    if (status_ == DagStatus::Ready) {
        return;
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
    check_status_  = DagSyncStatus::LastInfo;
    requests_count = std::max(1, node->network()->active_connections_count() - 1);
    node->network()->send_message(true,
                                  MessageType::DagSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 9 start_check");
}

void Dag::network_status_sync_request(const Responder &responder) {
#ifdef IS_RC
    return;
#endif

    if (mode_ == DagMode::Light) {
        return;
    }

    if (status_ != DagStatus::Ready) {
        return;
    }

    auto      section      = this->read_section(current_section_);
    BigNumber section_id   = section.has_value() ? section->id : BigNumber(-1);
    auto      hashs        = section.has_value() ? section->hashs() : std::set<std::string> {};
    auto      zero_section = this->read_section(BigNumber(0));

    std::uint64_t zero_timestamp =
        zero_section.has_value() ? (zero_section->transactions.size() == 1 ? zero_section->middle() : 0) : 0;

    auto last_info = DagLastInfo { .last_section_id = section_id,
                                   .last_hash       = hashs,
                                   .zero_date       = zero_section.has_value() ? zero_timestamp : 0 };
    // eLog("network_status_sync_request, send: {}", last_info);
    responder.send_response(last_info, MessageType::DagSyncLastInfo, SendMode::Focused, MessageStatus::Response);
}

void Dag::network_status_sync_response(const DagLastInfo &last_info, const Responder &responder) {
    if (sync_status_ != DagSyncStatus::LastInfo && check_status_ != DagSyncStatus::LastInfo) {
        return;
    }
    // min(connections size, 5)

    auto zero_section = read_section(BigNumber(0));
    if (zero_section.has_value() && !last_info.last_hash.empty() && last_info.last_section_id != BigNumber(-1)
        && zero_section->middle() < last_info.zero_date) {
        // TODO: need to remove
        // removeAll(false, true);
    }

    int count = std::min(requests_count, min_req_count);

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    if (sync_status_ == DagSyncStatus::LastInfo && last_info_.size() >= count) {
        set_sync_status(DagSyncStatus::Sections);
        check_status_ = DagSyncStatus::None;
        eLog("BC 6 sync status");
        send_sync_request();
    }

    if (check_status_ == DagSyncStatus::LastInfo && last_info_.size() >= count) {
        check_status_ = DagSyncStatus::Sections;
        eLog("BC 7 check status");
        send_sync_request();
    }
}

void Dag::request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder) {
    auto range         = SectionRange { .first = from == -1 ? "0" : from.to_string(), .last = to.to_string() };
    auto responder_new = responder.with_new_message_id();
    responder_new.send_response(range, MessageType::DagSections, SendMode::Focused, MessageStatus::Request);

    // if (status_ != DagStatus::Sync) {
    eLog("[Dag] Request sections from {} to {}", range.first, range.last);
    // }
}

void Dag::network_request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder) {
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

    if (to - from >= 150) {
        // return;
    }

    std::set<Transaction> txs;

    for (BigNumber i = from; i <= to; i++) {
        auto section = this->read_section(i);
        if (!section.has_value()) {
            continue;
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

    auto ser      = MessagePack::serialize(std::make_pair(to, txs));
    auto compress = qCompress(QByteArray::fromStdString(ser));
    responder.send_response(compress.toStdString(),
                            MessageType::DagSections,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_request_sections_response(const std::string &compressed, const Responder &responder) {
    emit node->dagTimerStop();
    // eLog("Timer stop");

    ThreadPoolBoost::instance()->post([this, compressed, responder]() {
        const auto txs = MessagePack::deserialize<std::pair<BigNumber, std::vector<Transaction>>>(
            qUncompress(QByteArray::fromStdString(compressed)).toStdString());

        if (!txs.has_value()) {
            // eLog("network_request_sections_response 1");
            // eLog("sysync 2");
            return;
        }

        if (!txs->second.empty()) {
            auto res = save_transactions(txs->second);
            if (!res.has_value()) {
                // eLog("network_request_sections_response 2");
                // eLog("sysync 3");
                return;
            }

            const auto &[min, max] = res.value();
            // eLog("[Dag] Saved sections from {} to {}", min, max);
        }

        if (current_section_ >= sync_last_index - 1) {
            eLog("[Dag] Sync completed, processing cached transactions");

            if (this->status_ != DagStatus::Ready) {
                process_cached_transactions();
            }
            return;
        }

        emit node->dagSyncProgress(current_section_);
        set_current_section(txs->first);
        // eLog("curr: {}, sync last: {}, curr + 100 {}", current_section_, sync_last_index, current_section_ +
        // 100);

        // timer_sync->start();
        emit node->dagTimerStart(15002);
        request_sections(current_section_, std::min(sync_last_index, txs->first + 100), responder);
    });
}

void Dag::network_request_light(const Responder &responder) {
    ThreadPoolBoost::instance()->post([this, responder]() {
        QElapsedTimer timer;
        timer.start();
        std::vector<Transaction>                       txs;
        std::vector<std::pair<SectionId, std::string>> controls;

        if (cache().section() == BigNumber(-1) && current_section_ > 100) {
            return;
        }

        auto [cache_section, cache] = this->cache().read_cached_balances();
        txs.reserve(20);

        auto section = this->read_section(BigNumber(0));
        if (section.has_value()) {
            controls.push_back({ SectionId(0), section->control.value() });
            for (const auto &tx : section->transactions) {
                txs.push_back(tx);
            }
        }

        for (BigNumber i = cache_section; i <= current_section_; i++) {
            auto section = this->read_section(i);
            if (!section.has_value()) {
                continue;
            }

            if (section->control.has_value()) {
                controls.push_back({ i, section->control.value() });
            }

            for (const auto &tx : section->transactions) {
                txs.push_back(tx);
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

        eLog("DONE SENDING {}", timer.elapsed());
        eLog("[Dag] Sent light data: cache section {}, transactions count: {}", cache_section, txs.size());
    });
}

void Dag::network_response_light(const DagLightPackage &dag_light, const Responder &responder) {
    // eLog("network_response_light {}", dag_light);

    ThreadPoolBoost::instance()->post([this, responder, dag_light]() {
        // TIMER_START(network_response_light)
        cache_.write_cached_balances(dag_light.cache, dag_light.cache_section);

        // auto min = BigNumber(-1), max = BigNumber(-1);
        // for (const auto &tx : std::as_const(dag_light.txs)) {
        //     min = min != -1 ? std::min(tx.section(), min) : tx.section();
        //     max = std::max(tx.section(), max);
        //     save_transaction(tx);
        // }
        this->save_transactions(dag_light.txs);

        // if (first_saved_section_ == BigNumber(-1) && min >= BigNumber(0)) {
        //     first_saved_section_ = min;
        //     eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
        // }

        if (dag_light.cache_section == -1 || dag_light.cache_section == 0) {
            this->first_saved_section_ = 0;
        }

        for (const auto &[section_id, control] : dag_light.controls) {
            write_control(section_id, control);
        }

        this->update_range();

        eLog("[Dag] Light sync completed: cache section {}, saved sections from {} to {}",
             dag_light.cache_section,
             this->first_saved_section_,
             this->current_section_);

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

    auto last_control = this->find_last_control(hash_interval.to - 1);
    if (!last_control.has_value()) {
        eCritical("[Dag] No last control");
        return;
    }

    eLog("Last control: {}", last_control);

    auto interval_hash = this->hash_interval(hash_interval.from, hash_interval.to);
    if (!interval_hash.has_value()) {
        return;
    }

    if (!(hash_interval.to == last_control->first && hash_interval.from == last_control->first)) {
        interval_hash = Utils::calculate_hash(last_control->second + interval_hash.value());
    }

    if (interval_hash != hash_interval.hash) {
        // eLog(
        //     "[Dag] Hash interval check: false, request sections (NEED RECACHE IMPLMT). network: {}, interval:
        //     {}, " "last: {}", hash_interval, interval_hash, last_control);

        if (current_section_ < hash_interval.to) {
            if (status_ != DagStatus::Ready) {
                status_ = DagStatus::Maybe;
            }

            this->start_check(); // TODO: warning: check or sync?
        } else {
            request_sections(hash_interval.from, hash_interval.to, responder);
            // TODO: need add full network check
            // this->request_control_section(hash_interval.from, responder.with_new_message_id());
        }
    } else {
        eLog("[Dag] Hash interval check: true. {}", hash_interval);
    }
}

void Dag::set_sync_status(DagSyncStatus status) {
    sync_status_ = status;
}

void Dag::send_sync_request() {
    emit node->dagTimerStop();
    auto section = this->read_section(current_section_);

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    bool need_sync = false;

    // eLog("[Dag] current: {}; send_sync_request, last_info_: {}", current_section_, last_info_);

    if (!section.has_value()) {
        for (const auto &[_, info] : last_info_) {
            // eLog("----- {}", info);
            if (info.last_section_id >= 0 && (info.last_section_id == BigNumber(0) || !info.last_hash.empty())) {
                need_sync = true;
                break;
            }
        }
    } else {
        const auto my_index = section->id;
        const auto my_hash  = section->prev_hashs();

        for (const auto &[_, info] : last_info_) {
            if (info.last_section_id > my_index) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                break;
            }
            if (info.last_section_id == my_index && info.last_hash != my_hash) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                break;
            }
        }
    }

    if (!need_sync) {
        set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        process_cached_transactions();
        // timer_sync->stop();

        // emit syncEnd();

        eLog("BC 4");
        return; // end sync
    }

    int connections = requests_count;
    int max_nodes   = std::min(connections, 5);

    std::vector<std::pair<std::string, BigNumber>> nodes_by_block;
    for (const auto &[id, info] : last_info_) {
        if (info.last_section_id >= 0 && (info.last_section_id == BigNumber(0) || !info.last_hash.empty())) {
            nodes_by_block.emplace_back(id, info.last_section_id);
        }
    }

    // TODO: recheck
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        process_cached_transactions();
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

    auto last_block = this->read_section(current_section_);
    auto sync_index = last_block.has_value() ? last_block->id + 1 : BigNumber(0);
    sync_last_index = nodes_by_block.front().second;

    if (current_section_ >= sync_last_index) {
        eLog("[Dag] Not need sync");

        set_status(DagStatus::Ready);
        set_sync_status(DagSyncStatus::None);
        check_status_ = DagSyncStatus::None;
        // start_check();
        return;
    }

    eLog("[Dag] sync_last_index: 0x{} / {} sections",
         sync_last_index,
         sync_last_index.to_string(NumeralBase::Dec));
    // sync(sync_index, responder);
    if (mode_ == DagMode::Full) {
        request_sections(current_section_, std::min(sync_last_index, current_section_ + 100), responder);
    } else {
        auto responder_new = responder.with_new_message_id();
        node->network()->send_message(true,
                                      MessageType::DagLightData,
                                      SendMode::Focused,
                                      MessageStatus::Request,
                                      responder_new);
    }

    // request from to
    check_status_ = DagSyncStatus::None;
    emit node->dagSyncStart(sync_index, sync_last_index);
    emit node->dagTimerStart(30000);
    // eLog("Timer start");
    eLog("syncStart, timer 30 secs");
}

void Dag::clear_dag() {
#ifdef IS_RC
    eLog("[Dag] Clearing...");
    auto max_section = file_section(current_section_);

    QFile(QString::fromStdString(ChainConst::BALANCE_CACHE)).remove();
    QFile(QString::fromStdString(ChainConst::DAG_RANGE_PATH)).remove();

    #if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    for (BigNumber i = BigNumber(0); i <= max_section; ++i) {
        QString path = QString::fromStdString(ChainConst::DAG_FOLDER + "/" + i.to_string());
        QDir    dir(path);
        if (dir.exists()) {
            dir.removeRecursively();
        }
    }
    #else
    QStringList to_delete;
    QDir        parent_dir(QString::fromStdString(ChainConst::DAG_FOLDER));

    for (BigNumber i = BigNumber(0); i <= max_section; ++i) {
        QString old_name = QString::fromStdString(i.to_string());
        if (!parent_dir.exists(old_name)) {
            continue;
        }
        QString new_name = old_name + "_old1";
        int     counter  = 1;
        while (parent_dir.exists(new_name)) {
            new_name = old_name + "_old" + QString::number(++counter);
        }
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
    current_section_     = BigNumber(-1);
    first_saved_section_ = BigNumber(-1);
    status_              = DagStatus::Started;

    cache_.reset_db();
    cache_.init_db();
    eLog("[Dag] Cleared");
#endif
}

void Dag::tx_list_log(const ActorId &actor_id) {
    eLog("Start tx_list_log");
    Balances                 balances;
    std::vector<std::string> logs;

    for (BigNumber i = BigNumber(1); i <= current_section_; i++) {
        auto section = read_section(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }

        if (i % BigNumber(1000) == 0) {
            eLog("tx_list_log on 0x{} / {}", i, i.to_string(NumeralBase::Dec));
        }

        // Process each transaction
        for (const auto &tx : section->transactions) {
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

    for (SectionId i = current_section_; i >= BigNumber(0); i--) {
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

std::optional<std::pair<SectionId, std::string>> Dag::find_last_control(const SectionId from, bool disable_braek) {
    int j = 0;

    if (disable_braek) {
        auto section = this->read_section(SectionId(0));
        if (section.has_value()) {
            if (!section->control.has_value()) {
                return std::nullopt;
            }
        }
    }

    for (SectionId i = from < 0 ? current_section_ : from; i >= SectionId(0); i--) {
        auto section = this->read_section(i);
        if (!section.has_value()) {
            eLog("NO SECTION ---> {}", i);
            if (i % CONTROL_INTERVAL_MOD == 0) {
                j = 0;
            }
            continue;
        }

        if (section->control.has_value()) {
            return std::make_pair(i, section->control.value());
        }

        j += 1;
        if (!disable_braek && j > 25) {
            break;
        }
    }

    return std::nullopt;
}

std::optional<std::string> Dag::read_control(const SectionId &section_id) {
    auto section = read_section(section_id);
    if (!section.has_value()) {
        return std::nullopt;
    }

    return section->control;
}

std::optional<std::string> Dag::read_control_prev(const SectionId &section_id) {
    for (SectionId i = section_id; i >= BigNumber(0); i--) {
        if (i % CONTROL_INTERVAL_MOD == 0) {
            return read_control(i);
        }
    }

    return std::nullopt;
}

std::optional<std::string> Dag::read_control_next(const SectionId &section_id) {
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

    auto interval_hash = this->hash_interval(start, interval_end);
    if (!interval_hash.has_value()) {
        return std::nullopt;
    }

    if (start != BigNumber(0)) {
        last_hash = Utils::calculate_hash(last_hash + interval_hash.value());
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

std::optional<std::string> Dag::generate_hash_from_section(const SectionId &start, bool full_generation) {
    std::string last_hash = "";

    if (start > SectionId(0)) {
        auto last_control = this->find_last_control(start - SectionId(1));
        // eLog("LL 1 {}", last_control);
        if (last_control.has_value()) {
            last_hash = last_control.value().second;
        } else {
            return std::nullopt;
        }
    }

    if (full_generation || start == SectionId(0)) {
        this->generate_hash_for_interval(SectionId(0), last_hash);
        if (!full_generation) {
            return last_hash;
        }
    }

    SectionId current_start = start == SectionId(0) ? SectionId(1) : start;
    for (; current_start <= current_section_ && current_start < current_section_;
         current_start += CONTROL_INTERVAL) {
        if (current_start + CONTROL_INTERVAL > current_section_) {
            break;
        }

        this->generate_hash_for_interval(current_start, last_hash);
    }

    return last_hash;
}

bool Dag::generate_hash(const SectionId &start_section) {
    eLog("[Dag] Generate AcyclicChain controls...");
    node->dagControlStarted();

    auto result = this->generate_hash_from_section(start_section, true);

    node->dagControlEnded();
    return result.has_value();
}

std::optional<std::string> Dag::hash_interval(const SectionId &from, const SectionId &to) {
    std::string tx_hashs;

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

        if (!section.has_value()) {
            tx_hashs += Utils::calculate_hash(i.to_string(NumeralBase::Dec));
            continue;
        }

        tx_hashs += Utils::calculate_hash(i.to_string(NumeralBase::Dec) + section->calculate_hash());
    }

    return Utils::calculate_hash(tx_hashs);
}

void Dag::start_control() {
    // for tests
    generate_hash();
    return;

    eLog("[Dag] Check controls...");

    auto find_result = this->find_last_control();
    if (find_result.has_value()) {
        auto section_id = find_result->first;
        // write last control?
        eLog("[Dag] Find control in section 0x{} / {}", section_id, section_id.to_string(NumeralBase::Dec));

        if (section_id % 20 != 0) {
            eCritical("[Dag] Incorrect control section % 20 != 0: {}", section_id);
        }
        return;
    }

    auto find_result2 = this->find_last_control(current_section_, true);
    generate_hash(find_result2.has_value() ? find_result2->first : SectionId(0));
}

void Dag::request_control_section(const SectionId &section_id, const Responder &responder) {
    auto dag_control = DagControl { .section_id = section_id, .hash = this->read_control(section_id) };
    node->network()->send_message(dag_control,
                                  MessageType::DagControl,
                                  SendMode::Neighbours,
                                  MessageStatus::Request,
                                  responder);
}

void Dag::network_request_control_section(const DagControl &dag_control, const Responder &responder) {
    if (dag_control.section_id % 20 != 0) {
        return;
    }

    if (dag_control.section_id > current_section_) {
        // sitation must save global sync with hashs stats
        return;
    }

    if (!dag_control.hash.has_value()) {
        // request interval control sync
        request_sections(dag_control.section_id, dag_control.section_id + CONTROL_INTERVAL, responder); // ?
    }

    if (dag_control.hash.has_value()) {
        auto control = this->read_control(dag_control.section_id);

        if (control.value() == dag_control.hash.value()) {
            // 40: recheck. maybe 30?
            if (current_section_ - 40 >= dag_control.section_id) { // 60 >= 50
                // request sections?
                // TODO: set last sync?
                sync_last_index = current_section_;
                request_sections(dag_control.section_id,
                                 std::min(current_section_, dag_control.section_id + 100),
                                 responder);
            } else {
                // okay
            }
        } else {
            request_control_section(dag_control.section_id - CONTROL_INTERVAL, responder);
        }
    }

    // що робити, якщо 1 повний і 2 ні, а не обидва? все одно?
}

std::set<std::string> Section::prev_hashs() {
    std::set<std::string> hashs;

    for (const auto &tx : transactions) {
        const auto &prev_hashes = tx.prev_hashs();
        hashs.insert(prev_hashes.begin(), prev_hashes.end());
    }

    return hashs;
}

std::set<std::string> Section::hashs() {
    std::set<std::string> hashs;

    for (const auto &tx : transactions) {
        hashs.insert(tx.hash());
    }

    return hashs;
}

std::uint64_t Section::middle() {
    if (transactions.empty()) {
        return 0;
    }

    std::uint64_t sum = 0;

    for (const auto &tx : transactions) {
        sum += tx.timestamp();
    }

    return sum / transactions.size();
}

std::string Section::calculate_hash() {
    std::string tx_hashs;
    for (const auto &transaction : transactions) {
        tx_hashs += transaction.hash();
    }
    return Utils::calculate_hash(tx_hashs);
}
