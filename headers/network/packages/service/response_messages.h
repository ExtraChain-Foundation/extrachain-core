#ifndef RESPONSE_MESSAGES_H
#define RESPONSE_MESSAGES_H

#include <QList>
#include <QByteArray>

namespace Messages {
static const QList<QByteArray> RESPONSE = { "getReserveActorResponse", "getBlockCountResponse",
                                            "getActorCountResponse",   "getBlockResponse",
                                            "getActorResponse",        "getTxResponse",
                                            "getTxPairResponse" };
static const QByteArray GET_RESERVE_ACTOR_RESPONSE_MESSAGE = RESPONSE[0];
static const QByteArray GET_BLOCK_COUNT_RESPONSE_MESSAGE = RESPONSE[1];
static const QByteArray GET_ACTOR_COUNT_RESPONSE_MESSAGE = RESPONSE[2];
static const QByteArray GET_BLOCK_RESPONSE_MESSAGE = RESPONSE[3];
static const QByteArray GET_ACTOR_RESPONSE_MESSAGE = RESPONSE[4];
static const QByteArray GET_TX_RESPONSE_MESSAGE = RESPONSE[5];
static const QByteArray GET_TX_PAIR_RESPONSE_MESSAGE = RESPONSE[6];
}
///**
// * Response message template
// * Has data and requestHash fields
// */
// template <class T>
// class EntityResponseMessage : public EntityMessage<T>
//{
// private:
//    QByteArray requestHash;

// public:
//    EntityResponseMessage(const QByteArray &msgType, const T &data,
//                          const QByteArray &requestHash)
//        : EntityMessage<T>::EntityMessage(msgType, data)
//        , requestHash(requestHash)
//    {
//        qDebug() << "NET MANAGER: EntityResponseMessage:";
//    }

//    EntityResponseMessage(const QByteArray &serialized)
//        : EntityMessage<T>()
//    {
//        QList<QByteArray> msgElemList = BaseMessage::deserializeToList(serialized);
//        BaseMessage::initFields(msgElemList);
//        EntityMessage<T>::initFields(msgElemList);
//        initFields(msgElemList);
//        //            initFields(serialized);
//        //            EntityMessage<T>::deserialize(serialized);
//        //            BaseMessage::deserialize(serialized);
//        //            initFields(serialized)
//        //            deserialize(serialized);
//    }

//    // BaseMessage interface
// protected:
//    QList<QByteArray> serializedParams() const override
//    {
//        QList<QByteArray> l = EntityMessage<T>::serializedParams();
//        l << requestHash;
//        return l;
//    }

//    void initFields(QLinkedList<QByteArray> &list) override
//    {
//        requestHash = list.takeLast();

//        BaseMessage::initFields(list);
//    }

//    void initFields(QList<QByteArray> &list) override
//    {
//        requestHash = list.takeFirst();
//    }

//    short getFieldsCount() const override
//    {
//        return BaseMessage::getFieldsCount() + 2;
//    }

// public:
//    QByteArray getRequestHash() const
//    {
//        return requestHash;
//    }
//};

//// Constructing methods //

// static EntityResponseMessage<BigNumber>
// createGetBlockCountResponse(const BigNumber &blockCount, const QByteArray &requestHash)
//{
//    return EntityResponseMessage<BigNumber>(GET_BLOCK_COUNT_RESPONSE_MESSAGE, blockCount,
//                                            requestHash);
//}

// static EntityResponseMessage<BigNumber>
// createReserveActorResponse(const BigNumber &actorReserved, const QByteArray &requestHash)
//{
//    return EntityResponseMessage<BigNumber>(GET_RESERVE_ACTOR_RESPONSE_MESSAGE, actorReserved,
//                                            requestHash);
//}

// static EntityResponseMessage<BigNumber>
// createGetActorCountResponse(const BigNumber &actorCount, const QByteArray &requestHash)
//{
//    return EntityResponseMessage<BigNumber>(GET_ACTOR_COUNT_RESPONSE_MESSAGE, actorCount,
//                                            requestHash);
//}

// static EntityResponseMessage<Block> createGetBlockResponse(const Block &block,
//                                                           const QByteArray &requestHash)
//{
//    return EntityResponseMessage<Block>(GET_BLOCK_RESPONSE_MESSAGE, block, requestHash);
//}

// static EntityResponseMessage<Actor<KeyPublic>>
// createGetActorResponse(const Actor<KeyPublic> &actor, const QByteArray &requestHash)
//{
//    qDebug() << "ladsfkjlsadfjk;lk;adsfkl;dasf";
//    return EntityResponseMessage<Actor<KeyPublic>>(GET_ACTOR_RESPONSE_MESSAGE, actor,
//                                                   requestHash);
//}

// static EntityResponseMessage<Transaction> createGetTxResponse(const Transaction &tx,
//                                                              const QByteArray &requestHash)
//{
//    return EntityResponseMessage<Transaction>(GET_TX_RESPONSE_MESSAGE, tx, requestHash);
//}

// static EntityResponseMessage<TxPair> createGetTxPairResponse(const TxPair &pair,
//                                                             const QByteArray &requestHash)
//{
//    return EntityResponseMessage<TxPair>(GET_TX_PAIR_RESPONSE_MESSAGE, pair, requestHash);
//}
//}

#endif // RESPONSE_MESSAGES_H
