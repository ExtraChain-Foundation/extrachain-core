#include "datastorage/dfs/historical_chain.h"
#include <fstream>

static const std::string NUM      = "num";
static const std::string PREV_NUM = "prevNum";
static const std::string TYPE     = "type";
static const std::string DATA     = "data";
static const std::string HASH     = "hash";

HistoricalChain::HistoricalChain(std::string chainFilePath, std::string objectFilePath)
    : chainFile(chainFilePath) {
    if (!chainFile.open()) {
        exit(-1);
    }
    objectPath = objectFilePath;
    chainFile.query(DFS::Historical::CreateTableHistoricalChain);
}

HistoricalChain::~HistoricalChain() {
    chainFile.close();
}

bool HistoricalChain::apply(DFSP::EditSegmentMessage msg) {
    DBRow    lastRow = getLastRow();
    uint64_t num;
    uint64_t prevNum;
    if (lastRow.empty()) {
        num     = 0;
        prevNum = 0;
    } else {
        prevNum = std::stoull(lastRow.at(NUM));
        num     = prevNum + 1;
    }
    if (STDFS::is_directory(objectPath) && msg.Offset == 0) {
        return chainFile.insert(DFSHC::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data));
    } else if (STDFS::is_regular_file(objectPath)) {
        DFSHC::FileChange fc;
        fc.pos  = msg.Offset;
        fc.data = msg.Data;
        return chainFile.insert(
            DFSHC::TableNameHC,
            makeDBRow(num, prevNum, msg.ActionType, fc.toStdString()));
    }

    return false;
}

bool HistoricalChain::remove(DFSP::EditSegmentMessage msg) {
    bool       removed     = false;
    const auto lastSegment = getLastEditSegmentMessage();

    if (msg.Data == lastSegment.Data) {
        DBRow    lastRow = getLastRow();
        uint64_t prevNum = std::stoull(lastRow.at(PREV_NUM));
        uint64_t num     = std::stoull(lastRow.at(NUM));
        removed = chainFile.deleteRow(DFSHC::TableNameHC, makeDBRow(num, prevNum, msg.ActionType, msg.Data));
    } else {
        DBRow      dbRow   = getRow(msg.Data);
        DBRow      nextRow = getNextRow(std::stoi(dbRow.at(NUM)));
        const bool updated = chainFile.update(fmt::format(
            "UPDATE {} SET prevNum={} WHERE hash={}",
            DFSHC::TableNameHC,
            dbRow.at(PREV_NUM),
            nextRow.at(HASH)));

        if (!updated) {
            return false;
        }

        removed = chainFile.deleteRow(DFSHC::TableNameHC, dbRow);
    }
    return removed;
}

bool HistoricalChain::revert(DFSP::EditSegmentMessage msg) {
    bool reverted = true;

    DBRow       row = getRow(msg.Data);
    std::string queryGetListEditSegment =
        fmt::format("SELECT * FROM {} WHERE num >= {}", DFSHC::TableNameHC, row.at(NUM));
    std::vector<DBRow> editSegmentMessageList = chainFile.select(queryGetListEditSegment, DFSHC::TableNameHC);

    for (const DBRow &row : editSegmentMessageList) {
        if (!chainFile.deleteRow(DFSHC::TableNameHC, row)) {
            reverted = false;
            break;
        }
    }
    return reverted;
}

bool HistoricalChain::update(DFSP::EditSegmentMessage msg, const int &num) {
    bool                     updated                = false;
    DFSP::EditSegmentMessage editableSegmentMessage = getEditSegmentMessage(num);
    updated                                         = chainFile.update(fmt::format(
        "UPDATE {} SET type = {} data = {} hash = {} WHERE data= {}",
        DFSHC::TableNameHC,
        std::to_string(msg.ActionType),
        msg.Data,
        msg.FileHash,
        editableSegmentMessage.Data));
    return updated;
}

DFSP::EditSegmentMessage HistoricalChain::getEditSegmentMessage(const int &num) {
    DBRow dbRow = getRow(num);

    if (dbRow.empty()) {
        return DFSP::EditSegmentMessage();
    }
    return segmentMessageFromDBRow(dbRow);
}

DFSP::EditSegmentMessage HistoricalChain::getLastEditSegmentMessage() {
    DBRow lastRow = getLastRow();

    if (lastRow.empty()) {
        return DFSP::EditSegmentMessage();
    }

    return segmentMessageFromDBRow(lastRow);
}

DFSP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(
    const DFSP::SegmentMessage     &msg,
    const DFSP::SegmentMessageType &smType) {
    return DFSP::EditSegmentMessage { .Actor      = msg.Actor,
                                      .FileName   = msg.FileName,
                                      .FileHash   = msg.FileHash,
                                      .Data       = msg.Data,
                                      .Offset     = msg.Offset,
                                      .ActionType = smType };
}

DFSP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(
    const DFSP::DeleteSegmentMessage &msg,
    const DFSP::SegmentMessageType   &smType) {
    return DFSP::EditSegmentMessage {
        .Actor      = msg.Actor,
        .FileName   = msg.FileName,
        .FileHash   = msg.FileHash,
        .Data       = "",
        .Offset     = msg.Offset,
        .ActionType = smType,
    };
}

bool HistoricalChain::initLocal(
    const ActorId     &actor,
    const std::string &fileName,
    const std::string &fileHash) {
    std::filesystem::path filePath = DFS_PATH::filePath(actor, fileName);
    if (!std::filesystem::exists(filePath)) {
        return false;
        qFatal("[Dfs] No file");
    }

    std::ifstream ifs(filePath, std::ios::binary);
    ifs.seekg(0, ifs.beg);

    char                    *buffer = new char[DFS::Basic::historicalChainSectionSize];
    DFSP::EditSegmentMessage esm { .Actor      = actor,
                                   .FileHash   = fileHash,
                                   .Offset     = 0,
                                   .ActionType = DFSP::SegmentMessageType::insert };
    do {
        esm.Data = std::move(std::string(buffer, sizeof(buffer)));
        apply(esm);
    } while (ifs.read(buffer, DFS::Basic::historicalChainSectionSize));
    delete[] buffer;
    return true;
}

bool HistoricalChain::remove(const ActorId &actor, const std::string &fileHash) {
    std::filesystem::path filePath = DFS_PATH::filePath(actor, fileHash);
    if (std::filesystem::exists(chainFile.file()))
        return std::filesystem::remove(chainFile.file());
    return false;
}

bool HistoricalChain::rename(const std::string &fileHash, const std::string &newFileHash) {
    const auto path = std::filesystem::path(chainFile.file()).parent_path();
    std::filesystem::rename(
        path / std::string(fileHash + DFSF::Extension),
        path / std::string(newFileHash + DFSF::Extension));
    return std::filesystem::exists(path / std::string(newFileHash + DFSF::Extension));
}

DBRow HistoricalChain::makeDBRow(uint64_t num, uint64_t prevNum, int type, std::string data) {
    DBRow row;
    row.insert({ NUM, std::to_string(num) });
    row.insert({ PREV_NUM, std::to_string(prevNum) });
    row.insert({ TYPE, std::to_string(type) });
    row.insert({ DATA, data });
    row.insert({ HASH, Utils::calcHash(data) });
    return row;
}

DBRow HistoricalChain::getLastRow() {
    std::vector<DBRow>      res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = fmt::format("SELECT * FROM {} ORDER BY num DESC LIMIT 1", DFSHC::TableNameHC);
    res                      = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getNextRow(const int &currentNum) {
    std::vector<DBRow>      res;
    std::pair<DBRow, DBRow> ret;
    std::string             GetNextQuery = fmt::format(
        "SELECT * FROM {} WHERE num>{} ORDER BY num DESC LIMIT 1",
        DFSHC::TableNameHC,
        std::to_string(currentNum));
    res = chainFile.select(GetNextQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getRow(const int &num) {
    std::vector<DBRow>      res;
    std::pair<DBRow, DBRow> ret;
    std::string             GetLastQuery =
        fmt::format("SELECT * FROM {} WHERE num={}", DFSHC::TableNameHC, std::to_string(num));
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DBRow HistoricalChain::getRow(const std::string &data) {
    std::vector<DBRow>      res;
    std::pair<DBRow, DBRow> ret;
    std::string GetLastQuery = fmt::format("SELECT * FROM {} WHERE data={}", DFSHC::TableNameHC, data);
    res                      = chainFile.select(GetLastQuery);
    if (res.empty())
        return DBRow();
    else
        return res[0];
}

DFSP::EditSegmentMessage HistoricalChain::segmentMessageFromDBRow(const DBRow &dbRow) {
    DFSP::EditSegmentMessage result;
    result.ActionType = static_cast<DFSP::SegmentMessageType>(std::stoi(dbRow.at(TYPE)));

    DFS::Historical::FileChange fc;
    fc.fromStdString(dbRow.at(DATA));
    result.Data     = fc.data;
    result.Offset   = fc.pos;
    result.FileHash = dbRow.at(HASH);
    result.Actor    = "";
    return result;
}
