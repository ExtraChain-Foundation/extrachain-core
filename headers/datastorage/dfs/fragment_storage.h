#ifndef FRAGMENT_STORAGE_H
#define FRAGMENT_STORAGE_H

#include "extrachain_global.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include "utils/dfs_utils.h"

class EXTRACHAIN_EXPORT FragmentStorage {
private:
    DBConnector storageFile;
    ActorId actor;
    std::string fileName;
    std::string fileHash;

public:
    FragmentStorage(ActorId Actor, std::string FileName, std::string FileHash);
    FragmentStorage(DFSP::SegmentMessage segmentMessage);
    ~FragmentStorage() = default;

    bool initLocalFile(uint64_t filesize);
    bool insertFragment(DFSP::SegmentMessage msg);
    bool editFragment(DFSP::EditSegmentMessage msg);
    bool removeFragment(DFSP::DeleteSegmentMessage msg);
    DFSP::SegmentMessage getFragment(uint64_t pos);
    bool applyChanges(const std::string &data, uint64_t pos);

private:
    DBRow getPreviousFragment(uint64_t number);
    DBRow getNextFragment(uint64_t number);
    DBRow getRealPreviousFragment(uint64_t number);
    DBRow getRealNextFragment(uint64_t number);
    std::pair<DBRow, DBRow> getPrevNextPairFragment(uint64_t number);
    DBRow makeFragmentRow(DFSP::SegmentMessage msg, uint64_t storedPos);
    DBRow makeFragmentRow(uint64_t pos, uint64_t storedPos, uint64_t size);
    uint64_t writeFragment(DFSP::SegmentMessage msg);
    void moveRows(DBRow curRow, uint64_t moveSize);
    uint64_t write(std::filesystem::path filePath, uint64_t pos, std::string data);
    std::string extract(std::filesystem::path filePath, uint64_t pos, uint64_t size);
    uint64_t remove(std::filesystem::path filePath, uint64_t pos, uint64_t size);
    bool checkRenameFile(const DFS::Packets::EditSegmentMessage &msg);
};

#endif // FRAGMENT_STORAGE_H
