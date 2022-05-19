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

#ifndef METATYPES_H
#define METATYPES_H

#include "datastorage/blockchain.h"
#include "datastorage/contract.h"
#include "managers/extrachain_node.h"
#include "network/network_manager.h"

Q_DECLARE_METATYPE(BigNumber)
Q_DECLARE_METATYPE(QHostAddress)
Q_DECLARE_METATYPE(ActorId)
Q_DECLARE_METATYPE(Block)
Q_DECLARE_METATYPE(Actor<KeyPublic>)
Q_DECLARE_METATYPE(Transaction)
Q_DECLARE_METATYPE(SearchEnum::BlockParam)
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(GenesisBlock)
Q_DECLARE_METATYPE(Notification)
Q_DECLARE_METATYPE(QList<Notification>)
Q_DECLARE_METATYPE(ActorType)
Q_DECLARE_METATYPE(Network::Protocol)
Q_DECLARE_METATYPE(Network::SocketServiceError)

void registerMetaTypes() {
    qRegisterMetaType<BigNumber>();
    qRegisterMetaType<Block>();
    qRegisterMetaType<GenesisBlock>();
    qRegisterMetaType<QHostAddress>();
    qRegisterMetaType<ActorId>();
    qRegisterMetaType<Actor<KeyPublic>>();
    qRegisterMetaType<Transaction>();
    qRegisterMetaType<SearchEnum::BlockParam>();
    qRegisterMetaType<Notification>();
    qRegisterMetaType<QList<Notification>>();
    qRegisterMetaType<ActorType>();
    qRegisterMetaType<Network::Protocol>();
    qRegisterMetaType<Network::SocketServiceError>();
}

#endif
