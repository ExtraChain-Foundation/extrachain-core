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

#include <boost/generator_iterator.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/random.hpp>
#include <chrono>
#include <filesystem>
#include <fstream>

#include "datastorage/actor.h"
#include "datastorage/dfs/fragment_storage.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

#include <QThread>
class ThreadAddFiles;
class EXTRACHAIN_EXPORT DfsController : public QObject {
    Q_OBJECT

private:
    ExtraChainNode &node;
    uint64_t m_bytesLimit = 10995116277760;
    uint64_t m_sizeTaken = 0;
    std::map<std::string, DFSP::AddFileMessage> files;
    std::vector<std::string> m_compliteFiles;

public:
    explicit DfsController(ExtraChainNode &node, QObject *parent = nullptr);
    ~DfsController();

    void initializeActor(const ActorId &actorId);

    // Internal use only
    std::string addLocalFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                             std::string targetVirtualFilePath, DFS::Encryption securityLevel);
    bool removeLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath);
    // visualMoveFile

    // External interfaces
    std::string addFile(const DFSP::AddFileMessage &msg, bool loadBytes);
    std::string getFileFromStorage(ActorId owner, std::string fileName);
    bool removeFile(const DFSP::RemoveFileMessage &msg);
    bool renameFile(const ActorId &actor, const std::string &fileHash, const std::string &newFileHash);

    // Unique file ID: hash+msec+salt
    std::string createFileName(std::filesystem::path file);
    DBRow makeActrDirDBRow(std::string fileName, std::string fileNamePrev, std::string fileHash,
                           std::string filePath, uint64_t fileSize);
    uint64_t sizeTaken() const;
    void increaseSizeTaken(uintmax_t value);
    void insertToFiles(DFSP::AddFileMessage msg);

private:
    bool insertDataChunk(std::string data, uint64_t position, std::filesystem::path file);
    bool removeDataChunk(uint64_t position, uint64_t length, std::filesystem::path file);
    uint64_t calculateSizeTaken(const std::string &folder = DFSB::fsActrRoot);
    std::string extractNextFragment();
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset,
                                uint64_t fragmentSize);
    std::string extractFragment(boost::interprocess::file_mapping &fmapTarget, uint64_t offset);

public:
    void requestSync();
    void sendSync(uint64_t lastModified, const std::string &messageId);
    void requestDirData(const ActorId &actorId);
    void sendDirData(const ActorId &actorId, uint64_t lastModified, const std::string &messageId);
    void addDirData(const ActorId &actorId, const std::vector<DFSP::DirRow> &dirRows);
    void requestFile(const ActorId &actorId, const std::string &fileName);
    void sendFile(const ActorId &actorId, const std::string &fileName, const std::string &messageId = "");

    std::string sendNextFragment(uint64_t position, uint64_t size); // Attention~!!!
    std::string sendFragment(const DFSP::RequestFileSegmentMessage &msg, const std::string &messageId);
    void fetchFragments(DFS::Packets::RequestFileSegmentMessage &msg, std::string &messageId);

public slots:
    std::string addFragment(const DFSP::SegmentMessage &msg);
    void threadAddFragment(const DFSP::SegmentMessage &msg);
    std::string insertFragment(const DFSP::SegmentMessage &msg);
    void addListFiles(const QStringList &files);

public:
    std::string deleteFragment(const DFSP::DeleteSegmentMessage &msg);
    uint64_t bytesLimit() const;
    void setBytesLimit(uint64_t bytesLimit);

public:
    DFSP::AddFileMessage getFileHeader(const ActorId actor, const std::string fileName);
    uint64_t bytesAvailable();
    bool writeAvailable(uint64_t size = 10000);

signals:
    void added(ActorId actorId, std::string fileHash, std::string visual, uint64_t size);
    void uploaded(ActorId actorId, std::string fileHash); // TODO: loadId
    void downloaded(ActorId actorId, std::string fileHash);
    void downloadProgress(ActorId actorId, std::string fileHash, int progress);
    void uploadProgress(ActorId actorId, std::string fileHash, int progress);
    void resultAddFile(const QString &result, const QString &fileName);
};

class ThreadAddFiles : public QThread {
    Q_OBJECT
    DfsController *m_dfsController;
    QStringList m_files;
    Actor<KeyPrivate> m_actor;

public:
    ThreadAddFiles(DfsController *dfsController, const Actor<KeyPrivate> &actor, const QStringList &files,
                   QObject *parent = nullptr);
    ~ThreadAddFiles();

    virtual void run() override;
    void addFile(const Actor<KeyPrivate> &actor, const std::filesystem::path &filePath,
                 std::string targetVirtualFilePath);

signals:
    void error(std::string error, std::string fileName);
    void sendMessage(DFSP::AddFileMessage msg, MessageType messageType);
    void added(DFSP::AddFileMessage msg, std::string filePath);
};

#endif // DFS_CONTROLLER_H
