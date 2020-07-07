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

#ifndef CONNECTTIONS_MESSAGE_H
#define CONNECTTIONS_MESSAGE_H
#include <vector>
#include "network/packages/base_message.h"
namespace Messages {
static const QByteArray ENABLE_LIST_CONNECTIONS = "connections";

struct ConnectionsMessage : ISmallMessage
{
    const short FIELD_SIZE = 3;
    const short FIELDS_COUNT = 1;
    std::vector<std::pair<std::string, int>> hosts;

public:
    // IMessage interface
    void operator=(const QByteArray &serialized);
    void operator=(QByteArray &serialized) override;
    void operator=(QList<QByteArray> &list);
    bool isEmpty() const override;
    QList<QByteArray> serializedParams() const;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // CONNECTTIONS_MESSAGE_H
