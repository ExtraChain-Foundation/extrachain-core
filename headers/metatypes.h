#ifndef METATYPES_H
#define METATYPES_H

#include <QObject>
#include <QHostAddress>
#include <QMetaType>

#include "dfs/controls/headers/dfs.h"
#include "datastorage/blockchain.h"
#include "datastorage/contract.h"
#include "network/socket_pair.h"
#include "datastorage/profile.h"

#ifdef ETALONIUM_CLIENT
#include "datastorage/searchfilters.h"
#endif

Q_DECLARE_METATYPE(QHostAddress)
Q_DECLARE_METATYPE(Block)
Q_DECLARE_METATYPE(Messages::DfsMessage)
Q_DECLARE_METATYPE(Actor<KeyPublic>)
Q_DECLARE_METATYPE(Transaction)
Q_DECLARE_METATYPE(Contract)
Q_DECLARE_METATYPE(Messages::DfsRequest)
Q_DECLARE_METATYPE(Messages::DownloadDfsRequestData)
Q_DECLARE_METATYPE(Messages::BaseMessage)
Q_DECLARE_METATYPE(based_dfs_struct::Type)
Q_DECLARE_METATYPE(based_dfs_struct::SubType)
Q_DECLARE_METATYPE(based_dfs_struct::Status)
Q_DECLARE_METATYPE(SearchEnum::BlockParam)
Q_DECLARE_METATYPE(std::string)
Q_DECLARE_METATYPE(SocketPair)
Q_DECLARE_METATYPE(Profile)
Q_DECLARE_METATYPE(QList<Profile>)
Q_DECLARE_METATYPE(PublicProfile)
// Q_DECLARE_METATYPE(qintptr)

#ifdef ETALONIUM_CLIENT
Q_DECLARE_METATYPE(SearchFilters)
#endif

void registerMetaTypes()
{
    qRegisterMetaType<BigNumber>();
    qRegisterMetaType<Messages::DfsMessage>();
    qRegisterMetaType<Block>();
    qRegisterMetaType<QHostAddress>();
    qRegisterMetaType<Actor<KeyPublic>>();
    qRegisterMetaType<Transaction>();
    qRegisterMetaType<Messages::DfsRequest>();
    qRegisterMetaType<Messages::DownloadDfsRequestData>();
    qRegisterMetaType<Messages::BaseMessage>();
    qRegisterMetaType<Contract>();
    qRegisterMetaType<based_dfs_struct::Type>();
    qRegisterMetaType<based_dfs_struct::SubType>();
    qRegisterMetaType<based_dfs_struct::Status>();
    qRegisterMetaType<SearchEnum::BlockParam>();
    qRegisterMetaType<SocketPair>();
    qRegisterMetaType<Profile>();
    qRegisterMetaType<QList<Profile>>();
    qRegisterMetaType<PublicProfile>();

    // qRegisterMetaType<qintptr>();

#ifdef ETALONIUM_CLIENT
    qRegisterMetaType<SearchFilters>();
#endif
}

#endif
