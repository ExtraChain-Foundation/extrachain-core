#ifndef DFS_CONTROLLER_H
#define DFS_CONTROLLER_H

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QObject>

#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <filesystem>

#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

class EXTRACHAIN_EXPORT DfsController : public QObject {
    Q_OBJECT

private:
    ExtraChainNode &node;
    unsigned long long m_bytesLimit = 8589934592;
    unsigned long long m_sizeTaken = 0;
    std::map<std::string, DFS::Packets::AddFileMessage> files;

public:
    DfsController(ExtraChainNode &node, QObject *parent = nullptr);
    ~DfsController();

    // Internal use only
    std::string addLocalFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                             std::string targetVirtualFilePath, DFS::Encryption securityLevel);
    void removeLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath);

    // External interfaces
    std::string addFile(const DFS::Packets::AddFileMessage &msg, bool loadBytes);
    std::string getFileFromStorage(ActorId owner, std::string fileHash);
    bool removeFile(const DFS::Packets::RemoveFileMessage &msg);

private:
    bool insertDataChunk(std::string data, long long position, std::filesystem::path file);
    bool removeDataChunk(long long position, long long length, std::filesystem::path file);
    DBRow makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                           long long fileSize);
    DBRow makeLocalDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                            long long fileSegmentBegin, long long fileSegmentEnd, long long fileSize);
    unsigned long long calculateSizeTaken(const std::string &folder = DFS::Basic::fsActrRoot);
    std::string extractNextFragment();
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, unsigned long long offset,
                                unsigned long long fragmentSize);
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, unsigned long long offset);

public:
    std::string sendFragment(const DFS::Packets::RequestFileSegmentMessage &msg);
    std::string addFragment(const DFS::Packets::EditSegmentMessage &msg);
    std::string insertFragment(const DFS::Packets::EditSegmentMessage &msg);
    std::string deleteFragment(const DFS::Packets::DeleteSegmentMessage &msg);
    long long bytesLimit() const;
    void setBytesLimit(long long bytesLimit);
};

#endif // DFS_CONTROLLER_H
