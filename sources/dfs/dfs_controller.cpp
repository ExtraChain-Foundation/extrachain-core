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
#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_manager.h"
#include "dfs/fragment_storage.h"
#include "dfs/name_validator.h"
#include "dfs/collection_template.h"

DfsController::DfsController(ExtraChainNode *node)
    : QObject(node)
    , node(node) {
    std::filesystem::create_directories(DfsB::fsActrRoot);

    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.create_table(DfsT::DirsFile::CreateTableQuery);
    dirsFile.close();

    m_sizeTaken    = calculateSizeTaken();
    m_totalDfsSize = calculateFilesSize();
    // loadBytesLimit();
    eLog("[Dfs] Started. Current size: {}, available: {}", m_sizeTaken, bytesAvailable());

    if (!node->accountController()->empty())
        requestDirFileAllActors();
}

DfsController::~DfsController() {
    eInfo("DfsController::~DfsController()");
}

void DfsController::initializeActor(const ActorId &actorId) {
    std::string pathDelim = Utils::platformDelimeter();
    std::filesystem::create_directories(DfsB::fsActrRoot + pathDelim + actorId.to_string());
    DbConnector actrDirFile = DfsT::ActorDirFile::get_actor_dir_file(actorId);
    actrDirFile.query(DfsT::ActorDirFile::CreateTableQuery);
    requestDirData(actorId);
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

    auto file_size = new_file_path.file_size().value();
    // if size == 0 -> return
    if (!writeAvailable(file_size)) {
        return std::unexpected(Dfs::DfsError::StorageFull);
    }

    std::string           file_id = create_file_id(file_path);
    std::filesystem::path place_in_dfs =
        DfsB::fsActrRootW + DfsB::separator + owner_id.toQString().toStdWString() + DfsB::separator;
    auto dfs_path = DfsPath::file_path(owner_id, file_id).value();

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
            auto actor = node->accountController()->currentProfile().getActor(security_self->my_actor);
            auto res   = actor->key().encrypt_self_file(new_file_path, dfs_path);
            if (!res.has_value()) {
                return std::unexpected(Dfs::DfsError::IncorrectEncryption);
            }
        } else {
            return std::unexpected(Dfs::DfsError::IncorrectSecurityData);
        }
    }

    if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->accountController()->currentProfile().getActor(security_actor->sender_id);
            auto receiver = node->actorIndex()->getActor(security_actor->receiver_id);
            // TODO: checks
            auto res = sender->key().encrypt_file(new_file_path, dfs_path, receiver.key().public_key());
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

    FragmentStorage fs(owner_id, file_id, file_hash);
    fs.initLocalFile(file_size);
    fs.initHistoricalChain();

    updateDirsLastModified(owner_id, dir_row.last_modified);

    insertToFiles(dir_row);
    emit added(owner_id, dir_row);
    sendFile(owner_id, file_id);

    return dir_row;
    // return addFile(msg, false);
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
    auto        dfs_path = DfsPath::file_path(owner_id, file_id).value();
    auto        actor    = node->accountController()->currentProfile().getActor(owner_id);

    auto chain = HistoricalCollection::create(node, actor, actor->id(), file_id, collection_template);
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

    Dfs::DirRow dir_row     = { .actor_id      = author_id,
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

    insertToFiles(dir_row);
    emit added(owner_id, dir_row);
    sendFile(owner_id, file_id);

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
    auto main_actor = node->accountController()->currentProfile().main();
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

    auto sign = main_actor->key().sign(Utils::calculate_hash(dir_row));
    if (!sign.has_value()) {
        return std::unexpected(Dfs::DfsError::Unknown);
    }
    dir_row.sign = sign.value();
    Dfs::Tables::ActorDirFile::update_file_metadata(owner_id, dir_row);

    node->network()->send_message(std::make_tuple(owner_id, file_id, historical_row.value()),
                                  MessageType::DfsCollectionRowChange,
                                  Config::Net::TypeSend::AllParents);

    return std::pair { dir_row_result.value(), historical_row.value() };
}

bool DfsController::is_file_already_downloaded(const ActorId     &owner_id,
                                               const std::string &file_id,
                                               const std::string &hash) {
    const auto path = DfsPath::file_path(owner_id, file_id);
    if (!path.has_value()) {
        eWarning("[Dfs] Add file from network: incorrect dir row for owner {} and hash '{}'", owner_id, hash);
    }

    auto dir_row = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, file_id);
    if (!dir_row.has_value())
        return false; // TODO: temp, need expected
    if (dir_row->hash == hash && dir_row->state != Dfs::FileState::Ready) {
        // return true; // TODO: that's all
    }

    if (auto exists = path->exists(); exists.has_value()) {
        if (exists.value()) {
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

    eLog("[Dfs] Responce for request collection: {} / {}", owner_id, file_id);
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

    auto dfs_path = DfsPath::file_path(owner_id, file_id);
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
    sendFile(owner_id, dir_row->file_id);
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
    chain->change_collection(row);

    node->network()->send_message(std::make_tuple(owner_id, file_id, row),
                                  MessageType::DfsCollectionRowChange,
                                  Config::Net::TypeSend::Except,
                                  MessageStatus::NoStatus,
                                  message_id);

    emit collectionChanged(owner_id, dir_row.value(), row);
}

bool DfsController::removeLocalFile(const ActorId &owner_id, const std::string &fileId) {
    std::string             path = DfsPath::filePath(owner_id, fileId).string();
    DfsP::RemoveFileMessage msg  = { .actorId = owner_id, .file_id = fileId };
    bool                    res  = removeFile(msg);
    node->network()->send_message(msg, MessageType::DfsRemoveFile, Config::Net::TypeSend::AllParents);
    return res;
}

std::string DfsController::network_add_file(const ActorId &owner_id, const Dfs::DirRow &dir_row, bool load_bytes) {
    std::string pathDelim       = Utils::platformDelimeter();
    std::string actorFolderPath = DfsB::fsActrRoot + pathDelim + owner_id.to_string() + pathDelim;
    std::string actrDirFilePath = actorFolderPath + DfsB::fsMapName;
    std::string realFilePath    = actorFolderPath + dir_row.file_id;

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

    if (load_bytes) {
        if (std::filesystem::exists(realFilePath)) {
            eLog("[Dfs] File already exists"); // temp: not correct, add calculate file
            return dir_row.file_id;
        }
        if (!writeAvailable(dir_row.size)) {
            eLog("[Dfs] Storage full");
            eFatal("[Dfs] Storage full");
            return dir_row.file_id;
        }
    }

    if (load_bytes && !std::filesystem::exists(realFilePath)) {
        std::fstream fs;
        fs.open(realFilePath, std::ios::out | std::ios::binary);
        fs.close();
    }

    DbConnector actrDirFile(actrDirFilePath);

    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }

    auto        result       = actrDirFile.select(DfsT::filesTableLast);
    auto        prevRowOpt   = result.empty() ? std::optional<DbRow> {} : result[0];
    std::string lastFileName = prevRowOpt ? prevRowOpt->at("file_id") : "";

    DbRow dirRowDb  = Utils::to_dbrow(dir_row);
    bool  insertRes = actrDirFile.replace(DfsT::ActorDirFile::TableName, dirRowDb);

    if (!insertRes) {
        auto errorStr = fmt::format("[Dfs] addFile: insert failed:{} {}",
                                    actrDirFile.file().c_str(),
                                    DfsT::ActorDirFile::TableName.c_str());
        eLog("{}", errorStr);
        eFatal("Error 2: {}", errorStr);
        return "";
    }
    actrDirFile.close();

    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DfsT::DirsFile::TableName,
                     { { "actorId", dir_row.actor_id.to_string() },
                       { "last_modified", std::to_string(dir_row.last_modified) } });

    if (load_bytes && dir_row.type == Dfs::FileType::File) {
        if (dir_row.size >= m_bytesLimit - m_sizeTaken) {
            return dir_row.file_id;
        } else {
            DfsP::RequestFileSegmentMessage reqMessage = { .actorId = owner_id,
                                                           .file_id = dir_row.file_id,
                                                           .hash    = dir_row.hash,
                                                           .offset  = 0 };
            node->network()->send_message(reqMessage,
                                          MessageType::DfsRequestFileSegment,
                                          Config::Net::TypeSend::AllParents,
                                          MessageStatus::Request);
        }
    }

    if (load_bytes && dir_row.type == Dfs::FileType::Collection) {
        node->network()->send_message(std::make_pair(owner_id, dir_row.file_id),
                                      MessageType::DfsCollectionRequest,
                                      Config::Net::TypeSend::AllParents,
                                      MessageStatus::Request);
    }

    insertToFiles(dir_row);
    emit added(owner_id, dir_row);

    eLog("[Dfs] File {}/{} was added", owner_id, dir_row.file_id);

    return dir_row.file_id;
}

// TODO: remove?
std::string DfsController::getFileFromStorage(const ActorId &owner_id, const std::string &file_name) {
    auto                  localOwner      = node->accountController()->currentProfile().getActor(owner_id);
    std::string           pathDelim       = Utils::platformDelimeter();
    const std::string     ownerPath       = DfsB::fsActrRoot + pathDelim + owner_id.to_string() + pathDelim;
    std::filesystem::path realFilePath    = fmt::format("{}{}", ownerPath, file_name);
    std::string           actrDirFilePath = fmt::format("{}{}", ownerPath, DfsB::fsMapName);
    DbConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        eFatal("Can't open {}", actrDirFilePath);
        exit(EXIT_FAILURE);
    }

    std::vector<DbRow>    actrDirData  = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, file_name);
    std::filesystem::path tempFilePath = fmt::format("temp{}{}", pathDelim, owner_id.to_string());
    if (!actrDirData.empty()) {
        std::filesystem::path virtualFilePath = actrDirData.at(0).at("file_id");
        if ((virtualFilePath.end()--)->string() == "secured") {
            if (!localOwner->empty()) {
                std::filesystem::create_directories(tempFilePath);
                tempFilePath /= virtualFilePath.filename();
                // localOwner->key().decrypt_self_file(realFilePath, tempFilePath);
                return tempFilePath.string();
            }
        }
    }

    return realFilePath.string();
}

bool DfsController::removeFile(const DfsP::RemoveFileMessage &msg) {
    // if (msg.actor != node.accountController()->mainActor()->id().toStdString()) {
    //     eLog("[Dfs] Remove file: file has been removed");
    //     return false;
    // }
    std::string message =
        fmt::format("[Dfs] Remove file {}. Check equal actors. \"msg.Actor\":{}\n\"mainActor:\"{}",
                    msg.file_id,
                    msg.actorId,
                    node->accountController()->mainActor()->id().to_string());
    eLog("{}", message);

    auto dirRow = Dfs::Tables::ActorDirFile::get_dir_row(msg.actorId, msg.file_id);
    if (!dirRow.has_value()) {
        return false;
    }

    removeRowFromDB(msg);
    std::string path = DfsPath::filePath(msg.actorId, msg.file_id).string();

    {
        QFile file(QString::fromStdString(path));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            eLog("Could not open VPN localization file: {}", file.errorString());
        } else {
            QTextStream          in(&file);
            QString              oneLine = in.readLine();
            static const QString prefix  = "Country:";
            if (oneLine.startsWith(prefix))
                emit getRemovedVPNLocalizationInfo(oneLine, msg.actorId.to_string());
        }
    }

    const bool removedFile     = std::filesystem::remove(path);
    const bool removeStorjFile = std::filesystem::remove(fmt::format("{}{}", path, Dfs::Fragments::Extension));
    message                    = fmt::format("[Dfs] Remove file {} - {} by path - {}. Storj file has been - {}.",
                          msg.file_id,
                          (removedFile ? "removed" : "not removed"),
                          path,
                          removeStorjFile ? "removed" : "not removed");
    eLog("{}", message);

    // TODO: use real owner_id
    emit removed(dirRow->actor_id, dirRow.value());
    return removedFile;
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

bool DfsController::renameFile(const ActorId &actor, const std::string &fileHash, const std::string &newFileHash) {
    const std::string     actorId   = actor.to_string();
    std::string           pathDelim = Utils::platformDelimeter();
    std::filesystem::path path      = DfsB::fsActrRoot + pathDelim + actorId + pathDelim;
    std::filesystem::rename(path / std::string(fileHash), path / std::string(newFileHash));
    return std::filesystem::exists(path / std::string(newFileHash));
}

std::string DfsController::insertFragment(const DfsP::SegmentMessage &msg) {
    eLog("[Dfs] Edit file: {}", msg.hash);
    std::string           pathDelim       = Utils::platformDelimeter();
    std::string           actorPath       = DfsB::fsActrRoot + pathDelim + msg.actorId.to_string() + pathDelim;
    std::string           actrDirFilePath = fmt::format("{}{}", actorPath, DfsB::fsMapName);
    std::filesystem::path realFilePath    = fmt::format("{}{}", actorPath, msg.file_id);
    DbConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DbRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.file_id);

    if (actrDirData.empty()) {
        eLog("[Dfs] editFile: Skipped because of empty result");
        eFatal("[Dfs] editFile: Skipped because of empty result: actrDirData || localDirData empty");
        return "";
    }

    if (actrDirData.size() > 2) {
        const auto errorStr =
            fmt::format("[Dfs] editFile: Query select failed: Query result has unsupported size:{}",
                        actrDirData.size());
        eLog("{}", QString::fromStdString(errorStr));
        eFatal("Error 4: {}", errorStr);
        return "";
    }
    insertDataChunk(msg.data, msg.offset, realFilePath);
    actrDirFile.close();
    return Utils::calculate_hash_file(FsPath::create(realFilePath).value()).value();
}

// void DfsController::addListFiles(const QStringList &files) {
//     eLog("Files add in thread id: {} {}", QThread::currentThreadId(), files.size());
//     const auto     actor = node->accountController()->mainActor();
//     ThreadAddFiles addFilesThread(this, actor, files);
//     connect(&addFilesThread, &ThreadAddFiles::added, this,
//             [&](DFSP::AddFileMessage msg, std::string filePath) {
//                 insertToFiles(msg);
//                 emit added(msg.Actor, msg.FileName, msg.Path, msg.Size);
//                 emit resultAddFile("", QString::fromStdString(filePath));
//             });

//     connect(&addFilesThread, &ThreadAddFiles::sendMessage, this,
//             [&](DFSP::AddFileMessage msg, MessageType messageType) {
//                 eLog("send file: {}", msg.FileName);
//                 node->network()->send_message(msg, MessageType::DfsAddFile);
//             });
//     connect(&addFilesThread, &ThreadAddFiles::error, this, [&](std::string error, std::string fileName) {
//         eLog("{}");
//         emit resultAddFile(QString::fromStdString(error), QString::fromStdString(fileName));
//     });
//     addFilesThread.start();
//     addFilesThread.wait();
// }

bool DfsController::insertDataChunk(std::string data, std::uint64_t position, std::filesystem::path file) {
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = BlockchainConst::TMP_FOLDER + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream                     ofs(tempFilePath.string(), std::ios::binary);
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fz = std::filesystem::file_size(file);
    ofs.write(data.c_str(), data.size()); // add data to new temp file
    ofs.flush();
    std::size_t i = 0;
    for (i = position; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream                     ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
        if (i + DfsB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
}

bool DfsController::removeDataChunk(std::uint64_t position, std::uint64_t length, std::filesystem::path file) {
    std::string           pathDelim    = Utils::platformDelimeter();
    std::filesystem::path tempFilePath = BlockchainConst::TMP_FOLDER + pathDelim + file.stem().string();
    std::filesystem::create_directories(tempFilePath.remove_filename());
    tempFilePath = tempFilePath.string() + file.stem().string();
    std::ofstream                     ofs(tempFilePath.string());
    boost::interprocess::file_mapping fmapSource(file.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fz = std::filesystem::file_size(file);
    std::size_t                       i  = 0;
    for (i = position + length; i < fz; i = i + DfsB::sectionSize) { // copy old data to new temp file
        if (i + DfsB::sectionSize < fz) {
            boost::interprocess::mapped_region rightRegion(fmapSource,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        } else {
            boost::interprocess::mapped_region rightRegion(fmapSource, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofs.write(rr_ptr, rightRegion.get_size());
            ofs.flush();
        }
    }
    ofs.close();

    std::filesystem::resize_file(file, position); // cut right side from old file
    std::ofstream                     ofsres(file.c_str(), std::ios::out | std::ios::app | std::ios::binary);
    boost::interprocess::file_mapping fmapTarget(tempFilePath.c_str(), boost::interprocess::read_write);
    std::uint64_t                     fzres = std::filesystem::file_size(tempFilePath);

    for (i = 0; i < fzres; i = i + DfsB::sectionSize) { // copy new data to old file
        if (i + DfsB::sectionSize < fzres) {
            boost::interprocess::mapped_region rightRegion(fmapTarget,
                                                           boost::interprocess::read_write,
                                                           i,
                                                           DfsB::sectionSize);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        } else {
            boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_write, i);
            char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
            ofsres.write(rr_ptr, rightRegion.get_size());
        }
    }
    ofsres.close();

    std::filesystem::remove(tempFilePath);

    return true;
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

void DfsController::insertToFiles(const Dfs::DirRow &dirRow) {
    files[{ dirRow.actor_id, dirRow.file_id }] = dirRow;
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

    for (std::filesystem::directory_entry const &entry : std::filesystem::directory_iterator(folder)) {
        if (entry.is_regular_file() && entry.path().extension() == DfsF::Extension) {
            const auto actorId = ActorId(entry.path().parent_path().filename().string());
            size += DfsT::ActorDirFile::dataAmountStoredSize(actorId, entry.path().filename().string());
        } else if (entry.is_directory()) {
            size += calculateDataAmountStored(entry.path().string());
        }
    }
    return size;
}

std::string DfsController::makeReferenceFile(const ActorId                     &actor,
                                             const std::string                 &nameFile,
                                             const Dfs::Packets::ReferenceData &referenceData) {
    std::string result;
    result.append(DfsPath::filePath(actor, nameFile).string());
    result.append("|");
    result.append(referenceData.toString());
    return result;
}

void DfsController::dataFromReferenceString(const std::string           &referenceStr,
                                            std::string                 &actor,
                                            std::string                 &nameFile,
                                            Dfs::Packets::ReferenceData &referenceData) {
    std::string delimiter    = "|";
    int         posDelimiter = referenceStr.find(delimiter);
    std::string filePath     = referenceStr.substr(0, posDelimiter);
    filePath.erase(0, 4);
    std::string pathdelimiter    = "/";
    int         posPathDelimiter = filePath.find(pathdelimiter);
    actor                        = filePath.substr(0, posPathDelimiter);
    nameFile                     = filePath.substr(posPathDelimiter + 1, filePath.length() - 1);

    std::string referenceDataStr = referenceStr.substr(posDelimiter + 2, referenceStr.size() - 2);
    std::string comadelimiter    = ",";
    std::string keyData          = referenceDataStr.substr(0, referenceDataStr.find(comadelimiter) - 1);
    keyData.erase(0, keyData.find(":") + 2);

    std::string allowData =
        referenceDataStr.substr(referenceDataStr.find(comadelimiter) + 2, referenceDataStr.length() - 2);
    allowData.erase(0, allowData.find(":") + 2);
    allowData.erase(allowData.size() - 2, allowData.size() - 1);
    referenceData = Dfs::Packets::ReferenceData(keyData, allowData);
}

// TODO: move to utils
void DfsController::updateDirsLastModified(const ActorId &actorId, uint64_t last_modified) {
    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    dirsFile.replace(DfsT::DirsFile::TableName,
                     { { "actorId", actorId.to_string() }, { "last_modified", std::to_string(last_modified) } });
    dirsFile.close();
}

std::string DfsController::extractFragment(boost::interprocess::file_mapping &fmapTarget,
                                           std::uint64_t                      offset,
                                           std::uint64_t                      fragmentSize) {
    boost::interprocess::mapped_region rightRegion(fmapTarget,
                                                   boost::interprocess::read_only,
                                                   offset,
                                                   fragmentSize);
    char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, fragmentSize);
}

std::string DfsController::extractFragment(boost::interprocess::file_mapping &fmapTarget, std::uint64_t offset) {
    boost::interprocess::mapped_region rightRegion(fmapTarget, boost::interprocess::read_only, offset);
    char                              *rr_ptr = static_cast<char *>(rightRegion.get_address());
    return std::string(rr_ptr, rightRegion.get_size());
}

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

void DfsController::requestSync() {
    node->network()->send_message(Utils::current_date_secs(),
                                  MessageType::DfsLastModified,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

void DfsController::requestDirFileAllActors() {
    m_unsynchonizedDirs = node->actorIndex()->allActors();
    eLog("[Dfs] m_unsynchonizedDirs: {}", m_unsynchonizedDirs);
    if (!m_unsynchonizedDirs.empty()) {
        requestDirData(ActorId(m_unsynchonizedDirs.at(0)));
    }
}

void DfsController::sendSync(std::uint64_t last_modified, const std::string &messageId) {
    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();
    auto actors = dirsFile.select(fmt::format("SELECT actorId FROM {} WHERE last_modified = {}",
                                              DfsT::DirsFile::TableName,
                                              std::to_string(last_modified)));
    for (auto &row : actors) {
        sendDirData(ActorId(row["actorId"]), last_modified, messageId);
    }
}

void DfsController::requestDirData(const ActorId &owner_id) {
    node->network()->send_message(owner_id,
                                  MessageType::DfsDirData,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

void DfsController::sendDirData(const ActorId     &owner_id,
                                std::uint64_t      last_modified,
                                const std::string &messageId) {
    if (!std::filesystem::exists(DfsT::ActorDirFile::actorDbPath(owner_id))) {
        return;
    }
    auto dirRows = DfsT::ActorDirFile::get_dir_rows(owner_id, last_modified);
    if (!dirRows.has_value())
        return;
    if (!dirRows.value().empty()) {
        node->network()->send_message(std::pair { owner_id, dirRows.value() },
                                      MessageType::DfsDirData,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Response,
                                      messageId);
    } else {
        eraseFirstUnsynchronizedDir();
    }
}

void DfsController::addDirData(const ActorId &owner_id, const std::vector<Dfs::DirRow> &dirRows) {
    eLog("[Dfs] addDirData result: {}", dirRows.size());
    bool res = DfsT::ActorDirFile::add_dir_rows(owner_id, dirRows);

    for (const auto &row : dirRows) {
        m_dirRows.push_back({ owner_id, row });
    }

    process_next_file();
}

void DfsController::requestFile(const ActorId &actorId, const std::string &fileName) {
    eLog("[Dfs] Request file: {} / {}", actorId, fileName);
    if (fileName.empty())
        return;

    std::filesystem::remove(DfsPath::filePath(actorId, fileName));
    node->network()->send_message(std::pair { actorId, fileName },
                                  MessageType::DfsRequestFile,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

void DfsController::sendFile(const ActorId &owner_id, const std::string &file_id, const std::string &message_id) {
    if (file_id.empty()) {
        eFatal("[Dfs] Empty file name");
    }

    auto dirRow = DfsT::ActorDirFile::get_dir_row(owner_id, file_id);

    if (!dirRow.has_value()) {
        return;
    }

    auto msg = std::make_pair(owner_id, dirRow.value());
    if (message_id.empty()) {
        node->network()->send_message(msg, MessageType::DfsAddFile, Config::Net::TypeSend::AllParents);
    } else {
        node->network()->send_message(msg,
                                      MessageType::DfsAddFile,
                                      Config::Net::TypeSend::Focused,
                                      MessageStatus::Response,
                                      message_id);
    }
}

bool DfsController::requestFileSegment(const ActorId &owner_id, const Dfs::DirRow &dir_row) {
    const auto path = DfsPath::filePath(dir_row.actor_id, dir_row.file_id);

    const bool fileExist = std::filesystem::exists(path);
    if (!fileExist) {
        if (dir_row.type == Dfs::FileType::Collection) {
            // node->network()->send_message(std::make_pair(dir_row.actor_id, dir_row.file_id),
            //                               MessageType::DfsCollectionRequest,
            //                               MessageStatus::Request);
            // return;
        }

        requestFile(dir_row.actor_id, dir_row.file_id);
        return true;
    }

    bool file_downloaded = is_file_already_downloaded(owner_id, dir_row.file_id, dir_row.hash);
    if (file_downloaded) {
        return false;
    }

    auto dir_row_local = Dfs::Tables::ActorDirFile::get_dir_row(owner_id, dir_row.file_id);

    if (!dir_row_local.has_value()) {
        requestFile(dir_row.actor_id, dir_row.file_id);
    } else {
        if (dir_row.type == Dfs::FileType::File) {
            DfsP::RequestFileSegmentMessage reqMessage = { .actorId = owner_id,
                                                           .file_id = dir_row.file_id,
                                                           .hash    = dir_row.hash,
                                                           .offset  = 0 };
            node->network()->send_message(reqMessage,
                                          MessageType::DfsRequestFileSegment,
                                          Config::Net::TypeSend::AllParents,
                                          MessageStatus::Request);
            return true;
        } else if (dir_row.type == Dfs::FileType::Collection) {
            node->network()->send_message(std::make_pair(owner_id, dir_row.file_id),
                                          MessageType::DfsCollectionRequest,
                                          Config::Net::TypeSend::AllParents,
                                          MessageStatus::Request);
            return true;
        }
    }

    return false;
}

void DfsController::beginFetchNextFile() {
    if (m_dirRows.empty())
        return;

    eLog("begin fetch next file");
    m_dirRows.erase(m_dirRows.begin());
    process_next_file();
}

void DfsController::process_next_file() {
    while (!m_dirRows.empty()) {
        auto [owner_id, row] = m_dirRows[0];

        if ((row.type == Dfs::FileType::File || row.type == Dfs::FileType::Collection)
            && requestFileSegment(owner_id, row)) {
            eTemp("---------------------- Temp: Dfs: request {} {}", owner_id, row.file_id);
            break;
        }

        m_dirRows.erase(m_dirRows.begin());
    }
}

void DfsController::requestNextFragment(const Dfs::Packets::RequestFileSegmentMessage &msg) {
    // eLog("request next fragment");
    node->network()->send_message(msg,
                                  MessageType::DfsRequestFileSegment,
                                  Config::Net::TypeSend::AllParents,
                                  MessageStatus::Request);
}

std::string DfsController::sendFragment(const DfsP::RequestFileSegmentMessage &msg, const std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.file_id);
    if (!std::filesystem::exists(realFilePath)) {
        return "";
        eFatal("[Dfs] No file");
    }

    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    auto                              fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize - msg.offset > DfsB::sectionSize) {
        data = extractFragment(fmapTarget, msg.offset, DfsB::sectionSize);
    } else {
        data = extractFragment(fmapTarget, msg.offset);
    }

    DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                      .file_id = msg.file_id,
                                      .hash    = msg.hash,
                                      .data    = std::move(data),
                                      .offset  = msg.offset };

    node->network()->send_message(fragment,
                                  MessageType::DfsAddSegment,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  messageId);
    if (msg.offset + DfsB::sectionSize >= fileSize) {
        if (const auto dirRow = Dfs::Tables::ActorDirFile::get_dir_row(msg.actorId, msg.file_id);
            dirRow.has_value()) {
            emit uploaded(msg.actorId, dirRow.value());
        }
        return "";
    }
    emit uploadProgress(msg.actorId, msg.file_id, double(msg.offset) / double(fileSize) * 100);
    return "";
}

void DfsController::fetchFragments(Dfs::Packets::RequestFileSegmentMessage &msg, std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.file_id);
    if (!std::filesystem::exists(realFilePath)) {
        return;
    }

    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize == 0) {
        return;
    }
    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    std::uint64_t                     totalOffset  = 0;
    bool                              lastFragment = false;
    do {
        std::uint64_t limitSectionSize = 0;
        while (limitSectionSize <= DfsB::maxSectionSize && !lastFragment) {
            if (fileSize - totalOffset > DfsB::sectionSize) {
                data += extractFragment(fmapTarget, totalOffset, DfsB::sectionSize);
                totalOffset += DfsB::sectionSize;
                limitSectionSize += DfsB::sectionSize;
                eLog("progress: {}%", (double(totalOffset) / double(fileSize) * 100));
                emit uploadProgress(msg.actorId, msg.file_id, double(totalOffset) / double(fileSize) * 100);
            } else {
                lastFragment = true;
                data += extractFragment(fmapTarget, totalOffset);
            }
        }

        DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                          .file_id = msg.file_id,
                                          .hash    = msg.hash,
                                          .data    = std::move(data),
                                          .offset  = totalOffset };

        messageId = node->network()->send_message(fragment,
                                                  MessageType::DfsAddSegment,
                                                  Config::Net::TypeSend::Focused,
                                                  MessageStatus::Response,
                                                  messageId);

        if (lastFragment) {
            if (const auto dirRow = Dfs::Tables::ActorDirFile::get_dir_row(msg.actorId, msg.file_id);
                dirRow.has_value()) {
                emit uploaded(msg.actorId, dirRow.value());
            }
        } else {
            emit uploadProgress(msg.actorId, msg.file_id, double(totalOffset) / double(fileSize) * 100);
        }
    } while (!lastFragment);
}

void DfsController::fetchFragment(Dfs::Packets::RequestFileSegmentMessage &msg, std::string &messageId) {
    std::filesystem::path realFilePath = DfsPath::filePath(msg.actorId, msg.file_id);
    if (!std::filesystem::exists(realFilePath)) {
        return;
    }
    auto fileSize = std::filesystem::file_size(realFilePath);
    if (fileSize == 0) {
        return;
    }
    boost::interprocess::file_mapping fmapTarget(realFilePath.c_str(), boost::interprocess::read_only);
    std::string                       data;
    bool                              lastFragment = false;
    uint64_t                          newOffset    = msg.offset;

    if (fileSize - msg.offset > DfsB::sectionSize) {
        data         = extractFragment(fmapTarget, msg.offset, DfsB::sectionSize);
        newOffset    = msg.offset + DfsB::sectionSize;
        lastFragment = false;
    } else {
        data         = extractFragment(fmapTarget, msg.offset);
        newOffset    = fileSize;
        lastFragment = true;
    }

    // eLog("[Dfs] Progress for {} / {}: {}%",
    //      msg.actorId,
    //      msg.file_id,
    //      (double(msg.offset) / double(fileSize) * 100));

    // emit uploadProgress(msg.actorId, msg.file_id, double(msg.offset) / double(fileSize) * 100);

    DfsP::SegmentMessage fragment = { .actorId = msg.actorId,
                                      .file_id = msg.file_id,
                                      .hash    = msg.hash,
                                      .data    = std::move(data),
                                      .offset  = newOffset };

    node->network()->send_message(fragment,
                                  MessageType::DfsAddSegment,
                                  Config::Net::TypeSend::Focused,
                                  MessageStatus::Response,
                                  messageId);

    if (lastFragment) {
        if (const auto dirRow = Dfs::Tables::ActorDirFile::get_dir_row(msg.actorId, msg.file_id);
            dirRow.has_value()) {
            emit uploaded(msg.actorId, dirRow.value());
        }
    } else {
        emit uploadProgress(msg.actorId, msg.file_id, double(newOffset) / double(fileSize) * 100);
    }
}

void DfsController::verifyFiles(std::vector<Dfs::Packets::VerifyFileMessage> &fileList, std::string &messageId) {
    for (auto &file : fileList) {
        // check file exist
        std::filesystem::path realFilePath = DfsPath::filePath(file.actorId, file.file_id);
        if (!std::filesystem::exists(realFilePath)) {
            eLog("File by path {} doesn't exist", realFilePath);
            continue;
        }
        std::string fileHash = Utils::calculate_hash_file(FsPath::create(realFilePath).value()).value();
        if (fileHash == file.hash) {
            file.verified = true;
        }
    }
    std::vector<std::string> serializedData = MessagePack::serialize_container(fileList);
    node->network()->send_message(serializedData,
                                  MessageType::DfsVerifyList,
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

void DfsController::eraseFirstUnsynchronizedDir() {
    if (!m_unsynchonizedDirs.empty())
        m_unsynchonizedDirs.erase(m_unsynchonizedDirs.begin());
    if (!m_unsynchonizedDirs.empty())
        requestDirData(ActorId(m_unsynchonizedDirs.at(0)));
}

void DfsController::removeRowFromDB(const Dfs::Packets::RemoveFileMessage &msg) {
    std::string pathDelim       = Utils::platformDelimeter();
    std::string actorPath       = fmt::format("{}{}{}{}", DfsB::fsActrRoot, pathDelim, msg.actorId, pathDelim);
    std::string actrDirFilePath = fmt::format("{}{}", actorPath, DfsB::fsMapName);
    DbConnector actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }

    std::vector<DbRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.file_id);
    std::string        prevHash;
    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("file_id") == msg.file_id) {
            auto prevFileIdIt = it->find("prev_file_id");
            prevHash          = (prevFileIdIt != it->end()) ? prevFileIdIt->second : "";

            if (!prevHash.empty()) {
                actrDirFile.update(fmt::format("UPDATE {} SET prev_file_id = '{}' WHERE prev_file_id = '{}'",
                                               DfsT::ActorDirFile::TableName,
                                               prevHash,
                                               it->at("file_id")));
            }
            actrDirFile.query(fmt::format("DELETE FROM {} WHERE file_id = '{}'",
                                          DfsT::ActorDirFile::TableName,
                                          it->at("file_id")));
        }
    }

    actrDirFile.close();
}

std::string DfsController::addFragment(const DfsP::SegmentMessage &msg) {
    auto fileName = DfsPath::filePath(msg.actorId, msg.file_id);
    if (!std::filesystem::exists(fileName)
        || std::find(m_compliteFiles.begin(), m_compliteFiles.end(), msg.file_id) != m_compliteFiles.end()) {
        return "";
    }

    DbConnector actrDirFile = DfsT::ActorDirFile::get_actor_dir_file(msg.actorId);
    if (!actrDirFile.is_open()) {
        eFatal("Error addFragment 1");
        exit(EXIT_FAILURE);
    }
    std::vector<DbRow> actrDirData = actrDirFile.select(
        fmt::format("SELECT * FROM {} WHERE file_id = '{}';", DfsT::ActorDirFile::TableName, msg.file_id));
    actrDirFile.close();

    DbRow dirRowDb  = actrDirData[0];
    auto  dirRowExp = Utils::from_dbrow<Dfs::DirRow>(dirRowDb);
    if (!dirRowExp.has_value()) {
        return "expected !has_value()";
    }
    auto dirRow = dirRowExp.value();

    std::string   virtualPath     = dirRow.visual_path();
    std::uint64_t fileSize        = dirRow.size;
    auto          currentFileSize = std::filesystem::file_size(fileName);
    if (fileSize == currentFileSize) {
        m_compliteFiles.push_back(msg.file_id);
        eLog("[Dfs] File is complite");
        return "";
    }

    FragmentStorage fs(msg);
    fs.insertFragment(msg);
    currentFileSize = std::filesystem::file_size(fileName);
    emit downloadProgress(msg.actorId, msg.file_id, double(msg.offset) / double(fileSize) * 100);
    if (fileSize == currentFileSize) {
        const auto file_hash = Utils::calculate_hash_file(FsPath::create(fileName).value()).value();
        if (msg.hash == file_hash) {
            eLog("[Dfs] File {} done", fileName);
            auto dirRow = files.at({ msg.actorId, msg.file_id });
            files.erase({ msg.actorId, msg.file_id });
            emit downloaded(msg.actorId, dirRow);
            // sendFile(msg.actorId, msg.file_id); // temp
            fs.initHistoricalChain();
            return "hash";
        } else {
            requestFile(msg.actorId, msg.file_id);
            eFatal("[Dfs] Incorrect file check");
            return "";
        }
    }
    return "";
}

void DfsController::threadAddFragment(const Dfs::Packets::SegmentMessage &msg) {
    // eLog("add segment. Thread: {}", QThread::currentThreadId());
    FragmentWriter fw(msg, m_compliteFiles);

    connect(&fw, &FragmentWriter::requestNextFragment, this, &DfsController::requestNextFragment);
    connect(&fw,
            &FragmentWriter::downloadProgress,
            this,
            [=, this](const ActorId &actor, const std::string &fileName, const double progress) {
                emit this->downloadProgress(ActorId(actor), fileName, progress);
                this->updateFileState(msg.actorId, msg.file_id, Dfs::FileState::Partial);
            });
    connect(&fw, &FragmentWriter::eraseFromFiles, this, [=, this](DfsP::SegmentMessage msg) {
        files.erase({ msg.actorId, msg.file_id });
    });
    connect(&fw, &FragmentWriter::requestFile, this, &DfsController::requestFile);
    connect(&fw, &FragmentWriter::sendFile, this, &DfsController::sendFile);
    connect(&fw, &FragmentWriter::downloadedFile, this, &DfsController::downloaded);
    connect(&fw,
            &FragmentWriter::downloadedFile,
            this,
            [this](const ActorId &owner_id, const Dfs::DirRow &dirRow) {
                this->updateFileState(owner_id, dirRow.file_id, Dfs::FileState::Ready);
            });

    connect(&fw, &FragmentWriter::compliteFile, this, [this](const std::string &fileName) {
        m_compliteFiles.push_back(fileName);
    });
    connect(&fw, &FragmentWriter::finished, this, &DfsController::beginFetchNextFile);
    connect(this, &DfsController::collectionDownloaded, this, &DfsController::beginFetchNextFile);

    fw.start();
    fw.wait();
}

std::string DfsController::deleteFragment(const DfsP::DeleteSegmentMessage &msg) {
    std::string           pathDelim       = Utils::platformDelimeter();
    std::string           pathActor       = DfsB::fsActrRoot + pathDelim + msg.actorId.to_string() + pathDelim;
    std::string           actrDirFilePath = fmt::format("{}{}", pathActor, DfsB::fsMapName);
    std::filesystem::path realFilePath    = fmt::format("{}{}", pathActor, msg.hash);
    DbConnector           actrDirFile(actrDirFilePath);
    if (!actrDirFile.open()) {
        exit(EXIT_FAILURE);
    }
    std::vector<DbRow> actrDirData = DfsT::ActorDirFile::getFileDataByName(&actrDirFile, msg.file_id);

    if (actrDirData.empty()) {
        eLog("[Dfs] editFile: Skipped because of empty result");
        eFatal("Error: actrDirData.empty() || localDirData.empty() in delete");
        return "";
    }

    if (actrDirData.size() > 2) {
        eLog("[Dfs] editFile: Query select failed: Query result has unsupported size: {}", actrDirData.size());
        eFatal("Error 1");
        return "";
    }
    removeDataChunk(msg.offset, msg.size, realFilePath);
    std::string newFileHash = Utils::calculate_hash_file(FsPath::create(realFilePath).value()).value();
    // std::uint64_t newFileSize = std::filesystem::file_size(realFilePath);

    for (auto it = actrDirData.begin(); it < actrDirData.end(); it++) {
        if (it->at("hash") == msg.hash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET fileHash = " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "hash = " + "'" +
            //                    it->at("fileHash")
            //                    + "'");
        }
        if (it->at("prev_file_id") == msg.hash) {
            // actrDirFile.update("UPDATE " + DFST::ActorDirFile::TableName + " SET prev_file_id =
            // " +
            // "'"
            //                    + newFileHash + "' " + "WHERE " + "hash = " + "'" +
            //                    it->at("hash")
            //                    + "'");
        }
    }

    FragmentStorage fragmentStorage(msg.actorId, msg.file_id, msg.hash);
    fragmentStorage.removeFragment(msg);

    return newFileHash;
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

void DfsController::updateFileState(const ActorId &actorId, const std::string fileName, Dfs::FileState state) {
    auto actrDirFile = DfsT::ActorDirFile::get_actor_dir_file(actorId);
    actrDirFile.update(fmt::format("UPDATE {} SET state = '{}' WHERE file_id = '{}'",
                                   DfsT::ActorDirFile::TableName,
                                   std::to_underlying(state),
                                   fileName));
    actrDirFile.close();
}

void DfsController::loadVPNLocalizationFiles() {
    DbConnector dirsFile(DfsB::dirsPath);
    dirsFile.open();

    auto actors = dirsFile.select(fmt::format("SELECT actorId FROM {}", DfsT::DirsFile::TableName));
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
