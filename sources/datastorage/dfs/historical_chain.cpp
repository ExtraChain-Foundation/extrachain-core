#include "datastorage/dfs/historical_chain.h"

HistoricalChain::HistoricalChain(std::string chainFilePath, std::string objectFilePath) {
    chainPath = chainFilePath;
    if (!chainFile.open(chainFilePath)) {
        exit(-1);
    }
    objectPath = objectFilePath;
    chainFile.query(DFS::Historical::CreateTableHistoricalChain);
    chainFile.close();
}

HistoricalChain::~HistoricalChain() {
    chainFile.close();
}

bool HistoricalChain::apply(DFS::Packets::EditSegmentMessage msg) {
    chainFile.open(chainPath.string());
    DBRow lastRow = getLastRow();
    uint64_t num;
    uint64_t prevNum;
    if (lastRow.empty()) {
        num = 0;
        prevNum = 0;
    } else {
        prevNum = std::stoull(lastRow.at("num"));
        num = prevNum + 1;
    }
    if (stdfs::is_directory(objectPath) && msg.Offset == 0) {
        if (chainFile.insert(dfshc::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data))) {
            chainFile.close();
            return true;
        } else {
            chainFile.close();
            return false;
        }
    } else if (stdfs::is_regular_file(objectPath)) {
        dfshc::FileChange fc;
        fc.pos = msg.Offset;
        fc.data = msg.Data;
        if (chainFile.insert(dfshc::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, fc.toStdString()))) {
            chainFile.close();
            return true;
        } else {
            chainFile.close();
            return false;
        }
    } else {
        chainFile.close();
        return false;
    }
}

DBRow HistoricalChain::makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data) {
    DBRow row;
    row.insert({ "num", std::to_string(num) });
    row.insert({ "prevNum", std::to_string(prevNum) });
    row.insert({ "type", std::to_string(type) });
    row.insert({ "data", data });
    row.insert({ "hash", Utils::calcKeccak(data) });
    return row;
}

DBRow HistoricalChain::getLastRow() {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = "SELECT * FROM " + dfshc::TableNameHC + " ORDER BY num DESC LIMIT 1";
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}
