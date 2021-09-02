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

#ifndef MESSAGE_TYPES_H
#define MESSAGE_TYPES_H

#include <QMetaEnum>

namespace Messages {
Q_NAMESPACE

enum ChainMessage
{
    ActorMessage = 100,
    BlockMessage = 101,
    GenesisBlockMessage = 102,
    TxMessage = 103,
    ContractMessage = 104,
    ProfileMessage = 198,
    CoinRequest = 199,
};
Q_ENUM_NS(ChainMessage)

[[maybe_unused]] static bool isChainMessage(unsigned int value)
{
    if (value >= 100 && value <= 199)
        return true;
    return false;
}
enum GeneralRequest
{
    GetBlockCount = 200,
    GetActorCount = 201,
    GetActor = 202,
    GetBlock = 203,
    GetTx = 204,
    GetTxPair = 205,
    GetAllActors = 206,
    Notification = 270
};
Q_ENUM_NS(GeneralRequest)

[[maybe_unused]] static bool isGeneralRequest(unsigned int value)
{
    if (value >= 200 && value <= 299)
        return true;
    return false;
}
enum GeneralResponse
{
    GetBlockCountResponse = 300,
    GetActorCountResponse = 301,
    GetActorResponse = 302,
    GetBlockResponse = 303,
    GetTxResponse = 304,
    GetTxPairResponse = 305,
    GetAllActorsResponse = 306
};
Q_ENUM_NS(GeneralResponse)

[[maybe_unused]] static bool isGeneralResponse(unsigned int value)
{
    if (value >= 300 && value <= 399)
        return true;
    else
        return false;
}

enum DFSMessage
{
    titleMessage = 400,
    fileDataMessage = 401,
    requestMessage = 402,
    responseMessage = 403,
    statusMessage = 404,
    storageMessage = 405,
    closingMessage = 406,
    requestFragments = 407,
    changesMessage = 408,

    requestLast = 409,
    responseLast = 410,
    cardFileChange = 411,
    requestCardPath = 412,
    responseCardPath = 413,

    fileCompleted = 414,

    none = 499
};
Q_ENUM_NS(DFSMessage)

[[maybe_unused]] static bool isDFSMessage(unsigned int value)
{
    if (value >= 400 && value <= 499)
        return true;
    return false;
}
enum VerifyRequest
{
    verifyActor = 501,
    verifyActorResponse = 502
};
Q_ENUM_NS(VerifyRequest)

[[maybe_unused]] static bool isVerifyRequest(unsigned int value)
{
    if (value >= 500 && value <= 599)
        return true;
    return false;
}
}

template <typename QEnum>
QByteArray QtEnumToString(const QEnum value) // TODO: move to Utils
{
    return QMetaEnum::fromType<QEnum>().valueToKey(value);
}

#define PRINT_MESSAGE_TYPE(message, msgType)                                                            \
    if (Messages::isGeneralRequest(msgType))                                                            \
        qDebug().noquote() << message << QtEnumToString(Messages::GeneralRequest(msgType)) << msgType;  \
    else if (Messages::isGeneralResponse(msgType))                                                      \
        qDebug().noquote() << message << QtEnumToString(Messages::GeneralResponse(msgType)) << msgType; \
    else if (Messages::isChainMessage(msgType))                                                         \
        qDebug().noquote() << message << QtEnumToString(Messages::ChainMessage(msgType)) << msgType;    \
    else if (Messages::isDFSMessage(msgType))                                                           \
        qDebug().noquote() << message << QtEnumToString(Messages::DFSMessage(msgType)) << msgType;

#endif // MESSAGE_TYPES_H
