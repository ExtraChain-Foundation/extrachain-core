#ifndef RESPONSE_MESSAGES_H
#define RESPONSE_MESSAGES_H

#include <QList>
#include <QByteArray>

namespace Messages {
static const QList<QByteArray> RESPONSE = { "getReserveActorResponse", "getBlockCountResponse",
                                            "getActorCountResponse",   "getBlockResponse",
                                            "getActorResponse",        "getTxResponse",
                                            "getTxPairResponse",       "getAllActorsResponse" };
static const QByteArray GET_RESERVE_ACTOR_RESPONSE_MESSAGE = RESPONSE[0];
static const QByteArray GET_BLOCK_COUNT_RESPONSE_MESSAGE = RESPONSE[1];
static const QByteArray GET_ACTOR_COUNT_RESPONSE_MESSAGE = RESPONSE[2];
static const QByteArray GET_BLOCK_RESPONSE_MESSAGE = RESPONSE[3];
static const QByteArray GET_ACTOR_RESPONSE_MESSAGE = RESPONSE[4];
static const QByteArray GET_TX_RESPONSE_MESSAGE = RESPONSE[5];
static const QByteArray GET_TX_PAIR_RESPONSE_MESSAGE = RESPONSE[6];
static const QByteArray GET_ALL_ACTORS_RESPONSE_MESSAGE = RESPONSE[7];

static const QList<QByteArray> GETTERS = { "getBlockCount", "getActorCount", "getActors",   "getBlock",
                                           "getTxPair",     "getTx",         "getAllActors" };

static const QByteArray GET_BLOCK_COUNT_MESSAGE = GETTERS[0];
static const QByteArray GET_ACTOR_COUNT_MESSAGE = GETTERS[1];
static const QByteArray GET_ACTOR_MESSAGE = GETTERS[2];
static const QByteArray GET_BLOCK_MESSAGE = GETTERS[3];
static const QByteArray GET_TX_PAIR_MESSAGE = GETTERS[4];
static const QByteArray GET_TX_MESSAGE = GETTERS[5];
static const QByteArray GET_ALL_ACTORS = GETTERS[6];

static const QList<QByteArray> NEWCHANGES = {};
}

#endif // RESPONSE_MESSAGES_H
