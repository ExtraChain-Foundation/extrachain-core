/*
 * ExtraChain Core
 * Copyright (C) 2020 ExtraChain Foundation <extrachain@gmail.com>
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

Blockchain *AccountController::getBlockchain() const
{
    return blockchain;
}

QList<BigNumber> AccountController::getListAccounts() const
{
    QList<BigNumber> res;
    for (const auto &tmp : accounts)
    {
        res.append(tmp->id());
    }
    return res;
}

AccountController::AccountController(ActorIndex *actorIndex)
{
    this->actorIndex = actorIndex;
    // when private actor is verified by actor index -> save it locally
    // connect(actorIndex, &ActorIndex::PrivateActorIsVerified, this, &AccountController::savePrivateActor);
    //    if (!QFile(KeyStore::user_actor_state).exists())
    //    {
    //        QFile file(KeyStore::user_actor_state);
    //        file.open(QIODevice::WriteOnly);
    //        file.flush();
    //        file.close();
    //    }
    // loadActors();
}

QList<QByteArray> AccountController::getAccountID()
{
    QList<QByteArray> list;
    for (int i = 0; i < accounts.size(); i++)
        list.append(accounts[i]->id().toActorId());
    return list;
}

Actor<KeyPrivate> AccountController::createActor(ActorType account, QByteArray hashLogin)
{
    Actor<KeyPrivate> *actor = new Actor<KeyPrivate>();
    actor->create(account);

    qDebug() << actor->serialize();

    emit verifyActor(actor->convertToPublic());

    actorIndex->addActor(actor->convertToPublic());
    savePrivateActor(*actor, hashLogin);
    accounts.append(actor);
    if (accounts.size() - 1 == 0)
        emit savePrivateProfile(actor->id().toActorId());

    userNum = accounts.size() - 1;

    qDebug() << "create actor finished";
    if (account == ActorType::Account)
    {
        qDebug() << "Dfs hash init for me";
        emit initDfs(); //
    }
    emit newActorIsCreated(this->getMainActor()->id(), account == ActorType::Account); // TODO: send type

    if (!accounts.isEmpty())
        blockchain->getBlockZero();
    return *actor;
}

Actor<KeyPrivate> AccountController::getActor(BigNumber id)
{
    for (Actor<KeyPrivate> *actor : accounts)
    {
        if (id == actor->id())
        {
            return *actor;
        }
    }
    qDebug() << "Can't find actor with id:" << id;
    return Actor<KeyPrivate>();
}

Actor<KeyPrivate> AccountController::getActorByPublicKey(QByteArray pubkey)
{
    qDebug() << "Warning! DEPRECATED";
    return Actor<KeyPrivate>();
    //    for (Actor<KeyPrivate> *actor : accounts)
    //    {
    //        if (actor->getKey()->extractPublicKey() == pubkey)
    //        {
    //            qDebug() << "ACCOUNT CONTROLLER: currentActor:" << actor->getId();
    //            return *actor;
    //        }
    //    }
    //    qDebug() << "Can't find actor with pubkey:" << QString(pubkey);
    //    return Actor<KeyPrivate>();
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

void AccountController::loadActors(QByteArray id, QByteArrayList idList, QByteArray hashLogin)
{
    if (id.isEmpty() || hashLogin.isEmpty())
    {
        qDebug() << "[loadActors] id or hashLogin is empty";
        Q_ASSERT(!id.isEmpty());
        Q_ASSERT(!hashLogin.isEmpty());
        return;
    }

    accounts.clear();
    qDebug() << "ACCOUNT CONTROLLER : Attempting to load actors from local storage";
    QString path = KeyStore::USER_KEYSTORE;
    int loaded = 0;
    for (const QByteArray &fileName : idList)
    {
        QFile file(path + "/" + fileName + ".key");
        if (file.exists() && file.open(QIODevice::ReadOnly))
        {
            QByteArray serialized = BlowFish::decrypt(file.readAll(), hashLogin);
            qDebug() << serialized;
            file.close();
            if (!serialized.isEmpty())
            {
                Actor<KeyPrivate> *actor = new Actor<KeyPrivate>(serialized);

                qDebug() << "Actor" << actor->id() << "found locally -" << actor->key()->getPrivateKey();
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

void AccountController::savePrivateActor(Actor<KeyPrivate> actor, QByteArray hashLogin)
{
    qDebug() << "Attempting to save Private Actor" << actor.id();
    if (!accounts.isEmpty())
        emit editPrivateProfile(actor.id().toActorId());
    QString fileName = KeyStore::makeKeyFileName(actor.id().toActorId());
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
            qDebug() << "Private actor with id =" << actor.id() << "already exists";
        }
        else
        {
            qDebug() << "actor serialized: ---- " << actor.serialize();
            file->write(BlowFish::encrypt(actor.serialize(), hashLogin));
            file->flush();
            qDebug() << "Private Actor" << actor.id() << "is successfully saved";
        }
        file->close();
        delete file;
        return;
    }

    qDebug() << "Can't save actor" << actor.id();
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
        if (currAcc->id().toActorId() == wallId)
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
