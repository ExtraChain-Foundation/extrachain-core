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

#include <headers/network/packages/base_message_response.h>
using namespace Messages;

void BaseMessageResponse::operator=(BaseMessageResponse bm)
{
    QList<QByteArray> list = bm.BaseMessage::serializedParams();
    BaseMessage::operator=(list);
    dataHash = bm.dataHash;
}
// IMessage interface
void BaseMessageResponse::operator=(QByteArray &serialized)
{
    deserialize(serialized);
}

void BaseMessageResponse::operator=(QList<QByteArray> &list)
{
    BaseMessage::operator=(list);
    dataHash = list.takeFirst();
}

bool BaseMessageResponse::isEmpty() const
{
    if (BaseMessage::isEmpty() || dataHash.isEmpty())
        return true;
    else
        return false;
}

QList<QByteArray> BaseMessageResponse::serializedParams() const
{
    QList<QByteArray> list = BaseMessage::serializedParams();
    list << dataHash;
    return list;
}

short BaseMessageResponse::getFieldsCount() const
{
    return BaseMessage::getFieldsCount() + FIELDS_COUNT;
}
