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

bool HistoricalChain::apply(DFSP::EditSegmentMessage msg) {
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
    if (STDFS::is_directory(objectPath) && msg.Offset == 0) {
        if (chainFile.insert(DFSHC::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data))) {
            chainFile.close();
            return true;
        } else {
            chainFile.close();
            return false;
        }
    } else if (STDFS::is_regular_file(objectPath)) {
        DFSHC::FileChange fc;
        fc.pos = msg.Offset;
        fc.data = msg.Data;
        if (chainFile.insert(DFSHC::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, fc.toStdString()))) {
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

bool HistoricalChain::remove(DFSP::EditSegmentMessage msg) {
    bool removed = false;
    chainFile.open();
    const auto lastSegment = getLastEditSegmentMessage();

    if (msg.Data == lastSegment.Data) {
        DBRow lastRow = getLastRow();
        uint64_t prevNum = std::stoull(lastRow.at("prevNum"));
        uint64_t num = std::stoull(lastRow.at("num"));
        removed = chainFile.deleteRow(DFSHC::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data));
    } else {
        DBRow dbRow = getRow(msg.Data);
        DBRow nextRow = getNextRow(std::stoi(dbRow.at("num")));
        const bool updated = chainFile.update("UPDATE " + DFSHC::TableNameHC + "SET prevNum="
                                              + dbRow.at("prevNum") + "WHERE hash=" + nextRow.at("hash"));

        if (!updated) {
            chainFile.close();
            return false;
        }

        removed = chainFile.deleteRow(DFSHC::TableNameHC, dbRow);
    }
    chainFile.close();
    return removed;
}

bool HistoricalChain::revert(DFSP::EditSegmentMessage msg) {
    bool reverted = true;
    chainFile.open();

    DBRow row = getRow(msg.Data);
    std::string queryGetListEditSegment =
        "SELECT * FROM " + DFSHC::TableNameHC + " WHERE num >=" + row.at("num");
    std::vector<DBRow> editSegmentMessageList = chainFile.select(queryGetListEditSegment, DFSHC::TableNameHC);

    for (const DBRow &row : editSegmentMessageList) {
        if (!chainFile.deleteRow(DFSHC::TableNameHC, row)) {
            reverted = false;
            break;
        }
    }
    chainFile.close();
    return reverted;
}

bool HistoricalChain::update(DFSP::EditSegmentMessage msg, const int &num) {
    bool updated = false;
    chainFile.open();
    DFSP::EditSegmentMessage editableSegmentMessage = getEditSegmentMessage(num);
    updated = chainFile.update("UPDATE " + DFSHC::TableNameHC + " SET "
                               + " type = " + std::to_string(msg.ActionType) + " data = " + msg.Data
                               + " hash = " + msg.FileHash + " WHERE data=" + editableSegmentMessage.Data);
    chainFile.close();
    return updated;
}

DFSP::EditSegmentMessage HistoricalChain::getEditSegmentMessage(const int &num) {
    chainFile.open();
    DBRow dbRow = getRow(num);
    chainFile.close();

    if (dbRow.empty()) {
        return DFSP::EditSegmentMessage();
    }
    return segmentMessageFromDBRow(dbRow);
}

DFSP::EditSegmentMessage HistoricalChain::getLastEditSegmentMessage() {
    chainFile.open();
    DBRow lastRow = getLastRow();
    chainFile.close();

    if (lastRow.empty()) {
        return DFSP::EditSegmentMessage();
    }

    return segmentMessageFromDBRow(lastRow);
}

DFSP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(const DFSP::SegmentMessage &msg,
                                                                 const DFSP::SegmentMessageType &smType) {
    return DFSP::EditSegmentMessage { .Actor = msg.Actor,
                                      .FileName = msg.FileName,
                                      .FileHash = msg.FileHash,
                                      .Data = msg.Data,
                                      .Offset = msg.Offset,
                                      .ActionType = smType };
}

DFSP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(const DFSP::DeleteSegmentMessage &msg,
                                                                 const DFSP::SegmentMessageType &smType) {
    return DFSP::EditSegmentMessage {
        .Actor = msg.Actor,
        .FileName = msg.FileName,
        .FileHash = msg.FileHash,
        .Data = "",
        .Offset = msg.Offset,
        .ActionType = smType,
    };
}

bool HistoricalChain::initLocal(const std::string &actor, const std::string &fileName,
                                const std::string &fileHash) {
    std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
    if (!std::filesystem::exists(filePath)) {
        return false;
        qFatal("[Dfs] No file");
    }

    std::ifstream ifs(filePath, std::ios::binary);
    ifs.seekg(0, std::ios::beg);
    std::vector<char> buffer(DFS::Basic::historicalChainSectionSize);
    do {
        std::string data(buffer.data(), buffer.size());
        apply(DFSP::EditSegmentMessage { .Actor = actor,
                                         .FileHash = fileHash,
                                         .Data = data,
                                         .Offset = 0,
                                         .ActionType = DFSP::SegmentMessageType::insert });
    } while (ifs.read(buffer.data(), DFS::Basic::historicalChainSectionSize));
    ifs.close();
    return true;
}

bool HistoricalChain::remove(const std::string &actor, const std::string &fileHash) {
    std::filesystem::path filePath = DFS_PATH::filePath(actor, fileHash);
    if (std::filesystem::exists(chainFile.file()))
        return std::filesystem::remove(chainFile.file());
    return false;
}

bool HistoricalChain::rename(const std::string &fileHash, const std::string &newFileHash) {
    const auto path = std::filesystem::path(chainFile.file()).parent_path();
    std::filesystem::rename(path / std::string(fileHash + DFSF::Extension),
                            path / std::string(newFileHash + DFSF::Extension));
    return std::filesystem::exists(path / std::string(newFileHash + DFSF::Extension));
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
    std::string GetLastQuery = "SELECT * FROM " + DFSHC::TableNameHC + " ORDER BY num DESC LIMIT 1";
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getNextRow(const int &currentNum) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetNextQuery = "SELECT * FROM " + DFSHC::TableNameHC + " WHERE num>"
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
    std::string GetLastQuery = "SELECT * FROM " + DFSHC::TableNameHC + " WHERE num=" + std::to_string(num);
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getRow(const std::string &data) {
    std::vector<DBRow> res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = "SELECT * FROM " + DFSHC::TableNameHC + " WHERE data=" + data;
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DFSP::EditSegmentMessage HistoricalChain::segmentMessageFromDBRow(const DBRow &dbRow) {
    DFSP::EditSegmentMessage result;
    result.ActionType = static_cast<DFSP::SegmentMessageType>(std::stoi(dbRow.at("type")));

    DFS::Historical::FileChange fc;
    fc.fromStdString(dbRow.at("data"));
    result.Data = fc.data;
    result.Offset = fc.pos;
    result.FileHash = dbRow.at("hash");
    result.Actor = "";
    return result;
}
