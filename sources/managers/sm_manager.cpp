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

#include "managers/sm_manager.h"
#ifdef ECLIENT
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
    profileList.append(actor->id().toActorId());
    profileList.append(tokenName);
    profileList.append(tokenCount);
    profileList.append(relAddress);
    profileList.append(color);
    actorIndex->saveProfile(actor, profileList);
    profileList.insert(2, actor->key()->sign(Serialization::serialize(profileList, 4)));

    QFile file(SmartContractStorage::CONTRACTPROFILE + actor->id().toActorId() + ".profile");
    if (file.exists())
    {
        qDebug() << "[SmartContractManager][createContractProfile] Error. Contract profile already exist";
        return;
    }
    if (file.open(QIODevice::WriteOnly))
    {
        file.write(Serialization::serialize(profileList, 4));
        file.close();
    }
    else
    {
        qDebug() << "[SmartContractManager][createContractProfile] Error. File " << file.fileName()
                 << " not open";
        return;
    }

    sendInitialTransaction(actor, relAddress, tokenCount);
}

void SmartContractManager::process()
{
}

void SmartContractManager::sendInitialTransaction(Actor<KeyPrivate> *sender, QByteArray receiver,
                                                  QByteArray quantity)
{
    Transaction tx(sender->id(), receiver, Transaction::visibleToAmount(quantity));
    tx.setData("initcontract");

    tx.setToken(sender->id());
    tx.sign(*sender);

#ifdef ECLIENT
    emit sendTransactionCreateContract(tx.serialize(), Messages::ChainMessage::contractMessage);
#endif
#ifdef ECONSOLE
    emit initConsoleToken(tx);
#endif
}

Actor<KeyPrivate> *SmartContractManager::createContract(QByteArray tokenName)
{
    Actor<KeyPrivate> *actor = new Actor<KeyPrivate>();

    actor->create(ActorType::Wallet);

    emit verifyActor(actor->convertToPublic());
    actorIndex->addActor(actor->convertToPublic());
    // emit addContractActorInActorIndex(actor->convertToPublic());
    // emit saveActorInPrivateProfile(actor->getId().toActorId()); // TODO: not to wallet
    savePrivateActor(*actor);
    return actor;
}

void SmartContractManager::savePrivateActor(Actor<KeyPrivate> actor)
{
    qDebug() << "Attempting to save Private Actor" << actor.id();

    QString fileName = KeyStore::makeKeyFileName(actor.id().toActorId());
    QString path = SmartContractStorage::CONTRACTSTORE + fileName;
    qDebug() << "Path=" << path;
    QFile file(path);

    // move to another place
    FileSystem::createFolderIfNotExist(SmartContractStorage::CONTRACTSTORE);

    // TODO: encrypt
    if (file.open(QIODevice::ReadWrite))
    {
        QByteArray old;
        old = file.readAll();
        if (old == actor.serialize())
        {
            qDebug() << "Private actor with id =" << actor.id() << "already exists";
        }
        else
        {
            file.resize(0);
            qDebug() << "actor serial: ---- " << actor.serialize();
            file.write(actor.serialize());
            file.flush();
            qDebug() << "Private Actor" << actor.id() << "is successfully saved";
        }
        file.close();
        return;
    }

    qDebug() << "Can't save actor" << actor.id();
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
            QList<QByteArray> list = Serialization::deserialize(data, 4);
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
