#include "managers/token_manager.h"

#include <QString>

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "datastorage/transaction.h"
#include "enc/key_private.h"
#include "network/network_manager.h"
#include "utils/exc_utils.h"

TokenManager::TokenManager(ExtraChainNode *node)
    : node(node)
    , QObject(node) {
    initializeTokenArray();
    DbConnector db(Token::db_tokens_path);
    bool        isDbOpened = db.open();

    if (isDbOpened)
        db.createTable(Token::tokenTableCreate);
}

bool TokenManager::isContract(const QString &pathFile) {
    QFile file(pathFile);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qDebug() << "Can not open file " << pathFile << ".";
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
    QMap<QString, QString> map = { { ActorId().toQString(), "ExC" } };
    DbConnector            db(Token::db_tokens_path);
    bool                   isDbOpen = db.open();
    if (!isDbOpen) {
        qWarning() << "Database doesn't opened.";
        return map;
    }
    auto resultSelect = db.selectAll(Token::tokenTableName);
    for (auto &t : resultSelect) {
        auto tokenId = t.at("actorId").c_str();
        auto ticker  = t.at("ticker").c_str();
        map.insert(tokenId, ticker);
    }

    return map;
}

void TokenManager::sendInitialTransaction(
    const ActorId        &owner,
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
    //             qDebug() << "[TokenManager] Error when open file " << file.fileName() << " list size !=7";
    //             return;
    //         }
    //         file.close();
    //     }
    // }
}

bool TokenManager::tokenExist(const std::string &nameToken, const std::string &tickerToken) {
    DbConnector db(Token::db_tokens_path);
    bool        isDbOpen = db.open();

    if (!isDbOpen) {
        return false;
    }

    auto countRow = db.count(
        Token::tokenTableName,
        fmt::format("name='{}' OR ticker=UPPER('{}')", nameToken, tickerToken));
    qDebug() << "[TokenManager] Name: count row:" << countRow;
    return countRow > 0;
}

std::expected<TokenData, CreateTokenError> TokenManager::createToken(
    const std::string &count,
    const std::string &name,
    const std::string &ticker,
    const ActorId     &owner,
    const std::string &color) {
    if (!node->network()->isActiveConnectionExists()) {
        qDebug() << "[TokenManager] No connections";
    }

    auto countBn = BigNumberFloat::create(count, NumeralBase::Dec);
    if (!countBn.has_value()) {
        emit errorNameTokenExist("count");
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    if (countBn.value() < 0 || countBn.value() >= Token::MAX_TOKEN_COUNT) {
        qDebug() << "[TokenManager] Error create token. Count:" << count << "| name:" << name
                 << "| ticker:" << ticker << "| rull address:" << owner << "| color:" << color;
        return std::unexpected(CreateTokenError::InvalidAmount);
    }

    qDebug() << "[TokenManager] Create token. Count:" << count << "| name:" << name << "| ticker:" << ticker
             << "| rull address:" << owner << "| color:" << color;

    if (!isValidName(name) || !isValidTicker(ticker)) {
        qDebug() << "[TokenManager] Incorrecnt name:" << isValidName(name) << isValidTicker(ticker);
        qDebug() << "[TokenManager] Incorrecnt name. Name:" << name << "| ticker:" << ticker;
        emit errorNameTokenExist("name");
        return std::unexpected(CreateTokenError::InvalidName);
    }

    auto upperTokenName = Utils::str_to_upper(name);
    auto tickerSymbol   = Utils::str_to_upper(ticker);
    if (upperTokenName == "EXTRACOIN" || tickerSymbol == "EXC" || tokenExist(name, ticker)) {
        qDebug() << "[TokenManager] Name or ticker exists";
        emit errorNameTokenExist("exists");
        return std::unexpected(CreateTokenError::ExistToken);
    }

    auto    actor        = node->accountController()->createService();
    QString actorId      = QString(actor.id().toQString());
    QString jsonFilePath = QString("tmp/%1.json").arg(name.c_str());

    auto tokenData = TokenData { .token  = actor.id().toString(),
                                 .owner  = owner.toString(),
                                 .count  = count,
                                 .name   = name,
                                 .ticker = ticker,
                                 .color  = color,
                                 .smart  = "" };

    DbConnector db(Token::db_tokens_path);
    bool        isDbOpen = db.open();
    if (isDbOpen) {
        DbRow rowRow = tokenData.toDBRow();

        const bool inserted = db.insert(Token::tokenTableName, rowRow);
        qDebug() << "Inserted token into db:" << (inserted ? "success" : "failed") << ".";
    }

    QFile fileSaveJson(jsonFilePath);
    if (fileSaveJson.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream   stream(&fileSaveJson);
        QJsonDocument jsonDoc = tokenData.toJsonDocument();
        stream << jsonDoc.toJson();
        fileSaveJson.close();

        emit sendToken(actor.id(), jsonFilePath);
    } else {
        qDebug() << "[TokenManager] Error save json into file. File" << fileSaveJson.fileName()
                 << " not open.";
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
            rowRow.insert(
                { "actorId", jsonObj[Token::Fields::actorId.c_str()].toString().toStdString() }); // TODO
            rowRow.insert({ "name", jsonObj[Token::Fields::name.c_str()].toString().toStdString() });
            rowRow.insert({ "ticker", jsonObj[Token::Fields::ticker.c_str()].toString().toStdString() });
            rowRow.insert(
                { "count", QString::number(jsonObj[Token::Fields::count.c_str()].toInt()).toStdString() });
            rowRow.insert({ "owner", jsonObj[Token::Fields::owner.c_str()].toString().toStdString() });
            rowRow.insert({ "color", jsonObj[Token::Fields::color.c_str()].toString().toStdString() });
            rowRow.insert({ "smart", std::string() });

            const bool resultTokenExist = tokenExist(name, ticker);
            if (!resultTokenExist) {
                DbConnector db(Token::db_tokens_path);
                bool        isDbOpen = db.open();
                if (isDbOpen) {
                    const bool inserted = db.insert(Token::tokenTableName, rowRow);
                    qDebug() << "Inserted token into db - " << (inserted ? "success" : "failed") << ".";
                    emit newToken();
                }
            }
        }
    }
}

bool TokenManager::checkJsonObjectHasTokenFields(const QJsonObject &jsonObj) {
    for (const std::string &field : Token::Fields::fields) {
        if (!jsonObj.contains(field.c_str())) {
            qWarning() << "JSON contract doesn't has this field:" << field;
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
