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

#pragma once

#include <boost/describe/class.hpp>

#include <QObject>

#include "blockchain/actor.h"

class ExtraChainNode;
class Transaction;

struct TokenData {
    TokenId                    token_id;
    ActorId                    owner_id;
    std::string                name;
    std::string                ticker;
    BigNumberFloat             count;
    std::string                color;
    std::string                smart;
    std::optional<BigNumber>   section_id;
    std::optional<std::string> tx_hash;

    bool operator==(const TokenData &other) const {
        return token_id == other.token_id && owner_id == other.owner_id && count == other.count
               && name == other.name && ticker == other.ticker && color == other.color && smart == other.smart;
    }

    // Overload the inequality operator
    bool operator!=(const TokenData &other) const {
        return !(*this == other);
    }
};
BOOST_DESCRIBE_STRUCT(TokenData, (), (token_id, owner_id, count, name, ticker, color, smart, section_id, tx_hash))

struct TokenDataShort { // for data in transaction
    std::string                name;
    std::string                ticker;
    std::string                color;
    std::optional<std::string> smart;
};
BOOST_DESCRIBE_STRUCT(TokenDataShort, (), (name, ticker, color, smart))

enum class CreateTokenError {
    NoConnections,
    InvalidAmount,
    InvalidName,
    ExistToken,
    InvalidTx,
    InvalidOwnerId
};

class TokenManager : public QObject {
    Q_OBJECT

public:
    TokenManager(ExtraChainNode *node);
    ~TokenManager() = default;

    bool token_exists(const std::string &name, const std::string &ticker);
    bool name_exists(const std::string &name);
    bool ticker_exists(const std::string &ticker);

    static std::unordered_map<ActorId, std::string> read_tokens();

    std::expected<TokenData, CreateTokenError> create_token(const ActorId        &owner_id,
                                                            const std::string    &token_name,
                                                            const std::string    &symbol,
                                                            const BigNumberFloat &token_count,
                                                            const std::string    &color,
                                                            const std::string    &predefine_token_id = "");

    void final_token_creation(const Transaction &transaction);

    static bool is_valid_token_name(const std::string &name);
    static bool id_valid_token_ticker(const std::string &ticker);

private:
    ExtraChainNode                            *node;
    std::unordered_map<std::string, TokenData> cache_creation_; // TODO: also save to temp file

signals:
    void verifyActor(Actor<KeyPublic> actor);
    void errorNameTokenExist(const QString &);
    void errorTickerTokenExist(const QString &);
    void added(const ActorId &owner, const TokenId &token);
};
