#include "managers/token_manager.h"

#include <QString>

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "datastorage/transaction.h"
#include "enc/key_private.h"
#include "utils/exc_utils.h"

TokenManager::TokenManager(ExtraChainNode &node, QObject *parent)
    : node(node)
    , QObject(parent) {
    initializeTokenArray();
    DBConnector db(Token::db_tokens_path);
    bool isDbOpened = db.open();

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
    QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        return false;
    }

    const bool result = (jsonDoc.isObject() || jsonDoc.isArray());
    return result;
}

void TokenManager::sendInitialTransaction(
    const std::shared_ptr<Actor<KeyPrivate>> sender,
    ActorId receiver,
    std::string quantity) {
    Transaction tx(sender->id(), receiver, BigNumberFloat(quantity, NumeralBase::Dec));
    tx.setData("InitContract");
    tx.setToken(sender->id());
    tx.sign(sender);

           // emit sendTransactionCreateToken(tx);
    node.sendTransaction(tx);
    emit added();
}

void TokenManager::initializeTokenArray() {
    QDir directory(contract_profile.c_str());
    QStringList contractProfilies = directory.entryList(QDir::Files);

    for (QString &filename : contractProfilies) {
        QFile file(
            QString("%1/%2%3").arg(Token::folder_tokens.c_str()).arg(contract_profile.c_str()).arg(filename));

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readLine();
            auto deserializedDataList = Serialization::deserialize(data.toStdString());
            if (deserializedDataList.size() != size_of_data_list) {
                qDebug() << "[TokenManager] Error when open file " << file.fileName() << " list size !=7";
                return;
            }

            tokenBalance[deserializedDataList.at(6)] = { { deserializedDataList.at(4),
                                                           deserializedDataList.at(5) } };

            file.close();
        }
    }
}

bool TokenManager::tokenExist(const std::string &nameToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();

    if (isDbOpen) {
        return false;
    }
    if (db.count(Token::tokenTableName) == 0) {
        qDebug() << "[TokenManager] Name: count is zero.";
        return false;
    }

    auto countRow = db.count(Token::tokenTableName, fmt::format("name='{}'", nameToken));
    qDebug() << "[TokenManager] Name: count row:" << countRow;
    return countRow > 0;
}

bool TokenManager::tokenSymbolExist(const std::string &symbolToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();

    if (isDbOpen) {
        auto countRow = db.count(Token::tokenTableName, fmt::format("symbol='{}'", symbolToken));
        qDebug() << "[TokenManager] Symbol: count row:" << countRow;
        return countRow > 0;
    }

    return false;
}

void TokenManager::createToken(
    const std::string &tokenCount,
    const std::string &tokenName,
    const std::string &symbol,
    const ActorId &rulAddress,
    const std::string &color) {
    auto upperTokenName = QString::fromStdString(tokenName).toUpper();

    if (tokenExist(tokenName) || upperTokenName == "RACCOON" || upperTokenName == "EXTRACOIN") {
        qDebug() << "token name exist";
        emit errorNameTokenExist(QString::fromStdString(tokenName));
        return;
    }

    auto upperSymbol = QString::fromStdString(symbol).toUpper();
    if (tokenSymbolExist(symbol) || upperSymbol == "ROCC" || upperSymbol == "EXC") {
        qDebug() << "token symbol exist";
        emit errorSymbolTokenExist(QString::fromStdString(symbol));
        return;
    }

    tokenBalance[rulAddress] = { { tokenName, tokenCount } };
    auto actor = this->node.accountController()->createService();

    QString contractProfile = QString::fromStdString(contract_profile);
    QString actorId = QString(actor.id().toString());

    QString jsonFilePath = QString("%1/%2.json").arg(contractProfile).arg(tokenName.c_str());

    TokenData tokenData;
    tokenData.name = tokenName;
    tokenData.color = color;
    tokenData.owner = actorId.toStdString();
    tokenData.symbol = symbol;
    tokenData.count = tokenCount;

    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();
    if (isDbOpen) {
        DBRow rowRow = tokenData.toDBRow();

        const bool inserted = db.insert(Token::tokenTableName, rowRow);
        qDebug() << "Inserted token into db:" << (inserted ? "success" : "failed") << ".";
    }

    QFile fileSaveJson(jsonFilePath);
    if (fileSaveJson.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&fileSaveJson);
        QJsonDocument jsonDoc = tokenData.toJsonDocument();
        stream << jsonDoc.toJson();
        fileSaveJson.close();

        emit sendToken(jsonFilePath);
    } else {
        qDebug() << "[TokenManager] Error save json into file. File" << fileSaveJson.fileName()
        << " not open.";
    }

    sendInitialTransaction(std::make_shared<Actor<KeyPrivate>>(actor), ActorId(rulAddress), tokenCount);
}

void TokenManager::checkIsContract(const QString &pathToFile) {
    if (isContract(pathToFile)) {
        QFile file(pathToFile);
        file.open(QIODevice::ReadOnly);
        QByteArray fileData = file.readAll();
        file.close();

        QJsonDocument jsonDoc = QJsonDocument::fromJson(fileData);
        if (jsonDoc.isObject()) {
            QJsonObject jsonObj = jsonDoc.object();
            bool checkTokenExist = tokenExist(jsonObj[Token::Fields::name.c_str()].toString().toStdString());

            if (!checkJsonObjectHasTokenFields(jsonObj) || checkTokenExist)
                return;

            DBRow rowRow;
            rowRow.insert({ "name", jsonObj[Token::Fields::name.c_str()].toString().toStdString() });
            rowRow.insert({ "symbol", jsonObj[Token::Fields::symbol.c_str()].toString().toStdString() });
            rowRow.insert({ "count_coins",
                            QString::number(jsonObj[Token::Fields::count.c_str()].toInt()).toStdString() });
            rowRow.insert({ "owner", jsonObj[Token::Fields::owner.c_str()].toString().toStdString() });
            rowRow.insert({ "color", jsonObj[Token::Fields::color.c_str()].toString().toStdString() });
            rowRow.insert({ "url", std::string() });

            DBConnector db(Token::db_tokens_path);
            bool isDbOpen = db.open();
            if (isDbOpen) {
                const bool inserted = db.insert(Token::tokenTableName, rowRow);
                qDebug() << "Inserted token into db - " << (inserted ? "success" : "failed") << ".";
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
    jsonObj[Token::Fields::owner.c_str()] = owner.c_str();
    jsonObj[Token::Fields::count.c_str()] = std::stoi(count);
    jsonObj[Token::Fields::name.c_str()] = name.c_str();
    jsonObj[Token::Fields::symbol.c_str()] = symbol.c_str();
    jsonObj[Token::Fields::color.c_str()] = color.c_str();
    jsonObj[Token::Fields::url.c_str()] = "";
    QJsonDocument jsonDoc(jsonObj);
    return jsonDoc;
}

DBRow TokenData::toDBRow() {
    DBRow dbRow;
    dbRow.insert({ "name", name });
    dbRow.insert({ "symbol", symbol });
    dbRow.insert({ "count_coins", count });
    dbRow.insert({ "owner", owner });
    dbRow.insert({ "color", color });
    dbRow.insert({ "url", std::string() });
    return dbRow;
}
