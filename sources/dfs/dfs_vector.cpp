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

#include "utils/exc_utils.h"

DfsVector::DfsVector(ExtraChainNode              *node,
                     const Actor<KeyPrivate>     &actor,
                     const ActorId               &file_actor_id,
                     const std::string           &file_id,
                     Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
                     const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
    this->node           = node;
    this->file_path_     = Dfs::Path::file_path(file_actor_id, file_id).value();
    this->vector_path_   = FsPath::create(this->file_path_.native().string() + Dfs::Basic::VECTOR_FILE).value();
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
                                                           const Dfs::DataSecurityData   &security_data) {
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data);

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

    if (!is_link) {
        auto json     = Json::serialize(vector_template);
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

    DbConnector db(dfs_vector.file_path_);
    db.open();
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
                                                         const Dfs::DataSecurityData &security_data) {
    if (file_actor_id.is_zero() || file_id.empty()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data);

    auto vector_template = dfs_vector.read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    dfs_vector.collection_template_ = vector_template.value();
    // checks

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load_network(ExtraChainNode              *node,
                                                                 const Actor<KeyPrivate>     &actor,
                                                                 const ActorId               &file_actor_id,
                                                                 const std::string           &file_id,
                                                                 Dfs::DataSecurity            data_security,
                                                                 const Dfs::DataSecurityData &security_data) {
    DfsVector dfs_vector(node, actor, file_actor_id, file_id, data_security, security_data);
    return dfs_vector;
}

std::expected<DbRow, DfsVectorError> DfsVector::read_row(const std::string &primary_data) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(DfsVectorError::CollectionNotFound);
    }

    std::string field = "actor";
    if (collection_template_.primary.has_value()) {
        field = collection_template_.primary.value().name();
    }

    auto query = fmt::format("SELECT * FROM {} WHERE {} = '{}' AND status = '1'", "Vector", field, data);
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
    db.open();
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
        auto decryption_res = decrypt_data(row, security_data_);
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
    auto content = Utils::read_file_content(vector_path_);
    if (!content.has_value()) {
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
            Dfs::Tables::ActorDirFile::get_collection_template_file_id(vector_template_link->owner_id,
                                                                       vector_template_link->file_id);

        if (!vector_template_file_id.has_value()) {
            return std::unexpected(DfsVectorError::Unknown);
        }

        return vector_template_file_id.value();
    }

    return vector_template.value();
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::generate_content_package(
    const std::string &where_statement) {

    auto rows = read_rows("");

    auto res = Utils::read_file_content(vector_path_);
    if (!res.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto vector_template = read_template();
    if (!vector_template.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = file_actor_id_,
                                                   .file_id         = file_id_,
                                                   .vector_template = vector_template.value(),
                                                   .vector_file     = ByteArray(res.value()).toString(),
                                                   .content =
                                                       rows.has_value() ? rows.value() : std::vector<DbRow> {} };
}

bool DfsVector::handle_package(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    // TODO: move this to create?
    // auto json     = Json::serialize(dfs_vector_content.vector_template);
    auto res_json = Utils::write_file_content(vector_path_, dfs_vector_content.vector_file);
    if (!res_json.has_value()) {
        return false;
    }

    auto vector_template = dfs_vector_content.vector_template;
    if (vector_template.fields().size() == 0) {
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
        return false;
    }

    schema->set_table_name("Vector");

    DbConnector db(file_path_);
    db.open();
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

    // TODO: check if actor correct

    auto row = std::move(row_result.value());
    for (const auto &[key, _] : row) {
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

bool DfsVector::verify(const DbRow &row) {
    auto      actor_id = ActorId(row.at("actor"));
    auto      actor    = node->actorIndex()->getActor(actor_id);
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
        auto myself = node->accountController()->currentProfile().get_actor(security_self->my_actor);
        if (myself.has_value()) {
            encryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().encrypt_self(data.toBytes());
            };
        }
    } else if (const auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
        auto sender   = node->accountController()->currentProfile().get_actor(security_actor->sender_id);
        auto receiver = node->actorIndex()->get_actor(security_actor->receiver_id);
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
        auto myself = node->accountController()->currentProfile().get_actor(security_self->my_actor);
        if (myself.has_value()) {
            decryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().decrypt_self(data.toBytes());
            };
        }
    } else if (const auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
        auto sender   = node->accountController()->currentProfile().get_actor(security_actor->sender_id);
        auto receiver = node->actorIndex()->get_actor(security_actor->receiver_id);
        if (sender.has_value() && receiver.has_value()) {
            decryptor = [s = sender.value(), r = receiver.value()](const ByteArray &data) {
                return s.get().key().decrypt(data.toBytes(), r.key().public_key());
            };
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
