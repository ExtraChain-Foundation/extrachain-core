#include "dfs/historical_chain.h"
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
    chainFile.query(Dfs::Historical::CreateTableHistoricalChain);
}

HistoricalChain::~HistoricalChain() {
    chainFile.close();
}

bool HistoricalChain::apply(DfsP::EditSegmentMessage msg) {
    DbRow         lastRow = getLastRow();
    std::uint64_t num;
    std::uint64_t prevNum;
    if (lastRow.empty()) {
        num     = 0;
        prevNum = 0;
    } else {
        prevNum = std::stoull(lastRow.at(NUM));
        num     = prevNum + 1;
    }
    if (std::filesystem::is_directory(objectPath) && msg.offset == 0) {
        return chainFile.insert(DfsHc::TableNameHC, makeDBRow(num, prevNum, msg.actionType, msg.data));
    } else if (std::filesystem::is_regular_file(objectPath)) {
        DfsHc::FileChange fc;
        fc.pos  = msg.offset;
        fc.data = msg.data;
        return chainFile.insert(DfsHc::TableNameHC, makeDBRow(num, prevNum, msg.actionType, fc.toString()));
    }

    return false;
}

bool HistoricalChain::remove(DfsP::EditSegmentMessage msg) {
    bool       removed     = false;
    const auto lastSegment = getLastEditSegmentMessage();

    if (msg.data == lastSegment.data) {
        DbRow         lastRow = getLastRow();
        std::uint64_t prevNum = std::stoull(lastRow.at(PREV_NUM));
        std::uint64_t num     = std::stoull(lastRow.at(NUM));
        removed = chainFile.delete_row(DfsHc::TableNameHC, makeDBRow(num, prevNum, msg.actionType, msg.data));
    } else {
        DbRow      dbRow   = getRow(msg.data);
        DbRow      nextRow = getNextRow(std::stoi(dbRow.at(NUM)));
        const bool updated = chainFile.update(fmt::format(
            "UPDATE {} SET prevNum={} WHERE hash={}",
            DfsHc::TableNameHC,
            dbRow.at(PREV_NUM),
            nextRow.at(HASH)));

        if (!updated) {
            return false;
        }

        removed = chainFile.delete_row(DfsHc::TableNameHC, dbRow);
    }
    return removed;
}

bool HistoricalChain::revert(DfsP::EditSegmentMessage msg) {
    bool reverted = true;

    DbRow       row = getRow(msg.data);
    std::string queryGetListEditSegment =
        fmt::format("SELECT * FROM {} WHERE num >= {}", DfsHc::TableNameHC, row.at(NUM));
    std::vector<DbRow> editSegmentMessageList = chainFile.select(queryGetListEditSegment, DfsHc::TableNameHC);

    for (const DbRow &row : editSegmentMessageList) {
        if (!chainFile.delete_row(DfsHc::TableNameHC, row)) {
            reverted = false;
            break;
        }
    }
    return reverted;
}

bool HistoricalChain::update(DfsP::EditSegmentMessage msg, const int &num) {
    bool                     updated                = false;
    DfsP::EditSegmentMessage editableSegmentMessage = getEditSegmentMessage(num);
    updated                                         = chainFile.update(fmt::format(
        "UPDATE {} SET type = {} data = {} hash = {} WHERE data= {}",
        DfsHc::TableNameHC,
        std::to_string(msg.actionType),
        msg.data,
        msg.hash,
        editableSegmentMessage.data));
    return updated;
}

DfsP::EditSegmentMessage HistoricalChain::getEditSegmentMessage(const int &num) {
    DbRow dbRow = getRow(num);

    if (dbRow.empty()) {
        return DfsP::EditSegmentMessage();
    }
    return segmentMessageFromDBRow(dbRow);
}

DfsP::EditSegmentMessage HistoricalChain::getLastEditSegmentMessage() {
    DbRow lastRow = getLastRow();

    if (lastRow.empty()) {
        return DfsP::EditSegmentMessage();
    }

    return segmentMessageFromDBRow(lastRow);
}

DfsP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(
    const DfsP::SegmentMessage     &msg,
    const DfsP::SegmentMessageType &smType) {
    return DfsP::EditSegmentMessage { .actorId    = msg.actorId,
                                      .fileId     = msg.fileId,
                                      .hash       = msg.hash,
                                      .data       = msg.data,
                                      .offset     = msg.offset,
                                      .actionType = smType };
}

DfsP::EditSegmentMessage HistoricalChain::makeEditSegmentMessage(
    const DfsP::DeleteSegmentMessage &msg,
    const DfsP::SegmentMessageType   &smType) {
    return DfsP::EditSegmentMessage {
        .actorId    = msg.actorId,
        .fileId     = msg.fileId,
        .hash       = msg.hash,
        .data       = "",
        .offset     = msg.offset,
        .actionType = smType,
    };
}

bool HistoricalChain::initLocal(
    const ActorId     &actor,
    const std::string &fileName,
    const std::string &fileHash) {
    std::filesystem::path filePath = DfsPath::filePath(actor, fileName);
    if (!std::filesystem::exists(filePath)) {
        return false;
        eFatal("[Dfs] No file");
    }

    std::ifstream ifs(filePath, std::ios::binary);
    ifs.seekg(0, ifs.beg);

    char                    *buffer = new char[Dfs::Basic::historicalChainSectionSize];
    DfsP::EditSegmentMessage esm { .actorId    = actor,
                                   .hash       = fileHash,
                                   .offset     = 0,
                                   .actionType = DfsP::SegmentMessageType::Insert };
    do {
        esm.data = std::move(std::string(buffer, sizeof(buffer)));
        apply(esm);
    } while (ifs.read(buffer, Dfs::Basic::historicalChainSectionSize));
    delete[] buffer;
    return true;
}

bool HistoricalChain::remove(const ActorId &actor, const std::string &fileHash) {
    std::filesystem::path filePath = DfsPath::filePath(actor, fileHash);
    if (std::filesystem::exists(chainFile.file()))
        return std::filesystem::remove(chainFile.file());
    return false;
}

bool HistoricalChain::rename(const std::string &fileHash, const std::string &newFileHash) {
    const auto path = std::filesystem::path(chainFile.file()).parent_path();
    std::filesystem::rename(
        path / std::string(fileHash + DfsF::Extension),
        path / std::string(newFileHash + DfsF::Extension));
    return std::filesystem::exists(path / std::string(newFileHash + DfsF::Extension));
}

DbRow HistoricalChain::makeDBRow(std::uint64_t num, std::uint64_t prevNum, int type, std::string data) {
    DbRow row;
    row.insert({ NUM, std::to_string(num) });
    row.insert({ PREV_NUM, std::to_string(prevNum) });
    row.insert({ TYPE, std::to_string(type) });
    row.insert({ DATA, data });
    row.insert({ HASH, Utils::calcHash(data) });
    return row;
}

DbRow HistoricalChain::getLastRow() {
    std::vector<DbRow>      res;
    std::pair<DbRow, DbRow> ret;
    std::string GetLastQuery = fmt::format("SELECT * FROM {} ORDER BY num DESC LIMIT 1", DfsHc::TableNameHC);
    res                      = chainFile.select(GetLastQuery);
    if (res.empty())
        return DbRow();
    else
        return res[0];
}

DbRow HistoricalChain::getNextRow(const int &currentNum) {
    std::vector<DbRow>      res;
    std::pair<DbRow, DbRow> ret;
    std::string             GetNextQuery = fmt::format(
        "SELECT * FROM {} WHERE num>{} ORDER BY num DESC LIMIT 1",
        DfsHc::TableNameHC,
        std::to_string(currentNum));
    res = chainFile.select(GetNextQuery);
    if (res.empty())
        return DbRow();
    else
        return res[0];
}

DbRow HistoricalChain::getRow(const int &num) {
    std::vector<DbRow>      res;
    std::pair<DbRow, DbRow> ret;
    std::string             GetLastQuery =
        fmt::format("SELECT * FROM {} WHERE num={}", DfsHc::TableNameHC, std::to_string(num));
    res = chainFile.select(GetLastQuery);
    if (res.empty())
        return DbRow();
    else
        return res[0];
}

DbRow HistoricalChain::getRow(const std::string &data) {
    std::vector<DbRow>      res;
    std::pair<DbRow, DbRow> ret;
    std::string GetLastQuery = fmt::format("SELECT * FROM {} WHERE data={}", DfsHc::TableNameHC, data);
    res                      = chainFile.select(GetLastQuery);
    if (res.empty())
        return DbRow();
    else
        return res[0];
}

DfsP::EditSegmentMessage HistoricalChain::segmentMessageFromDBRow(const DbRow &dbRow) {
    DfsP::EditSegmentMessage result;
    result.actionType = static_cast<DfsP::SegmentMessageType>(std::stoi(dbRow.at(TYPE)));

    Dfs::Historical::FileChange fc;
    fc.fromStdString(dbRow.at(DATA));
    result.data    = fc.data;
    result.offset  = fc.pos;
    result.hash    = dbRow.at(HASH);
    result.actorId = "";
    return result;
}
