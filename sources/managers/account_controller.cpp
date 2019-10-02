#include "managers/account_controller.h"

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
        list.append(accounts[i]->getId().toByteArray());
    return list;
}

Actor<KeyPrivate> AccountController::createActor(bool account)
{
    Actor<KeyPrivate> *actor = new Actor<KeyPrivate>();
    // todo: local last saved id can be outdated
    actor->init(account);

    qDebug() << actor->serialize();

    emit verifyActor(actor->convertToPublic());
    QFile file(KeyStore::user_actor_state);
    file.open(QIODevice::WriteOnly | QIODevice::Append);
    QByteArray str = "\n";
    str += actor->getId().toByteArray() + Serialization::TX_PAIR_FIELD_SPLITTER + "0"
        + Serialization::TX_PAIR_FIELD_SPLITTER;
    file.write(str);
    file.flush();
    file.close();
    emit addActorInActorIndex(actor->convertToPublic());
    //    actorIndex->addActor(actor->convertToPublic());
    savePrivateActor(*actor);

    accounts.append(actor);
    if (account)
        emit initDfs();
    emit newActorIsCreated(this->getMainActor()->getId(), account);

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
    //    if (accounts.size() == 0)
    //        return &Actor<KeyPrivate>();
    return accounts.isEmpty() ? nullptr : accounts.first();
}

Actor<KeyPrivate> AccountController::getCurrentActor()
{
    return getActor(this->userNum);
}

void AccountController::loadActors()
{
    accounts.clear();
    qDebug() << "ACCOUNT CONTROLLER : Attempting to load actors from local storage";
    QString path = KeyStore::USER_KEYSTORE;
    QFile file(KeyStore::user_actor_state);
    file.open(QIODevice::ReadOnly);
    while (!file.atEnd())
    {
        QList<QByteArray> list =
            Serialization::deserialize(file.readLine(), Serialization::TX_PAIR_FIELD_SPLITTER);
        if (list.size() == 2)
            this->currentState[list.at(0)] = list.at(1);
    }
    QDir dir(path);
    QStringList filters;
    filters << KeyStore::KEY_FILTER;
    dir.setNameFilters(filters);

    int loaded = 0;

    for (QString fileName : dir.entryList())
    {
        QFile *file = new QFile(path + "/" + fileName);
        if (file->exists() && !file->isOpen())
        {
            if (file->open(QIODevice::ReadOnly))
            {
                QByteArray serialized;
                QDataStream stream(file);
                stream >> serialized;
                qDebug() << serialized;

                file->close();
                delete file;

                Actor<KeyPrivate> *actor = new Actor<KeyPrivate>;
                //                serialized
                //                QByteArray
                //                hasHH=Utils::calcKeccak("model@gmail.com--Pass1234567");

                actor->init(serialized);
                if (serialized.isEmpty())
                    continue;

                QByteArray prKey = actor->getKey()->getPrivateKey();
                //                EllipticPoints somepo(hasHH);
                //                prKey = somepo.CryptMessage(prKey);
                qDebug() << prKey;
                qDebug() << "Actor " << actor->getId() << "found locally - "
                         << actor->getKey()->getPrivateKey();
                this->accounts.append(actor);
                loaded++;
            }
        }
    }

    if (loaded > 0)
    {
        qDebug() << loaded << " accounts have been loaded";
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
    emit editPrivateProfile(actor.getId().toByteArray());
    QString fileName = KeyStore::makeKeyFileName(actor.getId().toString());
    QString path = KeyStore::USER_KEYSTORE + fileName;
    qDebug() << "Path=" << path;
    QFile *file = new QFile(path);

    // move to another place
    FileSystem::createFolderIfNotExist(KeyStore::USER_KEYSTORE);

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

//

void AccountController::regNewUser(bool account) // ~not ready yet
{
    Actor<KeyPrivate> keys = createActor(account);
    qDebug() << "AccountController::regNewUser";
    emit sentActorId(keys.getId());
}

void AccountController::changeUserNum(QByteArray wallId)
{
    userNum = 0;
    for (auto currAcc : accounts)
    {
        qDebug() << "ACCOUNT CONTROLLER: change userNum" << currAcc->getId().toStringDec().toUtf8() << " "
                 << wallId;
        if (currAcc->getId().serialize() == wallId)
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
