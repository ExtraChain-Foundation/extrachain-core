#ifndef MEMBLOCK_H
#define MEMBLOCK_H

#include <QString>
#include <QDebug>
#include "enc/sign_interface.h"
#include "utils/bignumber.h"
#include "utils/utils.h"
#include "datastorage/transaction.h"
#include "actor.h"

// Block comparison result
struct BlockCompare
{
    BigNumber indexDiff;
    BigNumber approverDiff;
    int dataDiff;
    int prevHashDiff;
    int hashDiff;
    int digitalSigDiff;
};

namespace Config {
static const QByteArray DATA_BLOCK_TYPE = "data";
}

class Block
{

protected:
    const int FIELDS_SIZE = 4;
    QByteArray type; // simple block, or genesis block (or other)
    QByteArray data; // payload (serialized tx's, or other)
private:
    BigNumber index = BigNumber(-1);    // block id
    BigNumber approver = BigNumber(-1); // block approver id

    long long date;
    QByteArray prevHash; // previous block hash
    QByteArray hash;     // this block hash (from all previous fields)
    QByteArray digSig;   // digital signature (from all fields)
public:
    Block();
    // Copy constructor
    Block(const Block &block);
    // Deserialize already constructed block
    Block(const QByteArray &serialized);
    // Initial block construction, prev = nullptr for first block
    Block(const QByteArray &data, const Block *prev);

    virtual ~Block();

private:
    /**
     * Calculates hash of this block and writes hash to "hash" variable.
     * Uses keccak.
     */
    void calcHash();

protected:
    /**
     * @brief Concatenates all fields that are used for digSig calculation
     * Override in subclasses
     * @return digSig data
     */
    virtual QByteArray getDataForHash() const;
    virtual QByteArray getDataForDigSig() const;

public:
    // data operations

    /**
     * @brief add data to this block
     * @param data
     */
    void addData(const QByteArray &data);
    /**
     * @brief extract non-empty transactions from data
     * @return transaction list
     */
    QList<Transaction> extractTransactions() const;

    bool contain(Block &from) const;

    // digital signature
    virtual void sign(const Actor<KeyPrivate> &actor) final;
    virtual bool verify(const Actor<KeyPublic> &actor) const final;

    // serialization

    virtual QByteArray serialize() const;
    virtual bool deserialize(const QByteArray &serialized);

    bool equals(const Block &block) const;
    BlockCompare compareBlock(const Block &b) const;
    bool isEmpty() const;
    QString toString() const;
    bool operator<(const Block &other);
    static bool isBlock(const QByteArray &data);

public:
    QList<Block> getDataFromAllBlocks(QList<QByteArray>);
    void setPrevHash(const QByteArray &value);
    QByteArray getType() const;
    BigNumber getApprover() const;
    BigNumber getIndex() const;
    QByteArray getData() const;
    QByteArray getHash() const;
    QByteArray getPrevHash() const;
    QByteArray getDigSig() const;

    // void setType(QByteArray type);
    long long getDate() const;
    void setDate(long long value);
    Block operator=(const Block &block);
};

inline bool operator<(const Block &l, const Block &r)
{
    return l.getIndex() < r.getIndex() || l.getData() < r.getData();
}

inline bool operator==(const Block &l, const Block &r)
{
    return l.getIndex() == r.getIndex() && l.getApprover() == r.getApprover() && l.getData() == r.getData()
        && l.getPrevHash() == r.getPrevHash() && l.getHash() == r.getHash() && l.getDigSig() == r.getDigSig();
}

#endif // MEMBLOCK_H
