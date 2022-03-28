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

#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

class EXTRACHAIN_EXPORT DFSController : public QObject {
    Q_OBJECT
private:
    std::shared_ptr<ExtraChainNode> node;
    unsigned long long bytesLimit = 2147483648;
    unsigned long long sizeTaken = 0;

public:
    DFSController(std::shared_ptr<ExtraChainNode> Node, QObject *parent = nullptr);
    ~DFSController();

    // Internal use only
    std::string addLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath,
                             std::string targetVirtualFilePath, DFS::Encryption securityLevel);

    // External interfaces
    std::string addFile(const DFS::Packets::AddFileMessage &msg, bool loadBytes);
    std::string getFileFromStorage(ActorId owner, std::string fileHash);
    bool removeFile(const Actor<KeyPrivate> &actor, const DFS::Packets::RemoveFileMessage &msg);

private:
    bool insertDataChunk(std::string data, long long position, std::filesystem::path file);
    bool removeDataChunk(long long position, long long length, std::filesystem::path file);
    DBRow makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                           long long fileSize);
    DBRow makeLocalDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                            long long fileSegmentBegin, long long fileSegmentEnd, long long fileSize);
    unsigned long long calculateSizeTaken();
    std::string extractNextFragment();
    std::string extractFragment(boost::interprocess::file_mapping fmapTarget, unsigned long long fragmentSize,
                                unsigned long long offset);

public:
    std::string addFragment(const DFS::Packets::AddSegmentMessage &msg);
    std::string insertFragment(const DFS::Packets::EditSegmentMessage &msg);
    std::string deleteFragment(const DFS::Packets::DeleteSegmentMessage &msg);
    long long getBytesLimit() const;
    void setBytesLimit(long long newBytesLimit);
};

#endif // DFS_CONTROLLER_H
