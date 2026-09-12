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
#include "dfs/vector_index.h"

#include "dfs/dfs_service.h"
#include "core/extrachain_node.h"
#include "utils/exc_utils.h"
#include "utils/file_io.h"

#include <charconv>

namespace {
    constexpr std::size_t MAX_PACKAGE_ROWS  = 100000;
    constexpr std::size_t MAX_PACKAGE_BYTES = 64ULL * 1024ULL * 1024ULL;

    std::expected<std::string, DfsVectorError> read_companion(const FsPath &path) {
        std::ifstream input(path.native(), std::ios::binary);
        if (!input)
            return std::unexpected(DfsVectorError::Unknown);
        std::string            content;
        std::array<char, 4096> buffer;
        while (input) {
            input.read(buffer.data(), buffer.size());
            const auto count = static_cast<std::size_t>(input.gcount());
            if (count > Dfs::VectorDescriptorLimit - content.size())
                return std::unexpected(DfsVectorError::Unknown);
            content.append(buffer.data(), count);
        }
        if (!input.eof())
            return std::unexpected(DfsVectorError::Unknown);
        return content;
    }

    void cache_companion(const FsPath &path, const std::string &content) {
        const auto current = read_companion(path);
        if (current.has_value() && current.value() == content)
            return;
        if (!FileIo::write_atomic(path.native(), content).has_value())
            eWarning("[DfsVector] Cannot refresh template cache: {}", path.native());
    }

    std::optional<std::uint64_t> row_timestamp(const DbRow &row) {
        const auto it = row.find("timestamp");
        if (it == row.end() || it->second.empty()) {
            return std::nullopt;
        }

        std::uint64_t value     = 0;
        const auto [end, error] = std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
        if (error != std::errc() || end != it->second.data() + it->second.size()) {
            return std::nullopt;
        }
        return value;
    }

    std::string canonical_row_hash(const DbRow &row) {
        std::vector<std::pair<std::string_view, std::string_view>> fields;
        fields.reserve(row.size());
        for (const auto &[name, value] : row) {
            fields.emplace_back(name, value);
        }
        std::ranges::sort(fields, {}, &std::pair<std::string_view, std::string_view>::first);

        std::string canonical;
        for (const auto &[name, value] : fields) {
            canonical += std::to_string(name.size());
            canonical += ':';
            canonical += name;
            canonical += std::to_string(value.size());
            canonical += ':';
            canonical += value;
        }
        return Utils::calculate_hash(canonical);
    }

    bool package_size_is_valid(const std::vector<DbRow> &rows) {
        if (rows.size() > MAX_PACKAGE_ROWS) {
            return false;
        }

        std::size_t bytes = 0;
        for (const auto &row : rows) {
            for (const auto &[name, value] : row) {
                if (name.size() > MAX_PACKAGE_BYTES - bytes) {
                    return false;
                }
                bytes += name.size();
                if (value.size() > MAX_PACKAGE_BYTES - bytes) {
                    return false;
                }
                bytes += value.size();
            }
        }
        return true;
    }
} // namespace

int DfsVector::compare_row_revisions(const DbRow &lhs, const DbRow &rhs) {
    const auto lhs_timestamp = row_timestamp(lhs);
    const auto rhs_timestamp = row_timestamp(rhs);
    if (!lhs_timestamp.has_value() || !rhs_timestamp.has_value()) {
        return lhs_timestamp.has_value() ? 1 : (rhs_timestamp.has_value() ? -1 : 0);
    }
    if (lhs_timestamp.value() != rhs_timestamp.value()) {
        return lhs_timestamp.value() < rhs_timestamp.value() ? -1 : 1;
    }

    const auto lhs_hash = canonical_row_hash(lhs);
    const auto rhs_hash = canonical_row_hash(rhs);
    return lhs_hash.compare(rhs_hash);
}

DfsVector::DfsVector(ExtraChain::Core::ExtraChainNode *node,
                     const Actor<KeyPrivate>          &actor,
                     const ActorId                    &file_actor_id,
                     const std::string                &file_id,
                     Dfs::DataSecurity                 data_security,
                     const Dfs::DataSecurityData      &security_data,
                     Dfs::FileType                     file_type) {
    this->node       = node;
    this->file_path_ = Dfs::Path::file_path(file_actor_id, file_id).value();
    this->file_type_ = file_type;

    const std::string &extension =
        (file_type == Dfs::FileType::Dictionary) ? Dfs::Basic::DICTIONARY_FILE : Dfs::Basic::VECTOR_FILE;
    this->vector_path_ = FsPath::create(this->file_path_.native().string() + extension).value();

    this->actor_         = actor;
    this->file_actor_id_ = file_actor_id;
    this->file_id_       = file_id;
    this->data_security_ = data_security;
    this->security_data_ = security_data;
    this->is_encrypted_ =
        data_security != Dfs::DataSecurity::Public || !std::holds_alternative<std::monostate>(security_data_);
}

// std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChain::Core::ExtraChainNode*node,
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

std::expected<DfsVector, DfsVectorError> DfsVector::create(ExtraChain::Core::ExtraChainNode *node,
                                                           const Actor<KeyPrivate>          &main_actor,
                                                           const ActorId                    &file_actor_id,
                                                           const std::string                &file_id,
                                                           const Dfs::DfsTemplateVariant    &variant_template,
                                                           Dfs::DataSecurity                 data_security,
                                                           const Dfs::DataSecurityData      &security_data,
                                                           Dfs::FileType                     file_type) {
    if (!Dfs::Path::file_path(file_actor_id, file_id).has_value())
        return std::unexpected(DfsVectorError::Unknown);
    DfsVector dfs_vector(node, main_actor, file_actor_id, file_id, data_security, security_data, file_type);

    // Own the directory this vector lives in rather than relying on someone having
    // pre-created a folder for the actor. That pre-creation exists for every *known*
    // actor, which is what we want to stop doing — a folder should mean "we store
    // something for this actor", not "we have heard of them" (see docs/TODO.md 0.46).
    if (auto parent = dfs_vector.file_path_.native().parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            eWarning("[DfsVector] create: cannot create {}: {}", parent.string(), ec.message());
            return std::unexpected(DfsVectorError::Unknown);
        }
    }

    // TODO: if vector template has actor, status, timestamp or sign -> error

    auto from_template_result = read_template_from_variant(variant_template);
    if (!from_template_result.has_value()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    auto [vector_template, is_link] = from_template_result.value();
    dfs_vector.collection_template_ = vector_template;

    const auto storage = Dfs::vector_storage_template(vector_template, dfs_vector.is_encrypted_);
    if (!storage.has_value())
        return std::unexpected(DfsVectorError::StructuralCreation);
    auto schema = storage.value().to_db_schema();
    if (!schema.has_value())
        return std::unexpected(DfsVectorError::StructuralCreation);
    schema.value().set_table_name("Vector");
    std::string companion = Json::serialize(vector_template);
    if (is_link && std::holds_alternative<Dfs::CollectionTemplateLink>(variant_template)) {
        auto link = std::get<Dfs::CollectionTemplateLink>(variant_template);
        link.name = vector_template.name();
        companion = Json::serialize(link);
    }
    DbConnector db(dfs_vector.file_path_);
    if (!db.open() || !db.query("BEGIN IMMEDIATE"))
        return std::unexpected(DfsVectorError::Unknown);
    if (!db.create_table(schema.value()).has_value()
        || !Dfs::store_vector_descriptor(db,
                                         { .schema = vector_template, .companion = companion },
                                         dfs_vector.is_encrypted_)
                .has_value()
        || !db.query("COMMIT")) {
        db.query("ROLLBACK");
        return std::unexpected(DfsVectorError::Unknown);
    }
    if (file_type != Dfs::FileType::Dictionary) {
        const auto saved = Dfs::read_vector_descriptor(db);
        if (saved.has_value() && saved.value().has_value())
            cache_companion(dfs_vector.vector_path_, saved.value().value().companion);
    }

    return dfs_vector;
}

std::expected<DfsVector, DfsVectorError> DfsVector::load(ExtraChain::Core::ExtraChainNode *node,
                                                         const Actor<KeyPrivate>          &actor,
                                                         const ActorId                    &file_actor_id,
                                                         const std::string                &file_id,
                                                         Dfs::DataSecurity                 data_security,
                                                         const Dfs::DataSecurityData      &security_data,
                                                         Dfs::FileType                     file_type) {
    if (file_actor_id.is_zero() || file_id.empty()) {
        return std::unexpected(DfsVectorError::Unknown);
    }

    if (!Dfs::Path::file_path(file_actor_id, file_id).has_value())
        return std::unexpected(DfsVectorError::Unknown);
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

std::expected<DfsVector, DfsVectorError> DfsVector::load_network(ExtraChain::Core::ExtraChainNode *node,
                                                                 const Actor<KeyPrivate>          &actor,
                                                                 const ActorId                    &file_actor_id,
                                                                 const std::string                &file_id,
                                                                 Dfs::DataSecurity                 data_security,
                                                                 const Dfs::DataSecurityData      &security_data,
                                                                 Dfs::FileType                     file_type) {
    if (!Dfs::Path::file_path(file_actor_id, file_id).has_value())
        return std::unexpected(DfsVectorError::Unknown);
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

    auto               query   = fmt::format("SELECT * FROM {} WHERE {} = ? AND status = '1'", "Vector", field);
    std::vector<DbRow> db_rows = db.select(query, "Vector", { { field, primary_data } });

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

std::expected<Dfs::VectorDescriptor, DfsVectorError> DfsVector::load_descriptor() {
    DbConnector database(file_path_);
    const bool  opened = database.open(false);
    if (opened) {
        const auto stored = Dfs::read_vector_descriptor(database);
        if (!stored.has_value())
            return std::unexpected(DfsVectorError::Unknown);
        if (stored.value().has_value())
            return stored.value().value();
    }
    Dfs::VectorDescriptor descriptor;
    if (file_type_ == Dfs::FileType::Dictionary) {
        descriptor.schema    = Dfs::dictionary_template();
        descriptor.companion = Json::serialize(descriptor.schema);
    } else {
        const auto content = read_companion(vector_path_);
        if (!content.has_value())
            return std::unexpected(content.error());
        descriptor.companion = content.value();
        const auto schema    = Json::deserialize<Dfs::CollectionTemplate>(content.value());
        if (schema.has_value() && !schema.value().fields().empty()) {
            descriptor.schema = schema.value();
        } else {
            const auto link = Json::deserialize<Dfs::CollectionTemplateLink>(content.value());
            if (!link.has_value()
                || !Dfs::Path::file_path(link.value().owner_id, link.value().file_id).has_value())
                return std::unexpected(DfsVectorError::Unknown);
            const auto resolved =
                Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(link.value().owner_id,
                                                                                   link.value().file_id);
            if (!resolved.has_value())
                return std::unexpected(DfsVectorError::Unknown);
            descriptor.schema = resolved.value();
        }
    }
    if (!Dfs::vector_storage_template(descriptor.schema, is_encrypted_).has_value())
        return std::unexpected(DfsVectorError::StructuralCreation);
    if (opened && database.table_exists("Vector")) {
        if (!database.query("BEGIN IMMEDIATE"))
            return std::unexpected(DfsVectorError::Unknown);
        if (!Dfs::store_vector_descriptor(database, descriptor, is_encrypted_).has_value()
            || !database.query("COMMIT")) {
            database.query("ROLLBACK");
            return std::unexpected(DfsVectorError::Unknown);
        }
    }
    return descriptor;
}

std::expected<Dfs::CollectionTemplate, DfsVectorError> DfsVector::read_template() {
    const auto descriptor = load_descriptor();
    if (descriptor.has_value())
        return descriptor.value().schema;
    node->dfs()->request_vector_content(file_actor_id_, file_id_);
    node->dfs()->request_file(file_actor_id_, file_id_);
    return std::unexpected(descriptor.error());
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError> DfsVector::generate_content_package(
    const std::string &where_statement) {
    auto package = generate_content_package_empty();
    if (!package.has_value())
        return std::unexpected(package.error());
    const auto rows = read_rows(where_statement);
    if (rows.has_value())
        package.value().content = rows.value();
    else if (rows.error() != DfsVectorError::CollectionEmpty)
        return std::unexpected(rows.error());
    return package;
}

std::expected<Dfs::Packets::DfsVectorContentPackage, DfsVectorError>
DfsVector::generate_content_package_empty() {
    const auto descriptor = load_descriptor();
    if (!descriptor.has_value())
        return std::unexpected(descriptor.error());
    return Dfs::Packets::DfsVectorContentPackage { .owner_id        = file_actor_id_,
                                                   .file_id         = file_id_,
                                                   .vector_template = descriptor.value().schema,
                                                   .vector_file     = file_type_ == Dfs::FileType::Dictionary
                                                                          ? ""
                                                                          : descriptor.value().companion };
}

bool DfsVector::handle_package(const Dfs::Packets::DfsVectorContentPackage &dfs_vector_content) {
    if (dfs_vector_content.owner_id != file_actor_id_ || dfs_vector_content.file_id != file_id_
        || dfs_vector_content.vector_file.size() > Dfs::VectorDescriptorLimit
        || !package_size_is_valid(dfs_vector_content.content)) {
        eWarning("[DfsVector] handle_package: package is for another vector: {} / {}", file_actor_id_, file_id_);
        return false;
    }

    auto vector_template = dfs_vector_content.vector_template;
    if (vector_template.fields().size() == 0) {
        eWarning("[DfsVector] handle_package: package template has no fields: {} / {}", file_actor_id_, file_id_);
        return false;
    }

    if (!Dfs::vector_storage_template(vector_template, is_encrypted_).has_value())
        return false;
    if (file_path_.exists()) {
        const auto existing = load_descriptor();
        if (existing.has_value() && Json::serialize(existing.value().schema) != Json::serialize(vector_template))
            return false;
    }
    auto verifier                 = *this;
    verifier.collection_template_ = vector_template;

    std::string primary_field = "actor";
    if (vector_template.primary.has_value()) {
        primary_field = vector_template.primary.value().name();
    }

    std::unordered_set<std::string> allowed_fields { "actor", "sign", "timestamp", "status" };
    allowed_fields.insert(primary_field);
    for (const auto &field : vector_template.fields()) {
        allowed_fields.insert(field.name());
    }

    for (const auto &row : dfs_vector_content.content) {
        const auto status = row.find("status");
        if (!row.contains(primary_field) || !row.contains("actor") || !row.contains("sign")
            || !row_timestamp(row).has_value() || status == row.end()
            || (status->second != "0" && status->second != "1")
            || std::ranges::any_of(row,
                                   [&](const auto &field) {
                                       return !allowed_fields.contains(field.first);
                                   })
            || !verifier.verify(row)) {
            eWarning("[DfsVector] handle_package: a row failed validation, package dropped: {} / {} ({} rows)",
                     file_actor_id_,
                     file_id_,
                     dfs_vector_content.content.size());
            return false;
        }
    }

    const auto storage_template = Dfs::vector_storage_template(vector_template, is_encrypted_);
    if (!storage_template.has_value())
        return false;
    auto schema = storage_template.value().to_db_schema();
    if (!schema.has_value())
        return false;
    schema.value().set_table_name("Vector");

    // The owner's directory may not exist yet: vector content can arrive before anything
    // else has created it, and sqlite then fails to open the file — 300 such failures on
    // one node during seeding, each one a vector that never arrived. This used to be
    // masked by a folder being pre-created for every known actor; that is gone now, so
    // the write path owns its directory. Placed after validation, so a rejected package
    // still leaves nothing behind.
    if (auto parent = file_path_.native().parent_path(); !parent.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(parent, ec);
    }

    DbConnector db(file_path_);
    if (!db.open()) {
        eWarning("[DfsVector] Can't open vector db {}, package will be retried", file_path_.string());
        return false;
    }
    if (!db.query("BEGIN IMMEDIATE")) {
        return false;
    }
    if (!db.create_table(schema.value()).has_value()) {
        db.query("ROLLBACK");
        return false;
    }

    const auto companion =
        dfs_vector_content.vector_file.empty() ? Json::serialize(vector_template) : dfs_vector_content.vector_file;
    if (!Dfs::store_vector_descriptor(db, { .schema = vector_template, .companion = companion }, is_encrypted_)
             .has_value()) {
        db.query("ROLLBACK");
        return false;
    }
    Dfs::VectorIndex index(db, primary_field);
    if (!index.root().has_value()) {
        db.query("ROLLBACK");
        return false;
    }

    for (const auto &db_row : dfs_vector_content.content) {
        auto existing = db.select(fmt::format("SELECT * FROM Vector WHERE {} = ?", primary_field),
                                  "Vector",
                                  { { primary_field, db_row.at(primary_field) } });
        if (!existing.empty() && compare_row_revisions(db_row, existing.front()) <= 0) {
            continue;
        }
        if (!Dfs::upsert_vector_row(db, primary_field, db_row)) {
            db.query("ROLLBACK");
            return false;
        }
        const auto stored = db.select(fmt::format("SELECT * FROM Vector WHERE {} = ?", primary_field),
                                      "Vector",
                                      { { primary_field, db_row.at(primary_field) } });
        if (stored.size() != 1 || !verifier.verify(stored.front())
            || !index.update(db_row.at(primary_field)).has_value()) {
            db.query("ROLLBACK");
            return false;
        }
    }

    if (!db.query("COMMIT")) {
        db.query("ROLLBACK");
        return false;
    }

    if (file_type_ != Dfs::FileType::Dictionary) {
        const auto saved = Dfs::read_vector_descriptor(db);
        if (saved.has_value() && saved.value().has_value())
            cache_companion(vector_path_, saved.value().value().companion);
    }

    collection_template_ = std::move(vector_template);
    return true;
}

bool DfsVector::store_add(DbRow &row) {
    // Every refusal below is a message the user never sees; say why.
    auto encryption_res = encrypt_data(row, security_data_);
    if (!encryption_res.has_value()) {
        eWarning("[DfsVector] store_add refused, encryption failed: {} / {}", file_actor_id_, file_id_);
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
        eWarning("[DfsVector] store_add refused, row has no hashable content: {} / {}", file_actor_id_, file_id_);
        return false;
    }

    auto sign = actor_.key().sign(hash);
    if (!sign.has_value()) {
        eWarning("[DfsVector] store_add refused, signing failed for actor {}: {} / {}",
                 actor_.id(),
                 file_actor_id_,
                 file_id_);
        return false;
    }

    row["actor"] = actor_.id().to_string();
    row["sign"]  = ByteArray(sign.value()).toString();
    auto res     = local_add(row, false);
    return res.has_value();
}

std::expected<bool, DfsVectorError> DfsVector::local_add(const DbRow &row, bool check) {
    if (!this->verify(row)) {
        eWarning("[DfsVector] local_add refused, row does not verify: {} / {}", file_actor_id_, file_id_);
        return std::unexpected(DfsVectorError::Adding);
    }

    std::string field = "actor";
    if (collection_template_.primary.has_value()) {
        field = collection_template_.primary.value().name();
    }
    if (!row.contains(field) || !row_timestamp(row).has_value()) {
        eWarning("[DfsVector] local_add refused, no primary field or timestamp: {} / {}", file_actor_id_, file_id_);
        return std::unexpected(DfsVectorError::Adding);
    }

    DbConnector db(file_path_);
    if (!db.open()) {
        eWarning("[DfsVector] local_add refused, cannot open {}", file_path_.string());
        return std::unexpected(DfsVectorError::Adding);
    }

    if (!db.query("BEGIN IMMEDIATE")) {
        return std::unexpected(DfsVectorError::Adding);
    }
    Dfs::VectorIndex index(db, field);
    if (!index.root().has_value()) {
        db.query("ROLLBACK");
        return std::unexpected(DfsVectorError::Adding);
    }
    if (check) {
        auto existing = db.select(fmt::format("SELECT * FROM Vector WHERE {} = ?", field),
                                  "Vector",
                                  { { field, row.at(field) } });
        if (!existing.empty() && compare_row_revisions(row, existing.front()) <= 0) {
            if (!db.query("COMMIT"))
                return std::unexpected(DfsVectorError::Adding);
            return false;
        }
    }
    if (!Dfs::upsert_vector_row(db, field, row)) {
        db.query("ROLLBACK");
        return std::unexpected(DfsVectorError::Adding);
    }
    const auto stored =
        db.select(fmt::format("SELECT * FROM Vector WHERE {} = ?", field), "Vector", { { field, row.at(field) } });
    if (stored.size() != 1 || !verify(stored.front()) || !index.update(row.at(field)).has_value()
        || !db.query("COMMIT")) {
        db.query("ROLLBACK");
        return std::unexpected(DfsVectorError::Adding);
    }
    return true;
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

    // A tombstone blanks every non-primary field. Numeric columns cannot take "-":
    // the INTEGER bind used to throw and take the node down with it, so they get
    // "0" (the tombstone's meaning is carried by status, and the row is re-signed).
    std::unordered_map<std::string, Dfs::FieldType> field_types;
    for (const auto &field : collection_template_.fields()) {
        field_types.emplace(field.name(), field.type());
    }
    for (const auto &[key, _] : row) {
        if (collection_template_.primary.has_value() && collection_template_.primary->name() == key) {
            continue;
        }

        const auto type = field_types.find(key);
        const bool numeric = type != field_types.end()
                             && (type->second == Dfs::FieldType::Integer || type->second == Dfs::FieldType::Real
                                 || type->second == Dfs::FieldType::Bool
                                 || type->second == Dfs::FieldType::Timestamp);
        row[key] = numeric ? "0" : "-";
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
    // Every lookup goes through find(): a row arriving from the network (DfsVectorAdd,
    // content package) may lack any field, and DbRow::at() on a missing key throws
    // std::out_of_range straight out of the network handler — a peer could terminate
    // the node with one malformed row. A missing field is simply an unhashable row.
    const auto status    = row.find("status");
    const auto timestamp = row.find("timestamp");
    if (status == row.end() || timestamp == row.end()) {
        return { "", true };
    }

    std::string to_hash   = status->second + timestamp->second + file_actor_id_.to_string() + file_id_;
    bool        all_empty = true;

    if (to_hash.size() != 14 + 40 + 64) { // 1 + 13 + 40 + 64
        return { "", true };
    }

    if (collection_template_.primary.has_value()) {
        const auto primary = row.find(collection_template_.primary->name());
        if (primary == row.end()) {
            return { "", true };
        }
        to_hash += primary->second;
    }

    const auto &fields = collection_template_.fields();
    for (const auto &field : fields) {
        const auto found = row.find(field.name());
        if (found == row.end()) {
            continue;
        }

        const std::string &value = found->second;
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
    const auto descriptor = load_descriptor();
    if (!descriptor.has_value())
        return std::nullopt;
    const auto &content = descriptor.value().companion;
    return std::pair { Utils::calculate_hash(content), content.size() };
}

std::expected<Dfs::VectorIndexRoot, std::string> DfsVector::index_root() {
    DbConnector db(file_path_.native());
    if (!db.open(/*create_if_missing*/ false)) {
        return std::unexpected("Cannot open vector index");
    }

    const auto field =
        collection_template_.primary.has_value() ? collection_template_.primary.value().name() : "actor";
    Dfs::VectorIndex index(db, field);
    return index.root();
}

std::optional<std::pair<std::string, uint64_t>> DfsVector::data_hash_size() {
    const auto root = index_root();
    if (!root.has_value()) {
        eWarning("[DfsVector] Cannot read the content index: {} / {}", file_actor_id_, file_id_);
        return std::nullopt;
    }
    const auto catalog  = node->dfs()->dirs_manager().get_db_instance();
    const auto metadata = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(catalog, file_actor_id_, file_id_);
    if (metadata.has_value() && metadata.value().state != Dfs::FileState::Removed) {
        bool migrate = !root.value().legacy_hash.empty() && metadata.value().hash == root.value().legacy_hash;
        if (!migrate && root.value().tree.rows == 0) {
            const auto companion = calculate_template_file_hash();
            migrate              = companion.has_value() && metadata.value().hash == companion.value().first;
        }
        if (migrate && metadata.value().hash != root.value().hash) {
            if (!catalog->update(Dfs::Tables::DirsFile::TableNameActorsFiles,
                                 { { "hash", root.value().hash },
                                   { "size", std::to_string(root.value().tree.bytes) } },
                                 { { "owner_id", file_actor_id_.to_string() },
                                   { "file_id", file_id_ },
                                   { "hash", metadata.value().hash } })) {
                return std::nullopt;
            }
        }
    }
    return std::pair { root.value().hash, root.value().tree.bytes };
}

bool DfsVector::verify(const DbRow &row) {
    if (!row.contains("actor") || !row.contains("sign") || !row.contains("status")
        || row.at("sign").size() != crypto_sign_BYTES || !row_timestamp(row).has_value()) {
        return false;
    }

    auto actor_id = ActorId::create(row.at("actor"));
    if (!actor_id.has_value()) {
        eWarning("[DfsVector] verify: malformed actor id in row: {} / {}", file_actor_id_, file_id_);
        return false;
    }

    auto      actor = node->actor_index()->read_actor_old(actor_id.value());
    Signature sign  = ByteArray(row.at("sign")).toArray<crypto_sign_BYTES>();

    auto [hash, all_empty] = calculate_hash(row);
    if (hash.empty() || all_empty) {
        eWarning("[DfsVector] verify: row has no hashable content: {} / {}", file_actor_id_, file_id_);
        return false;
    }

    auto verify = actor.key().verify(hash, sign);
    if (!verify.has_value()) {
        eWarning("[DfsVector] verify: signature check errored for actor {} (actor {}): {} / {}",
                 actor_id.value(),
                 actor.empty() ? "not in index" : "loaded",
                 file_actor_id_,
                 file_id_);
        return false;
    }
    if (!verify.value()) {
        eWarning("[DfsVector] verify: signature mismatch for actor {} (actor {}): {} / {}",
                 actor_id.value(),
                 actor.empty() ? "not in index" : "loaded",
                 file_actor_id_,
                 file_id_);
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
