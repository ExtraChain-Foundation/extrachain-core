#ifndef COIN_PRICE_H
#define COIN_PRICE_H

#include "dfs/types/headers/dfstruct.h"
#include "managers/tx_manager.h"

class CoinPrice
{
    const QString coint_price_data =
        dfsStruct::ROOT_FOOLDER_NAME + "/coint_price_data.etalonium";

private:
    BigNumber ammout;
    BigNumber price;
    BigNumber userId;
    Transaction tr;
    QByteArray tokenId;

public:
    CoinPrice(const Transaction &tr);
    CoinPrice(const QByteArray &serilaized);
    ~CoinPrice();

    QByteArray serialized() const;
    void addToFile();
    void upgradeFile(QFile *);
};

#endif // COIN_PRICE_H
