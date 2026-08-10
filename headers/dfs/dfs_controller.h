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
#include <atomic>
#include <filesystem>
#include <mutex>
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
    BOOST_DESCRIBE_STRUCT(DfsSize, (), (all, local));
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

    std::atomic_uint64_t staged_startup_response_count_ { 0 };

public:
    explicit DfsController(ExtraChainNode *node);
    ~DfsController();

    // auto: + network id + local actors
    // raccoon stays in priority (its files are rank 1 in download order);
    // startup_metadata_actors_ additionally guarantees it participates in bootstrap sync.
    std::set<ActorId> priority_actors_ = { ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373") };
    // actor -> {vectors_rank, files_rank}; -1 = default classification for that kind
    std::map<ActorId, std::pair<int, int>> download_rank_overrides_;
    // (actor, file name) -> rank; overrides that win over per-actor ranks
    std::map<std::pair<ActorId, std::string>, int> download_rank_name_overrides_;
    // Some service actors are needed during bootstrap for metadata discovery,
    // but their entire content must not become an eager download dependency.
    std::set<ActorId>       startup_metadata_actors_ = { ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373") };
    std::set<Dfs::FileLink> priority_file_link_;
    // Actors whose dirs were explicitly requested via refresh_actors() (e.g. chat owner-actors
    // after read_chats). Feeds startup_sync_actors(): without it the Selective filter in
    // network_response_dir_rows drops the response. Mutex-guarded: written from the node
    // thread, read from the DFS pool.
    mutable std::mutex requested_sync_actors_mutex_;
    std::set<ActorId>  requested_sync_actors_;
    DfsMode            dfs_mode_ = DfsMode::Full;

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

    void add_priority_file_link(const Dfs::FileLink &file_link) {
        priority_file_link_.insert(file_link);
    }

    void remove_priority_file_link(const Dfs::FileLink &file_link) {
        priority_file_link_.erase(file_link);
    }

    void clear_priority_file_link() {
        priority_file_link_.clear();
    }

    bool contains_priority_file_link(const Dfs::FileLink &file_link) const {
        return priority_file_link_.find(file_link) != priority_file_link_.end();
    }

    bool is_priority(const ActorId &actor_id) const {
        if (actor_id == node->network_id())
            return true;

        const auto actor_ids = node->account_controller()->accounts_ids();
        if (std::find(actor_ids.begin(), actor_ids.end(), actor_id) != actor_ids.end())
            return true;

        if (contains_priority_actor(actor_id))
            return true;

        return false;
    }

    bool is_priority(const Dfs::FileLink &file_link) const {
        if (is_priority(file_link.owner_id))
            return true;

        if (contains_priority_file_link(file_link))
            return true;

        return false;
    }

    // Download ordering (lower = first): 0 network-space vectors, 1 raccoon actor
    // files, 2 chat-actor vectors, 3 main-actor vectors, 4 other vectors, 5 files.
    // Vectors (0-4) are scheduled before plain files (5) — see LoadManager.
    // Per-actor overrides (set_download_rank) win over these defaults.
    static constexpr int RANK_OTHER_VECTORS = 4;
    static constexpr int RANK_FILES         = 5;

    int download_rank(const ActorId &owner_id, const Dfs::DirRow &dir_row) const;

    // Custom per-actor ranks: separate values for the actor's vectors and files;
    // -1 keeps the default classification for that kind.
    void set_download_rank(const ActorId &actor_id, int vectors_rank, int files_rank) {
        download_rank_overrides_[actor_id] = { vectors_rank, files_rank };
    }
    void clear_download_rank(const ActorId &actor_id) {
        download_rank_overrides_.erase(actor_id);
    }

    void request_vector_content(const ActorId &owner_id, const std::string &file_id);

    // Per-actor filename overrides win over per-actor ranks; used to pull a specific vector
    // off the critical path (e.g. the large network Usernames vector -> RANK_OTHER_VECTORS).
    void set_download_rank_by_name(const ActorId &actor_id, const std::string &name, int rank) {
        download_rank_name_overrides_[{ actor_id, name }] = rank;
    }
    void clear_download_rank_by_name(const ActorId &actor_id, const std::string &name) {
        download_rank_name_overrides_.erase({ actor_id, name });
    }

    DfsMode mode() const {
        return dfs_mode_;
    }

    std::uint64_t staged_startup_response_count() const {
        return staged_startup_response_count_.load(std::memory_order_relaxed);
    }

    void mark_startup_sync_response() {
        staged_startup_response_count_.fetch_add(1, std::memory_order_relaxed);
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

    void mark_forced_file(const Dfs::FileLink &file_link) {
        std::lock_guard lock(forced_files_mutex_);
        forces_files_.insert(file_link);
    }
    bool is_forced_file(const Dfs::FileLink &file_link) const {
        std::lock_guard lock(forced_files_mutex_);
        return forces_files_.contains(file_link);
    }
    void consume_forced_file(const Dfs::FileLink &file_link) {
        std::lock_guard lock(forced_files_mutex_);
        forces_files_.erase(file_link);
    }

    std::set<Dfs::FileLink> forces_files_;
    mutable std::mutex      forced_files_mutex_;

    // Throttle for request_file: read paths (read_template, avatars, chat vectors)
    // re-request a missing file on EVERY failed read; without this the client spams
    // the network several times per second for files the node can't serve.
    std::map<Dfs::FileLink, std::chrono::steady_clock::time_point> request_file_times_;
    // Separate throttle for request_vector_content: a shared map ate into request_file's
    // window and blocked the state-response -> add_to_queue path.
    std::map<Dfs::FileLink, std::chrono::steady_clock::time_point> request_vector_times_;
    std::mutex                                                     request_times_mutex_;

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

    std::expected<Dfs::DirRow, Dfs::DfsError> store_folder(
        const ActorId                    &owner_id,
        const std::string                &folder_name,
        const std::optional<std::string> &parent_folder_id = std::nullopt,
        Dfs::DataSecurity                 data_security    = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData      &security_data    = Dfs::DataSecurityData());

    std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> get_folders(const ActorId &owner_id);

    std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> get_folder_contents(const ActorId     &owner_id,
                                                                               const std::string &folder_file_id);

    std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> get_folder_path(const ActorId     &owner_id,
                                                                           const std::string &folder_file_id);

    std::expected<Dfs::DirRow, Dfs::DfsError> move_to_folder(const ActorId                    &owner_id,
                                                             const std::string                &file_id,
                                                             const std::optional<std::string> &new_folder_id);

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
                        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData(),
                        bool                         thothed       = false) {
        auto db_row  = Utils::to_dbrow(row);
        auto dir_row = this->add_vector_row(owner_id, file_id, db_row, signer_id, security_data, thothed);
        return dir_row;
    }

    bool add_vector_row(const ActorId               &owner_id,
                        const std::string           &file_id,
                        DbRow                        row,
                        const ActorId               &signer_id     = ActorId(),
                        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData(),
                        bool                         thothed       = false);

    bool rebroadcast_vector_row(const ActorId     &owner_id,
                                const std::string &file_id,
                                const std::string &primary_data);

    template <typename T>
    bool update_vector_row(const ActorId               &owner_id,
                           const std::string           &file_id,
                           T                            row,
                           const ActorId               &signer_id     = ActorId(),
                           const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
        return add_vector_row(owner_id, file_id, row, signer_id, security_data, false);
    }

    bool remove_vector_row(const ActorId     &owner_id,
                           const std::string &file_id,
                           const std::string &primary_data,
                           const ActorId     &signer_id = ActorId());

    std::optional<std::string> add_file_id(const ActorId     &network_id,
                                           const ActorId     &vector_owner_id,
                                           const std::string &vector_file_id,
                                           const ActorId     &owner_id,
                                           const std::string &file_id,
                                           const ActorId     &signer_id,
                                           int                state      = 0,
                                           Dfs::FileIdState   with_state = Dfs::FileIdState::Without);

    std::expected<DbRow, DfsVectorError> read_vector_row(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const std::string           &primary_data,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData(),
        Dfs::FileType                file_type     = Dfs::FileType::Vector);

    std::expected<std::vector<DbRow>, DfsVectorError> read_vector_rows(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const std::string           &where_statement = "",
        const Dfs::DataSecurityData &security_data   = Dfs::DataSecurityData(),
        Dfs::FileType                file_type       = Dfs::FileType::Vector);

    std::expected<Dfs::DirRow, Dfs::DfsError> store_dictionary(
        const ActorId               &owner_id,
        const ActorId               &author_id,
        const std::string           &visual_name,
        Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    bool dictionary_set_value(const ActorId               &owner_id,
                              const std::string           &file_id,
                              const std::string           &key,
                              const std::string           &value,
                              const ActorId               &author_id,
                              const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    std::optional<std::string> read_dictionary(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const std::string           &key,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

    bool dictionary_remove_value(const ActorId     &owner_id,
                                 const std::string &file_id,
                                 const std::string &key,
                                 const ActorId     &author_id);

    std::optional<std::map<std::string, std::string>> read_dictionary_rows(
        const ActorId               &owner_id,
        const std::string           &file_id,
        const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData());

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
    std::expected<void, ExportFileError> export_file(const ActorId                &owner_id,
                                                     const std::string            &file_id,
                                                     const FsPath                 &output_folder,
                                                     const std::optional<KeyPass> &key = std::nullopt);
    std::uint64_t calculateDataAmountStored(const std::string &folder = DfsB::DFS_FOLDER) const;

    DirsManager &dirs_manager();
    LoadManager &download_manager();
    size_t       load_manager_downloads_size();

    void sync(const std::string &identifier);
    bool refresh_actors(const std::vector<ActorId> &actors);
    bool is_file_already_downloaded(const ActorId &owner_id, const std::string &file_id, const std::string &hash);
    void refresh_calculate();

    // TODO: use for store files?
    std::expected<Dfs::DirRow, Dfs::DfsError> find_file_self(const ActorId &owner_id, const std::string &dfs_name);
    std::expected<Dfs::DirRow, Dfs::DfsError> read_file_status_self(const std::string &dfs_name);

    std::expected<Dfs::DirRow, Dfs::DfsError> read_file_status(
        const ActorId     &owner_id,
        const std::string &dfs_name,
        const std::string &folder = Dfs::Basic::TEMPLATE_VECTOR);

    void add_to_waiting_file(const ActorId &owner_id, const std::string &file_id);
    void download_waiting_files();
    void request_file(const ActorId &owner_id, const std::string &file_id);

private:
    DirsManager dirs_manager_;
    LoadManager load_manager_;

    std::unordered_map<std::string, Dfs::DirRow> files_ready_status_;
    std::set<std::pair<ActorId, std::string>>    files_waiting_;

    void                 check_all_files(std::string identifier);
    std::vector<ActorId> startup_sync_actors() const;

    // for store_vector and store_dictionary
    std::expected<Dfs::DirRow, Dfs::DfsError> store_vector_impl(const ActorId                 &owner_id,
                                                                const ActorId                 &author_id,
                                                                const std::string             &visual_name,
                                                                const Dfs::CollectionTemplate &collection_template,
                                                                Dfs::DataSecurity              data_security,
                                                                const Dfs::DataSecurityData   &security_data,
                                                                Dfs::FileType                  file_type);

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
    void stored(ActorId owner_id, Dfs::DirRow dir_row);
    void added(ActorId owner_id, Dfs::DirRow dir_row);
    void updated(ActorId owner_id, Dfs::DirRow dir_row);
    void removed(ActorId owner_id, std::string file_id);
    void localRemoved(ActorId owner_id, std::string file_id);

    void uploaded(ActorId owner_id, Dfs::DirRow dir_row);
    void uploadProgress(ActorId owner_id, std::string file_id, int progress);
    void downloaded(ActorId owner_id, Dfs::DirRow dir_row);
    void downloadProgress(ActorId owner_id, std::string file_id, int progress);
    void waitDownloaded(ActorId owner_id, Dfs::DirRow dir_row);

    void collectionDownloaded(); // temp signal for beginFetchNextFile
    void collectionChanged(ActorId owner_id, Dfs::DirRow dir_row, HistoricalCollectionRow historical_row);
    void vectorRowAdded(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);
    void vectorRowRemoved(ActorId owner_id, Dfs::DirRow dir_row, DbRow row);

    friend DirsManager;
    friend LoadManager;
};
