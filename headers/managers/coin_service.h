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
