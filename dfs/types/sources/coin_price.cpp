#include "dfs/types/headers/coin_price.h"

CoinPrice::CoinPrice(const Transaction &tr)
{
    this->ammout = tr.getAmount();
    this->userId = tr.getSender();
    this->tr = tr;
    // this ->price = tr.getToken();
    this->tokenId = tr.getToken().toByteArray();
}

CoinPrice::CoinPrice(const QByteArray &serilaized)
{
    if (Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).count()
        == 5)
    {
        this->ammout = BigNumber(
            Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).at(1));
        this->userId = BigNumber(
            Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).at(2));
        this->tr = Transaction(
            Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).at(3));
        this->price = BigNumber(
            Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).at(4));
        this->tokenId =
            Serialization::deserialize(serilaized, Serialization::Coin_Price_Delimiter).at(5);
    }
    else
    {
        qDebug() << "Trouble in CoinPrice constructor";
    }
}

CoinPrice::~CoinPrice()
{
}

QByteArray CoinPrice::serialized() const
{
    QList<QByteArray> list;
    list << this->userId.toByteArray() << this->ammout.toByteArray() << this->tr.serialize()
         << this->price.toByteArray() << this->tokenId;
    QByteArray serializedData =
        Serialization::serialize(list, Serialization::Coin_Price_Delimiter);
    return serializedData;
}

void CoinPrice::addToFile()
{
    QFile *file = new QFile(coint_price_data);
    QByteArray dataForSaving;
    QList<QByteArray> temp;
    temp << tokenId << serialized();
    dataForSaving = Serialization::serialize(temp, Serialization::Coin_Price_Delimiter_2);
    file->open(QIODevice::Append | QIODevice::Text);
    file->write(dataForSaving);
    file->flush();
    file->close();
}
// need tests
void CoinPrice::upgradeFile(QFile *file)
{
    QByteArray dataForSaving;
    QMap<QByteArray, QByteArray> forSorting;
    QMap<QByteArray, QByteArray>::iterator i;
    file->open(QIODevice::ReadOnly);

    while (!file->atEnd())
    {
        forSorting[Serialization::deserialize(file->readLine(),
                                              Serialization::Coin_Price_Delimiter_2)
                       .at(1)] =
            Serialization::deserialize(file->readLine(), Serialization::Coin_Price_Delimiter_2)
                .at(2);
    }

    file->close();
    if (forSorting[tokenId].isNull())
    {
        qDebug() << "add new one";
    }

    forSorting[tokenId] = serialized(); // add or change
    file->open(QIODevice::Truncate | QIODevice::Append);
    for (i = forSorting.begin(); i != forSorting.end(); ++i)
    {
        QList<QByteArray> temp;
        temp << i.key() << i.value();
        dataForSaving = Serialization::serialize(temp, Serialization::Coin_Price_Delimiter_2);
        file->write(dataForSaving);
    }
    file->flush();
    file->close();
}
