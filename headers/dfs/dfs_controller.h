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
#include <boost/random.hpp>
#include <boost/algorithm/string.hpp>
#include <filesystem>
#include <QThread>

#include "chain/actor_id.h"
#include "dfs/dfs_utils.h"
#include "dfs/dirs_manager.h"
#include "dfs/load_manager.h"
#include "dfs/historical_collection.h"
#include "dfs/dfs_vector.h"

class ExtraChainNode;
class DirsManager;
class LoadManager;
using FileId                   = std::string;
using ExpectedDirRow           = std::expected<Dfs::DirRow, Dfs::DfsError>;
using ExpectedDirHistoricalRow = std::expected<std::pair<Dfs::DirRow, HistoricalCollectionRow>, Dfs::DfsError>;

namespace Dfs {
    class CollectionTemplate;

    enum class ServiceFolder {
        Base,
        Collection,
        CollectionTemplate,
        Contracts,
        Chat
    };

    inline ServiceFolder toServiceFolder(int value) {
        switch (value) {
        case 0:
            return ServiceFolder::Base;
        case 1:
            return ServiceFolder::Collection;
        case 2:
            return ServiceFolder::CollectionTemplate;
        case 3:
            return ServiceFolder::Chat;
        default:
            throw std::invalid_argument("Invalid integer for ServiceFolder");
        }
    }

    inline int toInt(ServiceFolder folder) {
        return static_cast<int>(folder);
    }

    enum class NetworkStoreFile {
        Broadcast,
        Sync
    };

    struct DfsSize {
        std::size_t all   = 0;
        std::size_t local = 0;
    };
} // namespace Dfs

class ThreadAddFiles;

enum class ExportFileError {
    Unknown,
    DirRowNotExists,
    FileNotReadyState,
    IncorrectDfsPath,
    LocalFileNotExists,
    LocalFileNotValid,
    OutupDirNotExits,
    NoWritePermissions,
    OutputFileExists,
    CopyError
};

class EXTRACHAIN_EXPORT DfsController : public QObject {
    Q_OBJECT

private:
    ExtraChainNode *node;

    std::uint64_t m_bytesLimit = 10995116277760;
    std::size_t   m_sizeTaken  = 0;

    std::uint64_t m_totalDfsSize = 0;

public:
    explicit DfsController(ExtraChainNode *node);
    ~DfsController();

    // auto: + network id + local actors
    std::set<ActorId> priority_actors_ = { ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373") };
    DfsMode           dfs_mode_        = DfsMode::Full;

    std::shared_ptr<DbConnector> get_db_instance();

    const std::set<ActorId> &priority_actors() const {
        return priority_actors_;
    }

    void add_priority_actor(const ActorId &actor_id) {
        priority_actors_.insert(actor_id);
    }

    void remove_priority_actor(const ActorId &actor_id) {
        priority_actors_.erase(actor_id);
    }

    void clear_priority_actors() {
        priority_actors_.clear();
    }

    bool contains_priority_actor(const ActorId &actor_id) const {
        return priority_actors_.find(actor_id) != priority_actors_.end();
    }

    bool is_priority(const ActorId &actor_id) const {
        if (actor_id == node->network_id()) {
            return true;
        }

        const auto actor_ids = node->account_controller()->accounts_ids();
        if (std::find(actor_ids.begin(), actor_ids.end(), actor_id) != actor_ids.end()) {
            return true;
        }

        if (contains_priority_actor(actor_id)) {
            return true;
        }

        return false;
    }

    DfsMode mode() const {
        return dfs_mode_;
    }

    void set_mode(DfsMode mode) {
        if (dfs_mode_ == mode) {
            // return;
        }

        eLog("[Dfs] Set mode to {}", mode);
        dfs_mode_ = mode;

        auto settings     = Utils::read_settings();
        settings.dfs_mode = this->dfs_mode_;
        Utils::write_settings(settings);
    }

    bool is_dirs_loaded() {
        return is_dirs_loaded_;
    }

    std::expected<Dfs::DirRow, Dfs::DfsError> store_file(
        const ActorId               &owner_id,
        const ActorId               &author_id,
        const std::filesystem::path &file_path,
        const std::string           &visual_folder,
        const std::string           &visual_name,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::expected<Dfs::DirRow, Dfs::DfsError> store_file(
        const ActorId               &owner_id,
        const ActorId               &author_id,
        const std::filesystem::path &file_path,
        Dfs::ServiceFolder           service_folder,
        const std::string           &visual_name,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::expected<Dfs::DirRow, Dfs::DfsError> store_data_as_file(
        const ActorId                  &owner_id,
        const ActorId                  &author_id,
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
        const ActorId               &author_id,
        const std::string           &visual_name,
        const ActorId               &template_actor_id,
        const std::string           &template_file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());
    std::expected<Dfs::DirRow, Dfs::DfsError> store_collection(
        const ActorId                 &owner_id,
        const ActorId                 &author_id,
        const std::string             &visual_name,
        const Dfs::CollectionTemplate &collection_template,
        Dfs::DataSecurity              data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData   &security_data = Dfs::DataSecurityData());

    std::expected<Dfs::DirRow, Dfs::DfsError> store_vector(
        const ActorId               &owner_id,
        const ActorId               &author_id,
        const std::string           &visual_name,
        const ActorId               &template_actor_id,
        const std::string           &template_file_id,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());
    std::expected<Dfs::DirRow, Dfs::DfsError> store_vector(
        const ActorId                 &owner_id,
        const ActorId                 &author_id,
        const std::string             &visual_name,
        const Dfs::DfsTemplateVariant &vector_template,
        Dfs::DataSecurity              data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData   &security_data = Dfs::DataSecurityData());

    template <typename T>
    bool add_vector_row(const ActorId               &owner_id,
                        const std::string           &file_id,
                        T                            row,
                        const ActorId               &signer_id     = ActorId(),
                        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
        auto db_row  = Utils::to_dbrow(row);
        auto dir_row = this->add_vector_row(owner_id, file_id, db_row, signer_id, security_data);
        return dir_row;
    }

    bool add_vector_row(const ActorId               &owner_id,
                        const std::string           &file_id,
                        DbRow                        row,
                        const ActorId               &signer_id     = ActorId(),
                        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    bool remove_vector_row(const ActorId     &owner_id,
                           const std::string &file_id,
                           const std::string &primary_data,
                           const ActorId     &signer_id = ActorId());

    std::expected<DbRow, DfsVectorError> get_vector_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const std::string           &primary_data,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::expected<std::vector<DbRow>, DfsVectorError> get_vector_rows(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const std::string           &where_statement = "",
        const Dfs::DataSecurityData &security_data   = Dfs::DataSecurityData());

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
        const Dfs::DataSecurityData &security_data   = Dfs::DataSecurityData(),
        const std::string           &where_statement = "");

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
                                    const Responder   &responder);
    void network_response_historical_collection(const ActorId                              &owner_id,
                                                const std::string                          &file_id,
                                                const std::vector<HistoricalCollectionRow> &historical_rows);
    void network_response_content_collection(const ActorId            &owner_id,
                                             const std::string        &file_id,
                                             const std::vector<DbRow> &db_rows);
    void network_change_collection(const ActorId                 &owner_id,
                                   const std::string             &file_id,
                                   const HistoricalCollectionRow &row,
                                   const Responder               &responder);
    void network_remove_collection(const ActorId                 &owner_id,
                                   const std::string             &file_id,
                                   const HistoricalCollectionRow &row);

    std::expected<std::pair<Dfs::DirRow, DfsVector>, DfsVectorError> make_vector(
        const ActorId               &owner_id,
        const std::string           &file_id,
        bool                         is_network    = false,
        const ActorId               &signer_id     = ActorId(),
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    void network_request_vector(const ActorId &owner_id, const std::string &file_id, const Responder &responder);
    void network_response_content_vector(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content);
    void network_vector_add(const ActorId &owner_id, const std::string &file_id, const DbRow &row);

    void network_request_file_state(const ActorId     &owner_id,
                                    const std::string &file_id,
                                    const Responder   &responder);
    void network_request_file_existance(const Dfs::FileLink &file_link, const Responder &responder);
    void network_response_file_state(const Dfs::Packets::FileState &data, const Responder &responder);
    void network_file_exist_notification(const Dfs::Packets::FileState &data, const Responder &responder);

    // full file remove
    std::expected<void, bool> remove_stored_file(const ActorId &owner_id, const std::string &file_id);
    void                      network_remove_stored_file(const ActorId     &owner_id,
                                                         const std::string &file_id,
                                                         const Signature   &sign,
                                                         uint64_t           last_modified);

    // remove only local copy
    std::expected<void, bool> remove_local_file(const ActorId &owner_id, const std::string &file_id);

    // TODO: get rows from collection

    // TODO: need two function: remove LOCAL file and remove file from STORE

    // visualMoveFile
    void broadcast_stored(const ActorId &owner_id, const Dfs::DirRow &dir_row);
    void sync_stored(const Dfs::FileData &file_data, const Responder &responder);

    // External interfaces
    std::string network_store_file(const ActorId        &owner_id,
                                   const Dfs::DirRow    &dir_row,
                                   Dfs::NetworkStoreFile network_stote);
    std::string getFileFromStorage(const ActorId &owner_id, const std::string &file_name);

    // Unique file ID: hash+msec+salt
    std::string                          create_file_id(std::filesystem::path file);
    std::string                          create_file_id_from(const std::string &data);
    std::uint64_t                        sizeTaken() const;
    std::uint64_t                        totalDfsSize() const;
    void                                 increaseSizeTaken(uintmax_t value);
    std::expected<void, ExportFileError> export_file(const ActorId     &owner_id,
                                                     const std::string &file_id,
                                                     const FsPath      &output_folder);
    std::uint64_t calculateDataAmountStored(const std::string &folder = DfsB::DFS_FOLDER) const;

    DirsManager *dirs_manager();
    LoadManager &download_manager();

    void sync(const std::string &identifier);
    bool is_file_already_downloaded(const ActorId &owner_id, const std::string &file_id, const std::string &hash);
    void refresh_calculate();

    // TODO: use for store files?
    std::expected<Dfs::DirRow, Dfs::DfsError> find_file_self(const ActorId &owner_id, const std::string &dfs_name);
    std::expected<Dfs::DirRow, Dfs::DfsError> read_file_status(const std::string &dfs_name); // TODO: add folder

    void add_to_waiting_file(const ActorId &actor_id, const std::string &file_id) {
        files_waiting_.insert({ actor_id, file_id });
    }

private:
    DirsManager *dirs_manager_;
    LoadManager  load_manager_;
    bool         is_dirs_loaded_ = false;

    std::unordered_map<std::string, Dfs::DirRow> files_ready_status_;
    std::set<std::pair<ActorId, std::string>>    files_waiting_;

    void check_all_files(std::string identifier);

    ExpectedDirHistoricalRow universal_collection_row(const ActorId               &owner_id,
                                                      const std::string           &file_id,
                                                      DbRow                        row,
                                                      uint32_t                     id,
                                                      CollectionOperation          type,
                                                      const Dfs::DataSecurityData &security_data);

    Dfs::DfsSize calculate_size();

    void updateFileState(const ActorId &actorId, const std::string fileName, Dfs::FileState state);

    std::expected<std::pair<std::string, std::optional<std::string>>, Dfs::DfsError> encrypt_name(
        const std::string                &visual_name,
        const std::optional<std::string> &visual_folder,
        Dfs::DataSecurity                 data_security,
        const Dfs::DataSecurityData      &security_data);
    std::expected<std::pair<std::string, std::optional<std::string>>, Dfs::DfsError> decrypt_name(
        const std::string                &visual_name,
        const std::optional<std::string> &visual_folder,
        Dfs::DataSecurity                 data_security,
        const Dfs::DataSecurityData      &security_data);

public:
    void  sendSizeRequestMsg(const ActorId &actorId) const;
    void  sendSizeReponseMsg(const DfsP::RequestDfsSize &msg, const Responder &responder); // TODO: const
    float percentVerified(std::vector<DfsP::VerifyFileMessage> &fileList);

public slots:

public:
    std::uint64_t bytesLimit() const;
    std::uint64_t bytesAvailable();
    bool          writeAvailable(std::size_t = 10000);

signals:
    void stored(ActorId owner_id, Dfs::DirRow dirRow);
    void added(ActorId owner_id, Dfs::DirRow dirRow);
    void updated(ActorId owner_id, Dfs::DirRow dirRow);
    void removed(ActorId owner_id, std::string file_id);
    void localRemoved(ActorId owner_id, std::string file_id);

    void uploaded(ActorId owner_id, Dfs::DirRow dirRow);
    void uploadProgress(ActorId owner_id, std::string file_id, int progress);
    void downloaded(ActorId owner_id, Dfs::DirRow dirRow);
    void downloadProgress(ActorId owner_id, std::string file_id, int progress);
    void waitDownloaded(ActorId owner_id, Dfs::DirRow dirRow);
    void dirsLoaded();

    void collectionDownloaded(); // temp signal for beginFetchNextFile
    void collectionChanged(ActorId owner_id, Dfs::DirRow dir_row, HistoricalCollectionRow historical_row);
    void vectorRowAdded(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);
    void vectorRowRemoved(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);
    void convertion_begin();

    friend DirsManager;
    friend LoadManager;
};
