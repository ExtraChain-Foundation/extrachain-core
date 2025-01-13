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

#include <QObject>

#include "blockchain/actor.h"
#include "utils/db_connector.h"

class ExtraChainNode;
class Transaction;

static const uint size_of_data_list = 7;

struct TokenData {
    std::string token, owner, count, name, ticker, color, smart;

    bool operator==(const TokenData &other) const {
        return token == other.token && owner == other.owner && count == other.count && name == other.name
               && ticker == other.ticker && color == other.color && smart == other.smart;
    }

    // Overload the inequality operator
    bool operator!=(const TokenData &other) const {
        return !(*this == other);
    }

    QJsonDocument toJsonDocument();
    DbRow         toDBRow();
};

enum class CreateTokenError {
    InvalidAmount,
    InvalidName,
    ExistToken
};

class TokenManager : public QObject {
    Q_OBJECT

    ExtraChainNode *node;

    void sendInitialTransaction(const ActorId &owner, const TokenId &token, const BigNumberFloat &amount);
    void initializeTokenArray();
    bool tokenExist(const std::string &nameToken, const std::string &tickerToken);

public:
    TokenManager(ExtraChainNode *node);
    ~TokenManager() = default;

    bool isContract(const QString &pathFile);

    static bool                   isValidName(const std::string &name);
    static bool                   isValidTicker(const std::string &ticker);
    static QMap<QString, QString> mapTokens();

public slots:
    std::expected<TokenData, CreateTokenError> createToken(const std::string &tokenCount,
                                                           const std::string &tokenName,
                                                           const std::string &symbol,
                                                           const ActorId     &owner,
                                                           const std::string &color,
                                                           const std::string &predefine_token_id = "");
    void                                       checkIsContract(const QString &pathToFile);

protected:
    bool checkJsonObjectHasTokenFields(const QJsonObject &jsonObj);

signals:
    void verifyActor(Actor<KeyPublic> actor);
    void sendTransactionCreateToken(const ActorId &actorId, const Transaction &tx);
    void saveActorInPrivateProfile(const QByteArray &id,
                                   const QString    &type    = "wallet",
                                   const bool       &rewrite = false);
    void errorNameTokenExist(const QString &);
    void errorTickerTokenExist(const QString &);
    void added(const ActorId &owner, const TokenId &token);
    void sendToken(const ActorId &actor, const QString &pathToJson);
    void newToken();
};
