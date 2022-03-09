#ifndef BLOCKCHAIN_H
#define BLOCKCHAIN_H
// INCLUDE
#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "utils/db_connector.h"

#include <string>
#include <vector>

class EXTRACHAIN_EXPORT Blockchain2 : public QObject {
    Q_OBJECT
private:
public:
    Blockchain2(AccountController &accountController);
    ~Blockchain2();

public:
    int addBlock(const Block &block, bool isGenesis = false);
    Block getBlock(BigNumber id);
    int removeBlock(BigNumber id);
    int verifyBlock(Block block);
    /**
     * @brief startBlockMerge - Run block merging algorithm to unite main- and sub-chains
     * @param external block from sub-chain
     * @return operation success
     */
    int startBlockMerge(const Block &extBlock);
    int areMergable(const Block &blockTarget, const Block &blockSource);
};

#endif // BLOCKCHAIN_H
