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

#include "blockchain/dag.h"

#include "managers/extrachain_node.h"
#include "network/message_body.h"
#include "network/network_manager.h"

Dag::Dag(ExtraChainNode *node)
    : node(node)
    , transaction_cache_(node, node) {
    QFile file(QString::fromStdString(BlockchainConst::BLOCKCHAIN_RANGE_PATH));
    if (file.open(QFile::ReadOnly)) {
        auto last_id_content = file.readAll();

        auto section_range = Json::deserialize<SectionRange>(last_id_content.toStdString());
        if (section_range.has_value()) {
            auto first_id_result   = BigNumber::create(section_range->first);
            auto current_id_result = BigNumber::create(section_range->current);

            if (!first_id_result.has_value() || !current_id_result.has_value()) {
                return;
            }

            current_section_     = current_id_result.value();
            first_saved_section_ = first_id_result.value();

            // return;
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
        return std::unexpected(false);
    }

    TransactionProveError res = this->prove_transaction(transaction, {});
    TransactionResult     transaction_result { .hash = transaction.hash(), .result = res };

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

    const auto accounts = node->accountController()->accountsIds();
    for (const auto &accountId : accounts) {
        if (transaction.sender() == accountId || transaction.receiver() == accountId) {
            auto section = read_section(transaction.section());
            if (!section.has_value()) {
                continue;
            }

            emit transaction_cache_.add(section->id, section->timestamp, transaction);

#ifdef IS_RC
            if (transaction.type() == TransactionType::Reward
                && accountId == node->accountController()->system_actor().id()) {
                Transaction tx;
                tx.setSender(accountId);
                tx.setReceiver(accountId);
                tx.setType(TransactionType::Conversion);
                tx.setData(ActorId().to_string());
                tx.setAmount(transaction.amount());
                tx.setToken(
                    ActorId("468faf2f1be6504a9a26f7f027"
                            "f7e43380b0d77d"));
                eLog("[Reward] Send conversion: {} coins", tx.amount());
                node->sendTransaction(tx, node->accountController()->system_actor());
            }
#endif
        }
    }
}

void Dag::network_section(const Section &section) {
    //
}

std::optional<Section> Dag::read_section(const BigNumber &section_id) {
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
        // create new one
        Section section { .id           = transaction.section(),
                          .timestamp    = Utils::current_date_ms(),
                          .transactions = { transaction } };

        current_section_ = section.id;
        update_range();

        return write_section(section).has_value();
    }

    section->transactions.insert(transaction);
    return write_section(section.value()).has_value();
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> transactions) {
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

    // auto res = this->blockIndex.search_duplicate(tx_copy.hash());
    // if (res.second != BigNumber(-1)) {
    //     return TransactionProveError::Duplicate;
    // }

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

std::unordered_map<ActorId, BigNumberFloat> Dag::calculate_actors_balance(const std::vector<ActorId> &actor_ids,
                                                                          const TokenId              &token_id) {
    std::unordered_map<ActorId, BigNumberFloat> balances;

    for (const auto &actor_id : actor_ids) {
        balances[actor_id] = BigNumberFloat(0);
    }

    eLog("calculate_actorS_balance: {} for token {}", actor_ids, token_id);
    if (current_section_ == -1) {
        for (const auto &actor_id : actor_ids) {
            balances[actor_id] = BigNumberFloat(0);
        }
        return balances;
    }

    for (BigNumber i = current_section_ + 1; i >= first_saved_section_; i--) {
        auto section = this->read_section(i);

        if (!section.has_value()) {
            continue;
        }
        if (section.has_value() && (section->transactions.empty() || section->id < 0)) {
            continue;
        }

        // if (txs->is_genesis()) {
        //     if (ignore_genesis && currentBlock->id() != BigNumber(0)) {
        //         // if not mega
        //         continue;
        //     }

        //     // eLog("{} BAALANCE Genesis", i);
        //     auto       genesis = blockIndex.getGenesisBlockById(i);
        //     const auto rows    = genesis->dataRows();

        //     for (const auto &[key, row] : rows) {
        //         for (const auto &actor_id : actor_ids) {
        //             if (key.actorId == actor_id && key.tokenId == token_id)
        //                 balances[actor_id] += row.state;
        //         }
        //     }

        //     return balances;
        // }

        // tx check
        for (auto &tx : section->transactions) {
            for (const auto &actor_id : actor_ids) {
                if (tx.type() == TransactionType::Reward && tx.sender() == actor_id && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE Reward += {}, =
                    // {}", i, tx.amount(),
                    // balances[actor_id]);
                    continue;
                }

                if (tx.type() == TransactionType::InitContract && tx.sender() == actor_id
                    && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE InitContract += {},
                    // = {}", i, tx.amount(),
                    // balances[actor_id]);
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

                    if (from_token.value() == tx.token()) {
                        continue;
                    }

                    if (from_token.value() == token_id) {
                        balances[actor_id] -= tx.amount();
                        // eLog("{} BAALANCE Conversion -=
                        // {}, = {}", i, tx.amount(),
                        // balances[actor_id]);
                    }

                    if (tx.token() == token_id) {
                        balances[actor_id] += tx.amount();
                        // eLog("{} BAALANCE Conversion +=
                        // {}, = {}", i, tx.amount(),
                        // balances[actor_id]);
                    }
                    continue;
                }

                if (tx.receiver() == actor_id && tx.token() == token_id) {
                    balances[actor_id] += tx.amount();
                    // eLog("{} BAALANCE += {}, = {}", i,
                    // tx.amount(), balances[actor_id]);
                }

                if (tx.sender() == actor_id && tx.token() == token_id) {
                    balances[actor_id] -= tx.amount();
                    // eLog("{} BAALANCE -= {}, = {}", i,
                    // tx.amount(), balances[actor_id]);
                }
            }
        }
    }

    return balances;
}

void Dag::add_transaction_sended(const Transaction &transaction) {
    // eLog("[Dag] Add to sended: {}", transaction.hash());
    sended_transactions.insert({ transaction.hash(), transaction });
}
