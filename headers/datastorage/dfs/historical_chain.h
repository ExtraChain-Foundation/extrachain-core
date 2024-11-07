#ifndef HISTORICAL_CHAIN_H
#define HISTORICAL_CHAIN_H

#include <filesystem>

#include "managers/extrachain_node.h"
#include "utils/dfs_utils.h"

class EXTRACHAIN_EXPORT HistoricalChain {
private:
    std::filesystem::path objectPath;
    DBConnector           chainFile;

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
    DBRow                    makeDBRow(std::uint64_t num, std::uint64_t prevNum, int type, std::string data);
    DBRow                    getLastRow();
    DBRow                    getNextRow(const int& currentNum);
    DBRow                    getRow(const int& num);
    DBRow                    getRow(const std::string& data);
    DfsP::EditSegmentMessage segmentMessageFromDBRow(const DBRow& dbRow);
};

class HistoricalChainSql {
private:
    std::filesystem::path file_path;
    std::filesystem::path history_path;

    HistoricalChainSql(const std::filesystem::path& path) {
        this->file_path    = path;
        this->history_path = fmt::format("{}.history", path, ".history");
    }

public:
    static void create(const std::filesystem::path& path) {
        HistoricalChainSql chain(path);

        DBConnector db(chain.history_path);
        if (!db.open()) {
            eFatal("[History] Can't create historical database");
        }

        using namespace sqlite::literals;
        auto history_schema = DbSchema("historical_chain");
        history_schema.add_columns(
            "id"_text.primary_key(),
            "prevId"_text.unique().not_null(),
            "operation"_text.not_null().one_of("INSERT", "UPDATE", "REMOVE"),
            "data"_json.not_null(),
            "timestamp"_int.not_null(),
            "actorId"_text,
            "sign"_blob);
        db.createTable(history_schema);
    }

    // std::expected<std::vector<DBRow>, std::string> getHistory() {
    //     DBConnector db(this->file_path);
    //     if (!db.open()) {
    //         return std::unexpected("Can't open database");
    //     }

    //     std::vector<DBRow> history;
    //     db.select("SELECT * FROM historical_chain", history);
    //     return history;
    // }

    // void insert() {
    //     DBConnector db(this->file_path);
    //     if (!db.open()) {
    //         eFatal("[History] Can't open database");
    //     }

    //     DBConnector history_db(this->history_path);
    //     if (!history_db.open()) {
    //         eFatal("[History] Can't open history database");
    //     }

    //     std::vector<DBRow> history;
    //     db.select("SELECT * FROM historical_chain", history);
    //     for (auto& row : history) {
    //         history_db.insert("INSERT INTO historical_chain VALUES (?, ?, ?, ?, ?, ?, ?)", row);
    //     }
    // }

    // void insert(const DBRow& row) {
    //     DBConnector db(this->history_path);
    //     if (!db.open()) {
    //         eFatal("[History] Can't open database");
    //     }

    //     db.insert("INSERT INTO historical_chain VALUES (?, ?, ?, ?, ?, ?, ?)", row);
    // }
};

#endif // HISTORICAL_CHAIN_H
