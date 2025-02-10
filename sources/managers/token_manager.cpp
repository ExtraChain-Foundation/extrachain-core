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

#include "managers/token_manager.h"

#include <QString>
#include <QJsonDocument>
#include <QJsonObject>

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "blockchain/transaction.h"
#include "network/network_manager.h"
#include "utils/exc_utils.h"

TokenManager::TokenManager(ExtraChainNode *node)
    : node(node)
    , QObject(node) {
    initializeTokenArray();
    DbConnector db(Token::DB_TOKENS_PATH);
    bool        isDbOpened = db.open();

    if (isDbOpened)
        db.create_table(Token::TOKEN_TABLE_CREATE);
}

bool TokenManager::isContract(const QString &pathFile) {
    QFile file(pathFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        eLog("Can not open file {}", pathFile);
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument   jsonDoc = QJsonDocument::fromJson(fileData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return false;
    }

    const bool result = (jsonDoc.isObject() || jsonDoc.isArray());
    return result;
}

bool TokenManager::isValidName(const std::string &name) {
    if (name.size() < 3 || name.size() > 20) {
        return false;
    }

    return std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return std::isalnum(c) || c == ' ' || c == '-' || c == '_';
    });
}

bool TokenManager::isValidTicker(const std::string &ticker) {
    if (ticker.size() < 2 || ticker.size() > 5) {
        return false;
    }

    if (!std::isalpha(ticker[0])) {
        return false;
    }

    return std::all_of(ticker.begin(), ticker.end(), [](unsigned char c) {
        return std::isupper(c) || std::isdigit(c);
    });
}

QMap<QString, QString> TokenManager::mapTokens() {
    QMap<QString, QString> map = { { "ExC", ActorId().toQString() },
                                   { "ROCC", "468faf2f1be6504a9a26f7f027f7e43380b0d77d" } };
    DbConnector            db(Token::DB_TOKENS_PATH);
    bool                   isDbOpen = db.open();
    if (!isDbOpen) {
        eWarning("Database {} doesn't opened", db.file());
        return map;
    }
    auto resultSelect = db.select_all(Token::TOKEN_TABLE_NAME);
    for (auto &t : resultSelect) {
        auto name    = t.at("name").c_str();
        auto tokenId = t.at("actorId").c_str();
        map.insert(name, tokenId);
    }

    return map;
}

QMap<QString, QString> TokenManager::mapTokensByTokenId() {
    QMap<QString, QString> map = { { ActorId().toQString(), "ExC" } };
    DbConnector            db(Token::DB_TOKENS_PATH);
    bool                   isDbOpen = db.open();
    if (!isDbOpen) {
        eWarning("Database {} doesn't opened", db.file());
        return map;
    }
    auto resultSelect = db.select_all(Token::TOKEN_TABLE_NAME);
    for (auto &t : resultSelect) {
        auto name    = t.at("name").c_str();
        auto tokenId = t.at("actorId").c_str();
        map.insert(tokenId, QString(name).toUpper());
    }

    return map;
}

void TokenManager::sendInitialTransaction(const ActorId        &owner,
                                          const TokenId        &token,
                                          const BigNumberFloat &amount) {
    Transaction tx;
    tx.setSender(owner);
    tx.setReceiver(owner);
    tx.setAmount(amount);
    tx.setToken(token);
    tx.setType(TransactionType::InitContract);
    emit sendTransactionCreateToken(owner, tx);
    emit added(owner, token);
}

void TokenManager::initializeTokenArray() {
    // QDir directory(contract_profile.c_str());
    // QStringList contractProfilies = directory.entryList(QDir::Files);

    // for (QString &filename : contractProfilies) {
    //     QFile file(
    //         QString("%1/%2%3").arg(Token::folder_tokens.c_str()).arg(contract_profile.c_str()).arg(filename));

    //     if (file.open(QIODevice::ReadOnly)) {
    //         QByteArray data = file.readLine();
    //         auto deserializedDataList = Serialization::deserialize(data.toStdString());
    //         if (deserializedDataList.size() != size_of_data_list) {
    //             eLog("[TokenManager] Error when open file {}, list size !=7", file.fileName());
    //             return;
    //         }
    //         file.close();
    //     }
    // }
}

bool TokenManager::tokenExist(const std::string &nameToken, const std::string &tickerToken) {
    DbConnector db(Token::DB_TOKENS_PATH);
    bool        isDbOpen = db.open();

    if (!isDbOpen) {
        return false;
    }

    auto countRow =
        db.count(Token::TOKEN_TABLE_NAME, fmt::format("name='{}' OR ticker=UPPER('{}')", nameToken, tickerToken));
    eLog("[TokenManager] Name: count row: {}", countRow);
    return countRow > 0;
}

std::expected<TokenData, CreateTokenError> TokenManager::createToken(const std::string &count,
                                                                     const std::string &name,
                                                                     const std::string &ticker,
                                                                     const ActorId     &owner,
                                                                     const std::string &color,
                                                                     const std::string &predefine_token_id) {
    if (!node->network()->isActiveConnectionExists()) {
        eLog("[TokenManager] No connections");
    }

    auto countBn = BigNumberFloat::create(count, NumeralBase::Dec);
    if (!countBn.has_value()) {
        emit errorNameTokenExist("count");
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    if (countBn.value() < 0 || countBn.value() >= Token::MAX_TOKEN_COUNT) {
        eLog(
            "[TokenManager] Error create token. Count: {} | name: {} | ticker: {} | rull address: {} | "
            "color: {}",
            count,
            name,
            ticker,
            owner,
            color);
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    eLog("[TokenManager] Create token. Count: {} | name: {} | ticker: {} | rull address: {} | color: {}",
         count,
         name,
         ticker,
         owner,
         color);

    if (!isValidName(name) || !isValidTicker(ticker)) {
        eLog("[TokenManager] Incorrect name: {} {}", isValidName(name), isValidTicker(ticker));
        eLog("[TokenManager] Incorrect name. Name: {} | ticker: {}", name, ticker);
        emit errorNameTokenExist("name");
        return std::unexpected(CreateTokenError::InvalidName);
    }

    auto upperTokenName = Utils::str_to_upper(name);
    auto tickerSymbol   = Utils::str_to_upper(ticker);
    if (upperTokenName == "EXTRACOIN" || tickerSymbol == "EXC" || tokenExist(name, ticker)) {
        eLog("[TokenManager] Name or ticker exists");
        emit errorNameTokenExist("exists");
        return std::unexpected(CreateTokenError::ExistToken);
    }

    Actor<KeyPrivate> actor;
    if (predefine_token_id.empty())
        actor = node->accountController()->createService();
    else {
        auto temp_actor = actor.fromJson(QByteArray::fromStdString(predefine_token_id));
        actor           = node->accountController()->createService({}, temp_actor);
    }
    QString actorId      = QString(actor.id().toQString());
    QString jsonFilePath = QString("tmp/%1.json").arg(name.c_str());

    auto tokenData = TokenData { .token  = actor.id().to_string(),
                                 .owner  = owner.to_string(),
                                 .count  = count,
                                 .name   = name,
                                 .ticker = ticker,
                                 .color  = color,
                                 .smart  = "" };

    DbConnector db(Token::DB_TOKENS_PATH);
    bool        isDbOpen = db.open();
    if (isDbOpen) {
        DbRow rowRow = tokenData.toDBRow();

        const bool inserted = db.insert(Token::TOKEN_TABLE_NAME, rowRow);
        eLog("Inserted token into db: {}", (inserted ? "success" : "failed"));
    }

    QFile fileSaveJson(jsonFilePath);
    if (fileSaveJson.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream   stream(&fileSaveJson);
        QJsonDocument jsonDoc = tokenData.toJsonDocument();
        stream << jsonDoc.toJson();
        fileSaveJson.close();

        emit sendToken(actor.id(), jsonFilePath);
    } else {
        eLog("[TokenManager] Error save json into file. File {} not open", fileSaveJson.fileName());
    }

    sendInitialTransaction(owner, TokenId(tokenData.token), BigNumberFloat(count, NumeralBase::Dec));
    return tokenData;
}

void TokenManager::checkIsContract(const QString &pathToFile) {
    if (isContract(pathToFile)) {
        QFile file(pathToFile);
        file.open(QIODevice::ReadOnly);
        QByteArray fileData = file.readAll();
        file.close();

        QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData);
        if (jsonDoc.isObject()) {
            QJsonObject jsonObj         = jsonDoc.object();
            auto        name            = jsonObj[Token::Fields::name.c_str()].toString().toStdString();
            auto        ticker          = jsonObj[Token::Fields::ticker.c_str()].toString().toStdString();
            bool        checkTokenExist = tokenExist(name, ticker);

            if (!checkJsonObjectHasTokenFields(jsonObj) || checkTokenExist)
                return;

            DbRow rowRow;
            rowRow.insert({ "actorId", jsonObj[Token::Fields::actorId.c_str()].toString().toStdString() }); // TODO
            rowRow.insert({ "name", jsonObj[Token::Fields::name.c_str()].toString().toStdString() });
            rowRow.insert({ "ticker", jsonObj[Token::Fields::ticker.c_str()].toString().toStdString() });
            rowRow.insert(
                { "count", QString::number(jsonObj[Token::Fields::count.c_str()].toInt()).toStdString() });
            rowRow.insert({ "owner", jsonObj[Token::Fields::owner.c_str()].toString().toStdString() });
            rowRow.insert({ "color", jsonObj[Token::Fields::color.c_str()].toString().toStdString() });
            rowRow.insert({ "smart", std::string() });

            const bool resultTokenExist = tokenExist(name, ticker);
            if (!resultTokenExist) {
                DbConnector db(Token::DB_TOKENS_PATH);
                bool        isDbOpen = db.open();
                if (isDbOpen) {
                    const bool inserted = db.insert(Token::TOKEN_TABLE_NAME, rowRow);
                    eLog("Inserted token into db: {}", (inserted ? "success" : "failed"));
                    emit newToken();
                }
            }
        }
    }
}

bool TokenManager::checkJsonObjectHasTokenFields(const QJsonObject &jsonObj) {
    for (const std::string &field : Token::Fields::fields) {
        if (!jsonObj.contains(field.c_str())) {
            eWarning("JSON contract doesn't has this field: {}", field);
            return false;
        }
    }
    return true;
}

QJsonDocument TokenData::toJsonDocument() {
    QJsonObject jsonObj;
    jsonObj[Token::Fields::actorId.c_str()] = token.c_str();
    jsonObj[Token::Fields::owner.c_str()]   = owner.c_str();
    jsonObj[Token::Fields::count.c_str()]   = std::stoi(count);
    jsonObj[Token::Fields::name.c_str()]    = name.c_str();
    jsonObj[Token::Fields::ticker.c_str()]  = ticker.c_str();
    jsonObj[Token::Fields::color.c_str()]   = color.c_str();
    jsonObj[Token::Fields::smart.c_str()]   = smart.c_str();
    QJsonDocument jsonDoc(jsonObj);
    return jsonDoc;
}

DbRow TokenData::toDBRow() {
    DbRow dbRow;
    dbRow.insert({ "actorId", token });
    dbRow.insert({ "name", name });
    dbRow.insert({ "ticker", ticker });
    dbRow.insert({ "count", count });
    dbRow.insert({ "owner", owner });
    dbRow.insert({ "color", color });
    dbRow.insert({ "smart", smart });
    return dbRow;
}
