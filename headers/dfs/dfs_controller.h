/*
 * ExtraChain Core
 * Copyright (C) 2025 ExtraChain Foundation <official@extrachain.io>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QObject>

#include <boost/generator_iterator.hpp>
#include <boost/interprocess/file_mapping.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/random.hpp>
#include <filesystem>
#include <QThread>

#include "blockchain/actor.h"
#include "blockchain/actor_index.h"
#include "managers/account_controller.h"
#include "managers/extrachain_node.h"
#include "dfs/dfs_utils.h"
#include <QtConcurrent>
#include <boost/algorithm/string.hpp>

using FileId         = std::string;
using ExpectedDirRow = std::expected<Dfs::DirRow, Dfs::DfsError>;

namespace Dfs {
    class DfsTemplate;
}
class ThreadAddFiles;

class EXTRACHAIN_EXPORT DfsController : public QObject {
    Q_OBJECT

private:
    ExtraChainNode *node;

    std::uint64_t m_bytesLimit = 10995116277760;
    std::size_t   m_sizeTaken  = 0;

    std::map<std::pair<ActorId, FileId>, Dfs::DirRow> files;
    std::vector<std::string>                          m_compliteFiles;
    std::vector<ActorId>                              m_unsynchonizedDirs;
    std::uint64_t                                     m_totalDfsSize = 0;
    std::vector<Dfs::DirRow>                          m_dirRows;

public:
    explicit DfsController(ExtraChainNode *node);
    ~DfsController();

    void initializeActor(const ActorId &actorId);

    // Internal use only

    std::expected<Dfs::DirRow, Dfs::DfsError> store_file(const ActorId               &actorId,
                                                        const std::filesystem::path &filePath,
                                                        const std::string           &visualFolder,
                                                        const std::string           &visualName,
                                                        Dfs::SecurityLevel           securityLevel);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_data_as_file(const ActorId                  &actor_id,
                                                                 const std::vector<std::uint8_t> data,
                                                                 const std::string              &visual_folder,
                                                                 const std::string              &visual_name,
                                                                 Dfs::SecurityLevel              security_level);

    // TODO
    std::expected<Dfs::DirRow, Dfs::DfsError> store_folder(const ActorId     &actor_id,
                                                           const std::string &visual_folder);

    // TODO
    std::expected<Dfs::DirRow, Dfs::DfsError> store_folder_dapp(const ActorId &actor_id,
                                                                const ActorId &dmaster_id);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_template(const ActorId          &actor_id,
                                                             const Dfs::DfsTemplate &template_body);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_database(const ActorId     &actor_id,
                                                             const std::string &visual_name,
                                                             const ActorId     &template_actor_id,
                                                             const std::string &template_name);

    ExpectedDirRow insert_database(const ActorId &actorId, const std::string &fileId, DbRow row);
    ExpectedDirRow update_database(const ActorId &actorId, const std::string &fileId, DbRow row);
    ExpectedDirRow delete_database(const ActorId &actorId, const std::string &fileId, DbRow row);

    // TODO: get from database

    bool removeLocalFile(const ActorId &actorId, const std::string &fileId);
    // visualMoveFile

    // External interfaces
    std::string addFile(const Dfs::DirRow &dirRow, bool loadBytes);
    std::string getFileFromStorage(ActorId owner, std::string fileName);
    bool        removeFile(const DfsP::RemoveFileMessage &msg);
    bool        renameFile(const ActorId &actor, const std::string &fileHash, const std::string &newFileHash);

    // Unique file ID: hash+msec+salt
    std::string   createFileId(std::filesystem::path file);
    std::string   createFileIdFromData(const std::string &data);
    std::uint64_t sizeTaken() const;
    std::uint64_t totalDfsSize() const;
    void          increaseSizeTaken(uintmax_t value);
    void          insertToFiles(const Dfs::DirRow &dirRow);
    void exportFile(const std::string &pathTo, const std::string &pathFrom, const std::string &nameFile = "");
    std::uint64_t calculateDataAmountStored(const std::string &folder = DfsB::fsActrRoot) const;
    std::string   makeReferenceFile(const ActorId             &actor,
                                    const std::string         &nameFile,
                                    const DfsP::ReferenceData &referenceData);

    void dataFromReferenceString(const std::string   &referenceStr,
                                 std::string         &actor,
                                 std::string         &nameFile,
                                 DfsP::ReferenceData &referenceData);

    void updateDirsLastModified(const ActorId &actorId, std::uint64_t lastModified);

private:
    bool          insertDataChunk(std::string data, std::uint64_t position, std::filesystem::path file);
    bool          removeDataChunk(std::uint64_t position, std::uint64_t length, std::filesystem::path file);
    std::uint64_t calculateSizeTaken(const std::string &folder = DfsB::fsActrRoot) const;
    std::uint64_t calculateFilesSize(const std::string &folder = DfsB::fsActrRoot) const;
    std::string   extractNextFragment();
    std::string   extractFragment(boost::interprocess::file_mapping &fmapTarget,
                                  std::uint64_t                      offset,
                                  std::uint64_t                      fragmentSize);
    std::string   extractFragment(boost::interprocess::file_mapping &fmapTarget, std::uint64_t offset);
    void          eraseFirstUnsynchronizedDir();
    void          removeRowFromDB(const DfsP::RemoveFileMessage &msg);
    void          requestFileSegment(const Dfs::DirRow &row);
    void          updateFileState(const ActorId &actorId, const std::string fileName, Dfs::FileState state);

public:
    void        sendSizeRequestMsg(const ActorId &actorId) const;
    void        sendSizeReponseMsg(const DfsP::RequestDfsSize &msg, const std::string &messageId) const;
    void        sendCountRequestMsg(const ActorId &actorId) const;
    void        sendCountReponseMsg(const Dfs::Packets::RequestBlockCount &msg,
                                    const std::string                     &messageId,
                                    BigNumber                              dfsCount) const;
    void        requestSync();
    void        requestDirFileAllActors();
    void        sendSync(std::uint64_t lastModified, const std::string &messageId);
    void        requestDirData(const ActorId &actorId);
    void        sendDirData(const ActorId &actorId, std::uint64_t lastModified, const std::string &messageId);
    void        addDirData(const ActorId &actorId, const std::vector<Dfs::DirRow> &dirRows);
    void        requestFile(const ActorId &actorId, const std::string &fileName);
    void        sendFile(const ActorId &actorId, const std::string &fileId, const std::string &messageId = "");
    void        beginFetchNextFile();
    void        requestNextFragment(const DfsP::RequestFileSegmentMessage &msg);
    std::string sendNextFragment(std::uint64_t position, std::size_t size); // Attention~!!!
    std::string sendFragment(const DfsP::RequestFileSegmentMessage &msg, const std::string &messageId);
    void        fetchFragments(Dfs::Packets::RequestFileSegmentMessage &msg, std::string &messageId);
    void        fetchFragment(DfsP::RequestFileSegmentMessage &msg, std::string &messageId);
    void        verifyFiles(std::vector<DfsP::VerifyFileMessage> &fileList, std::string &messageId);
    float       percentVerified(std::vector<DfsP::VerifyFileMessage> &fileList);
    void        loadVPNLocalizationFiles();

public slots:
    std::string addFragment(const DfsP::SegmentMessage &msg);
    void        threadAddFragment(const DfsP::SegmentMessage &msg);
    std::string insertFragment(const DfsP::SegmentMessage &msg);

public:
    std::string   deleteFragment(const DfsP::DeleteSegmentMessage &msg);
    std::uint64_t bytesLimit() const;

public:
    std::uint64_t bytesAvailable();
    bool          writeAvailable(std::size_t = 10000);

signals:
    void added(Dfs::DirRow dirRow);
    void updated(Dfs::DirRow dirRow);
    void removed(Dfs::DirRow dirRow);

    void uploaded(Dfs::DirRow dirRow);
    void downloaded(Dfs::DirRow dirRow);

    void downloadProgress(ActorId actorId, std::string fileId, int progress);
    void uploadProgress(ActorId actorId, std::string fileId, int progress);

    //
    void getRemovedVPNLocalizationInfo(const QString data, const std::string actorId);
    void vpnLocalizationLoadedFromStorage(const std::string actorId, const std::string fileName);
};
