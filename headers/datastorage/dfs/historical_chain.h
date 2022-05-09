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
    stdfs::path objectPath;
    DBConnector chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool apply(dfsp::EditSegmentMessage msg);
    bool remove(dfsp::EditSegmentMessage msg);
    bool revert(dfsp::EditSegmentMessage msg);
    bool update(dfsp::EditSegmentMessage msg, const int& num);
    dfsp::EditSegmentMessage getEditSegmentMessage(const int& num);
    dfsp::EditSegmentMessage getLastEditSegmentMessage();

    dfsp::EditSegmentMessage makeEditSegmentMessage(const dfsp::SegmentMessage& msg, const dfsp::SegmentMessageType& smType);
    dfsp::EditSegmentMessage makeEditSegmentMessage(const dfsp::DeleteSegmentMessage& msg, const dfsp::SegmentMessageType& smType);

private:
    DBRow makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data);
    DBRow getLastRow();
    DBRow getNextRow(const int& currentNum);
    DBRow getRow(const int& num);
    DBRow getRow(const std::string& data);
    dfsp::EditSegmentMessage segmentMessageFromDBRow(const DBRow& dbRow);
};

#endif // HISTORICAL_CHAIN_H
