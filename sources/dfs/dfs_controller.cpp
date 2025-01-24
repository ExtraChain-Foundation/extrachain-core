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
    m_sizeTaken    = calculateSizeTaken();
    m_totalDfsSize = calculateFilesSize();
    // loadBytesLimit();
    eLog("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable());

    connect(node->actorIndex(), &ActorIndex::actorSaved, [this](ActorId actor_id) {
        Dfs::initialize_actor_folder(actor_id);
    });
}

DfsController::~DfsController() {
    eInfo("DfsController::~DfsController()");
}

std::expected<Dfs::DirRow, Dfs::DfsError> DfsController::store_file(const ActorId               &owner_id,
                                                                    const ActorId               &author_id,
                                                                    const std::filesystem::path &file_path,
                                                                    const std::string           &visual_folder,
                                                                    const std::string           &visual_name,
                                                                    Dfs::DataSecurity            data_security,
                                                                    const Dfs::DataSecurityData &security_data) {
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

    auto fpath         = FsPath::create(file_path).value();
    auto new_file_path = fpath;

    // TODO: check path, check :***
    auto name_res = NameValidator::validate(visual_name);
    if (!name_res.has_value()) {
        eLog("[Dfs] Can't load file: invalid name");
        return std::unexpected(Dfs::DfsError::InvalidName);
    }

    std::string newTargetVirtualFilePath = (!visual_folder.empty() ? visual_folder + "/" : "") + visual_name;

#ifdef ANDROID
    auto tempPath =
        "dfs/temp"
        + QString::number(QRandomGenerator::global()->bounded(1000) + QDateTime::currentMSecsSinceEpoch());
    QFile::copy(newFilePath.string().c_str(), tempPath);
    fpath       = tempPath.toStdString();
    newFilePath = fpath;
#endif

    if (!new_file_path.exists()) {
        eInfo("[Dfs] Can't load file: file doesn't exist");
        return std::unexpected(Dfs::DfsError::NotExists);
    }

    if (!new_file_path.is_regular_file()) {
        eInfo("[Dfs] This is not a file");
        return std::unexpected(Dfs::DfsError::NotFile);
    }

    std::ifstream my_file(new_file_path.native());
    if (!my_file) {
        eWarning("[Dfs] Can't read file");
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

    // create new dir row
    Dfs::DirRow dir_row = { .actor_id      = author_id,
                            .file_id       = file_id,
                            .prev_file_id  = "",
                            .hash          = file_hash,
                            .name          = visual_name,
                            .size          = file_size_dfs.value(),
                            .created       = 0,
                            .last_modified = 0,
                            .type          = Dfs::FileType::File,
                            .encryption    = data_security,
                            .state         = Dfs::FileState::Ready };
    if (!visual_folder.empty()) {
        dir_row.folder = visual_folder;
    }

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
    std::string file_id  = create_file_id_from("db");
    auto        dfs_path = Dfs::Path::file_path(owner_id, file_id).value();
    auto        actor    = node->accountController()->currentProfile().get_actor(owner_id);
    if (!actor.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto chain =
        HistoricalCollection::create(node, actor.value(), actor->get().id(), file_id, collection_template);
    if (!chain.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }

    auto schema = collection_template.to_db_schema();
    if (!schema.has_value()) {
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
                            .encryption    = data_security,
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

std::expected<DbRow, CollectionError> DfsController::get_collection_row(
    const ActorId               &owner_id,
    const std::string           &file_id,
    uint32_t                     id,
    const Dfs::DataSecurityData &security_data) {
    auto main_actor = node->accountController()->mainActor();
    auto chain      = HistoricalCollection::load(node, main_actor, owner_id, file_id);
    auto row        = chain->get_collection_rows("WHERE id=" + std::to_string(id));
    return row.value()[0];
}

std::expected<std::vector<DbRow>, CollectionError> DfsController::get_collection_rows(
    const ActorId               &owner_id,
    const std::string           &file_id,
    const Dfs::DataSecurityData &security_data,
    const std::string           &where_statement) {
    auto main_actor = node->accountController()->mainActor();
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
        historical_row = chain->add_row(row, dir_row_result->encryption, security_data);
        break;
    case CollectionOperation::Update:
        historical_row = chain->update_row(id, row, dir_row_result->encryption, security_data);
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
                                  Config::Net::TypeSend::AllParents);

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

        if (dir_row->type == Dfs::FileType::Collection) {
            auto [collection_hash, collection_size] =
                Dfs::Tables::ActorDirFile::calculate_collection_hash_size(owner_id, file_id);
            if (collection_hash == hash) {
                return true;
            }
        }
    }

    return false;
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
                                               const std::string &message_id) {
    auto dirRowExp = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dirRowExp.has_value()) {
        return;
    }
    auto dirRow = dirRowExp.value();

    auto main_actor = node->accountController()->mainActor();
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
    node->network()->send_message(std::make_tuple(owner_id, file_id, historical_rows.value()),
                                  MessageType::DfsCollectionHistory,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);

    node->network()->send_message(std::make_tuple(owner_id,
                                                  file_id,
                                                  rows.has_value() ? rows.value() : std::vector<DbRow> {}),
                                  MessageType::DfsCollectionContent,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);
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

    auto main_actor = node->accountController()->mainActor();
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
        auto template_link = Json::deserialize<CollectionTemplateLink>(first_row->data);
        if (!template_link.has_value()) {
            return;
        }

        auto collection_template_result =
            Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_link->actor_id,
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

    auto main_actor = node->accountController()->mainActor();

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
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, CollectionTemplateLink>) {
                auto template_opt =
                    Dfs::Tables::ActorDirFile::get_collection_template_file_id(value.actor_id, value.file_id);
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
    emit downloaded(owner_id, dir_row.value());
    emit collectionDownloaded();
    // sendFile(owner_id, dir_row->file_id);
}

void DfsController::network_change_collection(const ActorId                 &owner_id,
                                              const std::string             &file_id,
                                              const HistoricalCollectionRow &row,
                                              const std::string             &message_id) {
    // TODO: need verify
    auto main_actor = node->accountController()->mainActor();
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
    node->network()->send_message(std::make_tuple(owner_id, file_id, row),
                                  MessageType::DfsCollectionRowChange,
                                  Config::Net::TypeSend::Except,
                                  MessageStatus::NoStatus,
                                  message_id);

    emit collectionChanged(owner_id, dir_row.value(), row);
}

void DfsController::network_request_file_state(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               std::string        message_id) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

    if (!dir_row.has_value()) {
        auto file_state =
            Dfs::Packets::FileState { .owner_id = owner_id, .file_id = file_id, .state = Dfs::FileState::Unknown };
        node->network()->send_message(file_state,
                                      MessageType::DfsFileState,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Response,
                                      message_id);
        return;
    }

    auto file_state =
        Dfs::Packets::FileState { .owner_id = owner_id, .file_id = file_id, .state = dir_row->state };
    node->network()->send_message(file_state,
                                  MessageType::DfsFileState,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);
}

void DfsController::network_response_file_state(const ActorId     &owner_id,
                                                const std::string &file_id,
                                                Dfs::FileState     state,
                                                std::string        identifier) {
    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);

    if (!dir_row.has_value()) {
        return;
    }

    if (state == Dfs::FileState::Ready) {
        dir_row->state = state;
        load_manager_.add_to_queue(owner_id, dir_row.value(), identifier);
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

    auto hash = dir_row->calculate_hash(true);
    auto sign = actor.value().get().key().sign(hash);
    if (!sign.has_value()) {
        return std::unexpected(false); // sign
    }
    auto remove_file = Dfs::Packets::RemoveFile { .owner_id      = owner_id,
                                                  .file_id       = file_id,
                                                  .sign          = sign.value(),
                                                  .last_modified = Utils::current_date_ms() };

    remove_local_file(owner_id, file_id);
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, file_id, Dfs::FileState::Removed);
    // update last time
    // update dirs
    node->network()->send_message(remove_file, MessageType::DfsFileRemove, Config::Net::TypeSend::Broadcast);
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
        eWarning("[Dfs] Can't remove file, because no owner");
        return;
    }

    auto hash   = dir_row_new.calculate_hash(true);
    auto verify = actor.value().key().verify(hash, sign);
    if (!verify) {
        return;
    }

    remove_local_file(owner_id, file_id);
    Dfs::Tables::ActorDirFile::update_file_state(owner_id, file_id, Dfs::FileState::Removed);
    dir_row_new.last_modified = last_modified;
    dir_row_new.sign          = sign;
    Dfs::Tables::ActorDirFile::update_file_metadata(owner_id, dir_row_new);
    // update dirs

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
    node->network()->send_message(file_data, MessageType::DfsStoreFile, Config::Net::TypeSend::Broadcast);
}

void DfsController::sync_stored(const Dfs::FileData &file_data, const std::string &message_id) {
    node->network()->send_message(file_data,
                                  MessageType::DfsStoreFile,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  message_id);
}

std::string DfsController::network_store_file(const ActorId        &owner_id,
                                              const Dfs::DirRow    &dir_row,
                                              Dfs::NetworkStoreFile network_stote) {
    std::string actorFolderPath =
        DfsB::fsActrRoot + Utils::platformDelimeter() + owner_id.to_string() + Utils::platformDelimeter();
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
    //         //                               Config::Net::TypeSend::AllParents,
    //         //                               MessageStatus::Request);
    //     }
    // }

    // if (network_stote && dir_row.type == Dfs::FileType::Collection) {
    //     node->network()->send_message(std::make_pair(owner_id, dir_row.file_id),
    //                                   MessageType::DfsCollectionRequest,
    //                                   Config::Net::TypeSend::AllParents,
    //                                   MessageStatus::Request);
    // }

    // insertToFiles(dir_row);

    if (network_stote == Dfs::NetworkStoreFile::Broadcast) {
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
    const std::string     ownerPath       = DfsB::fsActrRoot + pathDelim + owner_id.to_string() + pathDelim;
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
        std::filesystem::path actorFolderPath = DfsB::fsActrRoot + "/" + actorId.to_string();
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

std::uint64_t DfsController::calculateSizeTaken(const std::string &folder) const {
    std::size_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file()) {
            size += entry.file_size();
        } else if (entry.is_directory()) {
            size += calculateSizeTaken(entry.path().string());
        }
    }

    return size;
}

std::uint64_t DfsController::calculateFilesSize(const std::string &folder) const {
    std::size_t size = 0;

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.path().filename() == Dfs::Basic::fsMapName) {
            const auto actorId = ActorId(entry.path().parent_path().filename().string());
            size += DfsT::ActorDirFile::totalFileSize(actorId);
        } else if (entry.is_directory()) {
            size += calculateFilesSize(entry.path().string());
        }
    }

    return size;
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
    node->network()->send_message(msg,
                                  MessageType::RequestDfsSize,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

void DfsController::sendSizeReponseMsg(const Dfs::Packets::RequestDfsSize &msg,
                                       const std::string                  &messageId) const {
    const auto            dfsSize = calculateSizeTaken();
    DfsP::ResponseDfsSize response { .actorId = msg.actorId, .size = dfsSize };
    node->network()->send_message(response,
                                  MessageType::ResponseDfsSize,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  messageId);
}

void DfsController::sendCountRequestMsg(const ActorId &actorId) const {
    DfsP::RequestDfsSize msg { .actorId = actorId };
    node->network()->send_message(msg,
                                  MessageType::RequestBlockCount,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

void DfsController::sendCountReponseMsg(const Dfs::Packets::RequestBlockCount &msg,
                                        const std::string                     &messageId,
                                        BigNumber                              dfsCount) const {
    DfsP::ResponseBlockCount response { .actorId = msg.actorId, .blockCount = dfsCount };
    node->network()->send_message(response,
                                  MessageType::ResponseBlockCount,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  messageId);
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
