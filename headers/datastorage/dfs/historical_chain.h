#ifndef HISTORICAL_CHAIN_H
#define HISTORICAL_CHAIN_H

#include <filesystem>

#include "managers/extrachain_node.h"
#include "utils/dfs_utils.h"

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    std::filesystem::path objectPath;
    DbConnector           chainFile;

public:
    HistoricalChain(std::string chainFilePath, std::string objectFilePath);
    ~HistoricalChain();

public:
    bool                     apply(DfsP::EditSegmentMessage msg);
    bool                     remove(DfsP::EditSegmentMessage msg);
    bool                     revert(DfsP::EditSegmentMessage msg);
    bool                     update(DfsP::EditSegmentMessage msg, const int& num);
    DfsP::EditSegmentMessage getEditSegmentMessage(const int& num);
    DfsP::EditSegmentMessage getLastEditSegmentMessage();

    DfsP::EditSegmentMessage
    makeEditSegmentMessage(const DfsP::SegmentMessage& msg, const DfsP::SegmentMessageType& smType);
    DfsP::EditSegmentMessage
    makeEditSegmentMessage(const DfsP::DeleteSegmentMessage& msg, const DfsP::SegmentMessageType& smType);

    bool initLocal(const ActorId& actor, const std::string& fileName, const std::string& fileHash);
    bool remove(const ActorId& actor, const std::string& fileHash);
    bool rename(const std::string& fileHash, const std::string& newFileHash);

private:
    DbRow                    makeDBRow(std::uint64_t num, std::uint64_t prevNum, int type, std::string data);
    DbRow                    getLastRow();
    DbRow                    getNextRow(const int& currentNum);
    DbRow                    getRow(const int& num);
    DbRow                    getRow(const std::string& data);
    DfsP::EditSegmentMessage segmentMessageFromDBRow(const DbRow& dbRow);
};

#endif // HISTORICAL_CHAIN_H
