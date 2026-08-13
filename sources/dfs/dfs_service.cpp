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

#include "dfs/dfs_service.h"

#include "chain/actor_index.h"
#include "dfs/dfs_utils.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "managers/thoth_manager.h"
#include "network/network_service.h"
#include "dfs/name_validator.h"
#include "dfs/collection_template.h"
#include "dfs/dirs_manager.h"
#include "dfs/load_manager.h"

#include "utils/thread_pool_boost.h"
#include "runtime/deadline_task.h"

#include <algorithm>

#include <boost/asio/post.hpp>

namespace {
    constexpr std::size_t request_history_limit = 4096;
    constexpr auto        request_history_ttl   = std::chrono::minutes(5);

    void prune_request_history(std::map<Dfs::FileLink, std::chrono::steady_clock::time_point> &history,
                               const std::chrono::steady_clock::time_point                     now) {
        if (history.size() < request_history_limit) {
            return;
        }

        std::erase_if(history, [now](const auto &entry) {
            return now - entry.second > request_history_ttl;
        });
        if (history.size() >= request_history_limit) {
            const auto oldest =
                std::min_element(history.begin(), history.end(), [](const auto &left, const auto &right) {
                    return left.second < right.second;
                });
            history.erase(oldest);
        }
    }
} // namespace

DfsService::DfsService(ExtraChain::Core::ExtraChainNode *node)
    : node(node)
    , dirs_manager_(DirsManager(node))
    , load_manager_(LoadManager(node)) {
    // Default download rank for the raccoon actor (vectors and files) is 1.
    // Chat/main actors register in ExtraChainNode::start(), network in download_rank.
    set_download_rank(ActorId("46710a2d823c23db9fc2ac01e0f84212a8128373"), 1, 1);

    refresh_calculate();
    // loadBytesLimit();
    eLog("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable());

    // Deliberately not creating a folder per saved actor. A node knows hundreds of
    // actors and stores files for a handful of them, so this made an empty directory
    // for almost every one. Directories are now created where content actually lands
    // (see handle_package / the load manager), so an empty actor leaves no trace on
    // disk — which is also what makes "does this actor have data" answerable by
    // looking at the filesystem.

    event_connections_.emplace_back(
        downloaded_event_.subscribe([this](const ActorId &owner_id, const Dfs::DirRow &dir_row) {
            bool was_waiting = false;
            {
                std::lock_guard lock(files_waiting_mutex_);
                was_waiting = files_waiting_.erase(std::make_pair(owner_id, dir_row.file_id)) != 0;
            }
            if (was_waiting) {
                notify_wait_downloaded(owner_id, dir_row);
            }
        }));

#ifdef IS_APP_UI_CLIENT
    // Light, not Selective: a client keeps the full catalogue and fetches payloads on
    // demand. Narrowing the catalogue itself is opt-in (Selective) and must not be the
    // default — a node that never learns a vector exists cannot repair itself.
    set_mode(DfsMode::Light);
#endif

    // #ifdef IS_RC
    //     set_mode(DfsMode::Light);
    // #endif

    //     auto settings = Utils::read_settings();
    //     if (settings.dfs_mode.has_value()) {
    //         if (settings.dfs_mode.value() == DfsMode::Light) {
    //             set_mode(DfsMode::Light);
    //         } /*else {
    //             settings.dfs_mode = this->dfs_mode_;
    //             Utils::write_settings(settings);
    //         }*/
    //     }
}

DfsService::~DfsService() {
    prepare_shutdown();
    eLog("DfsService::~DfsService()");
}

DfsService::FileEvent &DfsService::stored_event() noexcept {
    return stored_event_;
}

DfsService::FileEvent &DfsService::added_event() noexcept {
    return added_event_;
}

DfsService::FileEvent &DfsService::updated_event() noexcept {
    return updated_event_;
}

DfsService::FileIdEvent &DfsService::removed_event() noexcept {
    return removed_event_;
}

DfsService::FileIdEvent &DfsService::local_removed_event() noexcept {
    return local_removed_event_;
}

DfsService::FileEvent &DfsService::uploaded_event() noexcept {
    return uploaded_event_;
}

DfsService::ProgressEvent &DfsService::upload_progress_event() noexcept {
    return upload_progress_event_;
}

DfsService::FileEvent &DfsService::downloaded_event() noexcept {
    return downloaded_event_;
}

DfsService::ProgressEvent &DfsService::download_progress_event() noexcept {
    return download_progress_event_;
}

DfsService::FileEvent &DfsService::wait_downloaded_event() noexcept {
    return wait_downloaded_event_;
}

ExtraChain::Core::Event<> &DfsService::collection_downloaded_event() noexcept {
    return collection_downloaded_event_;
}

DfsService::CollectionEvent &DfsService::collection_changed_event() noexcept {
    return collection_changed_event_;
}

DfsService::VectorRowEvent &DfsService::vector_row_added_event() noexcept {
    return vector_row_added_event_;
}

DfsService::VectorRowEvent &DfsService::vector_row_removed_event() noexcept {
    return vector_row_removed_event_;
}

bool DfsService::is_priority(const ActorId &actor_id) const {
    if (actor_id == node->network_id()) {
        return true;
    }

    const auto actor_ids = node->account_controller()->accounts_ids();
    return std::ranges::find(actor_ids, actor_id) != actor_ids.end() || contains_priority_actor(actor_id);
}

bool DfsService::is_priority(const Dfs::FileLink &file_link) const {
    return is_priority(file_link.owner_id) || contains_priority_file_link(file_link);
}

void DfsService::notify_stored(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    stored_event_.publish(owner_id, dir_row);
}

void DfsService::notify_added(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    added_event_.publish(owner_id, dir_row);
}

void DfsService::notify_updated(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    updated_event_.publish(owner_id, dir_row);
}

void DfsService::notify_removed(const ActorId &owner_id, const std::string &file_id) {
    removed_event_.publish(owner_id, file_id);
}

void DfsService::notify_local_removed(const ActorId &owner_id, const std::string &file_id) {
    local_removed_event_.publish(owner_id, file_id);
}

void DfsService::notify_uploaded(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    uploaded_event_.publish(owner_id, dir_row);
}

void DfsService::notify_upload_progress(const ActorId &owner_id, const std::string &file_id, int progress) {
    upload_progress_event_.publish(owner_id, file_id, progress);
}

void DfsService::notify_downloaded(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    downloaded_event_.publish(owner_id, dir_row);
}

void DfsService::notify_download_progress(const ActorId &owner_id, const std::string &file_id, int progress) {
    download_progress_event_.publish(owner_id, file_id, progress);
}

void DfsService::notify_wait_downloaded(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    wait_downloaded_event_.publish(owner_id, dir_row);
}

void DfsService::notify_collection_downloaded() {
    collection_downloaded_event_.publish();
}

void DfsService::notify_collection_changed(const ActorId                 &owner_id,
                                           const Dfs::DirRow             &dir_row,
                                           const HistoricalCollectionRow &historical_row) {
    collection_changed_event_.publish(owner_id, dir_row, historical_row);
}

void DfsService::notify_vector_row_added(const ActorId &owner_id, const Dfs::DirRow &dir_row, const DbRow &row) {
    vector_row_added_event_.publish(owner_id, dir_row, row);
}

void DfsService::notify_vector_row_removed(const ActorId &owner_id, const Dfs::DirRow &dir_row, const DbRow &row) {
    vector_row_removed_event_.publish(owner_id, dir_row, row);
}

void DfsService::prepare_shutdown() {
    load_manager_.stop();
    std::lock_guard lock(delayed_tasks_mutex_);
    for (const auto &task : delayed_tasks_) {
        task->cancel();
    }
    delayed_tasks_.clear();
}

void DfsService::schedule_after(std::chrono::steady_clock::duration delay, std::function<void()> callback) {
    auto task = ExtraChain::Core::DeadlineTask::create(node->serial_executor(), std::move(callback));
    task->schedule_after(delay);
    {
        std::lock_guard lock(delayed_tasks_mutex_);
        std::erase_if(delayed_tasks_, [](const auto &pending) {
            return !pending->active();
        });
        delayed_tasks_.push_back(task);
    }
}

std::shared_ptr<DbConnector> DfsService::get_db_instance() {
    return dirs_manager_.get_db_instance();
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_file(const ActorId               &owner_id,
                                                                 const ActorId               &author_id,
                                                                 const std::filesystem::path &file_path,
                                                                 const std::string           &visual_folder,
                                                                 const std::string           &visual_name,
                                                                 Dfs::DataSecurity            data_security,
                                                                 const Dfs::DataSecurityData &security_data) {
    // TODO: move this checks to fn
    if (visual_folder.contains("'") || visual_name.contains("'")) {
        return std::unexpected(Dfs::DfsError::InvalidName);
    }
    // if (!visual_folder.empty()) {
    //     if (visual_folder.front() == ':') {
    //         if (visual_folder == Dfs::Basic::TEMPLATE_COLLECTION
    //             || visual_folder == Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE)
    //             return std::unexpected(Dfs::DfsError::WrongTemplate);
    //         if (visual_folder != Dfs::Basic::TEMPLATE_CHAT)
    //             return std::unexpected(Dfs::DfsError::WrongTemplate);
    //         // if (:DApp) -> Check if :ActorId is DAppMaster && allow to create his folder everyone
    //     }
    // }

    if (data_security == Dfs::DataSecurity::Self) {
        auto search_result = find_file_self(owner_id, visual_name);
        if (search_result.has_value() && search_result->folder == visual_folder) {
            return std::unexpected(Dfs::DfsError::DirDuplicate);
        }
    } else {
        auto search_result =
            Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dirs_manager_.get_db_instance(),
                                                                              owner_id,
                                                                              visual_folder,
                                                                              visual_name);
        if (search_result.has_value()) {
            return std::unexpected(Dfs::DfsError::DirDuplicate);
        }
    }

    auto fpath_result = FsPath::create(file_path);
    if (!fpath_result.has_value()) {
        return std::unexpected(Dfs::DfsError::NotFile);
    }

    auto fpath         = fpath_result.value();
    auto new_file_path = fpath;

    auto has_read_perm = fpath.has_read_permission();
    if (!has_read_perm.has_value()) {
        return std::unexpected(Dfs::DfsError::NotReadable);
    }
    if (!has_read_perm.value()) {
        return std::unexpected(Dfs::DfsError::NotReadable);
    }

    if (fpath.is_directory()) {
    }

    auto file_size_ = fpath.file_size();
    if (!file_size_.has_value()) {
        return std::unexpected(Dfs::DfsError::NotFile);
    }

    constexpr uintmax_t MB_700 = 700ULL * 1024 * 1024; // 734'003'200
    // constexpr uintmax_t GB_10 = 10ULL * 1024 * 1024 * 1024; // 10'737'418'240
    if (file_size_.value() > MB_700) {
        return std::unexpected(Dfs::DfsError::MaxFileSize);
    }

    // TODO: check path, check :***
    auto name_res = NameValidator::validate(visual_name);
    if (!name_res.has_value()) {
        eLog("[Dfs] Can't load file: invalid name");
        return std::unexpected(Dfs::DfsError::InvalidName);
    }

    if (!new_file_path.exists()) {
        eLog("[Dfs] Can't load file: file doesn't exist");
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    if (!new_file_path.is_regular_file()) {
        eLog("[Dfs] This is not a file");
        return std::unexpected(Dfs::DfsError::NotFile);
    }

    std::ifstream my_file(new_file_path.native());
    if (!my_file) {
        eLog("[Dfs] Can't read file");
        return std::unexpected(Dfs::DfsError::NotReadable);
    }
    my_file.close();

    auto file_size_result = new_file_path.file_size();
    if (!file_size_result.has_value()) {
        eWarning("[Dfs] Can't size file");
        return std::unexpected(Dfs::DfsError::NotReadable);
    }
    auto file_size = file_size_result.value();
    // if size == 0 -> return
    // if (!writeAvailable(file_size)) {
    //     return std::unexpected(Dfs::DfsError::StorageFull);
    // }

    std::string file_id      = create_file_id(file_path);
    auto        dfs_path     = Dfs::Path::file_path(owner_id, file_id).value();
    const auto  place_in_dfs = dfs_path.native().parent_path();

    try {
        std::filesystem::create_directories(place_in_dfs);
    } catch (const std::exception &e) {
        eWarning("[Dfs] Failed to create directory: {}", e.what());
        return std::unexpected(Dfs::DfsError::NotWritable);
    }

    if (dfs_path.exists()) {
        // TODO: maybe just regen id?
        // TODO: check from dir rows
        // auto dfs_path_file_size = dfs_path.file_size();
        // if (dfs_path_file_size.has_value()) {
        //     if (dfs_path_file_size.value() == fileSize) {
        //         return std::unexpected(Dfs::DfsError::Unknown);
        //     }
        // }

        // std::string dfs_file_hash = Utils::calculate_hash_file(dfs_path).value();
        // if (file_hash == dfs_file_hash) {
        //     eWarning("[Dfs] File already in dfs");
        //     return std::unexpected(Dfs::DfsError::AlreadyExists);
        // }
    }

    if (data_security == Dfs::DataSecurity::Public) {
        try {
            std::filesystem::create_directories(place_in_dfs.c_str());
            std::filesystem::copy(new_file_path.native(), dfs_path.native());
        } catch (std::filesystem::filesystem_error const &err) {
            eWarning("[Dfs] Copy error: {}", err.what());
            return std::unexpected(Dfs::DfsError::NotWritable);
        }
    } else {
        //  eLog("security_data = {}", security_data);
    }

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto actor = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!actor.has_value()) {
                return std::unexpected(Dfs::DfsError::Unknown);
            }

            auto res = actor->get().key().encrypt_self_file(new_file_path, dfs_path);
            if (!res.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
            auto receiver = node->actor_index()->read_actor_old(security_actor->receiver_id);
            // TODO: checks
            auto res = sender->get().key().encrypt_file(new_file_path, dfs_path, receiver.key().public_key());
            if (!res.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Key) {
        if (auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            auto res = Cryptography::symmetric_encrypt_file(new_file_path, dfs_path, security_key->key);
        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    auto names_result =
        this->encrypt_name(visual_name,
                           visual_folder.empty() ? std::nullopt : std::make_optional(visual_folder),
                           data_security,
                           security_data);
    if (!names_result.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    auto [visual_name_new, visual_folder_new] = names_result.value();

    std::string file_hash     = Utils::calculate_hash_file(dfs_path).value();
    auto        file_size_dfs = dfs_path.file_size();
    if (!file_size_dfs.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    // auto search_result2 = Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(owner_id,
    //                                                                                 visual_folder_new.has_value()
    //                                                                                     ?
    //                                                                                     visual_folder_new.value()
    //                                                                                     : "",
    //                                                                                 visual_name_new);
    // if (search_result2.has_value()) {
    //     return std::unexpected(Dfs::DfsError::DirDuplicate);
    // }

    // if (Dfs::Tables::ActorDirFile::search_file_by_hash(owner_id, file_hash).has_value()) {
    //     try {
    //         std::filesystem::remove(dfs_path.native());
    //     } catch (const std::exception &) {
    //     }
    //     return std::unexpected(Dfs::DfsError::DirDuplicate);
    // }

    // create new dir row
    Dfs::DirRow dir_row = { .actor_id      = author_id,
                            .owner_id      = owner_id,
                            .file_id       = file_id,
                            .prev_file_id  = "",
                            .hash          = file_hash,
                            .folder        = visual_folder_new,
                            .name          = visual_name_new,
                            .size          = file_size_dfs.value(),
                            .created       = 0,
                            .last_modified = 0,
                            .type          = Dfs::FileType::File,
                            .encryption    = data_security != Dfs::DataSecurity::Public,
                            .state         = Dfs::FileState::Ready };

    auto author_actor = node->account_controller()->current_profile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    auto res = Dfs::Tables::DirsFile::ActorSpace::add_dir_row(dirs_manager_.get_db_instance(),
                                                              owner_id,
                                                              dir_row,
                                                              author_actor.value());
    if (!res) {
        std::error_code error;
        std::filesystem::remove(dfs_path.native(), error);
        return std::unexpected(Dfs::DfsError::DirError);
    }

    increaseSizeTaken(file_size);
    m_totalDfsSize += file_size; // TODO: is need at this place?

    // TODO: Fragments: create

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // insertToFiles(dir_row);
    notify_stored(owner_id, dir_row);
    notify_added(owner_id, dir_row);

    broadcast_stored(owner_id, dir_row);

    // load_manager_.broadcast_stored_file(owner_id, dir_row.file_id);
    load_manager_.broadcast_file_exist(owner_id, dir_row.file_id);

    notify_upload_progress(owner_id, file_id, 0);

    return dir_row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_file(const ActorId               &owner_id,
                                                                 const ActorId               &author_id,
                                                                 const std::filesystem::path &file_path,
                                                                 Dfs::ServiceFolder           service_folder,
                                                                 const std::string           &visual_name,
                                                                 Dfs::DataSecurity            data_security,
                                                                 const Dfs::DataSecurityData &security_data) {
    std::string visual_path;

    switch (service_folder) {
    case Dfs::ServiceFolder::Collection:
        visual_path = Dfs::Basic::TEMPLATE_COLLECTION;
        break;
    case Dfs::ServiceFolder::CollectionTemplate:
        visual_path = Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE;
        break;
    case Dfs::ServiceFolder::Chat:
        visual_path = Dfs::Basic::TEMPLATE_CHAT;
        break;
    case Dfs::ServiceFolder::Contracts:
        visual_path = Dfs::Basic::TEMPLATE_CONTRACTS;
        break;
    case Dfs::ServiceFolder::Base:
        break;
    }

    return store_file(owner_id, author_id, file_path, visual_path, visual_name, data_security, security_data);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_data_as_file(
    const ActorId                  &owner_id,
    const ActorId                  &author_id,
    const std::vector<std::uint8_t> data,
    const std::string              &visual_folder,
    const std::string              &visual_name,
    Dfs::DataSecurity               data_security,
    const Dfs::DataSecurityData    &security_data) {
    std::string file_temp = create_file_id("data");
    std::string temp_path = fmt::format("tmp/{}", file_temp);

    std::ofstream temp_file(temp_path, std::ios::binary);
    if (!temp_file) {
        eWarning("[Dfs] Can't create temp file {}", temp_path);
        return std::unexpected(Dfs::DfsError::NotWritable);
    }

    temp_file.write(reinterpret_cast<const char *>(data.data()), data.size());
    temp_file.close();

    auto result =
        store_file(owner_id, author_id, temp_path, visual_folder, visual_name, data_security, security_data);

    std::filesystem::remove(temp_path);

    return result;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_folder(
    const ActorId                    &owner_id,
    const std::string                &folder_name,
    const std::optional<std::string> &parent_folder_id,
    Dfs::DataSecurity                 data_security,
    const Dfs::DataSecurityData      &security_data) {
    if (folder_name.empty() || folder_name.contains("'")) {
        return std::unexpected(Dfs::DfsError::InvalidFolderName);
    }

    auto name_res = NameValidator::validate(folder_name);
    if (!name_res.has_value()) {
        eLog("[Dfs] Can't create folder: invalid name");
        return std::unexpected(Dfs::DfsError::InvalidName);
    }

    auto db_instance = dirs_manager_.get_db_instance();

    if (parent_folder_id.has_value() && !parent_folder_id->empty()) {
        auto is_folder_res =
            DfsT::DirsFile::ActorSpace::is_folder(db_instance, owner_id, parent_folder_id.value());
        if (!is_folder_res.has_value()) {
            return std::unexpected(Dfs::DfsError::FolderNotFound);
        }
        if (!is_folder_res.value()) {
            return std::unexpected(Dfs::DfsError::ParentNotFolder);
        }
    }

    auto search_result = DfsT::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                                    owner_id,
                                                                                    parent_folder_id.value_or(""),
                                                                                    folder_name);
    if (search_result.has_value()) {
        return std::unexpected(Dfs::DfsError::DirDuplicate);
    }

    std::string file_id = create_file_id_from("folder:" + folder_name);

    auto author_actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoOwnerActor);
    }

    std::string stored_name = folder_name;
    bool        encrypted   = false;

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto actor = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!actor.has_value()) {
                return std::unexpected(Dfs::DfsError::NoOwnerActor);
            }
            auto encrypted_name = actor->get().key().encrypt_self(ByteArray(folder_name).toBytes());
            if (!encrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            stored_name = Utils::to_base64(encrypted_name.value());
            encrypted   = true;
        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    std::string folder_hash = Utils::calculate_hash(
        fmt::format("folder:{}:{}:{}", owner_id.to_string(), folder_name, parent_folder_id.value_or("")));

    Dfs::DirRow dir_row = { .actor_id      = owner_id,
                            .owner_id      = owner_id,
                            .file_id       = file_id,
                            .prev_file_id  = std::nullopt,
                            .hash          = folder_hash,
                            .folder        = parent_folder_id,
                            .name          = stored_name,
                            .size          = 0,
                            .created       = 0,
                            .last_modified = 0,
                            .type          = Dfs::FileType::Folder,
                            .encryption    = encrypted,
                            .state         = Dfs::FileState::Ready };

    auto res = DfsT::DirsFile::ActorSpace::add_dir_row(db_instance, owner_id, dir_row, author_actor->get());
    if (!res) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    notify_stored(owner_id, dir_row);
    notify_added(owner_id, dir_row);

    broadcast_stored(owner_id, dir_row);

    eLog("[Dfs] Folder '{}' created with file_id {}", folder_name, file_id);
    return dir_row;
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> DfsService::get_folders(const ActorId &owner_id) {
    return DfsT::DirsFile::ActorSpace::get_folders(dirs_manager_.get_db_instance(), owner_id);
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> DfsService::get_folder_contents(
    const ActorId     &owner_id,
    const std::string &folder_file_id) {
    return DfsT::DirsFile::ActorSpace::get_folder_contents(dirs_manager_.get_db_instance(),
                                                           owner_id,
                                                           folder_file_id);
}

std::expected<std::vector<Dfs::DirRow>, Dfs::DfsError> DfsService::get_folder_path(
    const ActorId     &owner_id,
    const std::string &folder_file_id) {
    return DfsT::DirsFile::ActorSpace::get_folder_path(dirs_manager_.get_db_instance(), owner_id, folder_file_id);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::move_to_folder(
    const ActorId                    &owner_id,
    const std::string                &file_id,
    const std::optional<std::string> &new_folder_id) {
    auto db_instance = dirs_manager_.get_db_instance();

    auto dir_row_res = DfsT::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, file_id, "file_id");
    if (!dir_row_res.has_value()) {
        return std::unexpected(Dfs::DfsError::NotExists);
    }
    auto dir_row = dir_row_res.value();

    if (new_folder_id.has_value() && !new_folder_id->empty()) {
        auto is_folder = DfsT::DirsFile::ActorSpace::is_folder(db_instance, owner_id, new_folder_id.value());
        if (!is_folder.has_value()) {
            return std::unexpected(Dfs::DfsError::FolderNotFound);
        }
        if (!is_folder.value()) {
            return std::unexpected(Dfs::DfsError::ParentNotFolder);
        }

        if (dir_row.is_folder()) {
            auto valid = DfsT::DirsFile::ActorSpace::validate_folder_hierarchy(db_instance,
                                                                               owner_id,
                                                                               file_id,
                                                                               new_folder_id.value());
            if (!valid.has_value() || !valid.value()) {
                return std::unexpected(Dfs::DfsError::FolderCycle);
            }
        }
    }

    auto author_actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoOwnerActor);
    }

    dir_row.folder        = new_folder_id;
    dir_row.last_modified = Utils::current_date_ms();

    auto sign = author_actor->get().key().sign(dir_row.calculate_hash(owner_id));
    if (!sign.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    dir_row.sign = sign.value();

    auto updated = DfsT::DirsFile::ActorSpace::update_file_metadata(db_instance, owner_id, dir_row, true);
    if (!updated) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    notify_stored(owner_id, dir_row);

    broadcast_stored(owner_id, dir_row);

    eLog("[Dfs] File {} moved to folder {}", file_id, new_folder_id.value_or("root"));
    return dir_row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_folder_dapp(const ActorId &owner_id,
                                                                        const ActorId &dmaster_id) {
    eUnimplemented;
    return {};
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_template(
    const ActorId                 &owner_id,
    const Dfs::CollectionTemplate &collection_template) {
    if (!collection_template.to_db_schema().has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    // TODO: check if another dublicate template exists
    // need new function in utils

    auto schema = collection_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    auto sql = schema->to_sql();
    if (!sql.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto json = Json::serialize(collection_template);
    return store_data_as_file(owner_id,
                              owner_id,
                              ByteArray(json).toVector(),
                              Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE,
                              collection_template.name(),
                              Dfs::DataSecurity::Public);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_collection(
    const ActorId                 &owner_id,
    const ActorId                 &author_id,
    const std::string             &visual_name,
    const Dfs::CollectionTemplate &collection_template,
    Dfs::DataSecurity              data_security,
    const Dfs::DataSecurityData   &security_data) {
    auto db_instance = dirs_manager_.get_db_instance();
    auto search_result =
        Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                          owner_id,
                                                                          Dfs::Basic::TEMPLATE_COLLECTION,
                                                                          visual_name);
    if (search_result.has_value()) {
        return std::unexpected(Dfs::DfsError::DirDuplicate);
    }

    std::string file_id  = create_file_id_from("db");
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id).value();
    auto        actor    = node->account_controller()->current_profile().get_actor(owner_id);
    if (!actor.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    // TODO: add author, not only owner
    auto chain =
        HistoricalCollection::create(node, actor.value(), actor->get().id(), file_id, collection_template);
    if (!chain.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto [collection_hash, collection_size] =
        Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(owner_id, file_id);

    auto author_actor = node->account_controller()->current_profile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    Dfs::DirRow dir_row = { .actor_id      = author_id,
                            .owner_id      = owner_id,
                            .file_id       = file_id,
                            .prev_file_id  = "",
                            .hash          = collection_hash,
                            .folder        = Dfs::Basic::TEMPLATE_COLLECTION,
                            .name          = visual_name,
                            .size          = collection_size,
                            .created       = 0,
                            .last_modified = 0,
                            .type          = Dfs::FileType::Collection,
                            .encryption    = data_security != Dfs::DataSecurity::Public,
                            .state         = Dfs::FileState::Ready };

    bool add_dir_row_result =
        Dfs::Tables::DirsFile::ActorSpace::add_dir_row(db_instance, owner_id, dir_row, author_actor.value());
    if (!add_dir_row_result) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // insertToFiles(dir_row);
    notify_stored(owner_id, dir_row);
    broadcast_stored(owner_id, dir_row);

    return dir_row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_collection(
    const ActorId               &owner_id,
    const ActorId               &author_id,
    const std::string           &visual_name,
    const ActorId               &template_actor_id,
    const std::string           &template_file_id,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    // if visual_name empty -> return
    // if template not exists -> return
    auto collection_template =
        Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(template_actor_id, template_file_id);

    if (!collection_template.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    return store_collection(owner_id,
                            author_id,
                            visual_name,
                            collection_template.value(),
                            data_security,
                            security_data);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_vector(const ActorId                 &owner_id,
                                                                   const ActorId                 &author_id,
                                                                   const std::string             &visual_name,
                                                                   const Dfs::DfsTemplateVariant &vector_template,
                                                                   Dfs::DataSecurity              data_security,
                                                                   const Dfs::DataSecurityData   &security_data) {
    auto template_result = Dfs::read_template_from_variant(vector_template);
    if (!template_result.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    return store_vector_impl(owner_id,
                             author_id,
                             visual_name,
                             template_result->first,
                             data_security,
                             security_data,
                             Dfs::FileType::Vector);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_vector_impl(
    const ActorId                 &owner_id,
    const ActorId                 &author_id,
    const std::string             &visual_name,
    const Dfs::CollectionTemplate &collection_template,
    Dfs::DataSecurity              data_security,
    const Dfs::DataSecurityData   &security_data,
    Dfs::FileType                  file_type) {
    auto db_instance = dirs_manager_.get_db_instance();

    const std::string &folder_template =
        (file_type == Dfs::FileType::Dictionary) ? Dfs::Basic::TEMPLATE_DICTIONARY : Dfs::Basic::TEMPLATE_VECTOR;

    if (data_security == Dfs::DataSecurity::Self) {
        auto search_result = find_file_self(owner_id, visual_name);
        if (search_result.has_value() && search_result->folder == folder_template) {
            return std::unexpected(Dfs::DfsError::DirDuplicate);
        }
    } else {
        auto search_result = Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(db_instance,
                                                                                               owner_id,
                                                                                               folder_template,
                                                                                               visual_name);
        if (search_result.has_value()) {
            return std::unexpected(Dfs::DfsError::DirDuplicate);
        }
    }

    std::string file_id  = create_file_id_from("db");
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id).value();
    auto        actor    = node->account_controller()->current_profile().get_actor(owner_id);
    if (!actor.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    try {
        auto parent_dir = dfs_path.parent_path();
        if (!parent_dir.has_value()) {
            return std::unexpected(Dfs::DfsError::NotWritable);
        }
        std::filesystem::create_directories(parent_dir->native());
    } catch (const std::exception &e) {
        eWarning("[Dfs] Failed to create directory: {}", e.what());
        return std::unexpected(Dfs::DfsError::NotWritable);
    }

    auto dfs_vector = DfsVector::create(node,
                                        actor.value(),
                                        owner_id,
                                        file_id,
                                        collection_template,
                                        data_security,
                                        security_data,
                                        file_type);
    if (!dfs_vector.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto author_actor = node->account_controller()->current_profile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    auto names_result = this->encrypt_name(visual_name, std::nullopt, data_security, security_data);
    if (!names_result.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    auto [visual_name_new, _] = names_result.value();

    auto vector_hash = dfs_vector->calculate_template_file_hash();
    if (!vector_hash.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    Dfs::DirRow dir_row = { .actor_id      = author_id,
                            .owner_id      = owner_id,
                            .file_id       = file_id,
                            .prev_file_id  = "",
                            .hash          = vector_hash.value().first,
                            .folder        = folder_template,
                            .name          = visual_name_new,
                            .size          = vector_hash.value().second,
                            .created       = 0,
                            .last_modified = 0,
                            .type          = file_type,
                            .encryption    = data_security != Dfs::DataSecurity::Public,
                            .state         = Dfs::FileState::Ready };

    bool add_dir_row_result =
        Dfs::Tables::DirsFile::ActorSpace::add_dir_row(db_instance, owner_id, dir_row, author_actor.value());
    if (!add_dir_row_result) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    notify_stored(owner_id, dir_row);
    broadcast_stored(owner_id, dir_row);

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> rows =
        dfs_vector->generate_content_package();
    if (!rows.has_value()) {
        if (rows.error() == DfsVectorError::CollectionEmpty) {
            Dfs::Packets::DfsVectorContentPackage empty_package;
            empty_package.owner_id = owner_id;
            empty_package.file_id  = file_id;
            node->network()->send_broadcast(empty_package, MessageType::DfsVectorCreation);
            return dir_row;
        }
        eCritical("[DfsCollection] Can't find row for {} and {}", owner_id, file_id);
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    node->network()->send_broadcast(rows.value(), MessageType::DfsVectorCreation);

    return dir_row;
}

bool DfsService::add_vector_row(const ActorId               &owner_id,
                                const std::string           &file_id,
                                DbRow                        row,
                                const ActorId               &signer_id,
                                const Dfs::DataSecurityData &security_data,
                                bool                         thothed) {
    eLog("[Dfs] add_vector_row: owner={}, file_id={}", owner_id.to_string(), file_id);
    auto res = this->make_vector(owner_id, file_id, false, signer_id, security_data);
    if (!res.has_value()) {
        eWarning("[Dfs] Can't find vector {} / {}", owner_id, file_id);
        return false;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto operation_res          = dfs_vector.store_add(row);
    if (!operation_res) {
        eWarning("[Dfs] Can't store to vector {} / {}", owner_id, file_id);
        return false;
    }
    // get and exists check id?

    auto hash_size = dfs_vector.data_hash_size();
    if (hash_size.has_value()) {
        dir_row.hash          = hash_size.value().first;
        dir_row.size          = hash_size.value().second;
        dir_row.last_modified = std::stoull(row.at("timestamp")); // try catch
        Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(dirs_manager_.get_db_instance(),
                                                                owner_id,
                                                                dir_row,
                                                                false);
    }

    if (row.at("status") == "1") {
        notify_vector_row_added(owner_id, dir_row, row);
    } else {
        notify_vector_row_removed(owner_id, dir_row, row);
    }

    auto package =
        Dfs::Packets::VectorRowAdd { .owner_id = owner_id, .file_id = file_id, .row = row, .thothed = thothed };
    node->network()->send_broadcast(package, MessageType::DfsVectorAdd);

    return operation_res;
}

bool DfsService::rebroadcast_vector_row(const ActorId     &owner_id,
                                        const std::string &file_id,
                                        const std::string &primary_data) {
    if (!node || !node->network()->is_active_connection_exists()) {
        return false;
    }

    auto row = read_vector_row(owner_id, file_id, primary_data);
    if (!row.has_value()) {
        return false;
    }

    auto package = Dfs::Packets::VectorRowAdd {
        .owner_id = owner_id,
        .file_id  = file_id,
        .row      = row.value(),
    };
    return !node->network()->send_broadcast(package, MessageType::DfsVectorAdd).empty();
}

std::optional<std::string> DfsService::add_file_id(const ActorId     &network_id,
                                                   const ActorId     &vector_owner_id,
                                                   const std::string &vector_file_id,
                                                   const ActorId     &owner_id,
                                                   const std::string &file_id,
                                                   const ActorId     &signer_id,
                                                   int                state,
                                                   Dfs::FileIdState   with_state) {
    auto file_row = read_file_status(network_id,
                                     with_state == Dfs::FileIdState::With ? "FilesListState" : "FilesList",
                                     Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE);
    if (!file_row.has_value()) {
        return std::nullopt;
    }

    if (file_row->state != Dfs::FileState::Ready) {
        add_to_waiting_file(network_id, file_row->file_id);
        request_file(network_id, file_row->file_id);
        return std::nullopt;
    }

    auto files_id_data = Dfs::FileIdData { .id        = Utils::generate_random_hex(6),
                                           .timestamp = 0,
                                           .actor     = signer_id,
                                           .owner     = owner_id,
                                           .file_id   = file_id,
                                           .state     = state };

    auto db_row = Utils::to_dbrow(files_id_data);
    if (with_state == Dfs::FileIdState::Without) {
        db_row.erase("state");
    }

    auto res = add_vector_row(vector_owner_id, vector_file_id, db_row, signer_id);
    if (!res) {
        return std::nullopt;
    }

    return files_id_data.id;
}

bool DfsService::remove_vector_row(const ActorId     &owner_id,
                                   const std::string &file_id,
                                   const std::string &primary_data,
                                   const ActorId     &signer_id) {
    auto res = make_vector(owner_id, file_id, false, signer_id);
    if (!res.has_value()) {
        return false;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto row                    = dfs_vector.remove(primary_data);
    if (!row.has_value()) {
        return false;
    }

    auto hash_size = dfs_vector.data_hash_size();
    if (hash_size.has_value()) {
        dir_row.hash          = hash_size.value().first;
        dir_row.size          = hash_size.value().second;
        dir_row.last_modified = std::stoull(row->at("timestamp")); // try catch
        Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(dirs_manager_.get_db_instance(),
                                                                owner_id,
                                                                dir_row,
                                                                false);
    }

    auto package = Dfs::Packets::VectorRowAdd { .owner_id = owner_id, .file_id = file_id, .row = row.value() };
    node->network()->send_broadcast(package, MessageType::DfsVectorAdd);

    // emit vectorRowRemoved(owner_id, dir_row, row);

    // auto package = Dfs::Packets::VectorRowRemove { .owner_id = owner_id, .file_id = file_id, .row = row };
    // node->network()->send_broadcast(package, MessageType::DfsVectorRemove);

    return true;
}

std::expected<DbRow, DfsVectorError> DfsService::read_vector_row(const ActorId               &owner_id,
                                                                 const std::string           &file_id,
                                                                 const std::string           &primary_data,
                                                                 const Dfs::DataSecurityData &security_data,
                                                                 Dfs::FileType                file_type) {
    if (!node_enabled.load()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto v = DfsVector::load(node,
                             node->account_controller()->current_profile().main()->get(),
                             owner_id,
                             file_id,
                             Dfs::DataSecurity::Encrypted,
                             security_data,
                             file_type);
    if (!v.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto row = v->read_row(primary_data);
    if (!row.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return row;
}

std::expected<std::vector<DbRow>, DfsVectorError> DfsService::read_vector_rows(
    const ActorId               &owner_id,
    const std::string           &file_id,
    const std::string           &where_statement,
    const Dfs::DataSecurityData &security_data,
    Dfs::FileType                file_type) {
    if (!node_enabled.load()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto v = DfsVector::load(node,
                             node->account_controller()->system_actor(),
                             owner_id,
                             file_id,
                             Dfs::DataSecurity::Public,
                             security_data,
                             file_type);

    if (!v.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto row = where_statement.empty() ? v->read_rows() : v->read_rows(where_statement);
    if (!row.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_vector(const ActorId               &owner_id,
                                                                   const ActorId               &author_id,
                                                                   const std::string           &visual_name,
                                                                   const ActorId               &template_actor_id,
                                                                   const std::string           &template_file_id,
                                                                   Dfs::DataSecurity            data_security,
                                                                   const Dfs::DataSecurityData &security_data) {
    auto vector_template =
        Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(template_actor_id, template_file_id);
    if (!vector_template.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto link =
        Dfs::CollectionTemplateLink { .owner_id = template_actor_id, .file_id = template_file_id, .name = "" };
    return store_vector(owner_id, author_id, visual_name, link, data_security, security_data);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::store_dictionary(
    const ActorId               &owner_id,
    const ActorId               &author_id,
    const std::string           &visual_name,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    auto templ = Dfs::dictionary_template();
    return store_vector_impl(owner_id,
                             author_id,
                             visual_name,
                             templ,
                             data_security,
                             security_data,
                             Dfs::FileType::Dictionary);
}

bool DfsService::dictionary_set_value(const ActorId               &owner_id,
                                      const std::string           &file_id,
                                      const std::string           &key,
                                      const std::string           &value,
                                      const ActorId               &author_id,
                                      const Dfs::DataSecurityData &security_data) {
    DbRow row;
    row["id"]    = key;
    row["value"] = value;
    return add_vector_row(owner_id, file_id, row, author_id, security_data);
}

std::optional<std::string> DfsService::read_dictionary(const ActorId               &owner_id,
                                                       const std::string           &file_id,
                                                       const std::string           &key,
                                                       const Dfs::DataSecurityData &security_data) {
    auto rows = read_vector_rows(owner_id, file_id, "", security_data, Dfs::FileType::Dictionary);
    if (!rows.has_value()) {
        return std::nullopt;
    }

    for (const auto &row : rows.value()) {
        auto it = row.find("id");
        if (it != row.end() && it->second == key) {
            auto val_it = row.find("value");
            if (val_it != row.end()) {
                return val_it->second;
            }
        }
    }
    return std::nullopt;
}

bool DfsService::dictionary_remove_value(const ActorId     &owner_id,
                                         const std::string &file_id,
                                         const std::string &key,
                                         const ActorId     &author_id) {
    return remove_vector_row(owner_id, file_id, key, author_id);
}

std::optional<std::map<std::string, std::string>> DfsService::read_dictionary_rows(
    const ActorId               &owner_id,
    const std::string           &file_id,
    const Dfs::DataSecurityData &security_data) {
    auto rows = read_vector_rows(owner_id, file_id, "", security_data, Dfs::FileType::Dictionary);
    if (!rows.has_value()) {
        return std::nullopt;
    }

    std::map<std::string, std::string> result;
    for (const auto &row : rows.value()) {
        auto id_it  = row.find("id");
        auto val_it = row.find("value");
        if (id_it != row.end() && val_it != row.end()) {
            result[id_it->second] = val_it->second;
        }
    }
    return result;
}

std::expected<DbRow, CollectionError> DfsService::get_collection_row(const ActorId               &owner_id,
                                                                     const std::string           &file_id,
                                                                     uint32_t                     id,
                                                                     const Dfs::DataSecurityData &security_data) {
    auto main_actor = node->account_controller()->system_actor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    auto row        = chain->get_collection_rows("WHERE id=" + std::to_string(id));
    return row.value()[0];
}

std::expected<std::vector<DbRow>, CollectionError> DfsService::get_collection_rows(
    const ActorId               &owner_id,
    const std::string           &file_id,
    const Dfs::DataSecurityData &security_data,
    const std::string           &where_statement) {
    auto main_actor = node->account_controller()->system_actor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);

    if (!chain.has_value()) {
        return std::unexpected(CollectionError::CollectionNotFound);
    }

    auto row = chain->get_collection_rows(where_statement);
    return row;
}

ExpectedDirHistoricalRow DfsService::universal_collection_row(const ActorId               &owner_id,
                                                              const std::string           &file_id,
                                                              DbRow                        row,
                                                              std::uint32_t                id,
                                                              CollectionOperation          type,
                                                              const Dfs::DataSecurityData &security_data) {
    auto db_instance    = dirs_manager_.get_db_instance();
    auto dir_row_result = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, file_id);
    if (!dir_row_result.has_value()) {
        return std::unexpected(dir_row_result.error());
    }
    // TODO: check fields

    // TODO: choose sign actor from args
    auto main_actor = node->account_controller()->current_profile().system();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    if (!chain.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    std::expected<HistoricalCollectionRow, CollectionError> historical_row;
    switch (type) {
    case CollectionOperation::Add:
        historical_row = chain->add_row(row, Dfs::DataSecurity::Public, security_data);
        break;
    case CollectionOperation::Update:
        historical_row = chain->update_row(id, row, Dfs::DataSecurity::Public, security_data);
        break;
    case CollectionOperation::Remove:
        historical_row = chain->remove_row(id);
        break;
    default:
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    if (!historical_row.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto dir_row          = dir_row_result.value();
    dir_row.last_modified = historical_row.value().timestamp;
    auto [hash, size]     = Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(owner_id, file_id);
    dir_row.hash          = hash;
    dir_row.size          = size;

    auto sign = main_actor.key().sign(dir_row.calculate_hash(owner_id));
    if (!sign.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    dir_row.sign = sign.value();
    Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(db_instance, owner_id, dir_row);
    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    node->network()->send_message(std::make_tuple(owner_id, file_id, historical_row.value()),
                                  MessageType::DfsCollectionRowChange,
                                  SendMode::Neighbours);

    return std::pair { dir_row_result.value(), historical_row.value() };
}

bool DfsService::is_file_already_downloaded(const ActorId     &owner_id,
                                            const std::string &file_id,
                                            const std::string &hash) {
    const auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        eWarning("[Dfs] Add file from network: incorrect dir row for owner {} and hash '{}'", owner_id, hash);
    }

    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (!dir_row.has_value()) {
        return false; // TODO: temp, need expected
    }
    if (dir_row->hash == hash && dir_row->state != Dfs::FileState::Ready) {
        // return true; // TODO: that's all
    }

    if (dir_row->type == Dfs::FileType::Folder) {
        return dir_row->hash == hash && dir_row->state == Dfs::FileState::Ready;
    }

    bool exists = path->exists();
    if (exists) {
        // TODO: use cached hash and state from dir row?
        if (dir_row->type == Dfs::FileType::File) {
            auto existing_hash = Utils::calculate_hash_file(path.value());
            if (existing_hash.has_value() && existing_hash.value() == hash) {
                return true;
            }
            // if (dir_row->hash == hash) {
            // }
        }

        if (dir_row->type == Dfs::FileType::Collection || dir_row->type == Dfs::FileType::Vector
            || dir_row->type == Dfs::FileType::Dictionary) {
            auto [collection_hash, collection_size] =
                Dfs::Tables::DirsFile::ActorSpace::calculate_collection_hash_size(owner_id, file_id);
            if (collection_hash == hash) {
                return true;
            }
        }
    }

    return false;
}

void DfsService::refresh_calculate() {
    auto dfs_size  = calculate_size();
    m_sizeTaken    = dfs_size.local;
    m_totalDfsSize = dfs_size.all;
    // TODO: update prepare status
    eLog("[Dfs] Size: {}", dfs_size);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::find_file_self(const ActorId     &owner_id,
                                                                     const std::string &dfs_name) {
    auto db_instance = dirs_manager_.get_db_instance();
    auto row         = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, dfs_name);
    if (row.has_value()) {
        return row;
    }

    auto actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoOwnerActor);
    }

    auto rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, owner_id);
    if (!rows.has_value()) {
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    for (const auto &row : rows.value()) {
        if (row.name == dfs_name) {
            return row;
        }

        auto decoded = ByteArray::fromBase64(row.name);
        if (!decoded.has_value()) {
            continue;
        }

        auto res = actor->get().key().decrypt_self(decoded->toBytes());
        if (!res.has_value()) {
            continue;
        }

        auto result = ByteArray(res.value()).toString();

        if (result == dfs_name) {
            return row;
        }
    }

    // TODO: select 50-100

    return std::unexpected(Dfs::DfsError::NotExists);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::read_file_status(const ActorId     &owner_id,
                                                                       const std::string &dfs_name,
                                                                       const std::string &folder) {
    // eLog("read_file_status {} {} {}", owner_id, dfs_name, folder);
    return Dfs::Tables::DirsFile::ActorSpace::search_file_by_folder_and_name(dirs_manager_.get_db_instance(),
                                                                             owner_id,
                                                                             folder,
                                                                             dfs_name);
}

void DfsService::add_to_waiting_file(const ActorId &owner_id, const std::string &file_id) {
    std::lock_guard lock(files_waiting_mutex_);
    files_waiting_.insert({ owner_id, file_id });
}

void DfsService::download_waiting_files() {
    std::vector<std::pair<ActorId, std::string>> waiting;
    {
        std::lock_guard lock(files_waiting_mutex_);
        waiting.assign(files_waiting_.begin(), files_waiting_.end());
    }
    for (const auto &el : waiting) {
        const auto &owner_id  = el.first;
        const auto &file_id   = el.second;
        auto        file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };

        this->node->network()->send_message(file_link,
                                            MessageType::DfsFileState,
                                            SendMode::Neighbours,
                                            MessageStatus::Request);
    }
}

int DfsService::download_rank(const ActorId &owner_id, const Dfs::DirRow &dir_row) const {
    // Vector class = the DBs themselves (Vector/Dictionary/Collection) plus their templates
    // (File type under :CollectionTemplate — no template means the vector can't be read).
    // Classifying by folder doesn't work: chat attachments live under :DApp:Chat:*.
    const bool is_vector =
        dir_row.type == Dfs::FileType::Vector || dir_row.type == Dfs::FileType::Dictionary
        || dir_row.type == Dfs::FileType::Collection
        || (dir_row.folder.has_value() && dir_row.folder.value() == Dfs::Basic::TEMPLATE_COLLECTION_TEMPLATE);

    // Name-based overrides (owner+name) have top priority in the registry: let a specific
    // vector (e.g. the network Usernames vector) be demoted off the critical path.
    if (auto it = download_rank_name_overrides_.find({ owner_id, dir_row.name });
        it != download_rank_name_overrides_.end()) {
        return it->second;
    }

    // Rank registry (raccoon from the constructor, chat/main actors from
    // ExtraChainNode::start(), custom via set_download_rank from the app).
    if (auto it = download_rank_overrides_.find(owner_id); it != download_rank_overrides_.end()) {
        const int rank = is_vector ? it->second.first : it->second.second;
        if (rank >= 0) {
            return rank;
        }
    }

    if (owner_id == node->network_id() && is_vector) {
        return 0;
    }
    return is_vector ? RANK_OTHER_VECTORS : RANK_FILES;
}

// Direct request for full vector content (DfsFileRequest -> peer replies with a
// DfsVectorContent package): handle_package restores both the DB and the .vector companion.
// Used to repair vectors with a lost template (read_template).
void DfsService::request_vector_content(const ActorId &owner_id, const std::string &file_id) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };

    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(request_times_mutex_);
        prune_request_history(request_vector_times_, now);
        auto it = request_vector_times_.find(file_link);
        if (it != request_vector_times_.end() && now - it->second < std::chrono::seconds(30)) {
            return;
        }
        request_vector_times_[file_link] = now;
    }

    eLog("[Dfs] Request vector content: {} / {}", owner_id, file_id);
    Dfs::FileLinkFragment request;
    request.file_link = file_link;
    request.fragment_numbers.emplace(1);
    node->network()->send_message(request,
                                  MessageType::DfsFileRequest,
                                  SendMode::Neighbours,
                                  MessageStatus::NoStatus);
}

void DfsService::request_file(const ActorId &owner_id, const std::string &file_id) {
    auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };

    // First request goes out immediately, retries at most every 30s per file.
    const auto now = std::chrono::steady_clock::now();
    {
        std::lock_guard lock(request_times_mutex_);
        prune_request_history(request_file_times_, now);
        auto it = request_file_times_.find(file_link);
        if (it != request_file_times_.end() && now - it->second < std::chrono::seconds(30)) {
            return;
        }
        request_file_times_[file_link] = now;
    }

    eLog("[Dfs] Request file: {} / {}", owner_id, file_id);
    mark_forced_file(file_link);

    this->node->network()->send_message(file_link,
                                        MessageType::DfsFileState,
                                        SendMode::Neighbours,
                                        MessageStatus::Request);

    // Do not wait for a state response. Old nodes do not respond when they do
    // not have the file. LoadManager probes the available peers from this queue.
    auto row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (row.has_value()) {
        load_manager_.add_to_queue(owner_id, row.value(), std::string {}, false);
    } else {
        // A new profile may not have this actor's directory. Request it before
        // the next file attempt. refresh_actors handles retries and fallback.
        eLog("[Dfs] Request file: no dir_row for {}; refreshing actor dirs", owner_id);
        refresh_actors({ owner_id });
    }
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsService::read_file_status_self(const std::string &dfs_name) {
    auto it = files_ready_status_.find(dfs_name);
    if (it != files_ready_status_.end()) {
        return it->second;
    }

    auto row = this->find_file_self(node->account_controller()->current_profile().main_id(), dfs_name);
    if (row.has_value()) {
        if (row->state != Dfs::FileState::Ready) {
            return row;
        }

        files_ready_status_[dfs_name] = row.value();
        return row;
    } else {
        return std::unexpected(row.error());
    }
}

ExpectedDirHistoricalRow DfsService::add_collection_row(const ActorId               &owner_id,
                                                        const std::string           &file_id,
                                                        DbRow                        row,
                                                        const Dfs::DataSecurityData &security_data) {
    auto res = universal_collection_row(owner_id, file_id, row, 0, CollectionOperation::Add, security_data);
    if (res.has_value()) {
        auto &res_ = res.value();

        notify_collection_changed(owner_id, res_.first, res_.second);
    }
    return res;
}

ExpectedDirHistoricalRow DfsService::update_collection_row(const ActorId               &owner_id,
                                                           const std::string           &file_id,
                                                           uint32_t                     id,
                                                           DbRow                        row,
                                                           const Dfs::DataSecurityData &security_data) {
    auto res = universal_collection_row(owner_id, file_id, row, id, CollectionOperation::Update, security_data);
    if (res.has_value()) {
        auto &res_ = res.value();

        notify_collection_changed(owner_id, res_.first, res_.second);
    }
    return res;
}

ExpectedDirHistoricalRow DfsService::remove_collection_row(const ActorId     &owner_id,
                                                           const std::string &file_id,
                                                           uint32_t           id) {
    auto res =
        universal_collection_row(owner_id, file_id, {}, id, CollectionOperation::Remove, Dfs::DataSecurityData());
    if (res.has_value()) {
        auto &res_ = res.value();

        notify_collection_changed(owner_id, res_.first, res_.second);
    }
    return res;
}

void DfsService::network_request_collection(const ActorId     &owner_id,
                                            const std::string &file_id,
                                            const Responder   &responder) {
    auto dirRowExp =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    auto main_actor = node->account_controller()->system_actor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);

    if (!chain.has_value()) {
        return;
    }

    auto historical_rows = chain->get_historical_rows();
    if (!historical_rows.has_value()) {
        eCritical("[DfsCollection] Can't find historical for {} and {}", owner_id, file_id);
        return;
    }
    auto rows = chain->get_collection_rows();
    if (!rows.has_value() && rows.error() != CollectionError::CollectionEmpty) {
        eCritical("[DfsCollection] Can't find row for {} and {}", owner_id, file_id);
        return;
    }

    eLog("[Dfs] Response for request collection: {} / {}", owner_id, file_id);

    auto historical_message = std::make_tuple(owner_id, file_id, historical_rows.value());
    auto collection_message =
        std::make_tuple(owner_id, file_id, rows.has_value() ? rows.value() : std::vector<DbRow> {});

    responder.send_response(historical_message,
                            MessageType::DfsCollectionHistory,
                            SendMode::Focused,
                            MessageStatus::Response);

    responder.send_response(collection_message,
                            MessageType::DfsCollectionContent,
                            SendMode::Focused,
                            MessageStatus::Response);
}

// TODO: checks
void DfsService::network_response_historical_collection(
    const ActorId                              &owner_id,
    const std::string                          &file_id,
    const std::vector<HistoricalCollectionRow> &historical_rows) {
    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    // TODO: check state

    auto main_actor = node->account_controller()->system_actor();
    // auto template_link = Json::deserialize<CollectionTemplateLink>(historical_rows.begin()->data).value();
    // collection
    auto first_row = historical_rows.begin(); // where id = 0

    Dfs::CollectionTemplate collection_template;
    if (first_row->operation == CollectionOperation::StructuralTemplated) {
        auto collection_template_result = Json::deserialize<Dfs::CollectionTemplate>(first_row->data);
        if (!collection_template_result.has_value()) {
            return;
        }
        collection_template = collection_template_result.value();
    } else if (first_row->operation == CollectionOperation::Structural) {
        auto template_link = Json::deserialize<Dfs::CollectionTemplateLink>(first_row->data);
        if (!template_link.has_value()) {
            return;
        }

        auto collection_template_result =
            Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(template_link->owner_id,
                                                                               template_link->file_id);
        if (!collection_template_result.has_value()) {
            return;
        }
        collection_template = collection_template_result.value();
    }

    auto chain = HistoricalCollection::create(node, main_actor, owner_id, file_id, collection_template);

    if (!chain.has_value()) {
        return;
    }

    auto dfs_path = Dfs::Path::file_path(owner_id, file_id);
    if (!dfs_path->exists()) {
        return;
    }

    DbConnector db(chain->get_historical_path().native());
    db.open();
    for (const auto &historical_row : historical_rows) {
        // TODO: verify
        auto db_row = Utils::to_dbrow(historical_row);
        db.replace(Dfs::Historical::HISTORICAL_TABLE, db_row);
    }
    db.close();
}

// TODO: checks
void DfsService::network_response_content_collection(const ActorId            &owner_id,
                                                     const std::string        &file_id,
                                                     const std::vector<DbRow> &db_rows) {
    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    // TODO: check state

    auto main_actor = node->account_controller()->system_actor();

    auto chain_opt = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    if (!chain_opt.has_value()) {
        return;
    }
    auto chain = chain_opt.value();

    auto creation_result = chain.get_creation();
    if (!creation_result.has_value()) {
        // remove historical and file
        return;
    }

    Dfs::CollectionTemplate collection_template;

    std::visit(
        [&](const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, Dfs::CollectionTemplateLink>) {
                auto template_opt =
                    Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(value.owner_id,
                                                                                       value.file_id);
                if (template_opt.has_value()) {
                    collection_template = template_opt.value();
                }
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, Dfs::CollectionTemplate>) {
                collection_template = value;
            }
        },
        creation_result.value());

    auto schema_opt = collection_template.to_db_schema();
    if (!schema_opt.has_value()) {
        return;
    }

    DbConnector db(chain.get_file_path().native());
    if (!db.open()) {
        eWarning("[Dfs] Cannot open collection database");
        return;
    }

    auto table_result = db.create_table(schema_opt.value());
    if (!table_result.has_value()) {
        eWarning("[Dfs] Cannot create collection table: error {}", static_cast<int>(table_result.error()));
        return;
    }

    for (const auto &db_row : db_rows) {
        if (!db.insert(schema_opt->table_name(), db_row)) {
            eWarning("[Dfs] Cannot insert a row into collection table {}", schema_opt->table_name());
            return;
        }
    }
    db.close();

    Dfs::FileLinkFragment file_link_fragment;
    file_link_fragment.file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = file_id };
    file_link_fragment.fragment_numbers.emplace(1);
    load_manager_.remove_active_download(file_link_fragment);

    // check if history and file ok
    load_manager_.finish_him(owner_id, dir_row.value());
}

void DfsService::network_change_collection(const ActorId                 &owner_id,
                                           const std::string             &file_id,
                                           const HistoricalCollectionRow &row,
                                           const Responder               &responder) {
    // TODO: need verify
    auto main_actor = node->account_controller()->system_actor();
    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);

    if (!dir_row.has_value()) {
        return;
    }

    if (dir_row->state != Dfs::FileState::Ready) {
        // return;
    }

    auto chain = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    if (!chain.has_value()) {
        return;
    }
    chain->insert_row_to_database(row);
    auto res = chain->change_collection(row);
    if (res.has_value()) {
        // dir time update
        dirs_manager_.update_dirs(owner_id, row.timestamp);
    }

    // TODO: broadcast
    responder.send_response(std::make_tuple(owner_id, file_id, row),
                            MessageType::DfsCollectionRowChange,
                            SendMode::Except,
                            MessageStatus::NoStatus);

    notify_collection_changed(owner_id, dir_row.value(), row);
}

void DfsService::network_request_vector(const ActorId     &owner_id,
                                        const std::string &file_id,
                                        const Responder   &responder) {
    auto dirRowExp =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    auto main_actor = node->account_controller()->current_profile().main()->get();
    auto encryption = dirRow.encryption ? Dfs::DataSecurity::Encrypted : Dfs::DataSecurity::Public;
    auto dfs_vector =
        DfsVector::load(node, main_actor, owner_id, file_id, encryption, Dfs::DataSecurityData(), dirRow.type);

    if (!dfs_vector.has_value()) {
        return;
    }

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> rows =
        dfs_vector->generate_content_package();
    if (!rows.has_value() && rows.error() != DfsVectorError::CollectionEmpty) {
        eCritical("[DfsCollection] Can't find row for {} and {}", owner_id, file_id);
        return;
    }
    // An empty vector still has to be answered: staying silent left the requester
    // without the vector files forever (the dir row replicates, the payload never
    // does, and nothing retries). Freshly created vectors are exactly this case.
    //
    // The answer must carry the template even when there are no rows. A package with
    // only owner_id/file_id set is undeliverable: handle_package rejects it at
    // `vector_template.fields().size() == 0` and the receiver drops it — 952 such
    // rejections in the first three minutes of a run. Rebuild the package with an
    // explicitly empty row set instead of hand-rolling a stub.
    Dfs::Packets::DfsVectorContentPackage package;
    if (rows.has_value()) {
        package = rows.value();
    } else {
        auto empty = dfs_vector->generate_content_package_empty();
        if (!empty.has_value()) {
            eWarning("[DfsCollection] Can't build empty package for {} / {}", owner_id, file_id);
            return;
        }
        package = empty.value();
    }

    responder.send_response(package, MessageType::DfsVectorContent, SendMode::Focused, MessageStatus::Response);
}

std::expected<std::pair<Dfs::DirRow, DfsVector>, DfsVectorError> DfsService::make_vector(
    const ActorId               &owner_id,
    const std::string           &file_id,
    bool                         is_network,
    const ActorId               &signer_id,
    const Dfs::DataSecurityData &security_data) {
    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);

    if (!dir_row.has_value()) {
        eLog("[Dfs] make_vector: no dir_row for {} / {}", owner_id, file_id);
        return std::unexpected(DfsVectorError::Unknown);
    }
    // if (dir_row->state == Dfs::FileState::Ready) {
    //     return std::unexpected(DfsVectorError::Unknown);
    // }

    auto signer_actor = node->account_controller()->current_profile().get_actor(
        !signer_id.is_zero() ? signer_id : node->account_controller()->current_profile().main_id());
    auto encryption = dir_row->encryption ? Dfs::DataSecurity::Encrypted : Dfs::DataSecurity::Public;

    if (!signer_actor.has_value()) {
        eLog("[Dfs] make_vector: no signer actor for {} / {}", owner_id, file_id);
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto dfs_vector = !is_network ? DfsVector::load(node,
                                                    signer_actor.value(),
                                                    owner_id,
                                                    file_id,
                                                    encryption,
                                                    security_data,
                                                    dir_row->type)
                                  : DfsVector::load_network(node,
                                                            signer_actor.value(),
                                                            owner_id,
                                                            file_id,
                                                            encryption,
                                                            security_data,
                                                            dir_row->type);

    if (!dfs_vector.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return std::pair { dir_row.value(), dfs_vector.value() };
}

void DfsService::network_response_content_vector(
    const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) { // check hash
    ThreadPoolBoost::instance_dfs()->post([this, dfs_vector_content] {
        eLog("[Dfs] Vector content package: {} / {}", dfs_vector_content.owner_id, dfs_vector_content.file_id);
        auto dfs_vector_result = make_vector(dfs_vector_content.owner_id, dfs_vector_content.file_id, true);
        if (!dfs_vector_result.has_value()) {
            eWarning("[Dfs] Vector content package: make_vector failed for {} / {}",
                     dfs_vector_content.owner_id,
                     dfs_vector_content.file_id);
            return;
        }

        auto &[dir_row, dfs_vector] = dfs_vector_result.value();

        bool res_handle = dfs_vector.handle_package(dfs_vector_content);
        if (!res_handle) {
            eWarning("[Dfs] Vector content package: handle failed for {} / {}",
                     dfs_vector_content.owner_id,
                     dfs_vector_content.file_id);
            Dfs::FileLinkFragment failed_fragment;
            failed_fragment.file_link =
                Dfs::FileLink { .owner_id = dfs_vector_content.owner_id, .file_id = dfs_vector_content.file_id };
            failed_fragment.fragment_numbers.emplace(1);
            load_manager_.remove_active_download(failed_fragment);
            schedule_after(std::chrono::seconds(30),
                           [this, owner_id = dfs_vector_content.owner_id, file_id = dfs_vector_content.file_id] {
                               request_vector_content(owner_id, file_id);
                           });
            return;
        }

        Dfs::FileLinkFragment completed_fragment;
        completed_fragment.file_link =
            Dfs::FileLink { .owner_id = dfs_vector_content.owner_id, .file_id = dfs_vector_content.file_id };
        completed_fragment.fragment_numbers.emplace(1);
        load_manager_.remove_active_download(completed_fragment);
        load_manager_.finish_him(dfs_vector_content.owner_id, dir_row);
    });
}

void DfsService::network_vector_add(const ActorId &owner_id, const std::string &file_id, const DbRow &row) {
    // Off the dispatch thread, like network_response_content_vector next door. This path
    // writes sqlite, and since the connection now waits for a contended write lock
    // instead of dropping the row, doing it inline could stall message dispatch for
    // seconds — the same starvation that used to push consensus traffic out of the
    // acceptance window behind bulk transfers.
    ThreadPoolBoost::instance_dfs()->post([this, owner_id, file_id, row] {
        auto res = make_vector(owner_id, file_id);
        if (!res.has_value()) {
            boost::asio::post(node->serial_executor(), [this, owner_id, file_id] {
                request_vector_content(owner_id, file_id);
            });
            return;
        }

        auto &[dir_row, dfs_vector] = res.value();
        auto operation_res          = dfs_vector.local_add(row, true);
        // load_manager_.finish_him(owner_id, dir_row);

        if (!operation_res) {
            // Was silent before: a row rejected here is a chat message the user never
            // sees, and nothing re-requests it (docs/TODO.md 0.45).
            eWarning("[Dfs] Vector row not stored: {} / {}", owner_id, file_id);
        }

        auto hash_size = dfs_vector.data_hash_size();
        if (hash_size.has_value()) {
            dir_row.hash          = hash_size.value().first;
            dir_row.size          = hash_size.value().second;
            dir_row.last_modified = std::stoull(row.at("timestamp")); // try catch
            Dfs::Tables::DirsFile::ActorSpace::update_file_metadata(dirs_manager_.get_db_instance(),
                                                                    owner_id,
                                                                    dir_row,
                                                                    false);
        }

        if (operation_res) {
            // dirs_manager_.update_dirs(owner_id, dir_row.last_modified);
            if (row.at("status") == "1") {
                notify_vector_row_added(owner_id, dir_row, row);
            } else {
                notify_vector_row_removed(owner_id, dir_row, row);
            }
            node->thoth_manager()->dfs_vector_add_check(owner_id, file_id, row);
        }
    });
}

void DfsService::network_request_file_state(const ActorId     &owner_id,
                                            const std::string &file_id,
                                            const Responder   &responder) {
    auto dir_row =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);

    if (!dir_row.has_value()) {
        auto file_state =
            Dfs::Packets::FileState { .owner_id = owner_id, .file_id = file_id, .state = Dfs::FileState::Unknown };
        responder.send_response(file_state, MessageType::DfsFileState, SendMode::Focused, MessageStatus::Response);
        return;
    }

    auto available_state = dir_row->state;
    if (available_state == Dfs::FileState::Ready
        && !is_file_already_downloaded(owner_id, file_id, dir_row->hash)) {
        // Metadata can arrive before content. Do not advertise such a row as
        // a usable source: the requester would otherwise retry a peer that
        // cannot serve the file.
        available_state = Dfs::FileState::Known;
    }

    auto file_state = Dfs::Packets::FileState { .owner_id = owner_id,
                                                .file_id  = file_id,
                                                .state    = available_state,
                                                .hash     = dir_row->hash };
    responder.send_response(file_state, MessageType::DfsFileState, SendMode::Focused, MessageStatus::Response);
}

void DfsService::network_request_file_existance(const Dfs::FileLink &file_link, const Responder &responder) {
    auto dir_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(),
                                                                  file_link.owner_id,
                                                                  file_link.file_id);

    if (!dir_row.has_value())
        return;

    responder.send_response(file_link,
                            MessageType::DfsFileRequestContinueUpload,
                            SendMode::Focused,
                            MessageStatus::Response);
}

void DfsService::network_response_file_state(const Dfs::Packets::FileState &data, const Responder &responder) {
    auto dir_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(),
                                                                  data.owner_id,
                                                                  data.file_id);

    eLog("[Dfs] File state response: {}/{} state={}", data.owner_id, data.file_id, data.state);

    if (!dir_row.has_value()) {
        return;
    }

    if (data.state == Dfs::FileState::Ready) {
        dir_row->state = data.state;
        dir_row->hash  = data.hash;
        load_manager_.add_to_queue(data.owner_id,
                                   dir_row.value(),
                                   *responder.identifiers().begin(),
                                   data.notify_neighbours);
    }
}

void DfsService::network_file_exist_notification(const Dfs::Packets::FileState &data, const Responder &responder) {
    // TODO: check light node or not and some logic do we want to download new file or not
    //  auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(data.owner_id, data.file_id);

    // if (!dir_row.has_value()) {
    //     return;
    // }

    // if (data.state == Dfs::FileState::Ready) {
    //     dir_row->state = data.state;
    //     dir_row->hash  = data.hash;
    //     load_manager_.add_to_queue(data.owner_id, dir_row.value(), *responder.identifiers().begin());
    // }
}

std::expected<void, bool> DfsService::remove_stored_file(const ActorId &owner_id, const std::string &file_id) {
    auto db_instance = dirs_manager_.get_db_instance();
    auto dir_row     = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, file_id);
    if (!dir_row.has_value()) {
        return std::unexpected(false);
    }

    auto actor = node->account_controller()->current_profile().get_actor(owner_id);
    if (!actor.has_value()) {
        eWarning("[Dfs] Can't remove file, because no owner");
        return std::unexpected(false);
    }

    auto last_modified     = Utils::current_date_ms();
    dir_row->hash          = "";
    dir_row->folder        = std::nullopt;
    dir_row->name          = "";
    dir_row->size          = 0;
    dir_row->state         = Dfs::FileState::Removed;
    dir_row->last_modified = last_modified;
    auto hash              = dir_row->calculate_hash(owner_id);
    auto sign              = actor.value().get().key().sign(hash);
    if (!sign.has_value()) {
        return std::unexpected(false); // sign
    }
    auto remove_file = Dfs::Packets::RemoveFile { .owner_id      = owner_id,
                                                  .file_id       = file_id,
                                                  .sign          = sign.value(),
                                                  .last_modified = last_modified };

    auto remove_result = remove_local_file(owner_id, file_id);
    if (!remove_result.has_value()) {
        return std::unexpected(false);
    }
    Dfs::Tables::DirsFile::ActorSpace::update_file_state(db_instance, owner_id, file_id, Dfs::FileState::Removed);
    Dfs::Tables::DirsFile::ActorSpace::update_file_after_stored_remove(db_instance,
                                                                       remove_file.owner_id,
                                                                       remove_file.file_id,
                                                                       remove_file.sign,
                                                                       remove_file.last_modified);
    Dfs::Tables::DirsFile::DirsSpace::update_row(db_instance, owner_id, remove_file.last_modified);

    node->network()->send_broadcast(remove_file, MessageType::DfsFileRemove);
    notify_removed(owner_id, file_id);
    return {};
}

void DfsService::network_remove_stored_file(const ActorId     &owner_id,
                                            const std::string &file_id,
                                            const Signature   &sign,
                                            std::uint64_t      last_modified) {
    auto db_instance = dirs_manager_.get_db_instance();
    auto dir_row     = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    auto dir_row_new = dir_row.value();

    auto actor = node->actor_index()->read_actor(owner_id);
    if (!actor.has_value()) {
        eWarning("[Dfs] Can't remove file, because no owner {}", actor.error());
        return;
    }

    dir_row->hash          = "";
    dir_row->folder        = std::nullopt;
    dir_row->name          = "";
    dir_row->size          = 0;
    dir_row->state         = Dfs::FileState::Removed;
    dir_row->last_modified = last_modified;
    auto hash              = dir_row_new.calculate_hash(owner_id);
    auto verify            = actor.value().key().verify(hash, sign);
    if (!verify) {
        eWarning("[Dfs] Can't verify file remove {} / {}", owner_id, file_id);
        return;
    }

    auto remove_result = remove_local_file(owner_id, file_id);
    if (!remove_result.has_value()) {
        eWarning("[Dfs] Cannot remove local file {} / {}", owner_id, file_id);
        return;
    }
    Dfs::Tables::DirsFile::ActorSpace::update_file_state(db_instance, owner_id, file_id, Dfs::FileState::Removed);
    Dfs::Tables::DirsFile::ActorSpace::update_file_after_stored_remove(db_instance,
                                                                       owner_id,
                                                                       file_id,
                                                                       sign,
                                                                       last_modified);
    Dfs::Tables::DirsFile::DirsSpace::update_row(db_instance, owner_id, last_modified);

    // sizeTaken--, totalDfsSize--
    notify_removed(owner_id, file_id);
}

std::expected<void, bool> DfsService::remove_local_file(const ActorId &owner_id, const std::string &file_id) {
    auto file_path = Dfs::Path::file_path(owner_id, file_id);
    if (!file_path.has_value()) {
        return std::unexpected(false);
    }

    std::error_code error;
    if (std::filesystem::exists(file_path->native(), error)) {
        std::filesystem::remove(file_path->native(), error);
    }
    if (error) {
        eWarning("[Dfs] Cannot remove local file {}: {}",
                 file_path->string().value_or("<invalid path>"),
                 error.message());
        return std::unexpected(false);
    }

    Dfs::Tables::DirsFile::ActorSpace::update_file_state(dirs_manager_.get_db_instance(),
                                                         owner_id,
                                                         file_id,
                                                         Dfs::FileState::Known);
    notify_local_removed(owner_id, file_id);
    return {};
}

void DfsService::broadcast_stored(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    auto file_data = Dfs::FileData { .owner_id = owner_id, .dir_row = dir_row };
    node->network()->send_broadcast(file_data, MessageType::DfsStoreFile);
}

void DfsService::sync_stored(const Dfs::FileData &file_data, const Responder &responder) {
    responder.send_response(file_data, MessageType::DfsStoreFile, SendMode::Focused, MessageStatus::Response);
}

std::string DfsService::network_store_file(const ActorId        &owner_id,
                                           const Dfs::DirRow    &dir_row,
                                           Dfs::NetworkStoreFile network_stote) {
    std::string actorFolderPath =
        DfsB::DFS_FOLDER + Utils::platformDelimeter() + owner_id.to_string() + Utils::platformDelimeter();
    // std::string actrDirFilePath = actorFolderPath + DfsB::fsMapName;

    if (is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash)) {
        eSuccess("[Dfs] Ignoring file download: file already exists 👌😎👍");
        return "";
    }

    if (dir_row.type == Dfs::FileType::Folder) {
        auto db_instance = dirs_manager_.get_db_instance();
        auto dir_row2    = dir_row;
        dir_row2.state   = Dfs::FileState::Ready;
        DbRow dirRowDb   = Utils::to_dbrow(dir_row2);
        if (auto it = dirRowDb.find("prev_file_id"); it != dirRowDb.end() && it->second.empty()) {
            dirRowDb.erase(it);
        }
        bool insertRes = db_instance->replace(DfsT::DirsFile::TableNameActorsFiles, dirRowDb);

        eLog("[addFolder] owner={}, name={}, file_id={}, result={}",
             owner_id.to_string(),
             dir_row.name,
             dir_row.file_id,
             insertRes);

        if (!insertRes) {
            eLog("[Dfs] addFolder: insert failed");
            return "";
        }

        dirs_manager_.update_dirs(owner_id, dir_row.last_modified);
        notify_added(owner_id, dir_row2);

        eLog("[Dfs] Folder {}/{} was synced from network", owner_id, dir_row.file_id);
        return dir_row.file_id;
    }

    if (!writeAvailable(dir_row.size) && !std::filesystem::is_empty(actorFolderPath)) {
        // TODO: control space size, use file priority and time

        // std::vector<std::filesystem::path> files;
        // for (const auto &file : std::filesystem::directory_iterator(actorFolderPath)) {
        //     const auto fileName = file.path().filename();
        //     if (fileName == DfsB::fsMapName || fileName == DfsB::dsStoreExtention) {
        //         continue;
        //     }

        //     if (file.is_regular_file()) {
        //         files.push_back(file);
        //     }
        // }

        // std::sort(files.begin(), files.end(), [=](const std::filesystem::path p1, const
        // std::filesystem::path p2) {
        //     return std::filesystem::last_write_time(p1).time_since_epoch()
        //            > std::filesystem::last_write_time(p2).time_since_epoch();
        // });

        // while (!writeAvailable(dir_row.size) || std::filesystem::is_empty(actorFolderPath)) {
        //     removeLocalFile(owner_id, files.at(files.size() - 1).string());
        // }
    }

    auto             db_instance = dirs_manager_.get_db_instance();
    std::unique_lock size_state_lock(size_state_mutex_);
    auto previous_row = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(db_instance, owner_id, dir_row.file_id);

    auto dir_row2  = dir_row;
    dir_row2.state = Dfs::FileState::Known;
    if (previous_row.has_value() && previous_row->state == Dfs::FileState::Ready
        && previous_row->type == dir_row.type && previous_row->size == dir_row.size
        && previous_row->hash == dir_row.hash) {
        dir_row2.state = Dfs::FileState::Ready;
    }
    DbRow dirRowDb = Utils::to_dbrow(dir_row2);
    if (auto it = dirRowDb.find("prev_file_id"); it != dirRowDb.end() && it->second.empty()) {
        dirRowDb.erase(it);
    }
    bool insertRes = db_instance->replace(DfsT::DirsFile::TableNameActorsFiles, dirRowDb);

    eLog("[addFile] owner={}, name={}, file_id={}, result={}",
         owner_id.to_string(),
         dir_row.name,
         dir_row.file_id,
         insertRes);

    if (!insertRes) {
        auto errorStr = fmt::format("[Dfs] addFile: insert failed:{} {}",
                                    db_instance->file().c_str(),
                                    DfsT::DirsFile::TableNameActorsFiles.c_str());
        eLog("{}", errorStr);
        eFatal("Error 2: {}", errorStr);
        return "";
    }

    const auto previous_total = previous_row.has_value() ? previous_row->size : 0;
    const auto previous_local =
        previous_row.has_value() && previous_row->state == Dfs::FileState::Ready ? previous_row->size : 0;
    const auto current_local = dir_row2.state == Dfs::FileState::Ready ? dir_row2.size : 0;
    if (dir_row2.size >= previous_total) {
        m_totalDfsSize.fetch_add(dir_row2.size - previous_total);
    } else {
        m_totalDfsSize.fetch_sub(previous_total - dir_row2.size);
    }
    if (current_local >= previous_local) {
        m_sizeTaken.fetch_add(current_local - previous_local);
    } else {
        m_sizeTaken.fetch_sub(previous_local - current_local);
    }
    size_state_lock.unlock();

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // if (network_stote && dir_row.type == Dfs::FileType::File) {
    //     if (dir_row.size >= m_bytesLimit - m_sizeTaken) {
    //         return dir_row.file_id;
    //     } else {
    //         DfsP::RequestFileSegmentMessage reqMessage = { .actorId = owner_id,
    //                                                        .file_id = dir_row.file_id,
    //                                                        .hash    = dir_row.hash,
    //                                                        .offset  = 0 };
    //         // node->network()->send_message(reqMessage,
    //         //                               MessageType::DfsRequestFileSegment,
    //         //                               SendMode::AllParents,
    //         //                               MessageStatus::Request);
    //     }
    // }

    // if (network_stote && dir_row.type == Dfs::FileType::Collection) {
    //     node->network()->send_message(std::make_pair(owner_id, dir_row.file_id),
    //                                   MessageType::DfsCollectionRequest,
    //                                   SendMode::AllParents,
    //                                   MessageStatus::Request);
    // }

    // if (dir_row.type == Dfs::FileType::Vector) {
    // return dir_row.file_id;
    // }

    // insertToFiles(dir_row);

    if (dir_row.type == Dfs::FileType::File && network_stote == Dfs::NetworkStoreFile::Broadcast) {
        notify_stored(owner_id, dir_row2);

        // Full nodes replicate content, not only metadata: without this the
        // gossiped row lands as Known and the file itself is never fetched.
        if (mode() == DfsMode::Full && dir_row2.state != Dfs::FileState::Ready) {
            request_file(owner_id, dir_row.file_id);
        }
    }

    notify_added(owner_id, dir_row2);

    std::string stored_added = network_stote == Dfs::NetworkStoreFile::Broadcast ? "stored" : "added";
    eLog("[Dfs] File {}/{} was {}", owner_id, dir_row.file_id, stored_added);

    return dir_row.file_id;
}

// TODO: remove?
std::string DfsService::getFileFromStorage(const ActorId &owner_id, const std::string &file_name) {
    auto localOwner = node->account_controller()->current_profile().get_actor(owner_id);
    if (!localOwner.has_value()) {
        // eFatal("Can't get actor: {}", owner_id);
    }
    std::string           pathDelim    = Utils::platformDelimeter();
    const std::string     ownerPath    = DfsB::DFS_FOLDER + pathDelim + owner_id.to_string() + pathDelim;
    std::filesystem::path realFilePath = fmt::format("{}{}", ownerPath, file_name);

    std::vector<DbRow> actrDirData =
        DfsT::DirsFile::ActorSpace::getFileDataByName(dirs_manager_.get_db_instance(), owner_id, file_name);
    std::filesystem::path tempFilePath = fmt::format("temp{}{}", pathDelim, owner_id.to_string());
    if (!actrDirData.empty()) {
        std::filesystem::path virtualFilePath = actrDirData.at(0).at("file_id");
        if ((virtualFilePath.end()--)->string() == "secured") {
            if (!localOwner->get().empty()) {
                std::filesystem::create_directories(tempFilePath);
                tempFilePath /= virtualFilePath.filename();
                // localOwner->key().decrypt_self_file(realFilePath, tempFilePath);
                return tempFilePath.string();
            }
        }
    }

    return realFilePath.string();
}

std::string DfsService::create_file_id(std::filesystem::path file) {
    return create_file_id_from(file.string());
}

std::string DfsService::create_file_id_from(const std::string &data) {
    int64_t                                   time = std::chrono::system_clock::now().time_since_epoch().count();
    boost::mt11213b                           rng(time);
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string                               salt = Tools::typeToStdStringBytes<int>(dist(rng));

    std::string file_id =
        Utils::calculate_hash(fmt::format("{}{}{}", data, std::to_string(time), salt)).substr(0, 64);
    return file_id;
}

std::uint64_t DfsService::sizeTaken() const {
    return m_sizeTaken.load();
}

std::uint64_t DfsService::totalDfsSize() const {
    return m_totalDfsSize.load();
}

void DfsService::increaseSizeTaken(uintmax_t value) {
    m_sizeTaken.fetch_add(value);
}

void DfsService::completeDownloadedFile(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    std::lock_guard lock(size_state_mutex_);
    auto            current =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, dir_row.file_id);
    if (!current.has_value() || current->state == Dfs::FileState::Ready) {
        return;
    }
    Dfs::Tables::DirsFile::ActorSpace::update_file_state(dirs_manager_.get_db_instance(),
                                                         owner_id,
                                                         dir_row.file_id,
                                                         Dfs::FileState::Ready);
    refresh_calculate();
}

std::expected<void, ExportFileError> DfsService::export_file(const ActorId                &owner_id,
                                                             const std::string            &file_id,
                                                             const FsPath                 &output_folder,
                                                             const std::optional<KeyPass> &key) {
    if (!output_folder.exists()) {
        return std::unexpected(ExportFileError::OutupDirNotExits);
    }

    auto is_dir = output_folder.is_directory();
    if (!is_dir.has_value()) {
        return std::unexpected(ExportFileError::OutupDirNotExits);
    }
    if (!is_dir.value()) {
        return std::unexpected(ExportFileError::OutupDirNotExits);
    }

    auto has_write_perm = output_folder.has_write_permission();
    if (!has_write_perm.has_value()) {
        return std::unexpected(ExportFileError::NoWritePermissions);
    }
    if (!has_write_perm.value()) {
        return std::unexpected(ExportFileError::NoWritePermissions);
    }

    auto dir_row_result =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(dirs_manager_.get_db_instance(), owner_id, file_id);

    if (!dir_row_result.has_value()) {
        return std::unexpected(ExportFileError::DirRowNotExists);
    }

    if (dir_row_result->state != Dfs::FileState::Ready) {
        return std::unexpected(ExportFileError::FileNotReadyState);
    }

    auto dfs_path_result = Dfs::Path::file_path(owner_id, file_id);
    if (!dfs_path_result.has_value()) {
        return std::unexpected(ExportFileError::IncorrectDfsPath);
    }

    auto dfs_path = dfs_path_result.value();
    if (!dfs_path.exists()) {
        return std::unexpected(ExportFileError::LocalFileNotExists);
    }

    bool is_downloaded = is_file_already_downloaded(owner_id, file_id, dir_row_result->hash);
    if (!is_downloaded) {
        return std::unexpected(ExportFileError::LocalFileNotValid);
    }

    auto output_path = output_folder;

    if (dir_row_result->encryption) {
        auto encrypted_name = Utils::from_base64(dir_row_result->name);

        if (key.has_value()) {
            if (!encrypted_name.has_value()) {
                return std::unexpected(ExportFileError::Unknown);
            }

            auto res = Cryptography::symmetric_decrypt(ByteArray(encrypted_name.value()).toBytes(), key.value());
            if (res.has_value()) {
                auto name = ByteArray(res.value()).toString();
                if (!output_path.append(name).has_value()) {
                    return std::unexpected(ExportFileError::Unknown);
                }

                if (output_path.exists()) {
                    return std::unexpected(ExportFileError::OutputFileExists);
                }
            }

            auto decrypt_result = Cryptography::symmetric_decrypt_file(dfs_path, output_path, key.value());
            if (!decrypt_result.has_value()) {
                return std::unexpected(ExportFileError::Unknown);
            }
            return {};
        } else {
            auto actor = node->account_controller()->current_profile().get_actor(owner_id);
            if (!actor.has_value()) {
                return std::unexpected(ExportFileError::Unknown);
            }

            if (actor.has_value() && encrypted_name.has_value()) {
                auto res = actor->get().key().decrypt_self(ByteArray(encrypted_name.value()).toBytes());
                if (res.has_value()) {
                    auto name = ByteArray(res.value()).toString();
                    if (!output_path.append(name).has_value()) {
                        return std::unexpected(ExportFileError::Unknown);
                    }

                    if (output_path.exists()) {
                        return std::unexpected(ExportFileError::OutputFileExists);
                    }
                }
            }

            auto decrypt_result = actor->get().key().decrypt_self_file(dfs_path, output_path);
            if (!decrypt_result.has_value()) {
                return std::unexpected(ExportFileError::Unknown);
            }
            return {};
        }
    }

    if (!output_path.append(dir_row_result->name).has_value()) {
        return std::unexpected(ExportFileError::Unknown);
    }
    if (output_path.exists()) {
        return std::unexpected(ExportFileError::OutputFileExists);
    }

    try {
        std::filesystem::copy(dfs_path.native(), output_path.native());
    } catch (const std::filesystem::filesystem_error &e) {
        return std::unexpected(ExportFileError::CopyError);
    }

    return {};
}

Dfs::DfsSize DfsService::calculate_size() {
    Dfs::DfsSize dfs_size;

    auto db_instance = dirs_manager_.get_db_instance();
    auto rows =
        db_instance->select(fmt::format("SELECT "
                                        "SUM(CASE WHEN state = {} THEN size ELSE 0 END) as size_taken, "
                                        "SUM(size) as size_total "
                                        "FROM {}",
                                        int(Dfs::FileState::Ready),
                                        Dfs::Tables::DirsFile::TableNameActorsFiles));

    if (rows.empty()) {
        return dfs_size;
    }

    try {
        dfs_size.all   = std::stoull(rows[0].at("size_total"));
        dfs_size.local = std::stoull(rows[0].at("size_taken"));
    } catch (std::exception &e) {
    }

    m_totalDfsSize = dfs_size.all;
    m_sizeTaken    = dfs_size.local;

    return dfs_size;
}

std::expected<std::pair<std::string, std::optional<std::string>>, Dfs::DfsError> DfsService::encrypt_name(
    const std::string                &visual_name,
    const std::optional<std::string> &visual_folder,
    Dfs::DataSecurity                 data_security,
    const Dfs::DataSecurityData      &security_data) {
    std::string                visual_name_new   = visual_name;
    std::optional<std::string> visual_folder_new = visual_folder;

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto actor = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!actor.has_value()) {
                return std::unexpected(Dfs::DfsError::Unknown);
            }

            auto encrypted_name = actor->get().key().encrypt_self(ByteArray(visual_name_new).toBytes());
            if (!encrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(encrypted_name.value());

            // Don't encrypt folder if it's a file_id (hex string) - folder name is encrypted in its own DirRow
            // Only encrypt if it's a visual path (legacy behavior)
            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':'
                && !Utils::is_hex_string(visual_folder_new.value())) {
                auto encrypted_folder =
                    actor->get().key().encrypt_self(ByteArray(visual_folder_new.value()).toBytes());
                if (!encrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(encrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
            auto receiver = node->actor_index()->read_actor_old(security_actor->receiver_id);

            auto encrypted_name =
                sender->get().key().encrypt(ByteArray(visual_name_new).toBytes(), receiver.key().public_key());
            if (!encrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(encrypted_name.value());

            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':') {
                auto encrypted_folder = sender->get().key().encrypt(ByteArray(visual_folder_new.value()).toBytes(),
                                                                    receiver.key().public_key());
                if (!encrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(encrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Key) {
        if (auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            auto encrypted_name =
                Cryptography::symmetric_encrypt(ByteArray(visual_name_new).toBytes(), security_key->key);
            if (!encrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(encrypted_name.value());

            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':') {
                auto encrypted_folder =
                    Cryptography::symmetric_encrypt(ByteArray(visual_folder_new.value()).toBytes(),
                                                    security_key->key);

                if (!encrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(encrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    return std::pair { visual_name_new, visual_folder_new };
}

std::expected<std::pair<std::string, std::optional<std::string>>, Dfs::DfsError> DfsService::decrypt_name(
    const std::string                &visual_name,
    const std::optional<std::string> &visual_folder,
    Dfs::DataSecurity                 data_security,
    const Dfs::DataSecurityData      &security_data) {
    std::string                visual_name_new   = visual_name;
    std::optional<std::string> visual_folder_new = visual_folder;

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto actor = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!actor.has_value()) {
                return std::unexpected(Dfs::DfsError::Unknown);
            }

            auto decrypted_name = actor->get().key().decrypt_self(ByteArray(visual_name_new).toBytes());
            if (!decrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(decrypted_name.value());

            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':') {
                auto decrypted_folder =
                    actor->get().key().decrypt_self(ByteArray(visual_folder_new.value()).toBytes());
                if (!decrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(decrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
            auto receiver = node->actor_index()->read_actor_old(security_actor->receiver_id);

            auto decrypted_name =
                sender->get().key().decrypt(ByteArray(visual_name_new).toBytes(), receiver.key().public_key());
            if (!decrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(decrypted_name.value());

            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':') {
                auto decrypted_folder = sender->get().key().decrypt(ByteArray(visual_folder_new.value()).toBytes(),
                                                                    receiver.key().public_key());
                if (!decrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(decrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Key) {
        if (auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            auto decrypted_name =
                Cryptography::symmetric_decrypt(ByteArray(visual_name_new).toBytes(), security_key->key);
            if (!decrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(decrypted_name.value());

            if (visual_folder_new.has_value() && visual_folder_new.value().front() != ':') {
                auto decrypted_folder =
                    Cryptography::symmetric_decrypt(ByteArray(visual_folder_new.value()).toBytes(),
                                                    security_key->key);

                if (!decrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(decrypted_folder.value());
            }

        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    return std::pair { visual_name_new, visual_folder_new };
}

std::uint64_t DfsService::calculateDataAmountStored(const std::string &folder) const {
    std::size_t size = 0;

    // TODO: Fragments: Use new style
    // for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
    //     if (entry.is_regular_file() && entry.path().extension() == DfsF::Extension) {
    //         const auto actorId = ActorId(entry.path().parent_path().filename().string());
    //         size += DfsT::ActorDirFile::dataAmountStoredSize(actorId, entry.path().filename().string());
    //     } else if (entry.is_directory()) {
    //         size += calculateDataAmountStored(entry.path().string());
    //     }
    // }
    return size;
}

DirsManager &DfsService::dirs_manager() {
    return dirs_manager_;
}

LoadManager &DfsService::download_manager() {
    return load_manager_;
}

size_t DfsService::load_manager_downloads_size() {
    return load_manager_.active_downloads_size();
}

void DfsService::check_all_files(std::string identifier) {
    auto db_instance = dirs_manager_.get_db_instance();
    auto dirs        = Dfs::Tables::DirsFile::DirsSpace::load_all(db_instance);
    if (!dirs.has_value()) {
        return;
    }

    for (const auto &dir : dirs.value()) {
        bool is_full   = mode() == DfsMode::Full;
        bool need_load = is_full || is_priority(dir.actor_id);
        if (!need_load) {
            continue;
        }

        const auto dir_rows = Dfs::Tables::DirsFile::ActorSpace::get_dir_rows(db_instance, dir.actor_id);
        if (!dir_rows.has_value()) {
            //
            continue;
        }

        for (const auto &row : dir_rows.value()) {
            if (row.type == Dfs::FileType::File && !need_load) {
                continue;
            }

            if (row.state == Dfs::FileState::Ready) {
                auto file_path = Dfs::Path::file_path(dir.actor_id, row.file_id);
                if (!file_path.has_value()) {
                    continue;
                }

                if (row.type == Dfs::FileType::File && file_path->exists()) {
                    auto size = file_path->file_size();
                    if (size.has_value() && size == row.size) {
                        continue;
                    }

                    if (!need_load) {
                        continue;
                    }
                }

                if (row.type != Dfs::FileType::File && file_path->exists()) {
                    // TODO: vectorupdate
                    // continue;
                }
                // TODO: add checks for vector and collection
            }

            if (row.state == Dfs::FileState::Removed) {
                continue;
            }

            auto file_link = Dfs::FileLink { .owner_id = dir.actor_id, .file_id = row.file_id };

            // TODO: insert to queue

            // TODO: process from queue
            // search file
            if (identifier.empty()) {
                this->node->network()->send_message(file_link,
                                                    MessageType::DfsFileState,
                                                    SendMode::Neighbours,
                                                    MessageStatus::Request);
            } else {
                Responder responder(nullptr);
                responder.add_identifier(identifier);
                this->node->network()->send_message(file_link,
                                                    MessageType::DfsFileState,
                                                    SendMode::Focused,
                                                    MessageStatus::Request,
                                                    responder);
            }
        }
    }

    // bool is_downloaded = node->dfs()->is_file_already_downloaded(file_link.owner_id,
    //                                                              file_link.file_id,
    //                                                              active_download.dir_row.hash);
    // if (!is_downloaded) {5
    // }
}

std::vector<ActorId> DfsService::startup_sync_actors() const {
    std::set<ActorId> actors;

    actors.insert(node->network_id());
    actors.insert(startup_metadata_actors_.begin(), startup_metadata_actors_.end());
    actors.insert(priority_actors_.begin(), priority_actors_.end());
    {
        std::lock_guard lock(requested_sync_actors_mutex_);
        actors.insert(requested_sync_actors_.begin(), requested_sync_actors_.end());
    }

    for (const auto &actor_id : node->account_controller()->accounts_ids()) {
        actors.insert(actor_id);
    }

    // The chat actor owns the chat list, invites and chat vectors; without it the
    // Light-mode filter drops its dirs rows and chats never appear on a clean profile.
    if (auto chat_actor = node->account_controller()->chat_actor(); chat_actor.has_value()) {
        actors.insert(chat_actor->get().id());
    }

    for (const auto &file_link : priority_file_link_) {
        actors.insert(file_link.owner_id);
    }

    std::erase_if(actors, [](const ActorId &actor_id) {
        return actor_id.is_zero();
    });

    return { actors.begin(), actors.end() };
}

void DfsService::sync(const std::string &identifier) {
    ThreadPoolBoost::instance_dfs()->post([this, identifier]() {
        // Not once-per-process: a file left in a non-final state (peer had it only
        // as Known when we first asked, or our queue was lost to a restart mid-
        // download) gets re-offered on every sync until it actually lands.
        check_all_files(identifier);

        // Light pulls the whole catalogue exactly like Full: it saves on payloads, not
        // on knowing what exists. Only Selective asks for a narrowed actor list.
        if (mode() != DfsMode::Selective) {
            dirs_manager_.temp_sync_all(identifier);
            return;
        }

        auto       actors           = startup_sync_actors();
        const auto responses_before = staged_startup_response_count();

        eLog("[Dfs] Staged startup sync: identifier={}, actors={}", identifier, actors.size());
        dirs_manager_.temp_sync_actors(identifier, actors);

        // 3s: prod nodes without staged support don't respond at all, and every clean
        // sync used to pay this timeout in full (was 15s).
        constexpr auto stagedFallbackDelay = std::chrono::seconds(3);
        schedule_after(stagedFallbackDelay, [this, identifier, responses_before]() {
            ThreadPoolBoost::instance_dfs()->post([this, identifier, responses_before]() {
                if (mode() != DfsMode::Selective) {
                    return;
                }

                if (staged_startup_response_count() != responses_before) {
                    return;
                }

                eWarning("[Dfs] Staged startup sync fallback to full sync: identifier={}", identifier);
                dirs_manager_.temp_sync_all(identifier);
            });
        });
    });
}

bool DfsService::refresh_actors(const std::vector<ActorId> &actors) {
    std::set<ActorId> uniqueActors;
    for (const auto &actor : actors) {
        if (!actor.is_zero()) {
            uniqueActors.insert(actor);
        }
    }

    if (uniqueActors.empty()) {
        return false;
    }

    auto identifiers = node->network()->active_connection_identifiers();
    if (identifiers.empty()) {
        eLog("[Dfs] Targeted actor refresh deferred: no active connections");
        return false;
    }

    // Otherwise the Selective filter in network_response_dir_rows would drop the response to this
    // request.
    bool has_new_actors = false;
    {
        std::lock_guard lock(requested_sync_actors_mutex_);
        for (const auto &actor : uniqueActors) {
            has_new_actors = requested_sync_actors_.insert(actor).second || has_new_actors;
        }
    }

    std::vector<ActorId> requestedActors(uniqueActors.begin(), uniqueActors.end());
    const auto           responses_before = staged_startup_response_count();
    ThreadPoolBoost::instance_dfs()->post(
        [this, identifiers = std::move(identifiers), actors = std::move(requestedActors)]() {
            for (const auto &identifier : identifiers) {
                eLog("[Dfs] Targeted actor refresh: identifier={}, actors={}", identifier, actors.size());
                dirs_manager_.temp_sync_actors(identifier, actors);
            }
        });

    // Prod nodes without staged support ignore the targeted request (same as in sync()).
    // If no staged response arrives within 3s, request a full sync: network_response_dir_rows
    // will filter the response, and the requested actors are already in allowed.
    // Only for new actors: periodic refreshes (raccoon from ClientController) without new
    // actors shouldn't have to pull the full ~600-actor dump every time.
    if (has_new_actors) {
        constexpr auto stagedFallbackDelay = std::chrono::seconds(3);
        schedule_after(stagedFallbackDelay, [this, responses_before]() {
            ThreadPoolBoost::instance_dfs()->post([this, responses_before]() {
                if (staged_startup_response_count() != responses_before) {
                    return;
                }
                auto identifiers = node->network()->active_connection_identifiers();
                for (const auto &identifier : identifiers) {
                    eWarning("[Dfs] Targeted actor refresh fallback to full sync: identifier={}", identifier);
                    dirs_manager_.temp_sync_all(identifier);
                }
            });
        });
    }
    return true;
}

// TODO: use dfs size
void DfsService::sendSizeRequestMsg(const ActorId &actorId) const {
    DfsP::RequestDfsSize msg { .actorId = actorId };
    node->network()->send_message(msg, MessageType::RequestDfsSize, SendMode::Neighbours, MessageStatus::Request);
}

void DfsService::sendSizeReponseMsg(const Dfs::Packets::RequestDfsSize &msg, const Responder &responder) {
    const auto            dfsSize = m_sizeTaken.load(); // calculate_size().local;
    DfsP::ResponseDfsSize response { .actorId = msg.actorId, .size = dfsSize };
    responder.send_response(response, MessageType::ResponseDfsSize, SendMode::Focused, MessageStatus::Response);
}

float DfsService::percentVerified(std::vector<Dfs::Packets::VerifyFileMessage> &fileList) {
    float result             = 0.0;
    int   countFilesVerified = 0;
    for (const auto &msg : fileList) {
        if (msg.verified) {
            countFilesVerified++;
        }
    }
    result = ((float)countFilesVerified / (float)fileList.size()) * 100;
    return result;
}

std::uint64_t DfsService::bytesLimit() const {
    return m_bytesLimit;
}

std::uint64_t DfsService::bytesAvailable() {
    auto          freeDfs  = m_bytesLimit <= m_sizeTaken ? Dfs::Basic::minDfsLimit : m_bytesLimit - m_sizeTaken;
    std::uint64_t freeDisk = Utils::diskFreeMemory();
    auto          min      = m_bytesLimit == 0 ? freeDisk : std::min(freeDfs, freeDisk);
    return min;
}

bool DfsService::writeAvailable(std::size_t size) {
    return bytesAvailable() > size + 10000;
}
