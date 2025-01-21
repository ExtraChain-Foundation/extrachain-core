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

#include "dfs/historical_collection.h"

#include <blake3.h>

#include "blockchain/actor_index.h"
#include "managers/extrachain_node.h"
#include "managers/account_controller.h"
#include "utils/db_connector.h"

HistoricalCollection::HistoricalCollection(ExtraChainNode              *node,
                                           const Actor<KeyPrivate>     &actor,
                                           const ActorId               &file_actor_id,
                                           const std::string           &file_id,
                                           Dfs::DataSecurity            data_security = Dfs::DataSecurity::Public,
                                           const Dfs::DataSecurityData &security_data = Dfs::DataSecurityData()) {
    this->node               = node;
    this->file_path_         = Dfs::Path::file_path(file_actor_id, file_id).value();
    auto historical_path_str = fmt::format("{}{}", this->file_path_.native(), ".collection");
    this->historical_path_   = FsPath::create(historical_path_str).value();
    this->actor_             = actor;
    this->file_actor_id_     = file_actor_id;
    this->file_id_           = file_id;
    data_security_           = data_security;
    security_data_           = security_data;
}

std::expected<HistoricalCollection, CollectionError> HistoricalCollection::create(
    ExtraChainNode              *node,
    const Actor<KeyPrivate>     &main_actor,
    const ActorId               &file_actor_id,
    const std::string           &file_id,
    const ActorId               &template_actor_id,
    const std::string           &template_file_id,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    HistoricalCollection chain(node, main_actor, file_actor_id, file_id, data_security, security_data);

    DbConnector db(chain.historical_path_);
    if (!db.open()) { // TODO: expection
        eFatal("[History] Can't create historical file");
    }

    using namespace sqlite::literals;
    auto historical_schema = DbSchema("historical_chain");
    historical_schema.add_columns("id"_int.primary_key(),
                                  "prev_id"_int.unique(),
                                  "actor_id"_text.not_null(),
                                  "operation"_int.not_null().between(0, 3),
                                  "data"_json.not_null(),
                                  "timestamp"_int.not_null(),
                                  "sign"_blob.not_null());
    auto res = db.create_table(historical_schema);

    if (!res.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    auto created = chain.create_table(template_actor_id, template_file_id);
    if (!created.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    return chain;
}

std::expected<HistoricalCollection, CollectionError> HistoricalCollection::create(
    ExtraChainNode                *node,
    const Actor<KeyPrivate>       &main_actor,
    const ActorId                 &file_actor_id,
    const std::string             &file_id,
    const Dfs::CollectionTemplate &collection_template,
    Dfs::DataSecurity              data_security,
    const Dfs::DataSecurityData   &security_data) {
    HistoricalCollection chain(node, main_actor, file_actor_id, file_id, data_security, security_data);

    DbConnector db(chain.historical_path_);
    if (!db.open()) { // TODO: expection
        eFatal("[History] Can't create historical file");
    }

    using namespace sqlite::literals;
    auto historical_schema = DbSchema("historical_chain");
    historical_schema.add_columns("id"_int.primary_key(),
                                  "prev_id"_int.unique(),
                                  "actor_id"_text.not_null(),
                                  "operation"_int.not_null().between(0, 3),
                                  "data"_json.not_null(),
                                  "timestamp"_int.not_null(),
                                  "sign"_blob.not_null());
    auto res = db.create_table(historical_schema);

    if (!res.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    auto created = chain.create_table(collection_template);
    if (!created.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    return chain;
}

std::expected<HistoricalCollection, CollectionError> HistoricalCollection::load(
    ExtraChainNode              *node,
    const Actor<KeyPrivate>     &main_actor,
    const ActorId               &file_actor_id,
    const std::string           &file_id,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    HistoricalCollection chain(node, main_actor, file_actor_id, file_id, data_security, security_data);

    if (!chain.historical_path_.exists_and_size_not_zero()) {
        eWarning("[HistoricalCollection] Can't find historical file, or size == 0: {}",
                 chain.historical_path_.native());
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    if (!chain.file_path_.exists_and_size_not_zero()) {
        eWarning("[HistoricalCollection] Can't find collection file, or size == 0: {}", chain.file_path_.native());
        return std::unexpected(CollectionError::CollectionNotFound);
    }

    auto creation = chain.get_creation();
    if (!creation.has_value()) {
        return std::unexpected(creation.error());
    }

    std::visit(
        [&](const auto &value) {
            if constexpr (std::is_same_v<std::decay_t<decltype(value)>, CollectionTemplateLink>) {
                auto collection_template =
                    Dfs::Tables::ActorDirFile::get_collection_template_file_id(value.actor_id, value.file_id);
                if (collection_template.has_value()) {
                    chain.table_name_ = collection_template->name();
                }
            } else if constexpr (std::is_same_v<std::decay_t<decltype(value)>, Dfs::CollectionTemplate>) {
                chain.table_name_ = value.name();
            }
        },
        creation.value());

    if (chain.table_name_.empty()) {
        return std::unexpected(CollectionError::Unknown);
    }

    return chain;
}

std::expected<std::string, CollectionError> HistoricalCollection::create_table(
    const ActorId     &template_actor_id,
    const std::string &template_file_id) {
    auto collection_template =
        Dfs::Tables::ActorDirFile::get_collection_template_file_id(template_actor_id, template_file_id);
    if (!collection_template.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    auto schema = collection_template->to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    this->table_name_        = collection_template->name();
    auto collection_creation = CollectionTemplateLink { .actor_id = template_actor_id,
                                                        .file_id  = template_file_id,
                                                        .name     = collection_template->name() };

    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Structural,
                                                    .data      = Json::serialize(collection_creation),
                                                    .timestamp = Utils::current_date_ms(),
                                                    .actor_id  = this->actor_.id(),
                                                    .sign      = Signature() };
    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    auto res_create = db.create_table(schema.value());
    db.close();

    if (!res_create.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    return res_create.value();
}

std::expected<std::string, CollectionError> HistoricalCollection::create_table(
    const Dfs::CollectionTemplate &collection_template) {
    auto schema = collection_template.to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    this->table_name_ = collection_template.name();

    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::StructuralTemplated,
                                                    .data      = Json::serialize(collection_template),
                                                    .timestamp = Utils::current_date_ms(),
                                                    .actor_id  = this->actor_.id(),
                                                    .sign      = Signature() };
    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    auto res_create = db.create_table(schema.value());
    db.close();

    if (!res_create.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    return res_create.value();
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::add_row(
    const DbRow                 &row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    // TODO: check db row in dfs template

    DbRow db_row;
    if (data_security != Dfs::DataSecurity::Public) {
        auto res = encrypt_data(row, data_security, security_data);
        if (!res.has_value()) {
            return std::unexpected(CollectionError::Unknown);
        }
        db_row = res.value();
    } else {
        db_row = row;
    }

    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Add,
                                                    .data      = Json::serialize(db_row),
                                                    .actor_id  = this->actor_.id(),
                                                    .sign      = Signature() };

    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    db_row["id"]        = std::to_string(historical_row.id);
    db_row["timestamp"] = std::to_string(historical_row.timestamp);
    // row["sign"]      = Utils::to_base64("");

    auto res_insert = db.insert(table_name_, db_row);
    db.close();

    if (!res_insert) {
        // TODO: remove from historical
        return std::unexpected(CollectionError::Adding);
    }

    return historical_row;
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::update_row(
    const std::uint32_t          id,
    const DbRow                 &row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    DbRow db_row;
    if (data_security != Dfs::DataSecurity::Public) {
        auto res = encrypt_data(row, data_security, security_data);
        if (!res.has_value()) {
            return std::unexpected(CollectionError::Unknown);
        }
        db_row = res.value();
    } else {
        db_row = row;
    }
    if (db_row.find("id") != row.end()) {
        db_row.erase("id");
    }
    if (db_row.find("timestamp") != row.end()) {
        db_row.erase("timestamp");
    }

    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Update,
                                                    .data      = Json::serialize(std::make_pair(id, db_row)),
                                                    .actor_id  = this->actor_.id(),
                                                    .sign      = Signature() };

    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();

    // TODO: select from db, check changes

    auto res_update = db.update(table_name_, db_row, { { "id", std::to_string(id) } });
    db.close();

    if (!res_update) {
        return std::unexpected(CollectionError::Updating);
    }

    return historical_row;
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::remove_row(const std::uint32_t id) {
    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Remove,
                                                    .data      = std::to_string(id),
                                                    .actor_id  = this->actor_.id(),
                                                    .sign      = Signature() };
    // TODO: check if id exists
    static auto db_row_id = DbRow { { "id", std::to_string(id) } };
    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    auto res_delete = db.delete_row(table_name_, db_row_id);
    db.close();

    if (!res_delete) {
        return std::unexpected(CollectionError::Deleting);
    }

    return historical_row;
}

std::expected<std::vector<DbRow>, CollectionError> HistoricalCollection::get_collection_rows(
    const std::string &where_statement) {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(CollectionError::CollectionNotFound);
    }

    std::vector<DbRow> db_rows =
        db.select(fmt::format("SELECT * FROM {} {} ORDER by id", table_name_, where_statement));
    db.close();

    if (db_rows.empty()) {
        return std::unexpected(CollectionError::CollectionEmpty);
    }

    return db_rows;
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::get_row(
    const std::string &search_value,
    const std::string &field) {
    DbConnector db(historical_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    auto rows = db.select(
        fmt::format("SELECT * FROM {} WHERE {} = '{}';", Dfs::Historical::HISTORICAL_TABLE, field, search_value));
    db.close();

    if (rows.empty()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    auto &row    = rows[0];
    auto  hi_row = Utils::from_dbrow<HistoricalCollectionRow>(row);
    if (!hi_row.has_value()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    return hi_row.value();
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::get_last_row() {
    DbConnector db(historical_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }
    auto rows =
        db.select(fmt::format("SELECT * FROM {} ORDER BY id DESC LIMIT 1;", Dfs::Historical::HISTORICAL_TABLE));
    if (rows.empty()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }
    auto &row    = rows[0];
    auto  hi_row = Utils::from_dbrow<HistoricalCollectionRow>(row);
    if (!hi_row.has_value()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    return hi_row.value();
}

std::expected<std::variant<CollectionTemplateLink, Dfs::CollectionTemplate>, CollectionError>
HistoricalCollection::get_creation() {
    DbConnector db(historical_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }
    auto rows = db.select(fmt::format("SELECT * FROM {} WHERE id = 0;", Dfs::Historical::HISTORICAL_TABLE));
    if (rows.empty()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }
    auto &row    = rows[0];
    auto  hi_row = Utils::from_dbrow<HistoricalCollectionRow>(row);
    if (!hi_row.has_value()) {
        return std::unexpected(CollectionError::HistoryNotFound);
    }

    if (hi_row->operation == CollectionOperation::StructuralTemplated) {
        auto collection_template = Json::deserialize<Dfs::CollectionTemplate>(hi_row->data);
        if (!collection_template.has_value()) {
            return std::unexpected(CollectionError::Unknown);
        }
        return collection_template.value();
    } else if (hi_row->operation == CollectionOperation::Structural) {
        auto template_link = Json::deserialize<CollectionTemplateLink>(hi_row->data);
        if (!template_link.has_value()) {
            return std::unexpected(CollectionError::Unknown);
        }
        return template_link.value();
    }

    return std::unexpected(CollectionError::Unknown);
}

void HistoricalCollection::insert_historical_row(HistoricalCollectionRow &historical_row) {
    auto last_row = get_last_row();

    if (last_row.has_value()) {
        historical_row.prev_id = last_row->id;
        historical_row.id      = historical_row.prev_id.value() + 1;
    } else {
        historical_row.id = 0;
    }

    historical_row.timestamp = Utils::current_date_ms();
    historical_collection_row_sign(historical_row);

    insert_row_to_database(historical_row);
}

void HistoricalCollection::historical_collection_row_sign(HistoricalCollectionRow &row) {
    auto hash = Utils::calculate_hash(row);
    auto sign = this->actor_.key().sign(hash);
    if (!sign.has_value()) {
        return;
    }
    row.sign = sign.value();
}

bool HistoricalCollection::historical_collection_row_verify(const HistoricalCollectionRow &row) {
    return true;
}

FsPath HistoricalCollection::get_historical_path() const {
    return historical_path_;
}

FsPath HistoricalCollection::get_file_path() const {
    return file_path_;
}

void HistoricalCollection::insert_row_to_database(const HistoricalCollectionRow &historical_row) {
    auto historical  = DbConnector(historical_path_);
    auto row         = Utils::to_dbrow(historical_row);
    row["id"]        = std::to_string(historical_row.id);
    row["timestamp"] = std::to_string(historical_row.timestamp);

    historical.open();
    historical.insert(Dfs::Historical::HISTORICAL_TABLE, row);
    historical.close();
}

std::expected<void, CollectionError> HistoricalCollection::change_collection(
    const HistoricalCollectionRow &historical_row) {
    DbRow row;

    switch (historical_row.operation) {
    case CollectionOperation::Structural:
        break;
    case CollectionOperation::StructuralTemplated:
        break;
    case CollectionOperation::Add:
        row = Json::deserialize<DbRow>(historical_row.data).value();
        break;
    case CollectionOperation::Update:
        // ???
        break;
    case CollectionOperation::Remove: {
        // ???
        row = { { "id", historical_row.data } };
        break;
    }
    }

    row["id"]        = std::to_string(historical_row.id);
    row["timestamp"] = std::to_string(historical_row.timestamp);

    DbConnector db(file_path_);
    db.open();
    auto res_insert = db.insert(table_name_, row);
    db.close();

    if (!res_insert) {
        // TODO: remove from historical
        return std::unexpected(CollectionError::Adding);
    }

    return {};
}

std::expected<DbRow, CollectionError> HistoricalCollection::encrypt_data(
    const DbRow                 &row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray &)> encryptor;

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto myself = node->accountController()->currentProfile().get_actor(security_self->my_actor);
            if (!myself.has_value()) {
                return DbRow {};
            }
            encryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().encrypt_self(data.toBytes());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->accountController()->currentProfile().get_actor(security_actor->sender_id);
            auto receiver = node->actorIndex()->get_actor(security_actor->receiver_id);
            if (!sender.has_value() || !receiver.has_value()) {
                return DbRow {};
            }
            encryptor = [s = sender.value(), r = receiver.value(), this](const ByteArray &data) {
                return s.get().key().encrypt(data.toBytes(), r.key().public_key());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Key) {
        if (auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            encryptor = [key = security_key->key, this](const ByteArray &data) {
                return Cryptography::symmetric_encrypt(data.toBytes(), key);
            };
        }
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

        auto res = encryptor(ByteArray(value));
        if (!res.has_value()) {
            return std::unexpected(CollectionError::IncorrectEncryption);
        }
        encrypted_row[key] = ByteArray(res.value()).toBase64();
    }

    return encrypted_row;
}

std::expected<DbRow, CollectionError> HistoricalCollection::decrypt_data(
    const DbRow                 &row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData &security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray &)> decryptor;
    if (data_security == Dfs::DataSecurity::Self) {
        if (auto *security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto myself = node->accountController()->currentProfile().get_actor(security_self->my_actor);
            if (!myself.has_value()) {
                return DbRow {};
            }
            decryptor = [myself = myself.value()](const ByteArray &data) {
                return myself.get().key().decrypt_self(data.toBytes());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Actor) {
        if (auto *security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->accountController()->currentProfile().get_actor(security_actor->sender_id);
            auto receiver = node->actorIndex()->get_actor(security_actor->receiver_id);
            if (!sender.has_value() || !receiver.has_value()) {
                return DbRow {};
            }
            decryptor = [s = sender.value(), r = receiver.value(), this](const ByteArray &data) {
                return s.get().key().decrypt(data.toBytes(), r.key().public_key());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Key) {
        if (auto *security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            decryptor = [key = security_key->key, this](const ByteArray &data) {
                return Cryptography::symmetric_decrypt(data.toBytes(), key);
            };
        }
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

        auto res = decryptor(ByteArray::fromBase64(value));
        if (!res.has_value()) {
            return std::unexpected(CollectionError::IncorrectEncryption);
        }
        decrypted_row[key] = ByteArray(res.value()).toString();
    }
    return decrypted_row;
}
