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

#ifndef TITLE_MESSAGE_H
#define TITLE_MESSAGE_H

#include "dfs/packages/headers/dfs_message_interface.h"
#include "headers/network/packages/message_interface.h"

#include <QFile>

namespace DistFileSystem {
struct titleMessage : public Messages::ISmallMessage
{
    const short FIELDS_COUNT = 6;

    QString filePath;
    unsigned long pckgsAmount = 0;
    long long fileSize = 0;
    unsigned int f_type;
    QByteArray dataHash; // Keccak256
    QByteArray prevId;

    void calcHash(); // filesize, pckgAmount, datahash

    const QList<QByteArray> serializedParams() const;
    void operator=(titleMessage title);
    void operator=(const QByteArray &serialized);

public:
    void operator=(QByteArray &serialized) override;

    bool isEmpty() const override;
    short getFieldsCount() const override;
    QByteArray serialize() const override;
    void deserialize(const QByteArray &serialized) override;
};

typedef struct titleMessage TitleMessage;
}

#endif // TITLE_MESSAGE_H
