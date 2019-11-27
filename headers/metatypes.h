#ifndef METATYPES_H
#define METATYPES_H

#include <QObject>
#include <QHostAddress>
#include <QMetaType>

#include "dfs/controls/headers/dfs.h"
#include "datastorage/blockchain.h"
#include "datastorage/contract.h"
#include "network/socket_pair.h"
#include "profile/profile.h"
#include "profile/quickprofile.h"
#include "datastorage/searchfilters.h"
#include "network/network_manager.h"
#include "profile/private_profile.h"
#include "managers/chat.h"

Q_DECLARE_METATYPE(BigNumber)
// Q_DECLARE_METATYPE(BigNumber *)
Q_DECLARE_METATYPE(QHostAddress)
Q_DECLARE_METATYPE(Block)
Q_DECLARE_METATYPE(Actor<KeyPublic>)
Q_DECLARE_METATYPE(Transaction)
Q_DECLARE_METATYPE(SocketService)
// Q_DECLARE_METATYPE(SocketService*)
Q_DECLARE_METATYPE(Messages::DownloadDfsRequestData)
Q_DECLARE_METATYPE(Messages::BaseMessage)
Q_DECLARE_METATYPE(based_dfs_struct::Type)
Q_DECLARE_METATYPE(based_dfs_struct::SubType)
Q_DECLARE_METATYPE(based_dfs_struct::Status)
Q_DECLARE_METATYPE(SearchEnum::BlockParam)
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(SocketPair)
Q_DECLARE_METATYPE(Profile)
Q_DECLARE_METATYPE(QuickProfile)
Q_DECLARE_METATYPE(QList<Profile>)
Q_DECLARE_METATYPE(PublicProfile)
Q_DECLARE_METATYPE(SearchFilters)
Q_DECLARE_METATYPE(GenesisBlock)
Q_DECLARE_METATYPE(typeDataPrProfile)
Q_DECLARE_METATYPE(UIChat)
Q_DECLARE_METATYPE(QList<UIChat>)
Q_DECLARE_METATYPE(UIMessage)
Q_DECLARE_METATYPE(QList<UIMessage>)
Q_DECLARE_METATYPE(Network::DataStruct)

void registerMetaTypes()
{
    qRegisterMetaType<BigNumber>();
    qRegisterMetaType<Block>();
    qRegisterMetaType<GenesisBlock>();
    qRegisterMetaType<QHostAddress>();
    qRegisterMetaType<Actor<KeyPublic>>();
    qRegisterMetaType<Transaction>();
    qRegisterMetaType<SocketService>();
    // qRegisterMetaType<SocketService*>();
    qRegisterMetaType<Messages::DownloadDfsRequestData>();
    qRegisterMetaType<Messages::BaseMessage>();
    // qRegisterMetaType<Contract>();
    qRegisterMetaType<based_dfs_struct::Type>();
    qRegisterMetaType<based_dfs_struct::SubType>();
    qRegisterMetaType<based_dfs_struct::Status>();
    qRegisterMetaType<SearchEnum::BlockParam>();
    qRegisterMetaType<SocketPair>();
    qRegisterMetaType<Profile>();
    qRegisterMetaType<QuickProfile>();
    qRegisterMetaType<QList<Profile>>();
    qRegisterMetaType<PublicProfile>();
    qRegisterMetaType<SearchFilters>();
    qRegisterMetaType<typeDataPrProfile>();
    qRegisterMetaType<UIChat>();
    qRegisterMetaType<QList<UIChat>>();
    qRegisterMetaType<UIMessage>();
    qRegisterMetaType<QList<UIMessage>>();
    qRegisterMetaType<Network::DataStruct>();
}

#endif
