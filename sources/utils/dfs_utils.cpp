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

std::string DFS::Path::convertPathToPlatform(std::string path) {
    std::string ret;
    if (path.find(Utils::getPlatformDelimeterDFS()) == std::string::npos) {
        ret = boost::replace_all_copy(path, "/", Utils::getPlatformDelimeterDFS());
    }
    return ret;
}
