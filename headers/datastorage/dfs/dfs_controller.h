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
    uint64_t m_bytesLimit = 8589934592;
    uint64_t m_sizeTaken = 0;
    std::map<std::string, DFS::Packets::AddFileMessage> files;

public:
    explicit DfsController(ExtraChainNode &node, QObject *parent = nullptr);
    ~DfsController();

    void initializeActor(const ActorId &actorId);

    // Internal use only
    std::string addLocalFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                             std::string targetVirtualFilePath, DFS::Encryption securityLevel);
    void removeLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath);
    // visualMoveFile

    // External interfaces
    std::string addFile(const DFS::Packets::AddFileMessage &msg, bool loadBytes);
    std::string getFileFromStorage(ActorId owner, std::string fileHash);
    bool removeFile(const DFS::Packets::RemoveFileMessage &msg);

private:
    bool insertDataChunk(std::string data, uint64_t position, std::filesystem::path file);
    bool removeDataChunk(uint64_t position, uint64_t length, std::filesystem::path file);
    DBRow makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                           uint64_t fileSize);
    uint64_t calculateSizeTaken(const std::string &folder = DFS::Basic::fsActrRoot);
    std::string extractNextFragment();
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset,
                                uint64_t fragmentSize);
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset);

public:
    void requestSync();
    void sendSync(uint64_t lastModified, const std::string &messageId);
    void requestDirData(const ActorId &actorId);
    void sendDirData(const ActorId &actorId, uint64_t lastModified, const std::string &messageId);
    void addDirData(const ActorId &actorId, const std::vector<DFS::Packets::DirRow> &dirRows);
    void requestFile(const ActorId &actorId, const std::string &fileHash);
    void sendFile(const ActorId &actorId, const std::string &fileHash, const std::string &messageId);
    std::string sendFragment(const DFS::Packets::RequestFileSegmentMessage &msg,
                             const std::string &messageId);
    std::string addFragment(const DFS::Packets::EditSegmentMessage &msg);
    std::string insertFragment(const DFS::Packets::EditSegmentMessage &msg);
    std::string deleteFragment(const DFS::Packets::DeleteSegmentMessage &msg);
    uint64_t bytesLimit() const;
    void setBytesLimit(uint64_t bytesLimit);

signals:
    void added(ActorId actorId, std::string fileHash, std::string visual, int64_t size);
    void uploaded(ActorId actorId, std::string fileHash); // TODO: loadId
    void downloaded(ActorId actorId, std::string fileHash);
    void downloadProgress(ActorId actorId, std::string fileHash, int progress);
    void uploadProgress(ActorId actorId, std::string fileHash, int progress);
};

#endif // DFS_CONTROLLER_H
