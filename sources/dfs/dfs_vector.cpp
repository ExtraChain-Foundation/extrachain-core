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

#include "dfs/dfs_vector.h"

#include "dfs/dfs_controller.h"
#include "utils/exc_utils.h"

DfsVector::DfsVector(ExtraChainNode              *node,
                     const Actor<KeyPrivate>     &actor,
                     const ActorId               &file_actor_id,
                     const std::string           &file_id,
                     Dfs::DataSecurity            data_security,
                     const Dfs::DataSecurityData &security_data,
                     Dfs::FileType                file_type) {
    this->node           = node;
    this->file_path_     = Dfs::Path::file_path(file_actor_id, file_id).value();
    this->file_type_     = file_type;

    const std::string &extension = (file_type == Dfs::FileType::Dictionary) ? Dfs::Basic::DICTIONARY_FILE
                                                                            : Dfs::Basic::VECTOR_FILE;
    this->vector_path_ = FsPath::create(this->file_path_.native().string() + extension).value();

    this->actor_         = actor;
    this->file_actor_id_ = file_actor_id;
    this->file_id_       = file_id;
    this->data_security_ = data_security;
    this->security_data_ = security_data;
    this->is_encrypted_ =
        data_security != Dfs::DataSecurity::Public || !std::holds_alternative<std::monostate>(security_data_);
}

// std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChainNode              *node,
//                                                            const Actor<KeyPrivate>     &main_actor,
//                                                            const ActorId               &file_actor_id,
//                                                            const std::string           &file_id,
//                                                            const ActorId               &template_actor_id,
//                                                            const std::string           &template_file_id,
//                                                            Dfs::DataSecurity            data_security,
//                                                            const Dfs::DataSecurityData &security_data) {
//     auto vector_template =
//         Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_actor_id, template_file_id);
//     if (!vector_template.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     auto dfs_vector = create(node,
//                              main_actor,
//                              file_actor_id,
//                              file_id,
//                              vector_template.value(),
//                              data_security,
//                              security_data,
//                              false);

//     if (!dfs_vector.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     auto link     = CollectionTemplateLink { .actor_id = template_actor_id,
//                                              .file_id  = template_file_id,
//                                              .name     = vector_template->name() };
//     auto json     = Json::serialize(link);
//     auto res_json = Utils::write_file_content(dfs_vector->vector_path_, std::move(json));
//     if (!res_json.has_value()) {
//         return std::unexpected(DfsVectorError::Unknown);
//     }

//     return std::unexpected(DfsVectorError::Unknown);
// }

std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChainNode                *node,
                                                           const Actor<KeyPrivate>       &main_actor,
                                                           const ActorId                 &file_actor_id,
                                                           const std::string             &file_id,
                                                           const Dfs::DfsTemplateVariant &variant_template,
                                                           Dfs::DataSecurity              data_security,
                                                           const Dfs::DataSecurityData   &security_data,
                                                           Dfs::FileType                  file_type) {
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data, file_type);

    // TODO: if vector template has actor, status, timestamp or sign -> error

    auto from_template_result = read_template_from_variant(variant_template);
    if (!from_template_result.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto [vector_template, is_link] = from_template_result.value();
    dfs_vector.collection_template_ = vector_template;

    if (dfs_vector.is_encrypted_) {
        vector_template.set_to_blob();
    }

    // Save original template for file before adding service fields
    auto original_template = vector_template;

    if (vector_template.primary.has_value()) {
        const auto &primary = vector_template.primary.value();
        vector_template.preadd_fields({ primary,
                                        Dfs::Field::ActorId("actor").not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    } else {
        vector_template.preadd_fields({ Dfs::Field::ActorId("actor").unique().not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    }

    auto schema = vector_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(DfsVectorError::StructuralCreation);
    }

    schema->set_table_name("Vector");

    // Dictionary uses static template from dictionary_template(), no need to write file
    if (file_type != Dfs::FileType::Dictionary) {
        if (!is_link) {
            // Write original template without service fields to file
            auto json     = Json::serialize(original_template);
            auto res_json = Utils::write_file_content(dfs_vector.vector_path_, std::move(json));
            if (!res_json.has_value()) {
                return std::unexpected(DfsVectorError::Unknown);
            }
        } else {
            if (std::holds_alternative<Dfs::CollectionTemplateLink>(variant_template)) {
                auto link = std::get<Dfs::CollectionTemplateLink>(variant_template);
                link.name = vector_template.name();

                auto json     = Json::serialize(link);
                auto res_json = Utils::write_file_content(dfs_vector.vector_path_, std::move(json));
                if (!res_json.has_value()) {
                    return std::unexpected(DfsVectorError::Unknown);
                }
            }
        }
    }

    DbConnector db(dfs_vector.file_path_);
    if (!db.open()) {
        eWarning("[DfsVector] Can't open vector db {}", dfs_vector.file_path_.string());
        return std::unexpected(DfsVectorError::Unknown);
    }
    auto res_create = db.create_table(schema.value());
    db.close();

    if (!res_create.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load(ExtraChainNode              *node,
                                                         const Actor<KeyPrivate>     &actor,
                                                         const ActorId               &file_actor_id,
                                                         const std::string           &file_id,
                                                         Dfs::DataSecurity            data_security,
                                                         const Dfs::DataSecurityData &security_data,
                                                         Dfs::FileType                file_type) {
    if (file_actor_id.is_zero() || file_id.empty()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data, file_type);

    // Dictionary uses static template, no need to read from file
    if (file_type == Dfs::FileType::Dictionary) {
        dfs_vector.collection_template_ = Dfs::dictionary_template();
    } else {
        auto vector_template = dfs_vector.read_template();
        if (!vector_template.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }
        dfs_vector.collection_template_ = vector_template.value();
    }
    // checks

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load_network(ExtraChainNode              *node,
                                                                 const Actor<KeyPrivate>     &actor,
                                                                 const ActorId               &file_actor_id,
                                                                 const std::string           &file_id,
                                                                 Dfs::DataSecurity            data_security,
                                                                 const Dfs::DataSecurityData &security_data,
                                                                 Dfs::FileType                file_type) {
    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data, file_type);
    return dfs_vector;
}

std::expected<DbRow, DfsVectorError> DfsVector::read_row(const std::string &primary_data) {
    DbConnector db(file_path_);
    db.open(/*create_if_missing*/ false);
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    std::string field = "actor";
    if (collection_template_.primary.has_value()) {
        field = collection_template_.primary.value().name();
    }

    auto query = fmt::format("SELECT * FROM {} WHERE {} = '{}' AND status = '1'", "Vector", field, primary_data);
    std::vector<DbRow> db_rows = db.select(query);

    if (db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    db.close();

    auto row = db_rows.front();

    auto decryption_res = decrypt_data(row, security_data_);
    if (!decryption_res.has_value()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }
    if (!decryption_res.value().empty()) {
        row = decryption_res.value();
    }

    return row;
}

std::expected<std::vector<DbRow>, DfsVectorError> DfsVector::read_rows(const std::string &where_statement) {
    DbConnector db(file_path_);
    db.open(/*create_if_missing*/ false);
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    auto               query   = fmt::format("SELECT * FROM {} {}", "Vector", where_statement);
    std::vector<DbRow> db_rows = db.select(query);
    db.close();

    if (db_rows.empty()) {
        return std::unexpected(DfsVectorError::CollectionEmpty);
    }

    for (auto &row : db_rows) {
        // TODO: make security_data_ unique for actor / current (security_data_.receiver)
        Dfs::DataSecurityData adjusted_security_data = security_data_;

        if (auto *actor_data = std::get_if<Dfs::DataSecurityActor>(&adjusted_security_data)) {
            if (actor_data->sender_id.is_zero()) {
                actor_data->sender_id = ActorId(row["actor"]);
            }
        }

        auto decryption_res = decrypt_data(row, adjusted_security_data);
        if (!decryption_res.has_value()) {
            return std::unexpected(DfsVectorError::CollectionEmpty);
        }
        if (!decryption_res.value().empty()) {
            row = decryption_res.value();
        }
    }

    return db_rows;
}

std::expected<Dfs::CollectionTemplate, DfsVectorError> DfsVector::read_template() {
    // Dictionary uses static template, no file needed
    if (file_type_ == Dfs::FileType::Dictionary) {
        return Dfs::dictionary_template();
    }

    auto content = Utils::read_file_content(vector_path_);
    if (!content.has_value()) {
        // Companion file lost (interrupted write) — recover via two paths together: direct
        // content package + the normal state->queue (REPAIR in add_to_queue skips "already
        // downloaded" for an unreadable vector). Both are throttled.
        node->dfs()->request_vector_content(file_actor_id_, file_id_);
        node->dfs()->request_file(file_actor_id_, file_id_);
        eCritical("[DfsVector] Can't find {}", vector_path_.native());
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto vector_template = Json::deserialize<Dfs::CollectionTemplate>(content.value());

    if (vector_template.has_value()) {
        if (vector_template->fields().size() != 0) {
            return vector_template.value();
        }

        auto vector_template_link = Json::deserialize<Dfs::CollectionTemplateLink>(content.value());
        if (!vector_template_link.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }

        auto vector_template_file_id =
            Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(vector_template_link->owner_id,
                                                                               vector_template_link->file_id);

        if (!vector_template_file_id.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }

        return vector_template_file_id.value();
    }

    if (!vector_template.has_value()) {
        eCritical("[DfsVector] Can't parse {}", vector_path_.native());
        return std::unexpected(DfsVectorError::Unknown);
    }

    return vector_template.value();
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::generate_content_package(
    const std::string &where_statement) {

    auto rows = read_rows("");

    auto vector_template = read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    // Dictionary uses static template, no file to read
    std::string vector_file_content;
    if (file_type_ != Dfs::FileType::Dictionary) {
        auto res = Utils::read_file_content(vector_path_);
        if (!res.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }
        vector_file_content = ByteArray(res.value()).toString();
    }

    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = file_actor_id_,
                                                   .file_id         = file_id_,
                                                   .vector_template = vector_template.value(),
                                                   .vector_file     = vector_file_content,
                                                   .content =
                                                       rows.has_value() ? rows.value() : std::vector<DbRow> {} };
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError>
DfsVector::generate_content_package_empty() {
    // Same payload as generate_content_package minus the rows: a vector with no rows yet
    // still has a template and a .vector file, and the receiver needs both — without the
    // template handle_package rejects the package outright.
    auto vector_template = read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    std::string vector_file_content;
    if (file_type_ != Dfs::FileType::Dictionary) {
        auto res = Utils::read_file_content(vector_path_);
        if (!res.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }
        vector_file_content = ByteArray(res.value()).toString();
    }

    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = file_actor_id_,
                                                   .file_id         = file_id_,
                                                   .vector_template = vector_template.value(),
                                                   .vector_file     = vector_file_content,
                                                   .content         = std::vector<DbRow> {} };
}

bool DfsVector::handle_package(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    // Dictionary uses static template, no file to write
    // Each failure below used to return a bare false, so the caller's warning could not
    // say which step failed — 966 rejections in three minutes with no way to tell why.
    // The owner's directory may not exist yet: vector content can arrive before anything
    // else has created it, and std::ofstream then fails with "Failed to open file for
    // writing" — 300 such failures on one node during seeding, each one a vector that
    // never arrived. Creating it here is cheap and idempotent.
    if (auto parent = vector_path_.native().parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    if (file_type_ != Dfs::FileType::Dictionary && !dfs_vector_content.vector_file.empty()) {
        // An empty companion file is normal for a freshly created vector and must not
        // sink the whole package: write_file_content rejects empty content outright
        // (ContentError::EmptyContent), which made every answer about a new vector
        // undeliverable — 2081 rejections in one minute of seeding, and the receiving
        // node never got the vector at all.
        auto res_json = Utils::write_file_content(vector_path_, dfs_vector_content.vector_file);
        if (!res_json.has_value()) {
            eWarning("[DfsVector] handle_package: cannot write {} ({} bytes)",
                     vector_path_.string(),
                     dfs_vector_content.vector_file.size());
            return false;
        }
    }

    auto vector_template = dfs_vector_content.vector_template;
    if (vector_template.fields().size() == 0) {
        eWarning("[DfsVector] handle_package: empty template for {}", file_id_);
        return false;
    }

    if (is_encrypted_) {
        vector_template.set_to_blob();
    }

    if (vector_template.primary.has_value()) {
        const auto &primary = vector_template.primary.value();
        vector_template.preadd_fields({ primary,
                                        Dfs::Field::ActorId("actor").not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    } else {
        vector_template.preadd_fields({ Dfs::Field::ActorId("actor").unique().not_null(),
                                        Dfs::Field::Blob("sign").not_null(),
                                        Dfs::Field::Timestamp("timestamp").not_null(),
                                        Dfs::Field::Integer("status").not_null() });
    }

    auto schema = vector_template.to_db_schema();
    if (!schema.has_value()) {
        eWarning("[DfsVector] handle_package: cannot build schema for {}", file_id_);
        return false;
    }

    schema->set_table_name("Vector");

    DbConnector db(file_path_);
    if (!db.open()) {
        eWarning("[DfsVector] Can't open vector db {}, package will be retried", file_path_.string());
        return false;
    }
    db.create_table(schema.value());

    for (const auto &db_row : dfs_vector_content.content) {
        db.replace("Vector", db_row);
    }

    db.close();
    return true;
}

bool DfsVector::store_add(DbRow &row) {
    auto encryption_res = encrypt_data(row, security_data_);
    if (!encryption_res.has_value()) {
        return false;
    }
    if (!encryption_res->empty()) {
        row = encryption_res.value();
    }

    row["timestamp"] = std::to_string(Utils::current_date_ms());
    if (row["status"] != "0") {
        row["status"] = "1";
    }

    auto [hash, all_empty] = calculate_hash(row);
    if (hash.empty() || all_empty) {
        return false;
    }

    auto sign = actor_.key().sign(hash);
    if (!sign.has_value()) {
        return false;
    }

    row["actor"] = actor_.id().to_string();
    row["sign"]  = ByteArray(sign.value()).toString();
    auto res     = local_add(row, false);
    return res;
}

bool DfsVector::local_add(const DbRow &row, bool check) {
    bool verify = this->verify(row);
    if (!verify) {
        return false;
    }

    if (check) {
        std::string field = "actor";
        if (collection_template_.primary.has_value()) {
            field = collection_template_.primary.value().name();
        }

        auto exrow = read_row(row.at(field));
        if (exrow.has_value()) {
            auto extimestamp = std::stoull(exrow->at("timestamp"));
            auto timestamp   = std::stoull(row.at("timestamp"));
            if (extimestamp > timestamp) {
                return true;
            }
        }
    }

    DbConnector db(file_path_);
    if (!db.open()) {
        return false;
    }

    bool res = db.replace("Vector", row);
    db.close();
    return res;
}

std::optional<DbRow> DfsVector::remove(const std::string &primary_data) {
    auto row_result = read_row(primary_data);
    if (!row_result.has_value()) {
        return std::nullopt;
    }

    auto row = std::move(row_result.value());

    if (row["actor"] != actor_.id().to_string()) {
        return std::nullopt;
    }

    for (const auto &[key, _] : row) {
        if (collection_template_.primary.has_value() && collection_template_.primary->name() == key) {
            continue;
        }

        row[key] = "-";
    }

    row["status"] = "0";

    auto res = store_add(row);
    // bool res = db.delete_row("Vector", row);
    if (!res) {
        return std::nullopt;
    }

    return row;
}

std::pair<std::string, bool> DfsVector::calculate_hash(const DbRow &row) {
    // TODO: try..catch
    std::string to_hash   = row.at("status") + row.at("timestamp") + file_actor_id_.to_string() + file_id_;
    bool        all_empty = true;

    if (to_hash.size() != 14 + 40 + 64) { // 1 + 13 + 40 + 64
        return { "", true };
    }

    if (collection_template_.primary.has_value()) {
        to_hash += row.at(collection_template_.primary->name()); // TODO: crash?
    }

    const auto &fields = collection_template_.fields();
    for (const auto &field : fields) {
        if (row.find(field.name()) == row.end()) {
            continue;
        }

        std::string value = row.at(field.name());
        if (!value.empty()) {
            all_empty = false;
        }

        to_hash += value;
    }

    if (all_empty || to_hash.empty()) {
        return { "", true };
    }

    auto hash = Utils::calculate_hash(to_hash);
    return { hash, false };
}

std::optional<std::pair<std::string, std::size_t>> DfsVector::calculate_template_file_hash() {
    // Dictionary uses static template, calculate hash from JSON serialization
    if (file_type_ == Dfs::FileType::Dictionary) {
        auto json = Json::serialize(Dfs::dictionary_template());
        auto hash = Utils::calculate_hash(json);
        return std::pair { hash, json.size() };
    }

    auto hash_result = Utils::calculate_hash_file(vector_path_);
    if (!hash_result.has_value()) {
        return std::nullopt;
    }

    std::size_t size        = 1;
    auto        size_result = vector_path_.file_size();

    if (size_result.has_value()) {
        size = size_result.value();
    }

    return std::pair { hash_result.value(), size };
}

std::optional<std::pair<std::string, uint64_t>> DfsVector::data_hash_size() {
    DbConnector db(file_path_.native());
    if (!db.open(/*create_if_missing*/ false)) {
        return std::nullopt;
    }

    auto hash_size =
        db.hash_size(collection_template_.primary.has_value() ? collection_template_.primary->name() : "actor");
    return hash_size;
}

bool DfsVector::verify(const DbRow &row) {
    auto      actor_id = ActorId(row.at("actor"));
    auto      actor    = node->actor_index()->read_actor_old(actor_id);
    Signature sign     = ByteArray(row.at("sign")).toArray<crypto_sign_BYTES>();

    auto [hash, all_empty] = calculate_hash(row);
    if (hash.empty() || all_empty) {
        return false;
    }

    auto verify = actor.key().verify(hash, sign);
    if (!verify.has_value()) {
        return false;
    }

    return verify.value();
}

std::expected<DbRow, DfsVectorError> DfsVector::encrypt_data(const DbRow                 &row,
                                                             const Dfs::DataSecurityData &security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray &)> encryptor;

    if (const auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
        auto myself = node->account_controller()->current_profile().get_actor(security_self->my_actor);
        if (myself.has_value()) {
            encryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().encrypt_self(data.toBytes());
            };
        }
    } else if (const auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
        auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
        auto receiver = node->actor_index()->read_actor(security_actor->receiver_id);
        if (sender.has_value() && receiver.has_value()) {
            encryptor = [s = sender.value(), r = receiver.value()](const ByteArray &data) {
                return s.get().key().encrypt(data.toBytes(), r.key().public_key());
            };
        }
    } else if (const auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
        encryptor = [key = security_key->key](const ByteArray &data) {
            return Cryptography::symmetric_encrypt(data.toBytes(), key);
        };
    }

    if (!encryptor) {
        return DbRow {};
    }

    DbRow encrypted_row;

    for (const auto &[key, value] : row) {
        if (value.empty()) {
            encrypted_row[key] = "";
            continue;
        }

        if (collection_template_.primary.has_value() && key == collection_template_.primary->name()) {
            encrypted_row[key] = value;
            continue;
        }

        auto res = encryptor(ByteArray(value));

        if (!res.has_value()) {
            return std::unexpected(DfsVectorError::IncorrectEncryption);
        }

        encrypted_row[key] = ByteArray(res.value()).toString();
    }

    return encrypted_row;
}

std::expected<DbRow, DfsVectorError> DfsVector::decrypt_data(const DbRow                 &row,
                                                             const Dfs::DataSecurityData &security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray &)> decryptor;

    if (const auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
        auto myself = node->account_controller()->current_profile().get_actor(security_self->my_actor);
        if (myself.has_value()) {
            decryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().decrypt_self(data.toBytes());
            };
        }
    } else if (const auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
        auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
        auto receiver = node->actor_index()->read_actor(security_actor->receiver_id);

        if (sender.has_value() && receiver.has_value()) {
            decryptor = [s = sender.value(), r = receiver.value()](const ByteArray &data) {
                return s.get().key().decrypt(data.toBytes(), r.key().public_key());
            };
        } else if (!sender.has_value() && receiver.has_value()) {
            auto receiver_private =
                node->account_controller()->current_profile().get_actor(security_actor->receiver_id);
            auto sender_public = node->actor_index()->read_actor(security_actor->sender_id);

            if (receiver_private.has_value() && sender_public.has_value()) {
                decryptor = [r = receiver_private.value(), s = sender_public.value()](const ByteArray &data) {
                    return r.get().key().decrypt(data.toBytes(), s.key().public_key());
                };
            }
        }
    } else if (const auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
        decryptor = [key = security_key->key](const ByteArray &data) {
            return Cryptography::symmetric_decrypt(data.toBytes(), key);
        };
    }

    if (!decryptor) {
        return DbRow {};
    }

    DbRow decrypted_row;

    for (const auto &[key, value] : row) {
        if (value.empty()) {
            decrypted_row[key] = "";
            continue;
        }

        if (collection_template_.primary.has_value() && key == collection_template_.primary->name()) {
            decrypted_row[key] = value;
            continue;
        }

        if (key == "actor" || key == "status" || key == "timestamp" || key == "sign") {
            decrypted_row[key] = value;
            continue;
        }

        auto res = decryptor(ByteArray(value).toBytes());
        if (!res.has_value()) {
            return std::unexpected(DfsVectorError::IncorrectEncryption);
        }

        decrypted_row[key] = ByteArray(res.value()).toString();
    }

    return decrypted_row;
}
