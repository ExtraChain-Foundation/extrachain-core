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

#include "resolve/resolver_service.h"
#include "datastorage/blockchain.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"

#include "managers/extrachain_node.h"
#include "managers/tx_manager.h"
#include "resolve/resolve_manager.h"

using namespace Resolver;

void ResolverService::setNode(ExtraChainNode *value) {
    node = value;
}

void ResolverService::setBlockchain(Blockchain *value) {
    blockchain = value;
}

void ResolverService::setChatManager(ChatManager *value) {
    chatManager = value;
}

void ResolverService::setResolveManager(ResolveManager *value) {
    resolveManager = value;
}

Resolver::Type ResolverService::getType() const {
    return type;
}

void ResolverService::setType(const Resolver::Type &value) {
    type = value;
}

Resolver::Lifetime ResolverService::getLifetime() const {
    return lifetime;
}

// DFS::titleMessage ResolverService::getTitle() const
//{
//    return title;
//}

ResolverService::ResolverService(Resolver::Type type, Lifetime lifetime, ActorIndex *actorIndex,
                                 ResolveManager *resolveManager, QObject *parent)
    : QObject(parent) {
    this->type = type;
    this->lifetime = lifetime;
    this->actorIndex = actorIndex;
    this->resolveManager = resolveManager;
}

ResolverService::~ResolverService() {
    //    emit finished();
}

void ResolverService::finishWork() {
    active = false;

    if (this->lifetime == Resolver::Lifetime::SHORT) {
        emit TaskFinished();
    }
}

bool ResolverService::isActive() const {
    return active;
}

void ResolverService::setTask(QByteArray msg, SocketPair receiver) {
    active = true;
    this->msg = msg;
    this->hash = calcHash(msg);
    this->receiver = receiver;
}

bool ResolverService::validate(const Messages::BaseMessage &message) {
    ActorId signer = message.signer;
    if (signer.isEmpty())
        return false;
    Actor<KeyPublic> actor = actorIndex->getActor(signer);

    if (!actor.empty()) {
        return message.verifyDigSig(actor);
    } else {
        qDebug() << QString("There no actor %1 locally").arg(signer.toString());
        //        emit SendGetActor(signer);
        //        return false;
        this->thread()->sleep(5);
        return validate(message);
    }
}

QByteArray ResolverService::calcHash(const QByteArray &request) const {
    return Utils::calcKeccak(request);
}

bool ResolverService::MessageIsNotValid(const Messages::BaseMessage &message) {
    if (validate(message)) {
        qDebug() << "[ResolverService]"
                 << "checkMsgType(): valid";
        return false;
    }
    qWarning() << QString("Message [%1] digital sign is not valid. Signer was [%2]")
                      .arg(QString::fromLocal8Bit(message.serialize()), message.signer.toString());
    return true;
}

bool ResolverService::addResponseHandler(const QByteArray &message, const unsigned int &msgType) {
    bool flag = false;
    handlerFileMutex.lock();
    QByteArray hash = Utils::calcKeccak(message);
    if (Messages::isGeneralResponse(msgType)) {
        if (resolveManager->getRequestResponseMap()->find(hash)
            == resolveManager->getRequestResponseMap()->end()) {
            resolveManager->getRequestResponseMap()->insert(hash, Config::Net::NECESSARY_RESPONSE_COUNT);
            flag = true;
        }
    }
    handlerFileMutex.unlock();
    return flag;
}

bool ResolverService::checkResponseHandler(const QByteArray &hash) {
    handlerFileMutex.lock();
    bool flag = true;
    int value = Config::Net::NECESSARY_RESPONSE_COUNT;
    QMap<QByteArray, int>::iterator it = resolveManager->getRequestResponseMap()->find(hash);
    if (it != resolveManager->getRequestResponseMap()->end()) {
        int t = it.value() - 1;
        if (t <= 0) {
            //            requestResponseMap->remove(hash);
            flag = false;
        } else {
            resolveManager->getRequestResponseMap()->remove(hash);
            resolveManager->getRequestResponseMap()->insert(hash, t);
        }
    } else {
        resolveManager->getRequestResponseMap()->insert(hash, value);
    }

    handlerFileMutex.unlock();
    return flag;
}

void ResolverService::process() {
    resolveTask();
}

void ResolverService::resolveTask() {
    switch (this->type) {
    case Resolver::Type::GENERAL:
        resolveGeneralTask();
        break;
    default:
        break;
    }
}

void ResolverService::resolveGeneralTask() {
    qFatal("Why here?");
    // QList<QByteArray> res = Serialization::deserialize(msg);
    using namespace Messages;
    BaseMessage message;
    message = msg;

    unsigned int msgType = message.type;

    if (msgType == 0) {
        qDebug() << "[ResolverService] Receive empty message";
    }

    if (message.data.isEmpty()) { // && msgType != Messages::GeneralRequest::GetBlockCount) {
        finishWork();
        return;
    }

    PRINT_MESSAGE_TYPE("[ResolverService] Receive", msgType);

    if (Messages::isDFSMessage(msgType)) {
        if (Messages::isGeneralResponse(msgType)) {
            BaseMessageResponse responseMessage;
            responseMessage = msg;
            if (MessageIsNotValid(responseMessage)) {
                finishWork();
                return;
            }
        } else {
            // qDebug() << "received msg signature:" << message.getDigSig();
            if (MessageIsNotValid(message)) {
                finishWork();
                return;
            }
        }
    }

    switch (msgType) {
    // spread messages
    default: {
        finishWork();
        break;
    }
    }
}
// validation methods //

bool ResolverService::validateBlock(const Block &block) {
    qDebug() << "[ResolverService] validate(Block):";
    return actorIndex->validateBlock(block);
}

bool ResolverService::validate(const Transaction &tx) {
    qDebug() << "[ResolverService] validate(Transaction):";
    if (tx.getSender() == ActorId() && tx.getData().contains(Fee::STAKING_REWARD))
        return true;
    if (actorIndex->getActor(tx.getSender()).empty()) {
        this->thread()->sleep(5);
        return validate(tx);
    }
    bool result = actorIndex->validateTx(tx);
    return result;
}
