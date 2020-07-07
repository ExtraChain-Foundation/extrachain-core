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

#ifndef BASEMESSAGE_H
#define BASEMESSAGE_H

#include <QLinkedList>

#include "network/packages/message_interface.h"
#include "utils/bignumber.h"
#include "utils/utils.h"

namespace Messages {
/**
 * @brief Base package class
 * Simple implementation of IMessage interface
 *
 * Methods that should be overrided in subclasses:
 * 1) getFieldsCount
 * 2) serializedParams
 * 3) initFields
 *
 */
struct BaseMessage : IMessage
{
    QByteArray protocol = Config::Net::PROTOCOL_VERSION; // protocol version
    unsigned int type = 0;                               // message type
    BigNumber signer;                                    // message signer actor's id
    QByteArray digSig;                                   // msg digital signature
    QByteArray data = "";

    static const short FIELDS_COUNT = 5;

    void setMsgData(const QByteArray &data);
    virtual void calcDigSig(const Actor<KeyPrivate> &actor);
    virtual bool verifyDigSig(const Actor<KeyPublic> &actor) const;
    void operator=(BaseMessage b);

    // IMessage interface
public:
    virtual void operator=(QByteArray &serialized) override;
    virtual void operator=(QList<QByteArray> &list) override;
    virtual bool isEmpty() const override;
    virtual QByteArray concatenateAllData() const override;
    virtual QList<QByteArray> serializedParams() const override;
    virtual short getFieldsCount() const override;
    virtual QByteArray serialize() const override;
    virtual void deserialize(const QByteArray &serialized) override;

    virtual const QByteArray hash() const override;
};
}
#endif // BASEMESSAGE_H
