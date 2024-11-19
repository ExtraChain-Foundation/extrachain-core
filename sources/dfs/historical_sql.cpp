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

#include "dfs/historical_sql.h"
#include "utils/db_connector.h"

HistoricalSql
HistoricalSql::create(const std::shared_ptr<Actor<KeyPrivate>> &actor, const std::string &file_id) {
    HistoricalSql chain(actor, file_id);

    DbConnector db(chain.history_path);
    if (!db.open()) { // TODO: expection
        eFatal("[History] Can't create historical database");
    }

    using namespace sqlite::literals;
    auto history_schema = DbSchema("historical_chain");
    history_schema.add_columns(
        "id"_int.primary_key(),
        "prevId"_int.unique().not_null(),
        "actorId"_text,
        "operation"_int.not_null().between(0, 3),
        "data"_json.not_null(),
        "timestamp"_int.not_null(),
        "sign"_blob);
    db.create_table(history_schema);

    return chain;
}

HistoricalSql
HistoricalSql::load(const std::shared_ptr<Actor<KeyPrivate>> &actor, const std::string &file_id) {
    HistoricalSql chain(actor, file_id);
    // check db exists
    return chain;
}

std::expected<std::string, SqlCreateError> HistoricalSql::create_table(const DbSchema &schema) {
    DbConnector db(file_path);
    db.open();
    auto res_create = db.create_table(schema);
    db.close();

    if (!res_create.has_value()) {
        return res_create;
    }

    auto history_row = HistoricalRow { .id        = "0",
                                       .prevId    = "1",
                                       .operation = HistoricalSqlOperation::Create,
                                       .data      = schema.to_json(),
                                       .timestamp = 111,
                                       .actorId   = this->actor->id(),
                                       .sign      = Signature() };
    historicalRowSign(history_row);

    DbConnector history(this->history_path);
    history.open();
    auto dbRow = Utils::to_dbrow(history_row);
    history.insert("historical_chain", dbRow);
    history.close();

    return res_create;
}

std::expected<void, SqlCreateError> HistoricalSql::insert_into(DbRow &row, const std::string &temp_table) {
    DbConnector db(file_path);
    db.open();
    auto res_insert = db.insert(temp_table, row);
    db.close();

    if (!res_insert) {
        return std::unexpected(SqlCreateError::InvalidValue);
    }

    auto history = DbConnector(history_path);
    history.open();
    auto history_row = HistoricalRow { .id        = "1",
                                       .prevId    = "0",
                                       .operation = HistoricalSqlOperation::Insert,
                                       .data      = Json::serialize(row),
                                       .timestamp = 111,
                                       .actorId   = this->actor->id(),
                                       .sign      = Signature() };
    historicalRowSign(history_row);

    history.insert("historical_chain", Utils::to_dbrow(history_row));
    history.close();

    return {};
}

std::expected<void, SqlCreateError> HistoricalSql::update_where(DbRow &row, const std::string &temp_table) {
    DbConnector db(file_path);
    db.open();
    auto res_update = false; // db.update(temp_table, row);
    db.close();

    if (!res_update) {
        return std::unexpected(SqlCreateError::InvalidValue);
    }

    auto history = DbConnector(history_path);
    history.open();
    auto history_row = HistoricalRow { .id        = "2",
                                       .prevId    = "1",
                                       .operation = HistoricalSqlOperation::Update,
                                       .data      = Json::serialize(row),
                                       .timestamp = 111,
                                       .actorId   = this->actor->id(),
                                       .sign      = Signature() };
    historicalRowSign(history_row);

    history.insert("historical_chain", Utils::to_dbrow(history_row));
    history.close();

    return {};
}

std::expected<void, SqlCreateError> HistoricalSql::delete_where(DbRow &row, const std::string &temp_table) {
    DbConnector db(file_path);
    db.open();
    auto res_delete = db.delete_row(temp_table, row);
    db.close();

    if (!res_delete) {
        return std::unexpected(SqlCreateError::InvalidValue);
    }

    auto history = DbConnector(history_path);
    history.open();
    auto history_row = HistoricalRow { .id        = "3",
                                       .prevId    = "2",
                                       .operation = HistoricalSqlOperation::Remove,
                                       .data      = Json::serialize(row),
                                       .timestamp = 111,
                                       .actorId   = this->actor->id(),
                                       .sign      = Signature() };
    historicalRowSign(history_row);

    history.insert("historical_chain", Utils::to_dbrow(history_row));
    history.close();

    return {};
}

void HistoricalSql::historicalRowSign(HistoricalRow &row) {
    auto temp = fmt::format("{}", row);
    row.sign  = this->actor->key().sign(temp);
}

bool HistoricalSql::historicalRowVeryify(const HistoricalRow &row) {
    return true;
}
