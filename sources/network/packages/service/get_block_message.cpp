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

#include "network/packages/service/get_block_message.h"

using namespace Messages;

void GetBlockMessage::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

bool GetBlockMessage::isEmpty() const
{
    return (param == SearchEnum::BlockParam::Null) || (value.isEmpty());
}

short GetBlockMessage::getFieldsCount() const
{
    return GetBlockMessage::FIELDS_COUNT;
}

QByteArray GetBlockMessage::serialize() const
{
    return Serialization::serialize({ SearchEnum::toString(param).toUtf8(), value },
                                             GetBlockMessage::FIELD_SIZE);
}

void GetBlockMessage::deserialize(const QByteArray &serilaized)
{
    QList<QByteArray> list = Serialization::deserialize(serilaized, GetBlockMessage::FIELD_SIZE);
    this->param = SearchEnum::fromStringBlockParam(list.at(0));
    this->value = list.at(1);
}
