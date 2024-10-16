#include "managers/token_manager.h"

#include <QString>

#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "datastorage/transaction.h"
#include "enc/key_private.h"
#include "network/network_manager.h"
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
    // tx.sign(sender);

    // emit sendTransactionCreateToken(tx);
    node.sendTransaction(tx, sender);
    emit added();
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

    //         tokenBalance[deserializedDataList.at(6)] = { { deserializedDataList.at(4),
    //                                                        deserializedDataList.at(5) } };

    //         file.close();
    //     }
    // }
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

bool TokenManager::tokentTickerExist(const std::string &tickerToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();

    if (isDbOpen) {
        auto countRow = db.count(Token::tokenTableName, fmt::format("ticker='{}'", tickerToken));
        qDebug() << "[TokenManager] Ticker: count row:" << countRow;
        return countRow > 0;
    }

    return false;
}

void TokenManager::createToken(
    const std::string &count,
    const std::string &name,
    const std::string &ticker,
    const ActorId &owner,
    const std::string &color) {
    qDebug() << "[TokenManager] Create token. Count:" << count << "| name:" << name << "| ticker:" << ticker << "| rull address:" << owner << "| color:" << color;

    if (!node.network()->isActiveConnectionExists()) {
        qDebug() << "[TokenManager] No connections";
    }

    auto upperTokenName = QString::fromStdString(name).toUpper();

    if (tokenExist(name) || upperTokenName == "RACCOON" || upperTokenName == "EXTRACOIN") {
        qDebug() << "token name exist";
        emit errorNameTokenExist(QString::fromStdString(name));
        return;
    }

    auto tickerSymbol = QString::fromStdString(ticker).toUpper();
    if (tokentTickerExist(ticker) || tickerSymbol == "ROCC" || tickerSymbol == "EXC") {
        qDebug() << "token ticker exist";
        emit errorTickerTokenExist(QString::fromStdString(ticker));
        return;
    }

    tokenBalance[owner] = { { name, count } };
    auto actor = this->node.accountController()->createService();

    QString actorId = QString(actor.id().toString());

    QString jsonFilePath = QString("tmp/%1.json").arg(name.c_str());

    TokenData tokenData;
    tokenData.actor = actor.id().toStdString();
    tokenData.name = name;
    tokenData.color = color;
    tokenData.owner = actorId.toStdString();
    tokenData.ticker = ticker;
    tokenData.count = count;

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

        emit sendToken(actor.id(), jsonFilePath);
    } else {
        qDebug() << "[TokenManager] Error save json into file. File" << fileSaveJson.fileName()
        << " not open.";
    }

    // add to dfs

    sendInitialTransaction(std::make_shared<Actor<KeyPrivate>>(actor), ActorId(owner), count);
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
            rowRow.insert({ "actorId", "" }); // TODO
            rowRow.insert({ "name", jsonObj[Token::Fields::name.c_str()].toString().toStdString() });
            rowRow.insert({ "ticker", jsonObj[Token::Fields::ticker.c_str()].toString().toStdString() });
            rowRow.insert({ "count",
                            QString::number(jsonObj[Token::Fields::count.c_str()].toInt()).toStdString() });
            rowRow.insert({ "owner", jsonObj[Token::Fields::owner.c_str()].toString().toStdString() });
            rowRow.insert({ "color", jsonObj[Token::Fields::color.c_str()].toString().toStdString() });
            rowRow.insert({ "smart", std::string() });

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
    // jsonObj[Token::Fields::actorId.c_str()] = actor.c_str();
    jsonObj[Token::Fields::owner.c_str()] = owner.c_str();
    jsonObj[Token::Fields::count.c_str()] = std::stoi(count);
    jsonObj[Token::Fields::name.c_str()] = name.c_str();
    jsonObj[Token::Fields::ticker.c_str()] = ticker.c_str();
    jsonObj[Token::Fields::color.c_str()] = color.c_str();
    jsonObj[Token::Fields::smart.c_str()] = smart.c_str();
    QJsonDocument jsonDoc(jsonObj);
    return jsonDoc;
}

DBRow TokenData::toDBRow() {
    DBRow dbRow;
    dbRow.insert({ "actor", actor });
    dbRow.insert({ "name", name });
    dbRow.insert({ "ticker", ticker });
    dbRow.insert({ "count", count });
    dbRow.insert({ "owner", owner });
    dbRow.insert({ "color", color });
    dbRow.insert({ "smart", smart });
    return dbRow;
}
