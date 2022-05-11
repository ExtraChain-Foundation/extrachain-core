#include "datastorage/dfs/historical_chain.h"
#include <fstream>

HistoricalChain::HistoricalChain(std::string chainFilePath, std::string objectFilePath)
    : chainFile(chainFilePath) {
    if (!chainFile.open()) {
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
    chainFile.open();
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

bool HistoricalChain::remove(dfsp::EditSegmentMessage msg) {
    bool removed = false;
    chainFile.open();
    const auto lastSegment = getLastEditSegmentMessage();

    if (msg.Data == lastSegment.Data) {
        DBRow lastRow = getLastRow();
        uint64_t prevNum = std::stoull(lastRow.at("prevNum"));
        uint64_t num = std::stoull(lastRow.at("num"));
        removed = chainFile.deleteRow(dfshc::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data));
    } else {
        DBRow dbRow = getRow(msg.Data);
        DBRow nextRow = getNextRow(std::stoi(dbRow.at("num")));
        const bool updated = chainFile.update("UPDATE " + dfshc::TableNameHC + "SET prevNum="
                                              + dbRow.at("prevNum") + "WHERE hash=" + nextRow.at("hash"));

        if (!updated) {
            chainFile.close();
            return false;
        }

        removed = chainFile.deleteRow(dfshc::TableNameHC, dbRow);
    }
    chainFile.close();
    return removed;
}

bool HistoricalChain::revert(dfsp::EditSegmentMessage msg) {
    bool reverted = true;
    chainFile.open();

    DBRow row = getRow(msg.Data);
    std::string queryGetListEditSegment =
        "SELECT * FROM " + dfshc::TableNameHC + " WHERE num >=" + row.at("num");
    std::vector<DBRow> editSegmentMessageList = chainFile.select(queryGetListEditSegment, dfshc::TableNameHC);

    for (const DBRow &row : editSegmentMessageList) {
        if (!chainFile.deleteRow(dfshc::TableNameHC, row)) {
            reverted = false;
            break;
        }
    }
    chainFile.close();
    return reverted;
}

bool HistoricalChain::update(dfsp::EditSegmentMessage msg, const int &num) {
    bool updated = false;
    chainFile.open();
    DFS::Packets::EditSegmentMessage editableSegmentMessage = getEditSegmentMessage(num);
    updated = chainFile.update("UPDATE " + dfshc::TableNameHC + " SET "
                               + " type = " + std::to_string(msg.ActionType) + " data = " + msg.Data
                               + " hash = " + msg.FileHash + " WHERE data=" + editableSegmentMessage.Data);
    chainFile.close();
    return updated;
}

DFS::Packets::EditSegmentMessage HistoricalChain::getEditSegmentMessage(const int &num) {
    chainFile.open();
    DBRow dbRow = getRow(num);
    chainFile.close();

    if (dbRow.empty()) {
        return DFS::Packets::EditSegmentMessage();
    }
    return segmentMessageFromDBRow(dbRow);
}

DFS::Packets::EditSegmentMessage HistoricalChain::getLastEditSegmentMessage() {
    chainFile.open();
    DBRow lastRow = getLastRow();
    chainFile.close();

    if (lastRow.empty()) {
        return DFS::Packets::EditSegmentMessage();
    }

    return segmentMessageFromDBRow(lastRow);
}

dfsp::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(const dfsp::SegmentMessage &msg,
                                                                 const dfsp::SegmentMessageType &smType) {
    return dfsp::EditSegmentMessage {
        .Actor = msg.Actor,
        .FileHash = msg.FileHash,
        .Data = msg.Data,
        .Offset = msg.Offset,
        .ActionType = smType,
    };
}

dfsp::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(const dfsp::DeleteSegmentMessage &msg,
                                                                 const dfsp::SegmentMessageType &smType) {
    return dfsp::EditSegmentMessage {
        .Actor = msg.Actor,
        .FileHash = msg.FileHash,
        .Data = "",
        .Offset = msg.Offset,
        .ActionType = smType,
    };
}

bool HistoricalChain::initLocal(const std::string &actor, const std::string &fileHash) {
    std::filesystem::path filePath = DFS::Path::filePath(actor, fileHash);
    if (!std::filesystem::exists(filePath)) {
        return false;
        qFatal("[Dfs] No file");
    }

    std::ifstream ifs(filePath.c_str(), std::ios::binary);
    char buf[DFS::Basic::historicalChainSectionSize];

    if (!ifs.read(buf, sizeof(buf)) || !ifs.gcount()) {
        ifs.close();
        return false;
    }

    while (ifs.read(buf, sizeof(buf)) || ifs.gcount()) {
        std::string data(buf, ifs.gcount());
        apply(dfsp::EditSegmentMessage { .Actor = actor,
                                         .FileHash = fileHash,
                                         .Data = data,
                                         .Offset = 0,
                                         .ActionType = dfsp::SegmentMessageType::insert });
    }
    ifs.close();
    return true;
}

DBRow HistoricalChain::makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data) {
    DBRow row;
    row.insert({ "num", std::to_string(num) });
    row.insert({ "prevNum", std::to_string(prevNum) });
    row.insert({ "type", std::to_string(type) });
    row.insert({ "data", data });
    row.insert({ "hash", Utils::calcHash(data) });
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

DBRow HistoricalChain::getNextRow(const int &currentNum) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetNextQuery = "SELECT * FROM " + dfshc::TableNameHC + " WHERE num>"
        + std::to_string(currentNum) + " ORDER BY num DESC LIMIT 1";
    res = chainFile.select(GetNextQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getRow(const int &num) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = "SELECT * FROM " + dfshc::TableNameHC + " WHERE num=" + std::to_string(num);
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getRow(const std::string &data) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = "SELECT * FROM " + dfshc::TableNameHC + " WHERE data=" + data;
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

dfsp::EditSegmentMessage HistoricalChain::segmentMessageFromDBRow(const DBRow &dbRow) {
    DFS::Packets::EditSegmentMessage result;
    result.ActionType = static_cast<DFS::Packets::SegmentMessageType>(std::stoi(dbRow.at("type")));

    DFS::Historical::FileChange fc;
    fc.fromStdString(dbRow.at("data"));
    result.Data = fc.data;
    result.Offset = fc.pos;
    result.FileHash = dbRow.at("hash");
    result.Actor = "";
    return result;
}
