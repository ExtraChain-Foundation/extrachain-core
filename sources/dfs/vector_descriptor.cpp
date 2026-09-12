#include "dfs/vector_descriptor.h"

#include "dfs/vector_index.h"
#include <sqlite3.h>

namespace {
    bool valid_companion(const Dfs::VectorDescriptor& descriptor) {
        using namespace Dfs;
        const auto inline_schema = Json::deserialize<CollectionTemplate>(descriptor.companion);
        if (inline_schema.has_value() && !inline_schema.value().fields().empty()) {
            if (Json::serialize(inline_schema.value()) != Json::serialize(descriptor.schema))
                return false;
        } else {
            const auto link = Json::deserialize<CollectionTemplateLink>(descriptor.companion);
            if (!link.has_value() || link.value().owner_id.is_zero() || link.value().file_id.size() != 64
                || !std::ranges::all_of(link.value().file_id,
                                        [](char c) {
                                            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                                        })
                || link.value().name != descriptor.schema.name())
                return false;
        }
        return true;
    }
} // namespace

std::expected<Dfs::CollectionTemplate, std::string> Dfs::vector_storage_template(const CollectionTemplate& schema,
                                                                                 bool encrypted) {
    if (schema.fields().empty() || schema.fields().size() > 1995
        || Json::serialize(schema).size() > VectorDescriptorLimit)
        return std::unexpected("Invalid vector template size");
    std::set<std::string> names { "actor", "sign", "timestamp", "status" };
    if (schema.primary.has_value() && !names.insert(schema.primary.value().name()).second)
        return std::unexpected("Reserved vector primary field");
    for (const auto& field : schema.fields()) {
        if (!names.insert(field.name()).second)
            return std::unexpected("Duplicate or reserved vector field");
    }
    auto storage = schema;
    if (encrypted)
        storage.set_to_blob();
    if (storage.primary.has_value()) {
        storage.primary.value().unique().not_null();
        storage.preadd_fields({ storage.primary.value(),
                                Field::ActorId("actor").not_null(),
                                Field::Blob("sign").not_null(),
                                Field::Timestamp("timestamp").not_null(),
                                Field::Integer("status").not_null() });
    } else {
        storage.preadd_fields({ Field::ActorId("actor").unique().not_null(),
                                Field::Blob("sign").not_null(),
                                Field::Timestamp("timestamp").not_null(),
                                Field::Integer("status").not_null() });
    }
    if (!storage.to_db_schema().has_value())
        return std::unexpected("Invalid vector storage schema");
    return storage;
}

std::expected<std::optional<Dfs::VectorDescriptor>, std::string> Dfs::read_vector_descriptor(
    DbConnector& database) {
    if (!database.table_exists("ExVectorDescriptor"))
        return std::optional<VectorDescriptor> { };
    const auto rows = database.select(
        "SELECT data FROM ExVectorDescriptor WHERE version=1 "
        "AND length(CAST(data AS BLOB))<=524288");
    if (rows.size() != 1 || !rows.front().contains("data"))
        return std::unexpected("Vector descriptor is unavailable");
    const auto descriptor = Json::deserialize<VectorDescriptor>(rows.front().at("data"));
    if (!descriptor.has_value() || descriptor.value().version != 1
        || descriptor.value().companion.size() > VectorDescriptorLimit
        || !vector_storage_template(descriptor.value().schema, false).has_value()
        || !valid_companion(descriptor.value()))
        return std::unexpected("Invalid vector descriptor");
    return std::optional<VectorDescriptor>(descriptor.value());
}

std::expected<void, std::string> Dfs::store_vector_descriptor(DbConnector&            database,
                                                              const VectorDescriptor& descriptor,
                                                              bool                    encrypted) {
    if (sqlite3_get_autocommit(database.getDb()) != 0 || descriptor.version != 1
        || descriptor.companion.size() > VectorDescriptorLimit)
        return std::unexpected("Vector descriptor requires a bounded write transaction");
    const auto storage = vector_storage_template(descriptor.schema, encrypted);
    if (!storage.has_value())
        return std::unexpected(storage.error());
    if (!valid_companion(descriptor))
        return std::unexpected("Invalid vector companion");
    const auto serialized = Json::serialize(descriptor);
    if (serialized.size() > 512 * 1024)
        return std::unexpected("Vector descriptor exceeds its storage limit");
    const auto columns = database.table_columns("Vector");
    if (columns.size() != storage.value().fields().size())
        return std::unexpected("Vector descriptor differs from its database");
    for (std::size_t i = 0; i < columns.size(); ++i) {
        const auto column = storage.value().fields()[i].to_db_column();
        if (!column.has_value() || column.value().name() != columns[i].name
            || column.value().type_name() != columns[i].type)
            return std::unexpected("Vector descriptor column differs from its database");
    }
    const auto current = read_vector_descriptor(database);
    if (!current.has_value())
        return std::unexpected(current.error());
    const auto primary =
        descriptor.schema.primary.has_value() ? descriptor.schema.primary.value().name() : "actor";
    if (!database.query("CREATE UNIQUE INDEX IF NOT EXISTS ExVectorPrimary ON Vector(\"" + primary + "\")"))
        return std::unexpected("Vector primary values must be unique");
    if (current.value().has_value()) {
        if (Json::serialize(current.value().value().schema) != Json::serialize(descriptor.schema))
            return std::unexpected("Vector descriptor is immutable");
        return { };
    }
    // Capture the legacy hash before adding a table that did not exist in that format.
    VectorIndex index(database,
                      descriptor.schema.primary.has_value() ? descriptor.schema.primary.value().name() : "actor");
    const auto  root = index.root();
    if (!root.has_value())
        return std::unexpected(root.error());
    if (!database.query(
            "CREATE TABLE ExVectorDescriptor(version INTEGER PRIMARY KEY CHECK(version=1),data TEXT NOT NULL)")
        || !database.insert("ExVectorDescriptor", { { "version", "1" }, { "data", serialized } }))
        return std::unexpected("Cannot store vector descriptor");
    return { };
}

bool Dfs::upsert_vector_row(DbConnector& database, const std::string& primary, const DbRow& row) {
    if (!row.contains(primary) || !SqlValidator::validate_identifier(primary).has_value())
        return false;
    std::string columns, values, updates;
    for (const auto& [name, value] : row) {
        if (!SqlValidator::validate_identifier(name).has_value())
            return false;
        if (!columns.empty()) {
            columns += ',';
            values += ',';
            updates += ',';
        }
        columns += '"' + name + '"';
        values += '@' + name;
        updates += '"' + name + "\"=excluded.\"" + name + '"';
    }
    // REPLACE can delete another primary row when a secondary UNIQUE constraint conflicts.
    return database.query("INSERT INTO Vector(" + columns + ") VALUES(" + values + ") ON CONFLICT(\"" + primary
                              + "\") DO UPDATE SET " + updates,
                          "Vector",
                          row);
}
