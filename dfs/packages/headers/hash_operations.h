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

#ifndef HASH_OPERATIONS_H
#define HASH_OPERATIONS_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

namespace DistFileSystem {
// TODO: package root array

struct requestLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 1;
    QByteArrayList actors;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct responseLast : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 1;
    QByteArrayList lasts;
    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct CardFileChange : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 8;
    int key = -1;
    QByteArray actorId;
    QByteArray fileId;
    QByteArray prevId;
    QByteArray nextId;
    int type = -1;
    QByteArray sign;
    int version = -1;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct RequestCardPart : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 3;
    QByteArray actorId;
    int count = -1;
    int offset = -1;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

struct ResponseCardPart : Messages::ISmallMessage
{
    const short FIELDS_COUNT = 4;
    QByteArray actorId;
    int count = -1;
    int offset = -1;
    QByteArrayList data;

    const QList<QByteArray> serializedParams() const;
    void operator=(QList<QByteArray> &list);

public:
    void operator=(QByteArray &serialized) override;
    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};
}

#endif // HASH_OPERATIONS_H
