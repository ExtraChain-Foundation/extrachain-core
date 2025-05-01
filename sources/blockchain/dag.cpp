#include "blockchain/dag.h"

#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"

// Cache configuration constants
constexpr int  CACHE_LAG_SECTIONS        = 15;
constexpr int  DB_UPDATE_FREQUENCY       = 1;
constexpr bool ALLOW_CACHE_RECALCULATION = true;

Dag::Dag(ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node, node) {
    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    if (file.open(QFile::ReadOnly)) {
        auto last_id_content = file.readAll();

        auto section_range = Json::deserialize<SectionRange>(last_id_content.toStdString());
        if (section_range.has_value()) {
            auto first_id_result   = BigNumber::create(section_range->first);
            auto current_id_result = BigNumber::create(section_range->last);

            if (!first_id_result.has_value() || !current_id_result.has_value()) {
                return;
            }

            current_section_     = current_id_result.value();
            first_saved_section_ = first_id_result.value();
            eLog("[Dag] Current: {}, first: {}", current_section_, first_saved_section_);
            file.close();
        }
    } else {
        QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).removeRecursively();
    }

    if (!QDir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER)).exists()) {
        QDir().mkdir(QString::fromStdString(BlockchainConst::BLOCKCHAIN_FOLDER));
        transaction_cache_.make_files();
    }

    auto section = this->read_section(BigNumber(0));
    if (section.has_value() && section->transactions.size() == 1) {
        // prove_transaction()
        auto network_id = section->transactions.begin()->sender();
        node->actorIndex()->set_network_id(network_id);
    }

    // Initialize cache database
    init_cache_db();

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
        return std::unexpected(TransactionError::Unknown);
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
    if (status_ != DagStatus::Ready) {
        return {};
    }

    if (transaction.section() > current_section_ + 5) {
        TransactionResult transaction_result { .hash   = transaction.hash(),
                                               .result = TransactionProveError::SectionTooBig };
        responder.send_response(transaction_result,
                                MessageType::DagTransactionResult,
                                SendMode::Focused,
                                MessageStatus::Response);
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

        // Update memory cache immediately
        update_memory_cache_for_transaction(transaction);

        // Check if DB cache needs updating
        check_and_update_db_cache();
    }

    responder.send_response(transaction_result,
                            MessageType::DagTransactionResult,
                            SendMode::Focused,
                            MessageStatus::Response);

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

    // Update memory cache immediately
    update_memory_cache_for_transaction(transaction);

    // Check if DB cache needs updating
    check_and_update_db_cache();

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

BigNumber Dag::calculate_last_cache_id(const BigNumber &id) {
    return id / Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS
           * Config::DataStorage::CONSTRUCT_GENESIS_EVERY_BLOCKS;
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
        // create new one
        Section section { .id           = transaction.section(),
                          .timestamp    = Utils::current_date_ms(),
                          .transactions = { transaction } };

        current_section_ = section.id;
        update_range();

        if (calculate_last_cache_id(current_section_) != dag_cache.section) {
            update_cache();
        }

        return write_section(section).has_value();
    }

    section->transactions.insert(transaction);
    return write_section(section.value()).has_value();
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> &transactions) {
    // temp
    // if (tx.type() == TransactionType::Repeatable) {
    //     return TransactionProveError::NoError;
    // }

    if (tx.type() == TransactionType::Genesis) {
        if (tx.section() != BigNumber(0)) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        if (!node->network_id().is_zero() && tx.sender() != tx.receiver() && tx.sender() != node->network_id()) {
            return TransactionProveError::GenesisOnlyZeroSection;
        }

        return TransactionProveError::NoError;
    }

    auto section = this->read_section(BigNumber(tx.section() - 1));
    if (section.has_value()) {
        // TODO: check
    }

    // eLog("[Blockchain] Transaction prove started: {}",
    // tx);
    // TODO: temp, remove
    if (tx.amount() == 0) {
        return TransactionProveError::AmountZero;
    }

    if (tx.amount() < 0) {
        return TransactionProveError::AmountLessZero;
    }

    ActorId        targetSender   = tx.sender();
    ActorId        targetReceiver = tx.receiver();
    const ActorId &mainActorId    = node->accountController()->system_actor().id();

    const auto accounts = node->accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (targetSender == accountId || targetReceiver == accountId) {
            return TransactionProveError::SelfPleasure;
        }
    }

    auto tx_copy = tx;
    tx_copy.calculate_hash();
    if (tx.hash() != tx_copy.hash()) {
        return TransactionProveError::WrongHash;
    }

    auto tx_result = search_transaction(tx_copy.hash());
    if (tx_result.has_value()) {
        return TransactionProveError::Duplicate;
    }

    if (targetSender.is_zero()) {
        return TransactionProveError::SenderZero;
    }

    Actor<KeyPublic> senderActor;
    senderActor = node->actorIndex()->getActor(targetSender);
    if (senderActor.empty()) {
        return TransactionProveError::SenderNotExists;
    }

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

    if (targetReceiver.is_zero()) {
        return TransactionProveError::ReceiverZero;
    }

    Actor<KeyPublic> receiverActor;
    receiverActor = node->actorIndex()->getActor(targetReceiver);
    if (receiverActor.empty()) {
        return TransactionProveError::ReceiverNotExists;
    }

    if (tx.type() == TransactionType::Reward || tx.type() == TransactionType::InitContract
        || tx.type() == TransactionType::Conversion) {
        if (targetSender != targetReceiver) {
            return TransactionProveError::NotIdenticalSenderReceiver;
        }
    } else {
        if (targetSender == targetReceiver) {
            return TransactionProveError::IdenticalSenderReceiver;
        }
    }

    // auto block = read_last_block();
    // if (!block.has_value()) {
    //     return TransactionProveError::EmptyBlockchain;
    // }
    // if (block->isEmpty()) {
    //     return TransactionProveError::EmptyBlockchain;
    // }

    if (tx.signature().empty()) {
        return TransactionProveError::MissingSignature;
    }

    bool verify = tx.verify(senderActor);
    if (!verify) {
        return TransactionProveError::InvalidSignature;
    }

    if (tx.type() == TransactionType::Reward) {
        return TransactionProveError::NoError;
    }

    // special conditions: receiver is null - coins burning,
    // contract creation
    // TODO: InitContract: check duplicate
    if (tx.type() == TransactionType::InitContract) {
        auto count = tx.amount();
        if (count < 0 || count >= Token::MAX_TOKEN_COUNT) {
            return TransactionProveError::InvalidTokenCount;
        }

        return TransactionProveError::NoError;
    }

    if (tx.type() == TransactionType::Conversion) {
        return TransactionProveError::NoError;
    }

    TokenId token = tx.token();
    if (tx.type() == TransactionType::Conversion) {
        if (!tx.data().has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }
        auto from_token = TokenId::create(tx.data().value());
        if (!from_token.has_value()) {
            return TransactionProveError::ConversionIncorrectFromToken;
        }

        token = from_token.value();

        if (from_token == tx.token()) {
            return TransactionProveError::ConversionEqualToken;
        }
    }

    return TransactionProveError::NoError;

    BigNumberFloat transactionAmount = tx.amount();
    BigNumberFloat senderBalance     = BigNumberFloat(); // calculate_actor_balance(targetSender, token);

    // tx check
    for (const Transaction &tx_check : std::as_const(transactions)) {
        if (tx.hash() == tx_check.hash()) {
            continue;
        }

        if (tx_check.token() != token) {
            continue;
        }

        if (tx_check.type() == TransactionType::Reward && tx_check.sender() == tx_check.receiver()
            && tx_check.token() == token) {
            senderBalance += tx_check.amount();
            continue;
        }

        if (tx_check.type() == TransactionType::InitContract && tx_check.sender() == tx_check.receiver()
            && tx_check.token() == token) {
            senderBalance += tx_check.amount();
            continue;
        }

        if (tx_check.type() == TransactionType::Conversion && tx_check.sender() == tx_check.receiver()) {
            if (tx_check.data() == token.to_string()) {
                senderBalance -= tx_check.amount();
            }
            if (tx_check.token() == token) {
                senderBalance += tx_check.amount();
            }
            continue;
        }

        if (tx_check.sender() == targetSender && tx_check.token() == token) {
            senderBalance -= tx_check.amount();
        }

        if (tx_check.receiver() == targetReceiver && tx_check.token() == token) {
            senderBalance += tx_check.amount();
        }
    }

    if (senderBalance < transactionAmount) {
        return TransactionProveError::SenderBalanceBelowZero;
    }

    return TransactionProveError::NoError;
}

void Dag::update_cache() {
    auto cache_id = calculate_last_cache_id(current_section_);

    // If cache hasn't changed, no need to update
    if (dag_cache.section == cache_id && !dag_cache.dirty) {
        return;
    }

    // Account for safe lag between current section and cache
    auto safe_cache_id = calculate_last_cache_id(current_section_ - CACHE_LAG_SECTIONS);

    // Only update if we're moving forward
    if (safe_cache_id <= dag_cache.section) {
        return;
    }

    eLog("[Dag] Updating cache for section: {}", safe_cache_id);

    // Update the in-memory cache
    dag_cache.section   = safe_cache_id;
    dag_cache.timestamp = Utils::current_date_ms();

    // Clear previous balances
    dag_cache.balances.clear();

    // Collect unique actors and tokens from transactions
    std::set<ActorId> unique_actors;
    std::set<TokenId> unique_tokens;

    // Scan all sections up to the cache section to find actors and tokens
    for (BigNumber i = safe_cache_id; i >= first_saved_section_; i--) {
        auto section = this->read_section(i);
        if (!section.has_value() || section->transactions.empty()) {
            continue;
        }

        for (const auto &tx : section->transactions) {
            // Add sender and receiver to unique actors
            if (!tx.sender().is_zero()) {
                unique_actors.insert(tx.sender());
            }
            if (!tx.receiver().is_zero()) {
                unique_actors.insert(tx.receiver());
            }

            // Add token to unique tokens
            unique_tokens.insert(tx.token());

            // If Conversion transaction, also add from_token
            if (tx.type() == TransactionType::Conversion && tx.data().has_value()) {
                auto from_token = TokenId::create(tx.data().value());
                if (from_token.has_value()) {
                    unique_tokens.insert(from_token.value());
                }
            }
        }
    }

    eLog("[Dag] Found {} unique actors and {} unique tokens for caching",
         unique_actors.size(),
         unique_tokens.size());

    // Calculate balances for relevant actor-token pairs
    for (const auto &actor_id : unique_actors) {
        for (const auto &token_id : unique_tokens) {
            std::vector<ActorId> actor_vec = { actor_id };
            auto balance_map               = calculate_actors_balance_internal(actor_vec, token_id, BigNumber(-1));

            if (!balance_map.empty() && balance_map[actor_id] != BigNumberFloat(0)) {
                ActorPair pair { actor_id, token_id };
                dag_cache.balances[pair] = balance_map[actor_id];
            }
        }
    }

    dag_cache.dirty = true;
    dag_cache.sections_since_update++;

    // Check if we should persist to DB
    check_and_update_db_cache();

    eLog("[Dag] Cache updated for section: {} with {} balance entries", safe_cache_id, dag_cache.balances.size());
}

std::unordered_map<ActorId, BigNumberFloat> Dag::calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                          const TokenId              &token_id) {

    std::unordered_map<ActorId, BigNumberFloat> balances;

    // Initialize balances to zero
    for (const auto &actor_id : actor_ids) {
        balances[actor_id] = BigNumberFloat(0);
    }

    eLog("[Dag] Calculate balances for {} actors and token {}", actor_ids.size(), token_id);

    if (current_section_ == BigNumber(-1)) {
        return balances;
    }

    // Find the latest cache section before current
    BigNumber cache_section = calculate_last_cache_id(current_section_);

    // Check if we have this cache in memory
    bool used_cache = false;
    if (dag_cache.section == cache_section) {
        used_cache = true;

        // Get balances from memory cache
        for (const auto &actor_id : actor_ids) {
            ActorPair pair { actor_id, token_id };
            auto      it = dag_cache.balances.find(pair);
            if (it != dag_cache.balances.end()) {
                balances[actor_id] = it->second;
            }
        }
    }
    // Try to get from DB if not in memory
    else if (cache_db && cache_db->is_open()) {
        for (const auto &actor_id : actor_ids) {
            DbRow binds = { { "section_id", cache_section.to_string() },
                            { "actor_id", actor_id.to_string() },
                            { "token_id", token_id.to_string() } };

            auto rows = cache_db->select(
                "SELECT balance FROM balance_cache WHERE section_id = @section_id AND actor_id = @actor_id AND "
                "token_id = @token_id",
                "balance_cache",
                binds);

            if (!rows.empty() && rows[0].contains("balance")) {
                auto balance = BigNumberFloat::create(rows[0]["balance"]);
                if (balance.has_value()) {
                    balances[actor_id] = balance.value();
                    used_cache         = true;
                }
            }
        }
    }

    // If couldn't use cache, handle based on mode
    if (!used_cache) {
        if (mode_ == DagMode::Light) {
            // Light mode requires cache
            // !!! Cache should be requested from network here
            eLog("[Dag] Light mode missing cache for section {}", cache_section);
            request_cache_from_network(cache_section);

            // Return empty result, will retry when cache is available
            return balances;
        } else {
            // Full mode can calculate from scratch
            if (ALLOW_CACHE_RECALCULATION) {
                eLog("[Dag] Recalculating cache from scratch for section {}", cache_section);
                update_cache(); // Recalculate and store the cache

                // Try again with the newly calculated cache
                return calculate_actors_balance(actor_ids, token_id);
            } else {
                // Request from network instead of calculating
                eLog("[Dag] Requesting cache from network for section {}", cache_section);
                request_cache_from_network(cache_section);

                // Fall back to full calculation just this once
                return calculate_actors_balance_internal(actor_ids, token_id, BigNumber(-1));
            }
        }
    }

    // Process transactions after the cache section
    for (BigNumber i = current_section_; i > cache_section; i--) {
        auto section = this->read_section(i);
        if (!section.has_value() || section->transactions.empty() || section->id < 0) {
            continue;
        }

        // Process each transaction in the section
        for (auto &tx : section->transactions) {
            process_transaction_for_balance(tx, actor_ids, token_id, balances);
        }
    }

    return balances;
}

void Dag::process_transaction_for_balance(const Transaction                           &tx,
                                          const std::vector<ActorId>                  &actor_ids,
                                          const TokenId                               &token_id,
                                          std::unordered_map<ActorId, BigNumberFloat> &balances) {

    for (const auto &actor_id : actor_ids) {
        if (tx.type() == TransactionType::Reward && tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
            continue;
        }

        if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
            continue;
        }

        if (tx.type() == TransactionType::Conversion && tx.sender() == actor_id) {
            if (!tx.data().has_value()) {
                continue;
            }
            auto from_token = ActorId::create(tx.data().value());
            if (!from_token.has_value()) {
                continue;
            }

            if (from_token.value() == token_id) {
                balances[actor_id] -= tx.amount();
            }

            if (tx.token() == token_id) {
                balances[actor_id] += tx.amount();
            }
            continue;
        }

        if (tx.receiver() == actor_id && tx.token() == token_id) {
            balances[actor_id] += tx.amount();
        }

        if (tx.sender() == actor_id && tx.token() == token_id) {
            balances[actor_id] -= tx.amount();
        }
    }
}

std::unordered_map<ActorId, BigNumberFloat> Dag::calculate_actors_balance_internal(
    const std::vector<ActorId> &actor_ids,
    const TokenId              &token_id,
    const BigNumber            &start_section) {

    std::unordered_map<ActorId, BigNumberFloat> balances;

    for (const auto &actor_id : actor_ids) {
        balances[actor_id] = BigNumberFloat(0);
    }

    // Start from the requested section or from current if -1
    BigNumber begin_section = (start_section == BigNumber(-1)) ? current_section_ : start_section;

    for (BigNumber i = begin_section; i >= first_saved_section_; i--) {
        auto section = this->read_section(i);

        if (!section.has_value()) {
            continue;
        }

        if (section.has_value() && (section->transactions.empty() || section->id < 0)) {
            continue;
        }

        // Process each transaction in the section
        for (auto &tx : section->transactions) {
            process_transaction_for_balance(tx, actor_ids, token_id, balances);
        }
    }

    return balances;
}

void Dag::add_transaction_sended(const Transaction &transaction) {
    // eLog("[Dag] Add to sended: {}", transaction.hash());
    sended_transactions.insert({ transaction.hash(), transaction });
}

void Dag::update_range() {
    std::string json = Json::serialize(
        SectionRange { .first = first_saved_section_.to_string(), .last = current_section_.to_string() });
    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    if (file.open(QFile::WriteOnly)) {
        file.write(json.data());
        file.close();
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

bool Dag::init_cache_db() {
    if (cache_db && cache_db->is_open()) {
        return true;
    }

    std::string db_path = BlockchainConst::BLOCKCHAIN_FOLDER + "/balance_cache.db";
    cache_db            = std::make_unique<DbConnector>(db_path);

    if (!cache_db->open()) {
        eLog("[Dag] Failed to open cache database");
        return false;
    }

    // Create table if it doesn't exist
    bool success = cache_db->query(Config::DataStorage::DagCacheCreate);

    if (!success) {
        eLog("[Dag] Failed to create cache table");
        return false;
    }

    eLog("[Dag] Cache database initialized");
    return true;
}

void Dag::update_memory_cache_for_transaction(const Transaction &transaction) {
    // Skip if transaction doesn't affect balances
    // if (transaction.type() == TransactionType::Unknown) {
    //     return;
    // }

    // Get actors affected by this transaction
    std::vector<ActorId> actors;
    if (!transaction.sender().is_zero()) {
        actors.push_back(transaction.sender());
    }
    if (!transaction.receiver().is_zero() && transaction.receiver() != transaction.sender()) {
        actors.push_back(transaction.receiver());
    }

    if (actors.empty()) {
        return;
    }

    // Get tokens affected by this transaction
    std::vector<TokenId> tokens = { transaction.token() };

    // Also handle from_token in conversion transactions
    if (transaction.type() == TransactionType::Conversion && transaction.data().has_value()) {
        auto from_token = TokenId::create(transaction.data().value());
        if (from_token.has_value() && from_token.value() != transaction.token()) {
            tokens.push_back(from_token.value());
        }
    }

    // Mark cache as dirty
    dag_cache.dirty = true;
    dag_cache.sections_since_update++;

    // Process the transaction for each actor-token pair
    for (const auto &actor_id : actors) {
        for (const auto &token_id : tokens) {
            ActorPair pair { actor_id, token_id };

            // Get current balance or initialize to 0
            BigNumberFloat balance = BigNumberFloat(0);
            auto           it      = dag_cache.balances.find(pair);
            if (it != dag_cache.balances.end()) {
                balance = it->second;
            }

            // Update balance based on transaction type
            if (transaction.type() == TransactionType::Reward && transaction.sender() == actor_id
                && transaction.token() == token_id) {
                balance += transaction.amount();
            } else if (transaction.type() == TransactionType::InitContract && transaction.sender() == actor_id
                       && transaction.token() == token_id) {
                balance += transaction.amount();
            } else if (transaction.type() == TransactionType::Conversion && transaction.sender() == actor_id) {
                if (transaction.data().has_value()) {
                    auto from_token = TokenId::create(transaction.data().value());
                    if (from_token.has_value()) {
                        if (from_token.value() == token_id) {
                            balance -= transaction.amount();
                        }
                        if (transaction.token() == token_id) {
                            balance += transaction.amount();
                        }
                    }
                }
            } else {
                if (transaction.receiver() == actor_id && transaction.token() == token_id) {
                    balance += transaction.amount();
                }
                if (transaction.sender() == actor_id && transaction.token() == token_id) {
                    balance -= transaction.amount();
                }
            }

            // Update the cache with the new balance
            dag_cache.balances[pair] = balance;
        }
    }
}

void Dag::check_and_update_db_cache() {
    // Check if we need to update the DB cache
    if (!dag_cache.dirty || dag_cache.sections_since_update < DB_UPDATE_FREQUENCY) {
        return;
    }

    // Calculate safe section ID (with lag)
    auto current_cache_id = calculate_last_cache_id(current_section_);
    auto safe_cache_id    = calculate_last_cache_id(current_section_ - CACHE_LAG_SECTIONS);

    // Don't update if safe cache ID is behind current cache
    if (safe_cache_id <= dag_cache.section) {
        return;
    }

    // Ensure cache is for the safe ID
    if (dag_cache.section != safe_cache_id) {
        // Full update needed
        update_cache();
        return;
    }

    // Initialize DB if needed
    if (!init_cache_db()) {
        eLog("[Dag] Failed to initialize cache database, skipping cache update");
        return;
    }

    eLog("[Dag] Flushing cache to database for section {}", dag_cache.section);

    // Begin transaction for faster batch inserts
    cache_db->query("BEGIN TRANSACTION");

    // Write all balances to DB
    for (const auto &[pair, balance] : dag_cache.balances) {
        DbRow data = { { "section_id", dag_cache.section.to_string() },
                       { "actor_id", pair.actor_id.to_string() },
                       { "token_id", pair.token_id.to_string() },
                       { "balance", balance.to_string() } };

        cache_db->replace(Config::DataStorage::DagCacheTable, data);
    }

    // Commit transaction
    cache_db->query("COMMIT");

    // Reset counters
    dag_cache.dirty                 = false;
    dag_cache.sections_since_update = 0;

    eLog("[Dag] Cache flushed to database for section {}", dag_cache.section);

    // !!! Cache is sent to network from here
    // Each node distributes its cache to maintain decentralization
}

void Dag::request_cache_from_network(const BigNumber &section) {
    // !!! Cache should be requested from network here
    // In a p2p network, each client also serves as a server
    eLog("[Dag] Requesting cache for section {} from network", section);

    // This would typically involve creating a network message
    // and sending it to peers, similar to how sections are requested
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
        // emit statusChanged(status_);
    }

    last_info_.clear();
    set_sync_status(BlockchainSyncStatus::LastInfo);
    requests_count = node->network()->active_connections_count();
    node->network()->send_message(true,
                                  MessageType::BlockchainSyncLastInfo,
                                  SendMode::Neighbours,
                                  MessageStatus::Request);

    eLog("BC 10 start_sync");
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
    auto range = SectionRange { .first = from.to_string(), .last = to.to_string() };

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

    if (to <= from) {
        eLog("[Dag] Send sections error: {} <= {}", to, from);
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

    eLog("[Dag] Save sections from {} to {}", min, max);

    // emit syncProgress(blockIndex.last_saved_id);
    // eLog("-> {} {}", blockIndex.last_saved_id, sync_last_index - 1);
    if (current_section_ >= sync_last_index - 1) {
        eLog("-> START CHECK");
        status_ = DagStatus::Maybe;
        //     emit statusChanged(status_);
        start_check();
        return;
    }

    request_sections(current_section_, std::min(sync_last_index, current_section_ + 100), responder);
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
            if (info.last_block_id >= 0 && !info.last_hash.empty()) {
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
        status_       = DagStatus::Ready;
        // emit statusChanged(status_);
        // timer_sync->stop();

        // emit syncEnd();

        eLog("BC 4");
        return; // end sync
    }

    int connections = requests_count;
    int max_nodes   = std::min(connections, 5);

    std::vector<std::pair<std::string, BigNumber>> nodes_by_block;
    for (const auto &[id, info] : last_info_) {
        if (info.last_block_id >= 0 && !info.last_hash.empty()) {
            nodes_by_block.emplace_back(id, info.last_block_id);
        }
    }

    // TODO: recheck
    if (nodes_by_block.empty()) {
        eLog("BC 3");
        set_sync_status(BlockchainSyncStatus::None);
        check_status_ = BlockchainSyncStatus::None;
        status_       = DagStatus::Ready;
        // emit statusChanged(status_);
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
    request_sections(current_section_, std::min(sync_last_index, current_section_ + 100), responder);
    // request from to
    check_status_ = BlockchainSyncStatus::None;
    // emit syncStart(sync_index, sync_last_index);
    eLog("syncStart");
}
