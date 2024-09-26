#include "managers/create_token_manager.h"
#include <QString>
#include "utils/exc_utils.h"

CreateTokenManager::CreateTokenManager(ActorIndex *actorIndex, QObject *parent) : QObject(parent), actorIndex(actorIndex)
{
    initializeTokenArray();
}

bool CreateTokenManager::savePrivateActor(Actor<KeyPrivate> actor)
{
    qDebug() << "Attempting to save Private Actor" << actor.id();

    QString fileName = KeyStore::makeKeyFileName(actor.id().toByteArray());
    QString path(QString("%1/%2").arg(folder_tokens.c_str()).arg(fileName));
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

void CreateTokenManager::sendInitialTransaction(const std::shared_ptr<Actor<KeyPrivate>> sender, ActorId receiver, QByteArray quantity)
{
    Transaction tx(sender->id(), receiver, BigNumberFloat(quantity.toStdString(), NumeralBase::Dec));
    tx.setData("InitContract");

    tx.setToken(sender->id());
    tx.sign(sender);

    emit sendTransactionCreateToken(tx);
}

std::shared_ptr<Actor<KeyPrivate>> CreateTokenManager::createContract(QByteArray tokenName)
{
    std::shared_ptr<Actor<KeyPrivate>> actor = std::make_shared<Actor<KeyPrivate>>();

    actor->create(ActorType::DAppMaster);

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
        QFile file(QString("%1/%2%3").arg(folder_tokens.c_str()).arg(contract_profile.c_str()).arg(filename));

        if (file.open(QIODevice::ReadOnly)) {
            QByteArray data = file.readLine();
            auto deserializedDataList = Serialization::deserialize(data.toStdString());
            if (deserializedDataList.size() != size_of_data_list) {
                qDebug() << "[CreateTokenManager] Error when open file " << file.fileName() << " list size !=7";
                return;
            }
            QList<QByteArray> list;
            for(auto& value : deserializedDataList) {
                list.push_back(QByteArray::fromStdString(value));
            }

            tokenBalance[list.at(6)] = { { list.at(4), list.at(5) } };

            file.close();
        }
    }
}

void CreateTokenManager::createToken(QByteArray tokenCount, QByteArray tokenName, QByteArray relAddress, QByteArray color)
{
    tokenBalance[relAddress] = { { tokenName, tokenCount } };
    auto actor = createContract(tokenName);

    QByteArrayList profileList;
    profileList.clear();
    profileList.append("6");
    profileList.append("1");
    profileList.append(actor->id().toByteArray());
    profileList.append(tokenName);
    profileList.append(tokenCount);
    profileList.append(relAddress);
    profileList.append(color);
    std::vector<std::string> convertedList;
    for(auto &value : profileList) {
        convertedList.push_back(value.toStdString());
    }
    profileList.insert(2, QByteArray::fromStdString(actor->key().sign(Serialization::serialize(convertedList))));

    QString folderPath = QString::fromStdString(folder_tokens);
    QString contractProfile = QString::fromStdString(contract_profile);
    QString actorId = QString(actor->id().toByteArray());
    QString keyStoreFormat = QString::fromStdString(KeyStore::format);

    QString filePath = QString("%1/%2%3").arg(contractProfile).arg(actorId).arg(keyStoreFormat);
    QFile file(filePath);    if (file.exists()) {
        qDebug() << "[CreateTokenManager] Error. Contract profile already exist";
        return;
    }
    if (file.open(QIODevice::WriteOnly)) {
        std::vector<std::string> convertedList;
        for(auto &value : profileList) {
            convertedList.push_back(value.toStdString());
        }

        file.write(QByteArray::fromStdString(Serialization::serialize(convertedList)));
        file.close();
    } else {
        qDebug() << "[CreateTokenManager] Error. File " << file.fileName() << " not open";
        return;
    }

    sendInitialTransaction(actor, ActorId(relAddress.toStdString()), tokenCount);
}
