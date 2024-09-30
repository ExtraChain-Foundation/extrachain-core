#include "managers/create_token_manager.h"
#include <QString>
#include "utils/exc_utils.h"

CreateTokenManager::CreateTokenManager(ActorIndex *actorIndex, QObject *parent) : QObject(parent), actorIndex(actorIndex)
{
    initializeTokenArray();
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();

    bool isDbCreate = db.createTable(Token::tokenTableCreate);
}

bool CreateTokenManager::savePrivateActor(Actor<KeyPrivate> actor)
{
    qDebug() << "Attempting to save Private Actor" << actor.id();

    QString fileName = KeyStore::makeKeyFileName(actor.id().toByteArray());
    QString path(QString("%1/%2").arg(Token::folder_tokens.c_str()).arg(fileName));
    qDebug() << "Path=" << path;
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
            qDebug() << "actor serial: ---- " << serializedActor;
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

void CreateTokenManager::sendInitialTransaction(const std::shared_ptr<Actor<KeyPrivate>> sender, ActorId receiver, std::string quantity)
{
    Transaction tx(sender->id(), receiver, BigNumberFloat(quantity, NumeralBase::Dec));
    tx.setData("InitContract");

    tx.setToken(sender->id());
    tx.sign(sender);

    emit sendTransactionCreateToken(tx);
    emit added();
}

std::shared_ptr<Actor<KeyPrivate>> CreateTokenManager::createContract()
{
    std::shared_ptr<Actor<KeyPrivate>> actor = std::make_shared<Actor<KeyPrivate>>();

    actor->create(ActorType::Service);

    emit verifyActor(actor->convertToPublic());
    actorIndex->addActor(actor->convertToPublic());
    savePrivateActor(*actor);
    return actor;
}

void CreateTokenManager::initializeTokenArray()
{
    QDir directory(contract_profile.c_str());
    QStringList contractProfilies = directory.entryList(QDir::Files);

    for (QString &filename : contractProfilies) {
        QFile file(QString("%1/%2%3").arg(Token::folder_tokens.c_str()).arg(contract_profile.c_str()).arg(filename));

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readLine();
            auto deserializedDataList = Serialization::deserialize(data.toStdString());
            if (deserializedDataList.size() != size_of_data_list) {
                qDebug() << "[CreateTokenManager] Error when open file " << file.fileName() << " list size !=7";
                return;
            }

            tokenBalance[deserializedDataList.at(6)] = { { deserializedDataList.at(4), deserializedDataList.at(5) } };

            file.close();
        }
    }
}

bool CreateTokenManager::tokenExist(const std::string& nameToken) {
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();
    if(isDbOpen) {
        if(db.count(Token::tokenTableName) == 0) {
            qDebug() << "[nameToken] Count is zero.";
            return false;
        }
        auto countRow = db.count(Token::tokenTableName, fmt::format("name='{}'", nameToken));
        qDebug() << "[nameToken] Count row:" << countRow;
        return countRow > 0;
    }
    return false;
}

bool CreateTokenManager::tokenSymbolExist(const std::string &symbolToken)
{
    DBConnector db(Token::db_tokens_path);
    bool isDbOpen = db.open();
    if(isDbOpen) {
        auto countRow = db.count(Token::tokenTableName, fmt::format("symbol='{}'", symbolToken));
        qDebug() << "[symbolToken]Count row:" << countRow;
        return countRow > 0;
    }
    return false;
}

void CreateTokenManager::createToken(const std::string &tokenCount, const std::string &tokenName, const std::string &symbol, const std::string &relAddress, const std::string &color)
{
    if(tokenExist(tokenName)) {
        qDebug() << "token name exist";
        emit errorNameTokenExist(QString::fromStdString(tokenName));
        return;
    }

    if(tokenSymbolExist(symbol)) {
        qDebug() << "token symbol exist";
        emit errorSymbolTokenExist(QString::fromStdString(symbol));
        return;
    }

    tokenBalance[relAddress] = { { tokenName, tokenCount } };
    auto actor = createContract();

    std::list<std::string> profileList;
    profileList.clear();
    profileList.push_back("6");
    profileList.push_back("1");
    profileList.push_back(actor->id().toStdString());
    profileList.push_back(tokenName);
    profileList.push_back(tokenCount);
    profileList.push_back(relAddress);
    profileList.push_back(color);
    std::vector<std::string> convertedList(profileList.size());
    std::copy(profileList.begin(), profileList.end(), convertedList.begin());
    std::list<std::string>::iterator it = profileList.begin();
    std::advance(it, 2);
    profileList.insert(it, actor->key().sign(Serialization::serialize(convertedList)));

    QString contractProfile = QString::fromStdString(contract_profile);
    QString actorId = QString(actor->id().toString());
    QString keyStoreFormat = QString::fromStdString(KeyStore::format);

    QString filePath = QString("%1/%2%3").arg(contractProfile).arg(actorId).arg(keyStoreFormat);
    QString jsonFilePath = QString("%1/%2.json").arg(contractProfile).arg(tokenName.c_str()) ;
    QFile file(filePath);
    if (file.exists()) {
        qDebug() << "[CreateTokenManager] Error. Contract profile already exist";
        return;
    }
    if (file.open(QIODevice::WriteOnly)) {
        std::vector<std::string> convertedList(profileList.size());
        std::copy(profileList.begin(), profileList.end(), convertedList.begin());

        file.write(QByteArray::fromStdString(Serialization::serialize(convertedList)));
        file.close();

        DBConnector db(Token::db_tokens_path);
        bool isDbOpen = db.open();
        if(isDbOpen) {
            DBRow rowRow;
            rowRow.insert({ "name", tokenName });
            rowRow.insert({ "symbol", symbol });
            rowRow.insert({ "count_coins", tokenCount });
            const bool inserted = db.insert(Token::tokenTableName, rowRow);
            qDebug() << "Inserted token into db - " << inserted << ".";
        }


        QJsonObject jsonObj;
        jsonObj["owner"] = actorId;
        jsonObj["count"] = std::stoi(tokenCount);
        jsonObj["name"] = tokenName.c_str();
        jsonObj["symbol"] = symbol.c_str();
        jsonObj["color"] = color.c_str();
        jsonObj["url"] = "";
        QJsonDocument jsonDoc(jsonObj);

        qDebug() << __FUNCTION__ << jsonFilePath;
        QFile fileSaveJson(jsonFilePath);
        if (fileSaveJson.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&fileSaveJson);
            stream << jsonDoc.toJson();
            fileSaveJson.close();
        } else {
            qDebug() << "[CreateTokenManager] Error save json into file. File " << fileSaveJson.fileName() << " not open.";
        }
    } else {
        qDebug() << "[CreateTokenManager] Error. File " << file.fileName() << " not open.";
        return;
    }

    sendInitialTransaction(actor, ActorId(relAddress), tokenCount);
}
