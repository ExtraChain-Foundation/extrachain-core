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

#include "utils/db_connector.h"

HistoricalCollection::HistoricalCollection(const std::shared_ptr<Actor<KeyPrivate>> &actor,
                                           const ActorId                            &file_actor_id,
                                           const std::string                        &file_id) {
    this->file_path_         = DfsPath::file_path(file_actor_id, file_id).value();
    auto historical_path_str = fmt::format("{}{}", this->file_path_.native(), ".collection");
    this->historical_path_   = FsPath::create(historical_path_str).value();
    this->actor_             = actor;
    this->file_actor_id_     = file_actor_id;
    this->file_id_           = file_id;
}

std::expected<HistoricalCollection, CollectionError> HistoricalCollection::create(
    const std::shared_ptr<Actor<KeyPrivate>> &main_actor,
    const ActorId                            &file_actor_id,
    const std::string                        &file_id,
    const ActorId                            &tempalte_actor_id,
    const std::string                        &template_file_id) {
    HistoricalCollection chain(main_actor, file_actor_id, file_id);

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

    auto created = chain.create_table(tempalte_actor_id, template_file_id);
    if (!created.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    return chain;
}

std::expected<HistoricalCollection, CollectionError> HistoricalCollection::load(
    const std::shared_ptr<Actor<KeyPrivate>> &main_actor,
    const ActorId                            &file_actor_id,
    const std::string                        &file_id) {

    HistoricalCollection chain(main_actor, file_actor_id, file_id);
    // check db exists
    auto creation = chain.get_creation();
    if (!creation.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    auto collection_template =
        Dfs::Tables::ActorDirFile::get_collection_template_file_id(creation->actor_id, creation->file_id);
    if (!collection_template.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    chain.table_name_ = collection_template->name();
    return chain;
}

std::expected<std::string, CollectionError> HistoricalCollection::create_table(
    const ActorId     &tempalte_actor_id,
    const std::string &template_file_id) {
    auto collection_template =
        Dfs::Tables::ActorDirFile::get_collection_template_file_id(tempalte_actor_id, template_file_id);
    if (!collection_template.has_value()) {
        return std::unexpected(CollectionError::Unknown);
    }

    auto schema = collection_template->to_db_schema();
    if (!schema.has_value()) {
        return std::unexpected(CollectionError::StructuralCreation);
    }

    this->table_name_        = collection_template->name();
    auto collection_creation = CollectionTemplateLink { .actor_id = tempalte_actor_id,
                                                        .file_id  = template_file_id,
                                                        .name     = collection_template->name() };

    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Structural,
                                                    .data      = Json::serialize(collection_creation),
                                                    .timestamp = Utils::current_date_ms(),
                                                    .actor_id  = this->actor_->id(),
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

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::add_row(DbRow &row) {
    // TODO: check db row in dfs template
    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Add,
                                                    .data      = Json::serialize(row),
                                                    .timestamp = 111,
                                                    .actor_id  = this->actor_->id(),
                                                    .sign      = Signature() };

    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    row["id"]        = std::to_string(historical_row.id);
    row["timestamp"] = std::to_string(historical_row.timestamp);
    // row["sign"]      = Utils::to_base64("");

    auto res_insert = db.insert(table_name_, row);
    db.close();

    if (!res_insert) {
        // TODO: remove from historical
        return std::unexpected(CollectionError::Adding);
    }

    return historical_row;
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::update_row(uint32_t id, DbRow &row) {
    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Update,
                                                    .data      = Json::serialize(row),
                                                    .timestamp = Utils::current_date_ms(),
                                                    .actor_id  = this->actor_->id(),
                                                    .sign      = Signature() };

    insert_historical_row(historical_row);

    DbConnector db(file_path_);
    db.open();
    row["id"]        = std::to_string(historical_row.id);
    row["timestamp"] = std::to_string(historical_row.timestamp);

    auto res_update = false; // db.update(table_name_, id, row);
    db.close();

    if (!res_update) {
        return std::unexpected(CollectionError::Updating);
    }

    return historical_row;
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::remove_row(std::uint32_t id) {
    auto historical_row = HistoricalCollectionRow { .operation = CollectionOperation::Remove,
                                                    .data      = std::to_string(id),
                                                    .timestamp = Utils::current_date_ms(),
                                                    .actor_id  = this->actor_->id(),
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

std::expected<std::vector<DbRow>, CollectionError> HistoricalCollection::get_collection_rows() {
    DbConnector db(file_path_);
    db.open();
    if (!db.is_open()) {
        return std::unexpected(CollectionError::CollectionNotFound);
    }

    std::vector<DbRow> db_rows = db.select(fmt::format("SELECT * FROM {}", table_name_));
    db.close();

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

std::expected<CollectionTemplateLink, CollectionError> HistoricalCollection::get_creation() {
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

    auto template_link = Json::deserialize<CollectionTemplateLink>(hi_row->data).value();
    return template_link;
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
    row.sign  = this->actor_->key().sign(hash);
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
    auto historical = DbConnector(historical_path_);
    historical.open();
    historical.insert(Dfs::Historical::HISTORICAL_TABLE, Utils::to_dbrow(historical_row));
    historical.close();
}

std::expected<HistoricalCollectionRow, CollectionError> HistoricalCollection::change_collection(const HistoricalCollectionRow &historical_row) {
    DbRow row;

    switch (historical_row.operation) {
    case CollectionOperation::Structural:
        break;
    case CollectionOperation::Add:
        row = Json::_no_try_deserialize<DbRow>(historical_row.data).value();
        break;
    case CollectionOperation::Update:
        break;
    case CollectionOperation::Remove: {
        row = { { "id", historical_row.data } };
        break;
    }
    }

    DbConnector db(file_path_);
    db.open();
    auto res_insert = db.insert(table_name_, row);
    db.close();

    if (!res_insert) {
        // TODO: remove from historical
        return std::unexpected(CollectionError::Adding);
    }
}
