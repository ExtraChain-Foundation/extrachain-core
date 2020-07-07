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

#include "network/packages/service/get_tx_pair_message.h"

using namespace Messages;

void Messages::GetTxPairMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

short Messages::GetTxPairMessage::getFieldsCount() const
{
    return GetTxPairMessage::FIELDS_COUNT;
}

QByteArray GetTxPairMessage::serialize() const
{
    return Serialization::serialize({ senderId.toActorId(), receiverId.toActorId() },
                                             GetTxPairMessage::FIELD_SIZE);
}

void GetTxPairMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::deserialize(serilaized, GetTxPairMessage::FIELD_SIZE);
    this->senderId = BigNumber(list.at(0));
    this->receiverId = BigNumber(list.at(1));
}
