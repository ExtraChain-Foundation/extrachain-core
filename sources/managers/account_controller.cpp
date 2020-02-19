#include "managers/account_controller.h"
#include "datastorage/blockchain.h"

QMap<QByteArray, QByteArray> AccountController::getCurrentState() const
{
    return currentState;
}

void AccountController::setCurrentState(const QMap<QByteArray, QByteArray> &value)
{
    currentState = value;
}

QList<Actor<KeyPrivate> *> AccountController::getAccounts() const
{
    return accounts;
}

void AccountController::setAccounts(const QList<Actor<KeyPrivate> *> &value)
{
    accounts = value;
}

ActorIndex *AccountController::getActorIndex() const
{
    return actorIndex;
}

void AccountController::setActorIndex(ActorIndex *value)
{
    actorIndex = value;
}

void AccountController::setBlockchain(Blockchain *value)
{
    blockchain = value;
}

AccountController::AccountController(ActorIndex *actorIndex)
{
    this->actorIndex = actorIndex;
    // when private actor is verified by actor index -> save it locally
    connect(actorIndex, &ActorIndex::PrivateActorIsVerified, this, &AccountController::savePrivateActor);
    //    if (!QFile(KeyStore::user_actor_state).exists())
    //    {
    //        QFile file(KeyStore::user_actor_state);
    //        file.open(QIODevice::WriteOnly);
    //        file.flush();
    //        file.close();
    //    }
    loadActors();
}

QList<QByteArray> AccountController::getAccountID()
{
    QList<QByteArray> list;
    for (int i = 0; i < accounts.size(); i++)
        list.append(accounts[i]->getId().toActorId());
    return list;
}

Actor<KeyPrivate> AccountController::createActor(int account)
{
    Actor<KeyPrivate> *actor = new Actor<KeyPrivate>();
    actor->init(account);

    qDebug() << actor->serialize();

    emit verifyActor(actor->convertToPublic());

    actorIndex->addActor(actor->convertToPublic());
    savePrivateActor(*actor);
    accounts.append(actor);
    if (accounts.size() - 1 == 0)
        emit savePrivateProfile(actor->getId().toActorId());

    userNum = accounts.size() - 1;

    qDebug() << "create actor finished" << account;
    if (account == 1)
    {
        qDebug() << "Dfs hash init for me";
        emit initDfs(); //
    }
    emit newActorIsCreated(this->getMainActor()->getId(), account);

    if (!accounts.isEmpty())
        blockchain->getBlockZero();
    return *actor;
}

Actor<KeyPrivate> AccountController::getActor(BigNumber id)
{
    for (Actor<KeyPrivate> *actor : accounts)
    {
        if (id == actor->getId())
        {
            return *actor;
        }
    }
    qDebug() << "Can't find actor with id:" << id;
    return Actor<KeyPrivate>();
}

Actor<KeyPrivate> AccountController::getActor(QByteArray pubkey)
{
    for (Actor<KeyPrivate> *actor : accounts)
    {
        if (actor->getKey()->extractPublicKey() == pubkey)
        {
            qDebug() << "ACCOUNT CONTROLLER: currentActor: " << actor->getId();
            return *actor;
        }
    }
    qDebug() << "Can't find actor with pubkey:" << QString(pubkey);
    return Actor<KeyPrivate>();
}

Actor<KeyPrivate> AccountController::getActor(int number)
{
    //    return actorIndex->getActor(BigNumber(number));
    if (number >= 0 && !accounts.isEmpty() && number < accounts.size())
    {
        return *(accounts.at(number));
    }
    qDebug() << "Can't find actor with index:" << number;
    return Actor<KeyPrivate>();
}

Actor<KeyPrivate> *AccountController::getMainActor()
{
    // assert(!accounts.isEmpty());
    return accounts.isEmpty() ? nullptr : accounts.first();
}

Actor<KeyPrivate> AccountController::getCurrentActor()
{
    return getActor(this->userNum);
}

void AccountController::loadActors(QByteArray id, QByteArrayList idList)
{
    if (id.isEmpty())
        return;

    accounts.clear();
    qDebug() << "ACCOUNT CONTROLLER : Attempting to load actors from local storage";
    QString path = KeyStore::USER_KEYSTORE;
    int loaded = 0;
    for (const QByteArray &fileName : idList)
    {
        QFile file(path + "/" + fileName + ".key");
        if (file.exists() && file.open(QIODevice::ReadOnly))
        {
            QByteArray serialized;
            serialized = file.readAll();
            qDebug() << serialized;
            file.close();
            if (!serialized.isEmpty())
            {
                Actor<KeyPrivate> *actor = new Actor<KeyPrivate>;

                actor->init(serialized);

                qDebug() << "Actor" << actor->getId() << "found locally -"
                         << actor->getKey()->getPrivateKey();
                this->accounts.append(actor);
                loaded++;
            }
        }
    }

    if (loaded > 0)
    {
        qDebug() << loaded << "accounts have been loaded" << id;
        blockchain->getBlockZero();
        emit loadWallets(id, idList);
    }
    else
    {
        qDebug() << "There no accounts found locally";
    }
}

int AccountController::getAccountCount()
{
    return accounts.size();
}

int AccountController::getUserNum() const
{
    return userNum;
}

void AccountController::setUserNum(int value)
{
    userNum = value;
}

void AccountController::savePrivateActor(Actor<KeyPrivate> actor)
{
    qDebug() << "Attempting to save Private Actor" << actor.getId();
    if (!accounts.isEmpty())
        emit editPrivateProfile(actor.getId().toActorId());
    QString fileName = KeyStore::makeKeyFileName(actor.getId().toActorId());
    QString path = KeyStore::USER_KEYSTORE + fileName;
    qDebug() << "Path=" << path;
    QFile *file = new QFile(path);

    // move to another place
    FileSystem::createFolderIfNotExist(KeyStore::USER_KEYSTORE);

    if (file->open(QIODevice::ReadWrite))
    {
        QByteArray old = file->readAll();
        if (old == actor.serialize())
        {
            qDebug() << "Private actor with id =" << actor.getId() << "already exists";
        }
        else
        {
            qDebug() << "actor serialized: ---- " << actor.serialize();
            file->write(actor.serialize());
            file->flush();
            qDebug() << "Private Actor" << actor.getId() << "is successfully saved";
        }
        file->close();
        delete file;
        return;
    }

    qDebug() << "Can't save actor" << actor.getId();
}

void AccountController::clearAcc()
{
    accounts.clear();
    userNum = 0;
    qDebug() << accounts.size() << " acc after LogOut";
}

//

// void AccountController::regNewUser(bool account) // ~not ready yet
//{
//    Actor<KeyPrivate> keys = createActor(account);
//    qDebug() << "AccountController::regNewUser";
//    emit sentActorId(keys.getId());
//}

void AccountController::changeUserNum(QByteArray wallId)
{
    userNum = 0;
    for (const auto &currAcc : accounts)
    {
        // qDebug() << "ACCOUNT CONTROLLER: change userNum" << wallId;
        if (currAcc->getId().toActorId() == wallId)
        {
            emit updateTransactionListInModel();
            break;
        }
        ++userNum;
    }
}

void AccountController::process()
{
}
