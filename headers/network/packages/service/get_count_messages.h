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

#ifndef SIMPLE_MESSAGE_H
#define SIMPLE_MESSAGE_H
#include <QByteArray>
#include "headers/network/packages/service/message_types.h"

namespace Messages {

struct BlockCount
{
    QByteArray request;

    BlockCount()
    {
        request = QByteArray::number(Messages::GeneralRequest::GetBlockCount); /* GET_BLOCK_COUNT_MESSAGE;*/
    }
    BlockCount(const QByteArray &serialized)
    {
        request = serialized;
    }

    const QByteArray serialize() const
    {
        return request;
    }
};

struct ActorCount
{
    QByteArray request;

    ActorCount()
    {
        request = QByteArray::number(Messages::GeneralRequest::GetActorCount); /*GET_ACTOR_COUNT_MESSAGE;*/
    }
    ActorCount(const QByteArray &serialized)
    {
        request = serialized;
    }

    const QByteArray serialize() const
    {
        return request;
    }
};
}

#endif // SIMPLE_MESSAGE_H
