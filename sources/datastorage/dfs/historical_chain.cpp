#include "datastorage/dfs/historical_chain.h"

HistoricalChain::HistoricalChain(std::string chainFilePath, std::string objectFilePath) {
    if (!chainFile.open(chainFilePath)) {
        exit(-1);
    }
    objectPath = objectFilePath;
    chainFile.query(DFS::Historical::CreateTableHistoricalChain);
}

HistoricalChain::~HistoricalChain() {
    chainFile.close();
}
