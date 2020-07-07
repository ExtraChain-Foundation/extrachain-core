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

#ifndef COINSERVICE_H
#define COINSERVICE_H

#include <QObject>
#include <QThread>

//#include "utils/bignumber.h"
#include "utils/bignumber.h"
#include "datastorage/block.h"
#include "datastorage/transaction.h"

class CoinService : public QThread
{
    Q_OBJECT
private:
    //	ResolverService *resolver;

    bool active = false;

private:
    BigNumber PEER_LIM = 50;
    BigNumber GAS_LIM = 40;
    const BigNumber GEN_ADDR = BigNumber(1);

public:
    CoinService(/*ResolverService *resolverService*/);

    ~CoinService() override;

public:
    void run() override;
    int exec();
    void quit();
    bool isActive();

public:
    BigNumber getPeerLimit();
    BigNumber getGasLimit();
    const BigNumber getGenesisAddress();
signals:
    void GenBlock(Block block);
    void CoinMined(Transaction tx);
public slots:
    void checkGenBlock(Block block);
    void mineCoin(BigNumber blockCountLocal, BigNumber blockCountMax, int peerCount, Transaction tx);
};

#endif // COINSERVICE_H
