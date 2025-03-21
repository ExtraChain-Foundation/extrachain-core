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

#include "dfs/dfs_controller.h"

#include "blockchain/actor_index.h"
#include "dfs/dfs_utils.h"
#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_manager.h"
#include "dfs/name_validator.h"
#include "dfs/collection_template.h"
#include "dfs/dirs_manager.h"
#include "dfs/load_manager.h"

DfsController::DfsController(ExtraChainNode *node)
    : QObject(node)
    , node(node)
    , dirs_manager_(DirsManager(node))
    , load_manager_(LoadManager(node)) {
    refresh_calculate();
    // loadBytesLimit();
    eLog("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable());

    connect(node->actorIndex(), &ActorIndex::actorSaved, [this](ActorId actor_id) {
        Dfs::initialize_actor_folder(actor_id);
    });

#ifdef IS_RC
    this->dfs_mode_ = DfsMode::Light;
#endif

    auto settings = Utils::read_settings();
    if (settings.dfs_mode.has_value()) {
        this->dfs_mode_ = settings.dfs_mode.value();
    } else {
        settings.dfs_mode = this->dfs_mode_;
        Utils::write_settings(settings);
    }
}

DfsController::~DfsController() {
    eLog("DfsController::~DfsController()");
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_file(const ActorId               &owner_id,
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

    auto search_result =
        Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(owner_id, visual_folder, visual_name);
    if (search_result.has_value()) {
        return std::unexpected(Dfs::DfsError::DirDuplicate);
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

    constexpr uintmax_t MB_500 = 500ULL * 1024 * 1024; // 524'288'000
    if (file_size_.value() > MB_500) {
        return std::unexpected(Dfs::DfsError::MaxFileSize);
    }

    // TODO: check path, check :***
    auto name_res = NameValidator::validate(visual_name);
    if (!name_res.has_value()) {
        eLog("[Dfs] Can't load file: invalid name");
        return std::unexpected(Dfs::DfsError::InvalidName);
    }

#ifdef ANDROID
    auto tempPath =
        "dfs/temp"
        + QString::number(QRandomGenerator::global()->bounded(1000) + QDateTime::currentMSecsSinceEpoch());
    QFile::copy(newFilePath.string().c_str(), tempPath);
    fpath       = tempPath.toStdString();
    newFilePath = fpath;
#endif

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
    if (!writeAvailable(file_size)) {
        return std::unexpected(Dfs::DfsError::StorageFull);
    }

    std::string           file_id = create_file_id(file_path);
    std::filesystem::path place_in_dfs =
        DfsB::fsActrRootW + DfsB::separator + owner_id.toQString().toStdWString() + DfsB::separator;
    auto dfs_path = Dfs::Path::file_path(owner_id, file_id).value();

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

    auto visual_name_new   = visual_name;
    auto visual_folder_new = visual_folder.empty() ? std::nullopt : std::make_optional(visual_folder);

    if (data_security == Dfs::DataSecurity::Public) {
        try {
            std::filesystem::create_directories(place_in_dfs.c_str());
#ifdef ANDROID
            std::filesystem::rename(newFilePath, dfsPath);
#else
            std::filesystem::copy(new_file_path.native(), dfs_path.native());
#endif
        } catch (std::filesystem::filesystem_error const &err) {
            eWarning("[Dfs] Copy error: {}", err.what());
            return std::unexpected(Dfs::DfsError::NotWritable);
        }
    } else {
        eLog("security_data = {}", security_data);
    }

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto actor = node->accountController()->currentProfile().get_actor(security_self->my_actor);
            if (!actor.has_value()) {
                return std::unexpected(Dfs::DfsError::Unknown);
            }

            auto encrypted_name = actor->get().key().encrypt_self(ByteArray(visual_name_new).toBytes());
            if (!encrypted_name.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
            visual_name_new = Utils::to_base64(encrypted_name.value());

            if (visual_folder_new.has_value()) {
                auto encrypted_folder =
                    actor->get().key().encrypt_self(ByteArray(visual_folder_new.value()).toBytes());
                if (!encrypted_folder.has_value()) {
                    return std::unexpected(Dfs::DfsError::IncorrectEncryption);
                }
                visual_folder_new = Utils::to_base64(encrypted_folder.value());
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
            auto sender   = node->accountController()->currentProfile().get_actor(security_actor->sender_id);
            auto receiver = node->actorIndex()->getActor(security_actor->receiver_id);
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

    auto author_actor = node->accountController()->currentProfile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    auto res = Dfs::Tables::ActorDirFile::add_dir_row(owner_id, dir_row, author_actor.value());
    if (!res) {
        // TODO: remove file?
        return std::unexpected(Dfs::DfsError::DirError);
    }

    increaseSizeTaken(file_size);
    m_totalDfsSize += file_size; // TODO: is need at this place?

    // TODO: Fragments: create

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // insertToFiles(dir_row);
    emit stored(owner_id, dir_row);
    emit added(owner_id, dir_row);

    broadcast_stored(owner_id, dir_row);

    load_manager_.broadcast_stored_file(owner_id, dir_row.file_id);

    return dir_row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_file(const ActorId               &owner_id,
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
    }

    return store_file(owner_id, author_id, file_path, visual_path, visual_name, data_security, security_data);
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_data_as_file(
    const ActorId                  &owner_id,
    const ActorId                  &author_id,
    const std::vector<std::uint8_t> data,
    const std::string              &visual_folder,
    const std::string              &visual_name,
    Dfs::DataSecurity               data_security,
    const Dfs::DataSecurityData    &security_data) {
    std::string file_temp = create_file_id("data");
    std::string temp_path = std::format("tmp/{}", file_temp);

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

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_folder(const ActorId     &owner_id,
                                                                      const std::string &visual_folder) {
    eUnimplemented;
    return {};
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_folder_dapp(const ActorId &owner_id,
                                                                           const ActorId &dmaster_id) {
    eUnimplemented;
    return {};
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_template(
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

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_collection(
    const ActorId                 &owner_id,
    const ActorId                 &author_id,
    const std::string             &visual_name,
    const Dfs::CollectionTemplate &collection_template,
    Dfs::DataSecurity              data_security,
    const Dfs::DataSecurityData   &security_data) {
    auto search_result = Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(owner_id,
                                                                                   Dfs::Basic::TEMPLATE_COLLECTION,
                                                                                   visual_name);
    if (search_result.has_value()) {
        return std::unexpected(Dfs::DfsError::DirDuplicate);
    }

    std::string file_id  = create_file_id_from("db");
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id).value();
    auto        actor    = node->accountController()->currentProfile().get_actor(owner_id);
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
        Dfs::Tables::ActorDirFile::calculate_collection_hash_size(owner_id, file_id);

    auto author_actor = node->accountController()->currentProfile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    Dfs::DirRow dir_row = { .actor_id      = author_id,
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

    bool add_dir_row_result = Dfs::Tables::ActorDirFile::add_dir_row(owner_id, dir_row, author_actor.value());
    if (!add_dir_row_result) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // insertToFiles(dir_row);
    emit stored(owner_id, dir_row);
    broadcast_stored(owner_id, dir_row);

    return dir_row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_collection(
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
        Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_actor_id, template_file_id);

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

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_vector(
    const ActorId                 &owner_id,
    const ActorId                 &author_id,
    const std::string             &visual_name,
    const Dfs::DfsTemplateVariant &vector_template,
    Dfs::DataSecurity              data_security,
    const Dfs::DataSecurityData   &security_data) {
    auto search_result = Dfs::Tables::ActorDirFile::search_file_by_folder_and_name(owner_id,
                                                                                   Dfs::Basic::TEMPLATE_VECTOR,
                                                                                   visual_name);
    if (search_result.has_value()) {
        return std::unexpected(Dfs::DfsError::DirDuplicate);
    }

    std::string file_id  = create_file_id_from("db");
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id).value();
    auto        actor    = node->accountController()->currentProfile().get_actor(owner_id);
    if (!actor.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto res = DfsVector::create(node,
                                 actor.value(),
                                 actor->get().id(),
                                 file_id,
                                 vector_template,
                                 data_security,
                                 security_data);

    auto [collection_hash, collection_size] =
        Dfs::Tables::ActorDirFile::calculate_collection_hash_size(owner_id, file_id);

    auto author_actor = node->accountController()->currentProfile().get_actor(author_id);
    if (!author_actor.has_value()) {
        return std::unexpected(Dfs::DfsError::NoAuthorActor);
    }

    Dfs::DirRow dir_row = { .actor_id      = author_id,
                            .file_id       = file_id,
                            .prev_file_id  = "",
                            .hash          = collection_hash,
                            .folder        = Dfs::Basic::TEMPLATE_VECTOR,
                            .name          = visual_name,
                            .size          = collection_size,
                            .created       = 0,
                            .last_modified = 0,
                            .type          = Dfs::FileType::Vector,
                            .encryption    = data_security != Dfs::DataSecurity::Public,
                            .state         = Dfs::FileState::Ready };

    bool add_dir_row_result = Dfs::Tables::ActorDirFile::add_dir_row(owner_id, dir_row, author_actor.value());
    if (!add_dir_row_result) {
        return std::unexpected(Dfs::DfsError::DirError);
    }

    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    // insertToFiles(dir_row);
    emit stored(owner_id, dir_row);
    broadcast_stored(owner_id, dir_row);

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> rows = res->generate_content_package();
    if (!rows.has_value() && rows.error() != DfsVectorError::CollectionEmpty) {
        eCritical("[DfsCollection] Can't find row for {} and {}", owner_id, file_id);
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    if (!rows.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    node->network()->send_broadcast(rows.value(), MessageType::DfsVectorContent);

    return dir_row;
}

bool DfsController::add_vector_row(const ActorId               &owner_id,
                                   const std::string           &file_id,
                                   DbRow                        row,
                                   const Dfs::DataSecurityData &security_data) {
    auto res = make_vector(owner_id, file_id);
    if (!res.has_value()) {
        return false;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto operation_res          = dfs_vector.store_add(row);
    if (!operation_res) {
        return false;
    }
    // get id?
    emit vectorRowAdded(owner_id, dir_row, row);

    auto package = Dfs::Packets::VectorRowAdd { .owner_id = owner_id, .file_id = file_id, .row = row };
    node->network()->send_broadcast(package, MessageType::DfsVectorAdd);

    return operation_res;
}

bool DfsController::remove_vector_row(const ActorId     &owner_id,
                                      const std::string &file_id,
                                      const ActorId     &actor_id) {
    auto res = make_vector(owner_id, file_id);
    if (!res.has_value()) {
        return false;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto row                    = dfs_vector.remove(actor_id);
    if (!row.has_value()) {
        return false;
    }

    auto package = Dfs::Packets::VectorRowAdd { .owner_id = owner_id, .file_id = file_id, .row = row.value() };
    node->network()->send_broadcast(package, MessageType::DfsVectorAdd);

    // emit vectorRowRemoved(owner_id, dir_row, row);

    // auto package = Dfs::Packets::VectorRowRemove { .owner_id = owner_id, .file_id = file_id, .row = row };
    // node->network()->send_broadcast(package, MessageType::DfsVectorRemove);

    return true;
}

std::expected<DbRow, DfsVectorError> DfsController::get_vector_row(const ActorId     &owner_id,
                                                                   const std::string &file_id,
                                                                   const ActorId     &actor_id) {
    auto v = DfsVector::load(node, node->accountController()->system_actor(), owner_id, file_id);
    if (!v.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto row = v->read_row(actor_id);
    if (!row.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }
    return row;
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_vector(const ActorId     &owner_id,
                                                                      const ActorId     &author_id,
                                                                      const std::string &visual_name,
                                                                      const ActorId     &template_actor_id,
                                                                      const std::string &template_file_id,
                                                                      Dfs::DataSecurity  data_security,
                                                                      const Dfs::DataSecurityData &security_data) {
    auto vector_template =
        Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_actor_id, template_file_id);
    if (!vector_template.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto link =
        Dfs::CollectionTemplateLink { .owner_id = template_actor_id, .file_id = template_file_id, .name = "" };
    return store_vector(owner_id, author_id, visual_name, link, data_security, security_data);
}

std::expected<DbRow, CollectionError> DfsController::get_collection_row(
    const ActorId               &owner_id,
    const std::string           &file_id,
    uint32_t                     id,
    const Dfs::DataSecurityData &security_data) {
    auto main_actor = node->accountController()->system_actor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    auto row        = chain->get_collection_rows("WHERE id=" + std::to_string(id));
    return row.value()[0];
}

std::expected<std::vector<DbRow>, CollectionError> DfsController::get_collection_rows(
    const ActorId               &owner_id,
    const std::string           &file_id,
    const Dfs::DataSecurityData &security_data,
    const std::string           &where_statement) {
    auto main_actor = node->accountController()->system_actor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);

    if (!chain.has_value()) {
        return std::unexpected(CollectionError::CollectionNotFound);
    }

    auto row = chain->get_collection_rows(where_statement);
    return row;
}

ExpectedDirHistoricalRow DfsController::universal_collection_row(const ActorId               &owner_id,
                                                                 const std::string           &file_id,
                                                                 DbRow                        row,
                                                                 std::uint32_t                id,
                                                                 CollectionOperation          type,
                                                                 const Dfs::DataSecurityData &security_data) {
    auto dir_row_result = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row_result.has_value()) {
        return std::unexpected(dir_row_result.error());
    }
    // TODO: check fields

    // TODO: choose sign actor from args
    auto main_actor = node->accountController()->currentProfile().system();
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
    auto [hash, size]     = Dfs::Tables::ActorDirFile::calculate_collection_hash_size(owner_id, file_id);
    dir_row.hash          = hash;
    dir_row.size          = size;

    auto sign = main_actor.key().sign(Utils::calculate_hash(dir_row));
    if (!sign.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    dir_row.sign = sign.value();
    Dfs::Tables::ActorDirFile::update_file_metadata(owner_id, dir_row);
    dirs_manager_.update_dirs(owner_id, dir_row.last_modified);

    node->network()->send_message(std::make_tuple(owner_id, file_id, historical_row.value()),
                                  MessageType::DfsCollectionRowChange,
                                  SendMode::Neighbours);

    return std::pair { dir_row_result.value(), historical_row.value() };
}

bool DfsController::is_file_already_downloaded(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               const std::string &hash) {
    const auto path = Dfs::Path::file_path(owner_id, file_id);
    if (!path.has_value()) {
        eWarning("[Dfs] Add file from network: incorrect dir row for owner {} and hash '{}'", owner_id, hash);
    }

    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value())
        return false; // TODO: temp, need expected
    if (dir_row->hash == hash && dir_row->state != Dfs::FileState::Ready) {
        // return true; // TODO: that's all
    }

    bool exists = path->exists();
    if (exists) {
        // TODO: use cached hash and state from dir row?
        if (dir_row->type == Dfs::FileType::File) {
            auto existing_hash = Utils::calculate_hash_file(path.value());
            if (existing_hash.has_value() && existing_hash.value() == hash) {
                return true;
            }
        }

        if (dir_row->type == Dfs::FileType::Collection || dir_row->type == Dfs::FileType::Vector) {
            auto [collection_hash, collection_size] =
                Dfs::Tables::ActorDirFile::calculate_collection_hash_size(owner_id, file_id);
            if (collection_hash == hash) {
                return true;
            }
        }
    }

    return false;
}

void DfsController::refresh_calculate() {
    auto dfs_size  = calculate_size();
    m_sizeTaken    = dfs_size.local;
    m_totalDfsSize = dfs_size.all;
}

ExpectedDirHistoricalRow DfsController::add_collection_row(const ActorId               &owner_id,
                                                           const std::string           &file_id,
                                                           DbRow                        row,
                                                           const Dfs::DataSecurityData &security_data) {
    auto res = universal_collection_row(owner_id, file_id, row, 0, CollectionOperation::Add, security_data);
    if (res.has_value()) {
        auto &res_ = res.value();

        emit collectionChanged(owner_id, res_.first, res_.second);
    }
    return res;
}

ExpectedDirHistoricalRow DfsController::update_collection_row(const ActorId               &owner_id,
                                                              const std::string           &file_id,
                                                              uint32_t                     id,
                                                              DbRow                        row,
                                                              const Dfs::DataSecurityData &security_data) {
    auto res = universal_collection_row(owner_id, file_id, row, id, CollectionOperation::Update, security_data);
    if (res.has_value()) {
        auto &res_ = res.value();

        emit collectionChanged(owner_id, res_.first, res_.second);
    }
    return res;
}

ExpectedDirHistoricalRow DfsController::remove_collection_row(const ActorId     &owner_id,
                                                              const std::string &file_id,
                                                              uint32_t           id) {
    auto res =
        universal_collection_row(owner_id, file_id, {}, id, CollectionOperation::Remove, Dfs::DataSecurityData());
    if (res.has_value()) {
        auto &res_ = res.value();

        emit collectionChanged(owner_id, res_.first, res_.second);
    }
    return res;
}

void DfsController::network_request_collection(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               const Responder   &responder) {
    auto dirRowExp = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    auto main_actor = node->accountController()->system_actor();
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
void DfsController::network_response_historical_collection(
    const ActorId                              &owner_id,
    const std::string                          &file_id,
    const std::vector<HistoricalCollectionRow> &historical_rows) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    // TODO: check state

    auto main_actor = node->accountController()->system_actor();
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
            Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_link->owner_id,
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
void DfsController::network_response_content_collection(const ActorId            &owner_id,
                                                        const std::string        &file_id,
                                                        const std::vector<DbRow> &db_rows) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    // TODO: check state

    auto main_actor = node->accountController()->system_actor();

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
                    Dfs::Tables::ActorDirFile::get_collection_template_file_id(value.owner_id, value.file_id);
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
    db.open();
    db.create_table(schema_opt.value());
    for (const auto &db_row : db_rows) {
        // TODO: verify
        db.insert(schema_opt->table_name(), db_row);
    }
    db.close();

    // check if history and file ok
    load_manager_.finish_him(owner_id, dir_row.value());
}

void DfsController::network_change_collection(const ActorId                 &owner_id,
                                              const std::string             &file_id,
                                              const HistoricalCollectionRow &row,
                                              const Responder               &responder) {
    // TODO: need verify
    auto main_actor = node->accountController()->system_actor();
    auto dir_row    = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

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

    emit collectionChanged(owner_id, dir_row.value(), row);
}

void DfsController::network_request_vector(const ActorId     &owner_id,
                                           const std::string &file_id,
                                           const Responder   &responder) {
    auto dirRowExp = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    auto main_actor = node->accountController()->system_actor();
    auto dfs_vector = DfsVector::load(node, main_actor, owner_id, file_id);

    if (!dfs_vector.has_value()) {
        return;
    }

    std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> rows =
        dfs_vector->generate_content_package();
    if (!rows.has_value() && rows.error() != DfsVectorError::CollectionEmpty) {
        eCritical("[DfsCollection] Can't find row for {} and {}", owner_id, file_id);
        return;
    }
    if (!rows.has_value()) {
        return;
    }

    responder.send_response(rows.value(),
                            MessageType::DfsVectorContent,
                            SendMode::Focused,
                            MessageStatus::Response);
}

std::expected<std::pair<Dfs::DirRow, DfsVector>, DfsVectorError> DfsController::make_vector(
    const ActorId     &owner_id,
    const std::string &file_id,
    bool               is_network) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }
    // if (dir_row->state == Dfs::FileState::Ready) {
    //     return std::unexpected(DfsVectorError::Unknown);
    // }

    auto main_actor = node->accountController()->system_actor();
    auto dfs_vector = !is_network ? DfsVector::load(node, main_actor, owner_id, file_id)
                                  : DfsVector::load_network(node, main_actor, owner_id, file_id);
    if (!dfs_vector.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return std::pair { dir_row.value(), dfs_vector.value() };
}

void DfsController::network_response_content_vector(
    const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    auto dfs_vector_result = make_vector(dfs_vector_content.owner_id, dfs_vector_content.file_id, true);
    if (!dfs_vector_result.has_value()) {
        return;
    }

    auto &[dir_row, dfs_vector] = dfs_vector_result.value();

    auto res_handle = dfs_vector.handle_package(dfs_vector_content);
    load_manager_.finish_him(dfs_vector_content.owner_id, dir_row);
}

void DfsController::network_vector_add(const ActorId &owner_id, const std::string &file_id, const DbRow &row) {
    auto res = make_vector(owner_id, file_id);
    if (!res.has_value()) {
        return;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto operation_res          = dfs_vector.local_add(row);
    // load_manager_.finish_him(owner_id, dir_row);

    if (operation_res) {
        // dirs_manager_.update_dirs(owner_id, dir_row.last_modified);
        if (row.at("status") == "1") {
            emit vectorRowAdded(owner_id, dir_row, row);
        } else {
            emit vectorRowRemoved(owner_id, dir_row, row);
        }
    }
}

void DfsController::network_vector_remove(const ActorId &owner_id, const std::string &file_id, const DbRow &row) {
    auto res = make_vector(owner_id, file_id);
    if (!res.has_value()) {
        return;
    }

    auto &[dir_row, dfs_vector] = res.value();
    auto row2                   = row;
    auto operation_res          = dfs_vector.local_add(row2);
    // load_manager_.finish_him(owner_id, dir_row);

    if (operation_res) {
        emit vectorRowRemoved(owner_id, dir_row, row);
    }
}

void DfsController::network_request_file_state(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               const Responder   &responder) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

    if (!dir_row.has_value()) {
        auto file_state =
            Dfs::Packets::FileState { .owner_id = owner_id, .file_id = file_id, .state = Dfs::FileState::Unknown };
        responder.send_response(file_state, MessageType::DfsFileState, SendMode::Focused, MessageStatus::Response);
        return;
    }

    auto file_state =
        Dfs::Packets::FileState { .owner_id = owner_id, .file_id = file_id, .state = dir_row->state };
    responder.send_response(file_state, MessageType::DfsFileState, SendMode::Focused, MessageStatus::Response);
}

void DfsController::network_response_file_state(const ActorId     &owner_id,
                                                const std::string &file_id,
                                                Dfs::FileState     state,
                                                const Responder   &responder) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

    if (!dir_row.has_value()) {
        return;
    }

    if (state == Dfs::FileState::Ready) {
        dir_row->state = state;
        load_manager_.add_to_queue(owner_id, dir_row.value(), *responder.identifiers().begin());
    }
}

std::expected<void, bool> DfsController::remove_stored_file(const ActorId &owner_id, const std::string &file_id) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return std::unexpected(false);
    }

    auto actor = node->accountController()->currentProfile().get_actor(owner_id);
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
    auto hash              = dir_row->calculate_hash();
    auto sign              = actor.value().get().key().sign(hash);
    if (!sign.has_value()) {
        return std::unexpected(false); // sign
    }
    auto remove_file = Dfs::Packets::RemoveFile { .owner_id      = owner_id,
                                                  .file_id       = file_id,
                                                  .sign          = sign.value(),
                                                  .last_modified = last_modified };

    remove_local_file(owner_id, file_id);
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, file_id, Dfs::FileState::Removed);
    Dfs::Tables::ActorDirFile::update_file_after_stored_remove(remove_file.owner_id,
                                                               remove_file.file_id,
                                                               remove_file.sign,
                                                               remove_file.last_modified);
    Dfs::DirsFile::update_row(owner_id, remove_file.last_modified);

    node->network()->send_broadcast(remove_file, MessageType::DfsFileRemove);
    emit removed(owner_id, file_id);
    return {};
}

void DfsController::network_remove_stored_file(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               const Signature   &sign,
                                               std::uint64_t      last_modified) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value()) {
        return;
    }
    auto dir_row_new = dir_row.value();

    auto actor = node->actorIndex()->get_actor(owner_id);
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
    auto hash              = dir_row_new.calculate_hash();
    auto verify            = actor.value().key().verify(hash, sign);
    if (!verify) {
        eWarning("[Dfs] Can't verify file remove {} / {}", owner_id, file_id);
        return;
    }

    remove_local_file(owner_id, file_id);
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, file_id, Dfs::FileState::Removed);
    Dfs::Tables::ActorDirFile::update_file_after_stored_remove(owner_id, file_id, sign, last_modified);
    Dfs::DirsFile::update_row(owner_id, last_modified);

    // sizeTaken--, totalDfsSize--
    emit removed(owner_id, file_id);
}

std::expected<void, bool> DfsController::remove_local_file(const ActorId &owner_id, const std::string &file_id) {
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, file_id, Dfs::FileState::Known);
    auto file_path = Dfs::Path::file_path(owner_id, file_id);
    if (!file_path.has_value()) {
        // return error getting path
    }

    auto exists = file_path->exists();
    if (!file_path->exists()) {
        // return no local file
    }

    std::filesystem::remove(file_path->native());
    emit localRemoved(owner_id, file_id);
    return {};
}

void DfsController::broadcast_stored(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    auto file_data = Dfs::FileData { .owner_id = owner_id, .dir_row = dir_row };
    node->network()->send_broadcast(file_data, MessageType::DfsStoreFile);
}

void DfsController::sync_stored(const Dfs::FileData &file_data, const Responder &responder) {
    responder.send_response(file_data, MessageType::DfsStoreFile, SendMode::Focused, MessageStatus::Response);
}

std::string DfsController::network_store_file(const ActorId        &owner_id,
                                              const Dfs::DirRow    &dir_row,
                                              Dfs::NetworkStoreFile network_stote) {
    std::string actorFolderPath =
        DfsB::DFS_FOLDER + Utils::platformDelimeter() + owner_id.to_string() + Utils::platformDelimeter();
    // std::string actrDirFilePath = actorFolderPath + DfsB::fsMapName;

    if (is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash)) {
        eSuccess("[Dfs] Ignoring file download: file already exists 👌😎👍");
        return "";
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

    DbConnector dir_file = Dfs::Tables::ActorDirFile::get_actor_dir_file(owner_id);

    if (!dir_file.is_open()) {
        eLog("No dir file 1");
        return "";
    }

    auto dir_row2   = dir_row;
    dir_row2.state  = Dfs::FileState::Known;
    DbRow dirRowDb  = Utils::to_dbrow(dir_row2);
    bool  insertRes = dir_file.replace(DfsT::ActorDirFile::TableName, dirRowDb);

    if (!insertRes) {
        auto errorStr = fmt::format("[Dfs] addFile: insert failed:{} {}",
                                    dir_file.file().c_str(),
                                    DfsT::ActorDirFile::TableName.c_str());
        eLog("{}", errorStr);
        eFatal("Error 2: {}", errorStr);
        return "";
    }
    dir_file.close();

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
        emit stored(owner_id, dir_row);

        auto file_link = Dfs::FileLink { .owner_id = owner_id, .file_id = dir_row.file_id };
        auto load_info = LoadInfo { .dir_row = dir_row, .last_attempt = std::chrono::system_clock::now() };
        // check real status
        load_info.dir_row.state = Dfs::FileState::Known;
        load_manager_.active_downloads.insert({ file_link, load_info });
    }

    emit added(owner_id, dir_row);
    increaseSizeTaken(dir_row.size);
    m_totalDfsSize += dir_row.size;

    std::string stored_added = network_stote == Dfs::NetworkStoreFile::Broadcast ? "stored" : "added";
    eLog("[Dfs] File {}/{} was {}", owner_id, dir_row.file_id, stored_added);

    return dir_row.file_id;
}

// TODO: remove?
std::string DfsController::getFileFromStorage(const ActorId &owner_id, const std::string &file_name) {
    auto localOwner = node->accountController()->currentProfile().get_actor(owner_id);
    if (!localOwner.has_value()) {
        // eFatal("Can't get actor: {}", owner_id);
    }
    std::string           pathDelim       = Utils::platformDelimeter();
    const std::string     ownerPath       = DfsB::DFS_FOLDER + pathDelim + owner_id.to_string() + pathDelim;
    std::filesystem::path realFilePath    = fmt::format("{}{}", ownerPath, file_name);
    std::string           actrDirFilePath = fmt::format("{}{}", ownerPath, DfsB::fsMapName);
    DbConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        eFatal("Can't open {}", actrDirFilePath);
    }

    std::vector<DbRow>    actrDirData  = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, file_name);
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

std::string DfsController::create_file_id(std::filesystem::path file) {
    return create_file_id_from(file.string());
}

std::string DfsController::create_file_id_from(const std::string &data) {
    int64_t                                   time = std::chrono::system_clock::now().time_since_epoch().count();
    boost::mt11213b                           rng(time);
    boost::random::uniform_int_distribution<> dist(0, INT_MAX);
    std::string                               salt = Tools::typeToStdStringBytes<int>(dist(rng));

    std::string file_id =
        Utils::calculate_hash(fmt::format("{}{}{}", data, std::to_string(time), salt)).substr(0, 64);
    return file_id;
}

std::uint64_t DfsController::sizeTaken() const {
    return m_sizeTaken;
}

std::uint64_t DfsController::totalDfsSize() const {
    return m_totalDfsSize;
}

void DfsController::increaseSizeTaken(uintmax_t value) {
    m_sizeTaken += value;
}

void DfsController::exportFile(const std::string &pathTo,
                               const std::string &pathFrom,
                               const std::string &nameFile) {
    ActorId actorId;

    if (!std::filesystem::exists(pathTo)) {
        std::filesystem::create_directories(pathTo);
    }

    if (pathFrom.find('/') != std::string::npos) {
        size_t pos = pathFrom.rfind('/');
        actorId    = pathFrom.substr(pos + 1, pathFrom.size());
    } else {
        actorId                               = pathFrom;
        std::filesystem::path actorFolderPath = DfsB::DFS_FOLDER + "/" + actorId.to_string();
        exportFile(pathTo, actorFolderPath.string(), nameFile);
    }

    if (actorId.is_zero()) {
        eLog("[Dfs] Path or actorId hadn't been found. Please check in parameters");
        return;
    }

    if (!nameFile.empty()) {
        std::string pathFile      = pathFrom + "/" + nameFile;
        const bool  fileFromExist = std::filesystem::exists(pathFile);
        const bool  folderToExist = std::filesystem::exists(pathTo);
        if (fileFromExist && folderToExist) {
            std::filesystem::copy(pathFile, pathTo);
            auto dirRowsExp = Dfs::Tables::ActorDirFile::get_dir_rows(actorId);
            // TODO: error
            auto dirRows = Dfs::Tables::ActorDirFile::get_dir_rows(actorId).value();
            auto it      = std::find_if(dirRows.begin(), dirRows.end(), [&](Dfs::DirRow &dirRow) {
                transform(dirRow.file_id.begin(), dirRow.file_id.end(), dirRow.file_id.begin(), ::tolower);
                auto lowerNameFile = nameFile;
                transform(lowerNameFile.begin(), lowerNameFile.end(), lowerNameFile.begin(), ::tolower);
                if (dirRow.file_id == lowerNameFile) {
                    if (!std::filesystem::exists(pathTo + "/" + dirRow.visual_path())) {
                        std::filesystem::rename(pathTo + "/" + nameFile, pathTo + "/" + dirRow.visual_path());
                    } else {
                        const auto pathFile = std::filesystem::path(pathTo + "/" + dirRow.visual_path());
                        for (int index = 2; index < 100; index++) {
                            std::string possibleNewFile = pathTo + "/" + pathFile.stem().string() + "_"
                                                          + std::to_string(index) + pathFile.extension().string();
                            if (!std::filesystem::exists(possibleNewFile)) {
                                std::filesystem::rename(pathTo + "/" + nameFile, possibleNewFile);
                                break;
                            }
                        }
                    }
                    eLog("File {} of actor {} extracted", dirRow.visual_path(), actorId);
                    return true;
                }
                return false;
            });
        }
        // } else {
        //     const std::string nameDirectory = pathTo + "/" + actorId.to_string();
        //     std::filesystem::create_directories(nameDirectory);
        //     if (pathFrom.find('/') != std::string::npos) {
        //         for (std::filesystem::directory_entry const &entry :
        //         std::filesystem::directory_iterator(pathFrom)) {
        //             if (entry.path().extension() != DfsF::Extension
        //                 && entry.path().extension() != DfsF::ExtensionJournal
        //                 && entry.path().filename() != DfsB::fsMapName) {
        //                 auto copyTo = (pathTo + "/" + actorId.to_string());
        //                 exportFile(copyTo, pathFrom, entry.path().filename().string());
        //             }
        //         }
        //     }
    }
}

std::expected<void, ExportFileError> DfsController::export_file(const ActorId     &owner_id,
                                                                const std::string &file_id,
                                                                const FsPath      &output_folder) {
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

    auto dir_row_result = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

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

    bool is_downloaded = node->dfs()->is_file_already_downloaded(owner_id, file_id, dir_row_result->hash);
    if (!is_downloaded) {
        return std::unexpected(ExportFileError::LocalFileNotValid);
    }

    auto output_path = output_folder;

    if (dir_row_result->encryption) {
        auto actor = node->accountController()->currentProfile().get_actor(owner_id);
        if (!actor.has_value()) {
            return std::unexpected(ExportFileError::Unknown);
        }

        auto encrypted_name = Utils::from_base64(dir_row_result->name);
        if (actor.has_value() && encrypted_name.has_value()) {
            auto res = actor->get().key().decrypt_self(ByteArray(encrypted_name.value()).toBytes());
            if (res.has_value()) {
                auto name = ByteArray(res.value()).toString();
                output_path.append(name);

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

    output_path.append(dir_row_result->name);
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

Dfs::DfsSize DfsController::calculate_size() {
    Dfs::DfsSize dfs_size;

    auto all_actors = node->actorIndex()->allActors();
    for (const auto &actor_id : all_actors) {
        auto dir_rows = Dfs::Tables::ActorDirFile::get_dir_rows(actor_id);
        if (!dir_rows.has_value()) {
            continue;
        }

        for (const auto &row : dir_rows.value()) {
            dfs_size.all += row.size;

            if (row.state == Dfs::FileState::Ready) {
                dfs_size.local += row.size;
            }
        }
    }

    m_totalDfsSize = dfs_size.all;
    m_sizeTaken    = dfs_size.local;

    return dfs_size;
}

std::uint64_t DfsController::calculateDataAmountStored(const std::string &folder) const {
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

DirsManager &DfsController::dirs_manager() {
    return dirs_manager_;
}

LoadManager &DfsController::download_manager() {
    return load_manager_;
}

void DfsController::sync(const std::string &identifier) {
    load_manager_.check_all_files(identifier);
    dirs_manager_.temp_sync_all(identifier);
    // dirs_manager_.sync(identifier);
}

// TODO: use dfs size
void DfsController::sendSizeRequestMsg(const ActorId &actorId) const {
    DfsP::RequestDfsSize msg { .actorId = actorId };
    node->network()->send_message(msg, MessageType::RequestDfsSize, SendMode::Neighbours, MessageStatus::Request);
}

void DfsController::sendSizeReponseMsg(const Dfs::Packets::RequestDfsSize &msg, const Responder &responder) {
    const auto            dfsSize = calculate_size().local;
    DfsP::ResponseDfsSize response { .actorId = msg.actorId, .size = dfsSize };
    responder.send_response(response, MessageType::ResponseDfsSize, SendMode::Focused, MessageStatus::Response);
}

float DfsController::percentVerified(std::vector<Dfs::Packets::VerifyFileMessage> &fileList) {
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

std::uint64_t DfsController::bytesLimit() const {
    return m_bytesLimit;
}

std::uint64_t DfsController::bytesAvailable() {
    auto          freeDfs  = m_bytesLimit <= m_sizeTaken ? Dfs::Basic::minDfsLimit : m_bytesLimit - m_sizeTaken;
    std::uint64_t freeDisk = Utils::diskFreeMemory();
    auto          min      = m_bytesLimit == 0 ? freeDisk : std::min(freeDfs, freeDisk);
    return min;
}

bool DfsController::writeAvailable(std::size_t size) {
    return bytesAvailable() > size + 10000;
}

void DfsController::loadVPNLocalizationFiles() {
    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();

    auto actors = dirsFile.select(fmt::format("SELECT actor_id FROM {}", DfsT::DirsFile::TableName));
    for (const auto &row : actors) {
        auto        actorId     = ActorId(row.begin()->second);
        DbConnector actrDirFile = DfsT::ActorDirFile::get_actor_dir_file(actorId);

        auto actorRows =
            actrDirFile.select(fmt::format("SELECT file_id FROM {} WHERE name='localizationInfo' AND state={}",
                                           DfsT::ActorDirFile::TableName,
                                           std::to_string(std::to_underlying(Dfs::FileState::Ready))));
        for (const auto &actorRow : actorRows) {
            for (const auto &actorCol : actorRow) {
                auto fileName = actorCol.second;
                emit vpnLocalizationLoadedFromStorage(actorId.to_string(), fileName);
            }
        }

        actrDirFile.close();
    }

    dirsFile.close();
}
