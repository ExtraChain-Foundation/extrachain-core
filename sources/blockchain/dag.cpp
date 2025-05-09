#include "blockchain/dag.h"

#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"

Dag::Dag(ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node, node)
    , cache_(node) {
    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
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

            current_section_     = current_id_result.value();
            first_saved_section_ = first_id_result.value();

            if (last_cached_result.has_value()) {
                cache_.set_section(last_cached_result.value());
            }

            eLog("[Dag] Current: {}, first: {}, last cached: {}",
                 current_section_,
                 first_saved_section_,
                 cache_.section());
            file.close();
        }
    } else {
        // QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).removeRecursively();
        clear_dag();
        cache_.reset_db();
        cache_.init_db();
    }

    transaction_cache_.make_files();

    // if (!QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).exists()) {
    //     QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
    //     transaction_cache_.make_files();
    // }

    auto settings = Utils::read_settings();
    if (settings.blockchain_mode.has_value()) {
        mode_ = settings.blockchain_mode.value();
    }

#ifdef IS_RC
    mode_ = DagMode::Light;
#endif

    if (mode_ == DagMode::Light) {
        clear_dag();
        cache_.reset_db();
        cache_.init_db();
    }

    auto section = this->read_section(BigNumber(0));
    if (section.has_value() && section->transactions.size() == 1) {
        // prove_transaction()
        auto network_id = section->transactions.begin()->sender();
        node->actorIndex()->set_network_id(network_id);
    }

    eLog("[Dag] The One");
}

std::string Dag::file_folder(const BigNumber &section) const {
    BigNumber file_section = section / Config::DataStorage::SECTION_SIZE;
    auto      path         = std::format("{}/{}", BlockchainConst::BLOCKCHAIN_FOLDER, file_section.to_string());
    return path;
}

std::string Dag::file_path(const BigNumber &section) const {
    auto path = std::format("{}/{}", this->file_folder(section), section.to_string());
    return path;
}

std::expected<Transaction, TransactionError> Dag::prepare_transaction(const Transaction       &transaction,
                                                                      const Actor<KeyPrivate> &signer) {
    auto tx = transaction;
    tx.set_section(current_section_ + 1);

    auto section = read_section(tx.section() - 1);
    if (!section.has_value() && transaction.type() != TransactionType::Genesis) {
        return std::unexpected(TransactionError::NoLastBlock);
    }

    if (section.has_value()) {
        for (const auto &prev_tx : section->transactions) {
            tx.insert_prev_hash(prev_tx.hash());
        }
    }

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
    if (status_ != DagStatus::Ready && status_ != DagStatus::Final) {
        const std::string &transaction_hash = transaction.hash();
        bool               is_cache_duplicate =
            std::any_of(cached_txs_.begin(), cached_txs_.end(), [&transaction_hash](const Transaction &tx) {
                return tx.hash() == transaction_hash;
            });

        if (!is_cache_duplicate) {
            cached_txs_.push_back(transaction);
        }
        return {};
    }

    if (transaction.section() > current_section_ + 5) {
        TransactionResult transaction_result { .hash   = transaction.hash(),
                                               .result = TransactionProveError::SectionTooBig };

        if (!responder.identifiers().empty()) {
            responder.send_response(transaction_result,
                                    MessageType::DagTransactionResult,
                                    SendMode::Focused,
                                    MessageStatus::Response);
        }

        return std::unexpected(false);
    }

    auto                  section = read_section(transaction.section());
    TransactionProveError res =
        this->prove_transaction(transaction,
                                section.has_value() ? section->transactions : std::set<Transaction> {});
    TransactionResult transaction_result { .hash = transaction.hash(), .result = res };

    if (res != TransactionProveError::NoError) {
        eLog("[Dag] Transaction not approved: {} {}", transaction, res);
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

        current_section_ = transaction.section();
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

    return {};
}

void Dag::network_transaction_result(const std::string hash, TransactionProveError result) {
    if (sended_transactions.find(hash) == sended_transactions.end()) {
        eLog("[Dag] Ignore transaction result: {}", hash);
        return;
    }

    auto transaction = this->sended_transactions[hash];
    this->sended_transactions.erase(hash);

    if (result != TransactionProveError::NoError) {
        eLog("[Dag] Our transaction not approved: {} / {}", transaction.section(), transaction.hash());
        return;
    } else {
        eLog("[Dag] Our transaction approved: {} / {}", transaction.section(), transaction.hash());
        current_section_ = transaction.section();
        update_range();
    }

    auto save_result = this->save_transaction(transaction);
    if (!save_result) {
        eLog("[Dag] Can't save our approved transaction {} in section {}",
             transaction.hash(),
             transaction.section());
        return;
    }

    const auto accounts = node->accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (transaction.sender() == accountId || transaction.receiver() == accountId) {
            auto section = read_section(transaction.section());
            if (!section.has_value()) {
                continue;
            }

            emit transaction_cache_.add(section->id, section->timestamp, transaction);

#ifdef IS_RC
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
#endif
        }
    }
}

void Dag::network_section(const Section &section) {
    //
}

std::unordered_map<ActorId, BigNumberFloat> Dag::calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                          const TokenId              &token_id) {

    // Use the read_section callback to provide access to sections
    auto read_section_callback = [this](const BigNumber &section_id) -> std::optional<Section> {
        return this->read_section(section_id);
    };

    // Use DagCache to calculate balances
    return cache_.calculate_balances(actor_ids,
                                     token_id,
                                     current_section_,
                                     first_saved_section_,
                                     read_section_callback);
}

void Dag::process_cached_transactions() {
    eLog("[Dag] Processing {} cached transactions after sync", cached_txs_.size());

    status_ = DagStatus::Final;

    while (!cached_txs_.empty()) {
        std::vector<Transaction> txs_to_process(cached_txs_.begin(), cached_txs_.end());
        cached_txs_.clear();

        for (const auto &tx : txs_to_process) {
            Responder responder(node->network());
            network_transaction(tx, responder);
        }
    }

    status_ = DagStatus::Ready;
    emit node->dagStatus(status_);
    set_sync_status(BlockchainSyncStatus::None);
}

std::optional<Section> Dag::read_section(const BigNumber &section_id) const {
    // mutex

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
    // mutex

    // try
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

bool Dag::save_transaction(const Transaction &transaction) {
    auto section = this->read_section(transaction.section());

    if (!section.has_value()) {
        // Create new section
        Section section { .id           = transaction.section(),
                          .timestamp    = Utils::current_date_ms(),
                          .transactions = { transaction } };

        current_section_ = section.id;

        // Check if cache needs updating
        cache_.check_and_update_cache(current_section_);

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

    // Add transaction to existing section
    section->transactions.insert(transaction);

    // Check if cache needs updating
    cache_.check_and_update_cache(current_section_);

    // Update first_saved_section_ if this is the first section or has a lower ID
    if (first_saved_section_ == BigNumber(-1) && transaction.section() >= BigNumber(0)) {
        if (mode_ == DagMode::Full || mode_ == DagMode::Light && transaction.section() != BigNumber(0)) {
            first_saved_section_ = transaction.section();
        }

        eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
    }

    // Update range file
    update_range();

    return write_section(section.value()).has_value();
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions) {
    // Check Genesis transactions
    if (tx.type() == TransactionType::Genesis) {
        if (tx.section() != BigNumber(0)) {
            return TransactionProveError::GenesisOnlyZeroSection;
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
    const auto accounts = node->accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (targetSender == accountId || targetReceiver == accountId) {
            return TransactionProveError::SelfPleasure;
        }
    }

    // Verify transaction hash integrity
    auto tx_copy = tx;
    tx_copy.calculate_hash();
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
    if (tx.type() == TransactionType::Reward || tx.type() == TransactionType::InitContract
        || tx.type() == TransactionType::Conversion) {
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
        return TransactionProveError::NoError;
    }

    // special conditions: receiver is null - coins burning,
    // contract creation
    // TODO: InitContract: check duplicate
    // Validate InitContract transactions
    if (tx.type() == TransactionType::InitContract) {
        auto count = tx.amount();
        if (count < 0 || count >= Token::MAX_TOKEN_COUNT) {
            return TransactionProveError::InvalidTokenCount;
        }
        return TransactionProveError::NoError;
    }

    // Validate Conversion transactions
    if (tx.type() == TransactionType::Conversion) {
        // Check conversion token information
        if (!tx.data().has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }
        auto from_token = TokenId::create(tx.data().value());
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
    std::vector<ActorId> actor_ids         = { targetSender };
    BigNumberFloat       senderBalance     = calculate_actors_balance(actor_ids, token)[targetSender];
    BigNumberFloat       transactionAmount = tx.amount();

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
            if (tx_check.data().has_value()) {
                auto from_token = TokenId::create(tx_check.data().value());
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
    sended_transactions.insert({ transaction.hash(), transaction });
}

void Dag::update_range() {
    std::string json = Json::serialize(SectionRange { .first       = first_saved_section_.to_string(),
                                                      .last        = current_section_.to_string(),
                                                      .last_cached = cache_.section().to_string() });

    // eLog("[Dag] Updating range: first={}, last={}, last_cached={}",
    //      first_saved_section_,
    //      current_section_,
    //      cache_.section());

    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    if (file.open(QFile::WriteOnly)) {
        file.write(json.data());
        file.close();

        QFile check_file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
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

    // timer_sync->stop();
    // timer_sync->start(10000);

    if (status_ != DagStatus::Sync) {
        status_ = DagStatus::Sync;
        emit node->dagStatus(status_);
    }

    last_info_.clear();
    set_sync_status(BlockchainSyncStatus::LastInfo);
    requests_count = node->network()->active_connections_count();
    node->network()->send_message(true,
                                  MessageType::BlockchainSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("[Dag] Starting sync in thread");
}

void Dag::start_check() {
    if (status_ != DagStatus::Ready || status_ == DagStatus::Maybe) {
        start_sync();
        // eLog("BC 12 start_check return");
        return;
    }

    last_info_.clear();
    check_status_  = BlockchainSyncStatus::LastInfo;
    requests_count = node->network()->active_connections_count();
    node->network()->send_message(true,
                                  MessageType::BlockchainSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 9 start_check");
}

void Dag::network_status_sync_request(const Responder &responder) {
    auto      block      = this->read_section(current_section_);
    BigNumber block_id   = block.has_value() ? block->id : BigNumber(-1);
    auto      hashs      = block.has_value() ? block->prev_hashs() : std::set<std::string> {};
    auto      zero_block = this->read_section(BigNumber(0));
    auto      last_info  = BlockchainLastInfo { .last_block_id = block_id,
                                                .last_hash     = hashs,
                                                .zero_date = zero_block.has_value() ? zero_block->timestamp : 0 };
    // eLog("network_status_sync_request, send: {}", last_info);
    responder.send_response(last_info,
                            MessageType::BlockchainSyncLastInfo,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_status_sync_response(const BlockchainLastInfo &last_info, const Responder &responder) {
    if (sync_status_ != BlockchainSyncStatus::LastInfo && check_status_ != BlockchainSyncStatus::LastInfo) {
        return;
    }
    // min(connections size, 5)

    auto zero_block = read_section(BigNumber(0));
    if (zero_block.has_value() && !last_info.last_hash.empty() && last_info.last_block_id != BigNumber(-1)
        && zero_block->timestamp < last_info.zero_date) {
        // TODO: need to remove
        // removeAll(false, true);
    }

    int count = std::min(requests_count, 5);

    last_info_.insert({ *responder.identifiers().begin(), last_info });

    if (sync_status_ == BlockchainSyncStatus::LastInfo && last_info_.size() >= count) {
        set_sync_status(BlockchainSyncStatus::Blocks);
        check_status_ = BlockchainSyncStatus::None;
        eLog("BC 6 sync status");
        send_sync_request();
    }

    if (check_status_ == BlockchainSyncStatus::LastInfo && last_info_.size() >= count) {
        check_status_ = BlockchainSyncStatus::Blocks;
        eLog("BC 7 check status");
        send_sync_request();
    }
}

void Dag::request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder) {
    auto range         = SectionRange { .first = from == -1 ? "0" : from.to_string(), .last = to.to_string() };
    auto responder_new = responder.with_new_message_id();

    node->network()->send_message(range,
                                  MessageType::DagSections,
                                  SendMode::Focused,
                                  MessageStatus::Request,
                                  responder_new);

    eLog("[Dag] Request sections from {} to {}", range.first, range.last);
}

void Dag::network_request_sections(const BigNumber &from, const BigNumber &to, const Responder &responder) {
    if (current_section_ < from) { // to
        eLog("[Dag] Send sections error: {} < {}", current_section_, from);
        return;
    }

    if (to < from) {
        eLog("[Dag] Send sections error: {} < {}", to, from);
        return;
    }

    std::vector<Transaction> txs;

    if (auto count = (to - from).to_int()) {
        txs.reserve(count.value() * 1.2);
    }

    for (BigNumber i = from; i <= to; i++) {
        auto section = this->read_section(i);
        if (!section.has_value()) {
            continue;
        }

        for (const auto &tx : section->transactions) {
            txs.push_back(tx);
        }
    }

    if (txs.empty()) {
        return;
    }

    eLog("[Dag] Send sections from {} to {}", to, from);

    auto ser      = MessagePack::serialize(txs);
    auto compress = qCompress(QByteArray::fromStdString(ser));
    responder.send_response(compress.toStdString(),
                            MessageType::DagSections,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void Dag::network_request_sections_response(const std::string &compressed, const Responder &responder) {
    QThreadPool::globalInstance()->start([this, compressed, responder]() {
        const auto txs = MessagePack::deserialize<std::vector<Transaction>>(
            qUncompress(QByteArray::fromStdString(compressed)).toStdString());

        if (!txs.has_value()) {
            return;
        }

        auto min = BigNumber(-1), max = BigNumber(-1);
        for (const auto &tx : std::as_const(txs.value())) {
            min = min != -1 ? std::min(tx.section(), min) : tx.section();
            max = std::max(tx.section(), max);
            save_transaction(tx);
        }

        eLog("[Dag] Saved sections from {} to {}", min, max);

        if (current_section_ >= sync_last_index - 1) {
            eLog("[Dag] Sync completed, processing cached transactions");

            process_cached_transactions();
            return;
        }

        request_sections(current_section_, std::min(sync_last_index, current_section_ + 100), responder);
    });
}

void Dag::network_request_light(const Responder &responder) {
    QThreadPool::globalInstance()->start([this, responder]() {
        std::vector<Transaction> txs;

        auto [cache_section, cache] = this->cache().read_cached_balances();
        if (cache_section == BigNumber(-1)) {
            cache_section = BigNumber(0);
        }

        txs.reserve(50);

        auto section = this->read_section(BigNumber(0));
        if (section.has_value()) {
            for (const auto &tx : section->transactions) {
                txs.push_back(tx);
            }
        }

        for (BigNumber i = cache_section; i <= current_section_; i++) {
            auto section = this->read_section(i);
            if (!section.has_value()) {
                continue;
            }

            for (const auto &tx : section->transactions) {
                txs.push_back(tx);
            }
        }

        if (txs.empty()) {
            eLog("[Dag] No transactions to send in light mode");
            return;
        }

        auto dag_light = DagLightPackage { .cache = cache, .cache_section = cache_section, .txs = txs };

        node->network()->send_message(dag_light,
                                      MessageType::DagLightData,
                                      SendMode::Focused,
                                      MessageStatus::Response,
                                      responder);

        eLog("[Dag] Sent light data: cache section {}, transactions count: {}", cache_section, txs.size());
    });
}

void Dag::network_response_light(const DagLightPackage &dag_light, const Responder &responder) {
    QThreadPool::globalInstance()->start([this, responder, dag_light]() {
        cache_.write_cached_balances(dag_light.cache, dag_light.cache_section);

        auto min = BigNumber(-1), max = BigNumber(-1);
        for (const auto &tx : std::as_const(dag_light.txs)) {
            min = min != -1 ? std::min(tx.section(), min) : tx.section();
            max = std::max(tx.section(), max);
            save_transaction(tx);
        }

        // if (first_saved_section_ == BigNumber(-1) && min >= BigNumber(0)) {
        //     first_saved_section_ = min;
        //     eLog("[Dag] Updated first_saved_section to {}", first_saved_section_);
        // }

        update_range();

        eLog("[Dag] Light sync completed: cache section {}, saved sections from {} to {}",
             dag_light.cache_section,
             min,
             max);

        process_cached_transactions();
    });
}

void Dag::set_sync_status(BlockchainSyncStatus status) {
    sync_status_ = status;
}

void Dag::send_sync_request() {
    auto section = this->read_section(current_section_);

    if (last_info_.empty()) {
        eLog("BC 5");
        return;
    }

    bool need_sync = false;

    if (!section.has_value()) {
        for (const auto &[_, info] : last_info_) {
            eLog("----- {}", info);
            if (info.last_block_id >= 0 && (info.last_block_id == BigNumber(0) || !info.last_hash.empty())) {
                need_sync = true;
                break;
            }
        }
    } else {
        const auto my_index = section->id;
        const auto my_hash  = section->prev_hashs();

        for (const auto &[_, info] : last_info_) {
            if (info.last_block_id > my_index) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                eLog("[Blockchain] Sync: remove block {}", my_index);
                break;
            }
            if (info.last_block_id == my_index && info.last_hash != my_hash) {
                need_sync = true;
                // remove_last_block();
                // blockIndex.removeById(my_index);
                eLog("[Blockchain] Sync: remove block {}", my_index);
                break;
            }
        }
    }

    if (!need_sync) {
        set_sync_status(BlockchainSyncStatus::None);
        check_status_ = BlockchainSyncStatus::None;
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
        if (info.last_block_id >= 0 && (info.last_block_id == BigNumber(0) || !info.last_hash.empty())) {
            nodes_by_block.emplace_back(id, info.last_block_id);
        }
    }

    // TODO: recheck
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        set_sync_status(BlockchainSyncStatus::None);
        check_status_ = BlockchainSyncStatus::None;
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

    if (sync_status_ != BlockchainSyncStatus::Blocks) {
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
    auto sync_index = /*mode_ == DagMode::Light ? calculate_genesis_id_for_block(nodes_by_block.front().second)
                                              : */ // request cache from last cache
        (last_block.has_value() ? last_block->id + 1 : BigNumber(0));
    sync_last_index = nodes_by_block.front().second;

    if (current_section_ > sync_last_index) {
        eLog("Not need sync");
        start_check();
        return;
    }

    eLog("sync_last_index {}", sync_last_index);
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
    check_status_ = BlockchainSyncStatus::None;
    // emit syncStart(sync_index, sync_last_index);
    eLog("syncStart");
}

void Dag::clear_dag() {
    current_section_     = BigNumber(-1);
    first_saved_section_ = BigNumber(-1);
    QFile(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/" + BlockchainConst::BLOCKCHAIN_RANGE))
        .remove();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/0")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/1")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/2")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/3")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/4")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/5")).removeRecursively();
    QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER + "/6")).removeRecursively();
}
