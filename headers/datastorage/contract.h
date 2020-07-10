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

#ifndef CONTRACT_H
#define CONTRACT_H
/*
#include "utils/bignumber.h"
#include "datastorage/transaction.h"
#include "datastorage/actor.h"
#include "datastorage/block.h"
#include <QObject>
#include <QDate>
class Token : public QObject
{
    Q_OBJECT
private:
    BigNumber actorId;
    BigNumber amount_token;
    BigNumber amout_in_work;

    QByteArray Activity;
    QByteArray Funds;
    QByteArray Sum;

    BigNumber price;
    QByteArray logo;
    BigNumber WorkDone;
    BigNumber DaysGone;
    BigNumber Pt;
    BigNumber rating;
    // QByteArray

public:
    BigNumber calcRating();
    BigNumber getWorkDone();
    BigNumber getDaysGone();
    BigNumber getPt();
    BigNumber getSum();
    BigNumber getAmount(QList<Block>, BigNumber userId);
    Token(QObject *parent = nullptr);
    Token(BigNumber actorId, QObject *parent = nullptr);
    BigNumber getPrice() const;

    BigNumber calcActivity();
    BigNumber calcFunds();
    BigNumber calcSum();
    BigNumber getActorId() const;

signals:
    QByteArray LogoDFS(BigNumber actorId);
public slots:
private:
    QList<QByteArray> getPathtoAllBlocks();
};

class Contract : public QObject
{

    const QByteArray PERFORMER_DELIMETR = "|";
    const QByteArray PERFORMER_LIST_DELIMETR = ",";
    const QByteArray SCOPE_OF_WORK_DELIMETR = ",";
    const QByteArray CONTRACT_DATE_DELIMETR = ":";
    const short CONTRACT_FIELDS_SIZE = 4;

    enum performer_status
    {
        notApprove,
        Approve,
        Done
    };

    performer_status fromInt(const int &s) const;

    Q_OBJECT

private:
    BigNumber customer; // creator
    QMap<BigNumber, performer_status> performers;
    QByteArray _location;
    QByteArray event;
    QPair<long long, long long> _contract_date;
    QByteArrayList _scope_of_work;
    QByteArray _agreement;
    BigNumber _amount;

    QByteArray data;

    const QMap<BigNumber, performer_status> performersDeserialize(const QByteArray &serialized) const;
    const QByteArray performersSerialize() const;
    const QByteArrayList _scope_of_workDeserialize(const QByteArray &serialized) const;
    const QByteArray _scope_of_workSerialize() const;
    const QPair<long long, long long> _contract_dateDeserialize(const QByteArray &serialized) const;
    const QByteArray _contract_dateSerialize() const;

public:
    Contract(const QByteArray &serialize_contract, QObject *parent = nullptr);
    Contract(const Contract &contract, QObject *parent = nullptr);
    Contract(const BigNumber &_customer, const QMap<BigNumber, performer_status> &_performers,
             const QByteArray &_location, const QByteArray &event,
             const QPair<long long, long long> &_event_date, const QList<QByteArray> &_scope_of_work,
             const QByteArray &_agreement, const BigNumber &_amount, QObject *parent = nullptr);

    const QByteArray serialize() const;
    const QList<QByteArray> deserialize(const QByteArray &serialized) const;
};
*/
#endif // CONTRACT_H
