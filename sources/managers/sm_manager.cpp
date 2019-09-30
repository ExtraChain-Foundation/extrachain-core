#include "managers/sm_manager.h"
#ifdef ETALONIUM_CLIENT
#include "ui/wallet/walletcontroller.h"
#endif

SmartContractManager::SmartContractManager(ActorIndex *actorIndex, QObject *parent)
    : QObject(parent)
{
    this->actorIndex = actorIndex;
    initializeTokenArray();
}

void SmartContractManager::createContractProfile(QByteArray tokenCount, QByteArray tokenName,
                                                 QByteArray relAddress, QByteArray color)
{
    tokenBalance[relAddress] = { { tokenName, tokenCount } };
    FileSystem::createFolderIfNotExist(SmartContractStorage::CONTRACTPROFILE);
    Actor<KeyPrivate> *actor = createContract(tokenName);

    QByteArrayList profileList;
    profileList.clear();
    profileList.append("6");
    profileList.append("1");
    profileList.append(actor->getId().toByteArray());
    profileList.append(tokenName);
    profileList.append(tokenCount);
    profileList.append(relAddress);
    profileList.append(color);
    actorIndex->saveProfile(actor, profileList);
    profileList.insert(2, actor->getKey()->sign(Serialization::universalSerialize(profileList, 4)));

    QFile file(SmartContractStorage::CONTRACTPROFILE + actor->getId().toString() + ".profile");
    if (file.exists())
    {
        qDebug() << "[SmartContractManager][createContractProfile] Error. Contract profile already exist";
        return;
    }
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(Serialization::universalSerialize(profileList, 4));
        file.close();
    }
    else
    {
        qDebug() << "[SmartContractManager][createContractProfile] Error. File " << file.fileName()
                 << " not open";
        return;
    }

    sendTransaction(actor, relAddress, tokenCount);
}

void SmartContractManager::process()
{
}

void SmartContractManager::sendTransaction(Actor<KeyPrivate> *sender, QByteArray receiver,
                                           QByteArray quantity)
{
#ifdef ETALONIUM_CLIENT
    Transaction tx(sender->getId(), receiver, WalletController::toRealBigNumber(quantity));
    tx.setData("genesis");
    tx.setSenderBalance(WalletController::toRealBigNumber(quantity));

    tx.setToken(sender->getId());
    tx.sign(*sender);

    emit sendTransactionCreateContract(tx);
#else
    Q_UNUSED(sender)
    Q_UNUSED(receiver)
    Q_UNUSED(quantity)
#endif
}

Actor<KeyPrivate> *SmartContractManager::createContract(QByteArray tokenName)
{
    Actor<KeyPrivate> *actor = new Actor<KeyPrivate>();
    BigNumber lsid = actorIndex->getLastSavedId();

    actor->initNew(actorIndex->getLastSavedId() == 0 ? 1 : actorIndex->getLastSavedId() + 1, false);

    emit verifyActor(actor->convertToPublic());
    // QFile *file = new QFile(SmartContractStorage::CONTRACTSTORE);
    // BigNumber index("-1");
    // do
    // {
    //    index++;
    // file->setFileName(SmartContractStorage::CONTRACTSTORE + actor->getId().toByteArray());
    // } while (file->exists());
    // file->open(QIODevice::WriteOnly);
    // QByteArray str = "";
    // str += Serialization::universalSerialize(
    //    { actor->getId().toByteArray(), actor->getKey()->getPublicKey() }, 4);
    // file->write(str);
    // file->flush();
    // file->close();
    // qDebug() << "tokenName" << tokenName << "actor->getId()" << actor->getId();
    // qDebug() << "tokenId[actor->getId().toString()]" << tokenId[actor->getId().toString()];
    emit addContractActorInActorIndex(actor->convertToPublic());
    emit saveActorInPrivateProfile(actor->getId().toByteArray());
    //    actorIndex->addActor(actor->convertToPublic());

    savePrivateActor(*actor);
    // return actor->getId().toByteArray();

    return actor;
}
void SmartContractManager::savePrivateActor(Actor<KeyPrivate> actor)
{
    qDebug() << "Attempting to save Private Actor" << actor.getId();

    QString fileName = KeyStore::makeKeyFileName(actor.getId().toString());
    QString path = SmartContractStorage::CONTRACTSTORE + fileName;
    qDebug() << "Path=" << path;
    QFile *file = new QFile(path);

    // move to another place
    FileSystem::createFolderIfNotExist(SmartContractStorage::CONTRACTSTORE);

    if (file->open(QIODevice::ReadWrite))
    {
        QByteArray old;
        QDataStream read(file);
        read >> old;
        if (old == actor.serialize())
        {
            qDebug() << "Private actor with id =" << actor.getId() << "already exists";
        }
        else
        {
            QDataStream stream(file);
            qDebug() << "actor serial: ---- " << actor.serialize();
            stream << actor.serialize();
            file->flush();
            //            this->accounts << &actor;
            qDebug() << "Private Actor" << actor.getId() << "is successfully saved";
        }
        file->close();
        delete file;
        //        loadActors();
        return;
    }

    qDebug() << "Can't save actor" << actor.getId();
}
void SmartContractManager::initializeTokenArray()
{
    QDir directory(SmartContractStorage::CONTRACTPROFILE);
    QStringList contractProfilies = directory.entryList(QDir::Files);

    for (QString &filename : contractProfilies)
    {
        QFile file(SmartContractStorage::CONTRACTPROFILE + filename);

        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray data = file.readLine();
            QList<QByteArray> list = Serialization::universalDesirialize(data, 4);
            if (list.size() != 7)
            {
                qDebug() << "[smm_manager][initializeTokenArray] Error when open file " << file.fileName()
                         << " list size !=7";
                return;
            }
            tokenBalance[list.at(6)] = { { list.at(4), list.at(5) } };

            file.close();
        }
    }
}

// rename to .key
