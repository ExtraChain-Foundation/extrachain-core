#ifndef HISTORICAL_CHAIN_H
#define HISTORICAL_CHAIN_H

#include <filesystem>

#include "managers/extrachain_node.h"
#include "utils/dfs_utils.h"

namespace stdfs = std::filesystem;
namespace dfsp = DFS::Packets;

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    stdfs::path objectPath;
    DBConnector chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool apply(dfsp::EditSegmentMessage);
    bool revert(dfsp::EditSegmentMessage);
    bool update(dfsp::EditSegmentMessage);
    dfsp::EditSegmentMessage get(int num);
};

#endif // HISTORICAL_CHAIN_H
