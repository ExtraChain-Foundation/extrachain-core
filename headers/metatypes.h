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

#include <QObject>
#include <QHostAddress>
#include <QMetaType>

#include "dfs/controls/headers/dfs.h"
#include "datastorage/blockchain.h"
#include "datastorage/contract.h"
#include "network/socket_pair.h"
#include "datastorage/searchfilters.h"
#include "network/network_manager.h"
#include "profile/private_profile.h"
#include "managers/chat.h"
#include "dfs/controls/headers/subscribe_controller.h"

Q_DECLARE_METATYPE(BigNumber)
// Q_DECLARE_METATYPE(BigNumber *)
Q_DECLARE_METATYPE(QHostAddress)
Q_DECLARE_METATYPE(ActorId)
Q_DECLARE_METATYPE(Block)
Q_DECLARE_METATYPE(Actor<KeyPublic>)
Q_DECLARE_METATYPE(Transaction)
Q_DECLARE_METATYPE(SocketService)
// Q_DECLARE_METATYPE(SocketService*)
Q_DECLARE_METATYPE(Messages::BaseMessage)
Q_DECLARE_METATYPE(DfsStruct::Type)
Q_DECLARE_METATYPE(SearchEnum::BlockParam)
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(SocketPair)
Q_DECLARE_METATYPE(PublicProfile)
Q_DECLARE_METATYPE(SearchFilters)
Q_DECLARE_METATYPE(GenesisBlock)
Q_DECLARE_METATYPE(ChatInfo)
Q_DECLARE_METATYPE(QList<ChatInfo>)
Q_DECLARE_METATYPE(ChatMessageInfo)
Q_DECLARE_METATYPE(QList<ChatMessageInfo>)
Q_DECLARE_METATYPE(Network::DataStruct)
Q_DECLARE_METATYPE(Notification)
Q_DECLARE_METATYPE(QList<Notification>)
Q_DECLARE_METATYPE(ChatFileSender)
Q_DECLARE_METATYPE(DFSResolverService::FinishStatus)
Q_DECLARE_METATYPE(DfsStruct::DfsSave)
Q_DECLARE_METATYPE(DfsStruct::ChangeType)
Q_DECLARE_METATYPE(ActorType)
Q_DECLARE_METATYPE(Network::Protocol)

void registerMetaTypes()
{
    qRegisterMetaType<BigNumber>();
    qRegisterMetaType<Block>();
    qRegisterMetaType<GenesisBlock>();
    qRegisterMetaType<QHostAddress>();
    qRegisterMetaType<ActorId>();
    qRegisterMetaType<Actor<KeyPublic>>();
    qRegisterMetaType<Transaction>();
    qRegisterMetaType<SocketService>();
    // qRegisterMetaType<SocketService*>();
    qRegisterMetaType<Messages::BaseMessage>();
    // qRegisterMetaType<Contract>();
    qRegisterMetaType<DfsStruct::Type>();
    qRegisterMetaType<SearchEnum::BlockParam>();
    qRegisterMetaType<SocketPair>();
    qRegisterMetaType<PublicProfile>();
    qRegisterMetaType<SearchFilters>();
    qRegisterMetaType<ChatInfo>();
    qRegisterMetaType<QList<ChatInfo>>();
    qRegisterMetaType<ChatMessageInfo>();
    qRegisterMetaType<QList<ChatMessageInfo>>();
    qRegisterMetaType<Network::DataStruct>();
    qRegisterMetaType<Notification>();
    qRegisterMetaType<QList<Notification>>();
    qRegisterMetaType<ChatFileSender>();
    qRegisterMetaType<DFSResolverService::FinishStatus>();
    qRegisterMetaType<DfsStruct::DfsSave>();
    qRegisterMetaType<DfsStruct::ChangeType>();
    qRegisterMetaType<ActorType>();
    qRegisterMetaType<Network::Protocol>();
}

#endif
