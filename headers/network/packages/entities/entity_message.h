#ifndef ENTITY_MESSAGE_H
#define ENTITY_MESSAGE_H

#include "network/packages/base_message.h"

#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"
#include "dfs/packages/headers/dfs_universal.h"
#include "dfs/packages/headers/dfs_request.h"
#include "network/packages/service/downloaddfsrequest.h"

#include "network/packages/service/list_connections.h"

#include "datastorage/contract.h"

namespace Messages {
static const QByteArray RESERVE_ACTOR_MESSAGE = "reserveActor";
static const QByteArray ACTOR_MESSAGE = "actor";
static const QByteArray BLOCK_MESSAGE = "block";
static const QByteArray GENESIS_BLOCK_MESSAGE = "genesisBlock";
static const QByteArray TX_MESSAGE = "tx";
static const QByteArray CONTRACT_MESSAGE = "contractMessage";
static const QByteArray COIN_REQUEST = "coinRequest";
// static const QByteArray DFS_CHANGED_MESSAGE = "dfs";

template <class T>
class EntityMessage : public BaseMessage
{
protected:
    T data;

public:
    EntityMessage(const QByteArray &msgType, const T &data)
        : BaseMessage(msgType)
        , data(data)
    {
    }

    EntityMessage(const QByteArray &serialized)
        : BaseMessage()
    {
        QList<QByteArray> msgElemList = BaseMessage::deserializeToList(serialized);
        BaseMessage::initFields(msgElemList);
        initFields(msgElemList);
    }
    EntityMessage()
    {
    }

    void deserialize(const QByteArray &serialized) override
    {
        QList<QByteArray> list = {};
        int pos = 0;
        for (int i = 0; i < getFieldsCount(); i++)
        {
            int count = Utils::qByteArrayToInt(serialized.mid(pos, Messages::FIELD_SIZES));
            pos += Messages::FIELD_SIZES;
            QByteArray el = serialized.mid(pos, count);
            pos += count;
            list << el;
        }
        if (list.size() < getFieldsCount())
        {
            qDebug() << "Error: can't deserialize message:" << serialized;
        }
        initFields(list);
    }
    // BaseMessage interface
private:
    QByteArray concatenateAllData() const override
    {
        QByteArray concatenatedData;
        for (QByteArray d : serializedParams())
        {
            // in entry data for digSig calculation we don't need digSig field
            if (d == digSig)
                continue;
            concatenatedData += d;
        }
        return concatenatedData;
    }

protected:
    void initFields(QLinkedList<QByteArray> &list) override
    {
        //        list.removeLast();
        data = T(list.takeFirst());
        //        BaseMessage::initFields(list);
    }
    void initFields(QList<QByteArray> &list) override
    {
        //        list.removeLast();
        data = T(list.takeFirst());
        //        BaseMessage::initFields(list);
    }

    short getFieldsCount() const override
    {
        return BaseMessage::getFieldsCount() + 1;
    }

    QList<QByteArray> serializedParams() const override
    {
        QList<QByteArray> l = BaseMessage::serializedParams();
        l << data.serialize();
        return l;
    }

public:
    T getEntity() const
    {
        return data;
    }
};

// Constructing methods //

static EntityMessage<BigNumber> createReserveActorMessage(BigNumber logHash)
{
    return EntityMessage<BigNumber>(RESERVE_ACTOR_MESSAGE, logHash);
}

static EntityMessage<BigNumber> createRequestCoinMessage(const BigNumber &coinAmount)
{
    return EntityMessage<BigNumber>(COIN_REQUEST, coinAmount);
}

static EntityMessage<Messages::EnableConnections>
createConnectionListMessage(const Messages::EnableConnections &conList)
{
    return EntityMessage<Messages::EnableConnections>(ENABLE_LIST_CONNECTIONS, conList);
}

static EntityMessage<DownloadDfsRequestData>
createDownloadDfsRequest(const DownloadDfsRequestData &status)
{
    return EntityMessage<DownloadDfsRequestData>(DOWNLOAD_DFS_REQUEST, status);
}
static EntityMessage<DfsMessage> createDfsMessage(const DfsMessage &dfs)
{
    return EntityMessage<DfsMessage>(DFS_CHANGES_MESSAGE, dfs);
}

static EntityMessage<DfsRequest> requestDfsMessage(const DfsRequest &dfs)
{
    return EntityMessage<DfsRequest>(DFS_REQUEST_MESSAGE, dfs);
}

static EntityMessage<Block> createBlockMessage(const Block &block)
{
    return EntityMessage<Block>(BLOCK_MESSAGE, block);
}

static EntityMessage<Block> createGenesisBlockMessage(const Block &block)
{
    return EntityMessage<Block>(GENESIS_BLOCK_MESSAGE, block);
}

static EntityMessage<Actor<KeyPublic>> createActorMessage(const Actor<KeyPublic> &actor)
{
    return EntityMessage<Actor<KeyPublic>>(ACTOR_MESSAGE, actor);
}

static EntityMessage<Transaction> createTxMessage(const Transaction &tx)
{
    return EntityMessage<Transaction>(TX_MESSAGE, tx);
}

static EntityMessage<Contract> createContractMessage(const Contract &contract)
{
    return EntityMessage<Contract>(CONTRACT_MESSAGE, contract);
}
}

#endif // ENTITY_MESSAGE_H
