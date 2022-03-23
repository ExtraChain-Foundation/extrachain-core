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

//#include <boost/iostreams/device/mapped_file.hpp>

#include "datastorage/actor.h"
#include "datastorage/index/actorindex.h"
#include "managers/account_controller.h"
#include "utils/db_connector.h"
#include "utils/dfs_utils.h"
#include "utils/exc_utils.h"

class EXTRACHAIN_EXPORT DFSController : public QObject {
    Q_OBJECT
private:
    std::shared_ptr<ActorIndex> actorIndex;
    std::shared_ptr<AccountController> accountController;

public:
    DFSController(std::shared_ptr<ActorIndex> ActorIndex,
                  std::shared_ptr<AccountController> AccountController, QObject *parent = nullptr);
    ~DFSController();

    // Internal use only
    std::string secureAddLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath,
                                   std::string targetVirtualFilePath, DFS::Encryption securityLevel);
    std::string addLocalFile(const Actor<KeyPrivate> &actor, const std::string &filePath,
                             std::string targetVirtualFilePath);

    //    QByteArray readFile(const Actor<KeyPrivate> &actor, const QString &fileHash);

    // External interfaces
    std::string addFile(const DFS::Packets::AddFileMessage &msg);
    bool removeFile(const Actor<KeyPrivate> &actor, const DFS::Packets::RemoveFileMessage &msg);
    std::string insertFragment(const Actor<KeyPrivate> &actor, const DFS::Packets::EditSegmentMessage &msg);

    bool flushDirContent(const QString &userId);
    bool initDB(const Actor<KeyPrivate> &actor);

private:
    bool insertDataChunk(std::string data, long long position, std::filesystem::path file);

    static const QString DFSRootDirName;
    static const QString DFSDBName;
    static const QString DFSService;

    QString makeActorDirPath(const QString &actorId);
    QString makeSecurityDirPath(const Actor<KeyPrivate> &actor, SecurityLevel securityLevel);
    QString makeServiceDirPath(const Actor<KeyPrivate> &actor);
    QString makeGlobalPath(const QString &virtualPath, const QString &userId);
    SecurityLevel getSecurityLevel(const QString &virtualPath);

    bool createDirectory(const QString &path);
    void initGlobalDB(const QString &sqliteDBTargetPath);
    DBRow makeActrDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                           long long fileSize);
    DBRow makeLocalDirDBRow(std::string fileHash, std::string fileHashPrev, std::string filePath,
                            long long fileSegmentBegin, long long fileSegmentEnd, long long fileSize);
    DBRow findDBRow(DBConnector &db, const QString &tableName, const QString &fileHash);
    std::vector<DBRow> findDBRows(const std::string &fileHash);
    std::optional<DBRow> lastRow();
    QString lastHash();
    std::string toStdString(DBRow &r) const;
    QString toString(DBRow &r) const;

    bool setDBFieldValue(DBConnector &db, const QString &tableName, const QString &searchColumnTitle,
                         const QString &searchValue, const QString &changeColumnTitle,
                         const QString &changeValue);

public slots:
    QByteArray addFileSegment(const Actor<KeyPrivate> &actor,
                              const AddSegmentMsg &msg); // Place newSegment after the byteIndex pos
    QByteArray deleteFileSegment(const Actor<KeyPrivate> &actor,
                                 const DeleteSegmentMsg &msg); // Delete file content from the firstByteIndex
};

inline std::string DFSController::toStdString(DBRow &r) const {
    return "(" + r["fileHash"] + ", " + r["fileHashPrev"] + ", " + r["filePath"] + ")";
}

inline QString DFSController::toString(DBRow &r) const {
    return QString("(%1, %2, %3)")
        .arg(r["fileHash"].c_str(), r["fileHashPrev"].c_str(), r["filePath"].c_str());
}

#endif // DFS_CONTROLLER_H
