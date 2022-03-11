#ifndef BLOCKCHAIN2_H
#define BLOCKCHAIN2_H
// INCLUDE
#include "datastorage/actor.h"
#include "datastorage/block.h"
#include "datastorage/genesis_block.h"
#include "datastorage/index/actorindex.h"
#include "datastorage/transaction.h"
#include "managers/account_controller.h"
#include "utils/db_connector.h"

#include <filesystem>
#include <string>
#include <vector>
namespace sfs = std::filesystem;
class EXTRACHAIN_EXPORT Blockchain2 : public QObject {
    Q_OBJECT
private:
    ActorIndex *actorIndex;
    AccountController *accountController;

private:
    int sectionSize = Config::DataStorage::SECTION_SIZE;
    BigNumber sizeLimitBytes = -1;

private:
    BigNumber records = 0;
    BigNumber fistSavedId = -1;
    BigNumber lastSavedId = -1;

public:
    Blockchain2(int mode, AccountController *AccountController, ActorIndex *ActorIndex);
    ~Blockchain2();

public:
    int addBlock(const Block &block, bool isGenesis = false);
    int writeBlock(sfs::path path, const Block &block, bool isGenesis = false);
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

private:
    int writeGenBlock(sfs::path path, const GenesisBlock &block);
    int writeOrdBlock(sfs::path path, const Block &block);
    std::string buildBlockFilePath(const Block &block);
    bool isLimitReached();
};

#endif // BLOCKCHAIN2_H
