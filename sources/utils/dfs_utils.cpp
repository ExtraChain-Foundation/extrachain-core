#include "utils/dfs_utils.h"

std::vector<DBRow> DFS::Tables::ActorDirFile::getFileDataByHash(DBConnector *db, std::string hash) {
    std::string query = "SELECT * FROM " + TableName + " WHERE fileHash = '" + hash + "' "
        + "OR fileHashPrev = '" + hash + "' ";
    return db->select(query);
}

std::vector<DBRow> DFS::Tables::LocalDirFile::getFileDataByHash(DBConnector *db, std::string hash) {
    std::string query = "SELECT * FROM " + TableName + " WHERE fileHash = '" + hash + "' "
        + "OR fileHashPrev = '" + hash + "' ";
    return db->select(query);
}
