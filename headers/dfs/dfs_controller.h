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
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <QThread>

#include "blockchain/actor_id.h"
#include "dfs/dfs_utils.h"
#include "dfs/historical_collection.h"

class ExtraChainNode;
using FileId                   = std::string;
using ExpectedDirRow           = std::expected<Dfs::DirRow, Dfs::DfsError>;
using ExpectedDirHistoricalRow = std::expected<std::pair<Dfs::DirRow, HistoricalCollectionRow>, Dfs::DfsError>;

namespace Dfs {
    class CollectionTemplate;

    enum class ServiceFolder {
        Collection,
        CollectionTemplate,
        Chat
    };
} // namespace Dfs

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

    std::expected<Dfs::DirRow, Dfs::DfsError> store_file(
        const ActorId               &owner_id,
        const std::filesystem::path &file_path,
        const std::string           &visual_folder,
        const std::string           &visual_name,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::expected<Dfs::DirRow, Dfs::DfsError> store_file(
        const ActorId               &owner_id,
        const std::filesystem::path &file_path,
        Dfs::ServiceFolder           service_folder,
        const std::string           &visual_name,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::expected<Dfs::DirRow, Dfs::DfsError> store_data_as_file(
        const ActorId                  &owner_id,
        const std::vector<std::uint8_t> data,
        const std::string              &visual_folder,
        const std::string              &visual_name,
        Dfs::DataSecurity               data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData    &security_data = Dfs::DataSecurityData());

    // TODO
    std::expected<Dfs::DirRow, Dfs::DfsError> store_folder(const ActorId     &owner_id,
                                                           const std::string &visual_folder);

    // TODO
    std::expected<Dfs::DirRow, Dfs::DfsError> store_folder_dapp(const ActorId &owner_id,
                                                                const ActorId &dmaster_id);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_template(const ActorId                 &actor_id,
                                                             const Dfs::CollectionTemplate &collection_template);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_collection(
        const ActorId               &owner_id,
        const std::string           &visual_name,
        const ActorId               &template_actor_id,
        const std::string           &template_file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());
    std::expected<Dfs::DirRow, Dfs::DfsError> store_collection(
        const ActorId                 &owner_id,
        const std::string             &visual_name,
        const Dfs::CollectionTemplate &collection_template,
        Dfs::DataSecurity              data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData   &security_data = Dfs::DataSecurityData());

    // TODO: function: get collection size

    std::expected<DbRow, CollectionError> get_collection_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        std::uint32_t                id,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    // TODO: get collection(from, to)
    std::expected<std::vector<DbRow>, CollectionError> get_collection_rows(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    template <typename T>
    ExpectedDirHistoricalRow add_collection_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        T                            row,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
        auto db_row  = Utils::to_dbrow(row);
        auto dir_row = this->add_collection_row(owner_id, file_id, db_row, security_data);
        return dir_row;
    }

    ExpectedDirHistoricalRow add_collection_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        DbRow                        row,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());
    ExpectedDirHistoricalRow update_collection_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        uint32_t                     id,
        DbRow                        row,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());
    ExpectedDirHistoricalRow remove_collection_row(const ActorId     &owner_id,
                                                   const std::string &file_id,
                                                   uint32_t           id);

    void network_request_collection(const ActorId     &owner_id,
                                    const std::string &file_id,
                                    const std::string &message_id);
    void network_response_historical_collection(const ActorId                              &owner_id,
                                                const std::string                          &file_id,
                                                const std::vector<HistoricalCollectionRow> &historical_rows);
    void network_response_content_collection(const ActorId            &owner_id,
                                             const std::string        &file_id,
                                             const std::vector<DbRow> &db_rows);
    void network_change_collection(const ActorId                 &owner_id,
                                   const std::string             &file_id,
                                   const HistoricalCollectionRow &row);
    void network_remove_collection(const ActorId                 &owner_id,
                                   const std::string             &file_id,
                                   const HistoricalCollectionRow &row);

    // TODO: get rows from collection

    bool removeLocalFile(const ActorId &owner_id, const std::string &file_id);
    // visualMoveFile

    // External interfaces
    std::string network_add_file(const ActorId &owner_id, const Dfs::DirRow &dir_row, bool load_bytes);
    std::string getFileFromStorage(const ActorId &owner_id, const std::string &file_name);
    bool        removeFile(const DfsP::RemoveFileMessage &msg);
    bool        renameFile(const ActorId &actor, const std::string &fileHash, const std::string &newFileHash);

    // Unique file ID: hash+msec+salt
    std::string   create_file_id(std::filesystem::path file);
    std::string   create_file_id_from(const std::string &data);
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

    void updateDirsLastModified(const ActorId &actorId, std::uint64_t last_modified);

private:
    ExpectedDirHistoricalRow universal_collection_row(const ActorId               &owner_id,
                                                      const std::string           &file_id,
                                                      DbRow                        row,
                                                      uint32_t                     id,
                                                      CollectionOperation          type,
                                                      const Dfs::DataSecurityData &security_data);

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
    void          requestFileSegment(const Dfs::DirRow &dir_row);
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
    void        sendSync(std::uint64_t last_modified, const std::string &messageId);
    void        requestDirData(const ActorId &actorId);
    void        sendDirData(const ActorId &actorId, std::uint64_t last_modified, const std::string &messageId);
    void        addDirData(const ActorId &actorId, const std::vector<Dfs::DirRow> &dirRows);
    void        requestFile(const ActorId &actorId, const std::string &fileName);
    void        sendFile(const ActorId &owner_id, const std::string &file_id, const std::string &message_id = "");
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
    void added(ActorId owner_id, Dfs::DirRow dirRow);
    void updated(ActorId owner_id, Dfs::DirRow dirRow);
    void removed(ActorId owner_id, Dfs::DirRow dirRow);

    void uploaded(ActorId owner_id, Dfs::DirRow dirRow);
    void downloaded(ActorId owner_id, Dfs::DirRow dirRow);

    void collectionChanged(ActorId owner_id, Dfs::DirRow, HistoricalCollectionRow);

    void downloadProgress(ActorId owner_id, std::string file_id, int progress);
    void uploadProgress(ActorId owner_id, std::string file_id, int progress);

    //
    void getRemovedVPNLocalizationInfo(const QString data, const std::string actorId);
    void vpnLocalizationLoadedFromStorage(const std::string actorId, const std::string fileName);
};
