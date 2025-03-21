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
    // create folders
}

std::expected<Transaction, TransactionError> Dag::send_transaction(const Transaction       &transaction,
                                                                   const Actor<KeyPrivate> &signer) {
    auto tx = transaction;
    tx.set_section(current_section_ + 1);
    // hash array

    auto sign_res = tx.sign(signer);
    if (!sign_res) {
        return std::unexpected(TransactionError::Unknown);
    }

    eLog("[Dag] Send {}", tx);
    save_transaction(transaction); // temp
    node->network()->send_message(transaction, MessageType::DagTransaction, SendMode::Broadcast);

    return tx;
}

std::expected<void, bool> Dag::network_transaction(const Transaction &transaction) {
    if (status_ != DagStatus::Ready) {
        return std::unexpected(false);
    }

    TransactionProveError res = this->prove_transaction(transaction, {});

    if (res != TransactionProveError::NoError) {
        eLog("[Dag] Transaction not approved: {} {}", transaction, res);
    }

    eLog("[Dag] Transaction approved: {}", transaction);

    auto save_result = save_transaction(transaction);
    if (!save_result) {
        return std::unexpected(false);
    }

    current_section_ = transaction.section();

    return {};
}

std::optional<std::set<Transaction>> Dag::read_transactions(const BigNumber &section) {
    // mutex

    auto p    = std::format("{}/{}", BlockchainConst::BLOCKCHAIN_FOLDER, section.to_string());
    auto path = FsPath::create(p);
    if (path.has_value()) {
        auto content = Utils::read_file_content(path.value());
        if (content.has_value()) {
            auto txs_result = Json::deserialize<std::set<Transaction>>(content.value());
            if (txs_result.has_value()) {
                return txs_result.value();
            }
        }
    }

    return std::nullopt;
}

std::optional<bool> Dag::save_transactions(const BigNumber &section, const std::set<Transaction> &txs) {
    // mutex

    auto p    = std::format("{}/{}", BlockchainConst::BLOCKCHAIN_FOLDER, section.to_string());
    auto path = FsPath::create(p);
    if (!path.has_value()) {
        return std::nullopt;
    }

    auto res = Utils::write_file_content(path.value(), Json::serialize(txs));
    if (!res.has_value()) {
        return std::nullopt;
    }

    return true;
}

bool Dag::save_transaction(const Transaction &transaction) {
    std::set<Transaction> txs = {};

    auto txs_result = this->read_transactions(transaction.section());
    if (txs_result.has_value()) {
        txs = txs_result.value();
    }

    txs.insert(transaction);

    auto res = this->save_transactions(transaction.section(), txs);
    if (!res.has_value()) {
        return false;
    }

    return true;
}

TransactionProveError Dag::prove_transaction(const Transaction &tx, const std::set<Transaction> transactions) {
    // temp
    if (tx.type() == TransactionType::Repeatable) {
        return TransactionProveError::NoError;
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
        auto from_token = TokenId::create(tx.data());
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

    for (BigNumber i = current_section_; i >= first_saved_section_; i--) {
        auto txs = this->read_transactions(i);

        if (!txs.has_value()) {
            continue;
        }
        if (txs.has_value() && txs->empty()) {
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
        for (auto &tx : txs.value()) {
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
                    auto from_token = ActorId::create(tx.data());
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
