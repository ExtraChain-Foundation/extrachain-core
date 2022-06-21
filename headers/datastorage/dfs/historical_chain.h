#ifndef HISTORICAL_CHAIN_H
#define HISTORICAL_CHAIN_H

#include <filesystem>

#include "managers/extrachain_node.h"
#include "utils/dfs_utils.h"

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    STDFS::path objectPath;
    DBConnector chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool apply(DFSP::EditSegmentMessage msg);
    bool remove(DFSP::EditSegmentMessage msg);
    bool revert(DFSP::EditSegmentMessage msg);
    bool update(DFSP::EditSegmentMessage msg, const int& num);
    DFSP::EditSegmentMessage getEditSegmentMessage(const int& num);
    DFSP::EditSegmentMessage getLastEditSegmentMessage();

    DFSP::EditSegmentMessage makeEditSegmentMessage(const DFSP::SegmentMessage& msg,
                                                    const DFSP::SegmentMessageType& smType);
    DFSP::EditSegmentMessage makeEditSegmentMessage(const DFSP::DeleteSegmentMessage& msg,
                                                    const DFSP::SegmentMessageType& smType);

    bool initLocal(const std::string& actor, const std::string& fileName, const std::string& fileHash);
    bool remove(const std::string& actor, const std::string& fileHash);
    bool rename(const std::string& fileHash, const std::string& newFileHash);

private:
    DBRow makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data);
    DBRow getLastRow();
    DBRow getNextRow(const int& currentNum);
    DBRow getRow(const int& num);
    DBRow getRow(const std::string& data);
    DFSP::EditSegmentMessage segmentMessageFromDBRow(const DBRow& dbRow);
};

#endif // HISTORICAL_CHAIN_H
