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

#include "network/packages/service/get_all_actor_message.h"

using namespace Messages;

void GetAllActorMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool GetAllActorMessage::isEmpty() const
{
    return actorId.isEmpty();
}

short GetAllActorMessage::getFieldsCount() const
{
    return GetAllActorMessage::FIELDS_COUNT;
}

QByteArray GetAllActorMessage::serialize() const
{
    return Serialization::serialize({ actorId }, GetAllActorMessage::FIELD_SIZE);
}

void GetAllActorMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list =
        Serialization::deserialize({ serilaized }, GetAllActorMessage::FIELD_SIZE);
    if (list.size() > 0)
    {
        actorId = list.at(0);
    }
}
