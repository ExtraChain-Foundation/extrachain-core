#include "managers/create_token_manager.h"
#include "utils/exc_utils.h"
#include <QString>

CreateTokenManager::CreateTokenManager(ActorIndex *actorIndex, QObject *parent)
    : QObject(parent), actorIndex(actorIndex) {
    initializeTokenArray();
    DBConnector db(Token::db_tokens_path);
    bool isDbOpened = db.open();

    if (isDbOpened)
        db.createTable(Token::tokenTableCreate);
}

bool CreateTokenManager::isContract(const QString &pathFile) {
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

bool CreateTokenManager::savePrivateActor(Actor<KeyPrivate> actor) {
    qDebug() << "Attempting to save Private Actor" << actor.id();

    QString fileName = KeyStore::makeKeyFileName(actor.id().toByteArray());
    QString path(
        QString("%1/%2").arg(Token::folder_tokens.c_str()).arg(fileName));
    qDebug() << "Path save actor " << path << ".";
    QFile file(path);

    if (file.open(QIODevice::ReadWrite)) {
        std::string old = file.readAll().toStdString();
        auto serializedActor = MessagePack::serialize<Actor<KeyPrivate>>(actor);
        if (old == serializedActor) {
            qDebug() << "Private actor with id =" << actor.id() << "already exists";
            file.close();
            return false;
        } else {
            file.resize(0);
            file.write(QByteArray::fromStdString(serializedActor));
            file.flush();
            qDebug() << "Private Actor" << actor.id() << "is successfully saved";
        }
        file.close();
        return true;
    }

    qDebug() << "Can't save actor" << actor.id();
    return false;
}

void CreateTokenManager::sendInitialTransaction(
    const std::shared_ptr<Actor<KeyPrivate>> sender, ActorId receiver,
    std::string quantity) {
    Transaction tx(sender->id(), receiver,
                   BigNumberFloat(quantity, NumeralBase::Dec));
    tx.setData("InitContract");

    tx.setToken(sender->id());
    tx.sign(sender);

    emit sendTransactionCreateToken(tx);
    emit added();
}

std::shared_ptr<Actor<KeyPrivate>> CreateTokenManager::createContract() {
    std::shared_ptr<Actor<KeyPrivate>> actor =
        std::make_shared<Actor<KeyPrivate>>();

    actor->create(ActorType::Service);

    emit verifyActor(actor->convertToPublic());
    actorIndex->addActor(actor->convertToPublic());
    savePrivateActor(*actor);
    return actor;
}

void CreateTokenManager::initializeTokenArray() {
    QDir directory(contract_profile.c_str());
    QStringList contractProfilies = directory.entryList(QDir::Files);

    for (QString &filename : contractProfilies) {
        QFile file(QString("%1/%2%3")
                       .arg(Token::folder_tokens.c_str())
                       .arg(contract_profile.c_str())
                       .arg(filename));

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readLine();
            auto deserializedDataList =
                Serialization::deserialize(data.toStdString());
            if (deserializedDataList.size() != size_of_data_list) {
                qDebug() << "[CreateTokenManager] Error when open file "
                         << file.fileName() << " list size !=7";
                return;
            }

            tokenBalance[deserializedDataList.at(6)] = {
                                                         {deserializedDataList.at(4), deserializedDataList.at(5)}};

            file.close();
        }
    }
}

bool CreateTokenManager::tokenExist(const std::string &nameToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();
    if (isDbOpen) {
        if (db.count(Token::tokenTableName) == 0) {
            qDebug() << "[nameToken] Count is zero.";
            return false;
        }
        auto countRow =
            db.count(Token::tokenTableName, fmt::format("name='{}'", nameToken));
        qDebug() << "[nameToken] Count row:" << countRow;
        return countRow > 0;
    }
    return false;
}

bool CreateTokenManager::tokenSymbolExist(const std::string &symbolToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();
    if (isDbOpen) {
        auto countRow = db.count(Token::tokenTableName,
                                 fmt::format("symbol='{}'", symbolToken));
        qDebug() << "[symbolToken]Count row:" << countRow;
        return countRow > 0;
    }
    return false;
}

void CreateTokenManager::createToken(const std::string &tokenCount,
                                     const std::string &tokenName,
                                     const std::string &symbol,
                                     const std::string &relAddress,
                                     const std::string &color) {
    auto upperTokenName = QString::fromStdString(tokenName).toUpper();

    if (tokenExist(tokenName) || upperTokenName == "RACCOON" ||
        upperTokenName == "EXTRACOIN") {
        qDebug() << "token name exist";
        emit errorNameTokenExist(QString::fromStdString(tokenName));
        return;
    }

    auto upperSymbol = QString::fromStdString(symbol).toUpper();
    if (tokenSymbolExist(symbol) || upperSymbol == "ROCC" ||
        upperSymbol == "EXC") {
        qDebug() << "token symbol exist";
        emit errorSymbolTokenExist(QString::fromStdString(symbol));
        return;
    }

    tokenBalance[relAddress] = {{tokenName, tokenCount}};
    auto actor = createContract();

    QString contractProfile = QString::fromStdString(contract_profile);
    QString actorId = QString(actor->id().toString());

    QString jsonFilePath =
        QString("%1/%2.json").arg(contractProfile).arg(tokenName.c_str());

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
        qDebug() << "Inserted token into db - " << (inserted ? "success" : "failed")
                 << ".";
    }

    QFile fileSaveJson(jsonFilePath);
    if (fileSaveJson.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream stream(&fileSaveJson);
        QJsonDocument jsonDoc = tokenData.toJsonDocument();
        stream << jsonDoc.toJson();
        fileSaveJson.close();

        emit sendContract(jsonFilePath);
    } else {
        qDebug() << "[CreateTokenManager] Error save json into file. File "
                 << fileSaveJson.fileName() << " not open.";
    }

    sendInitialTransaction(actor, ActorId(relAddress), tokenCount);
}

void CreateTokenManager::checkIsContract(const QString &pathToFile) {
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
            rowRow.insert(
                {"name",
                  jsonObj[Token::Fields::name.c_str()].toString().toStdString()});
            rowRow.insert(
                {"symbol",
                  jsonObj[Token::Fields::symbol.c_str()].toString().toStdString()});
            rowRow.insert(
                {"count_coins",
                  QString::number(jsonObj[Token::Fields::count.c_str()].toInt())
                      .toStdString()});
            rowRow.insert(
                {"owner",
                  jsonObj[Token::Fields::owner.c_str()].toString().toStdString()});
            rowRow.insert(
                {"color",
                  jsonObj[Token::Fields::color.c_str()].toString().toStdString()});
            rowRow.insert({"url", std::string()});

            DBConnector db(Token::db_tokens_path);
            bool isDbOpen = db.open();
            if (isDbOpen) {
                const bool inserted = db.insert(Token::tokenTableName, rowRow);
                qDebug() << "Inserted token into db - "
                         << (inserted ? "success" : "failed") << ".";
            }
        }
    }
}

bool CreateTokenManager::checkJsonObjectHasTokenFields(
    const QJsonObject &jsonObj) {
    for (const std::string &field : Token::Fields::fields) {
        if (!jsonObj.contains(field.c_str())) {
            qWarning() << "JSON contract doesn't has this field:" << field;
            return false;
        }
    }
    return true;
}
