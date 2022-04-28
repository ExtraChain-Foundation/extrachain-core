#ifndef HISTORICAL_CHAIN_H
#define HISTORICAL_CHAIN_H

#include <filesystem>

#include "managers/extrachain_node.h"
#include "utils/dfs_utils.h"

namespace stdfs = std::filesystem;
namespace dfsp = DFS::Packets;
namespace dfshc = DFS::Historical;

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    stdfs::path chainPath;
    stdfs::path objectPath;
    DBConnector chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool apply(dfsp::EditSegmentMessage msg);
    bool revert(dfsp::EditSegmentMessage msg);
    bool update(dfsp::EditSegmentMessage msg);
    dfsp::EditSegmentMessage get(int num);

private:
    DBRow makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data);

private:
    DBRow getLastRow();
};

#endif // HISTORICAL_CHAIN_H
