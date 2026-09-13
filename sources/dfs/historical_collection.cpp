#include "dfs/historical_collection.h"

#include "chain/actor_index.h"
#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "utils/db_connector.h"

#include <charconv>
#include <limits>

namespace {
    using Error            = CollectionError;
    using Row              = HistoricalCollectionRow;
    using Schema           = std::pair<Dfs::CollectionTemplate, bool>;
    constexpr auto History = "historical_chain";

    struct HistoryTransaction {
        DbConnector& db;
        bool         committed = false;
        ~HistoryTransaction() {
            if (!committed)
                db.query("ROLLBACK");
        }
        bool commit() {
            committed = db.query("COMMIT");
            return committed;
        }
    };

    std::optional<std::uint64_t> number(std::string_view text) {
        std::uint64_t result = 0;
        auto [end, error]    = std::from_chars(text.data(), text.data() + text.size(), result);
        if (error != std::errc() || end != text.data() + text.size() || std::to_string(result) != text)
            return std::nullopt;
        return result;
    }

    std::optional<Row> decode(const DbRow& record) {
        const auto event = record.find("event");
        if (event == record.end() || event->second.size() > HistoricalCollection::MaxEventBytes + 1024
            || !MessagePack::has_bounded_structure(event->second, 256, 128, 8))
            return std::nullopt;
        auto row = MessagePack::deserialize<Row>(event->second);
        if (!row.has_value())
            return std::nullopt;
        return row.value();
    }

    std::optional<Row> last(DbConnector& db) {
        auto records = db.select("SELECT event FROM historical_chain ORDER BY id DESC LIMIT 1");
        return records.size() == 1 ? decode(records.front()) : std::nullopt;
    }

    std::optional<Schema> schema_from(const Row& row) {
        if (row.id != 0 || row.operation != CollectionOperation::StructuralTemplated
            || row.data.size() > 128 * 1024)
            return std::nullopt;
        auto result = Json::deserialize<Schema>(row.data);
        if (!result.has_value())
            return std::nullopt;
        return result.value();
    }

    std::optional<DbSchema> storage_schema(const Dfs::CollectionTemplate& source, bool encrypted) {
        auto table_name = source.name();
        std::ranges::transform(table_name, table_name.begin(), [](unsigned char c) {
            return char(std::tolower(c));
        });
        if (!SqlValidator::validate_identifier(source.name()).has_value() || table_name == History
            || table_name == "vector" || source.fields().size() > 512 || source.primary.has_value()
            || source.write_policy() != Dfs::VectorWritePolicy::OwnerOnly
            || Json::serialize(source).size() > 128 * 1024)
            return std::nullopt;
        DbSchema result(source.name());
        using namespace sqlite::literals;
        result.add_columns("id"_int.primary_key(), "timestamp"_int.not_null());
        std::set<std::string> names { "id", "timestamp" };
        for (const auto& field : source.fields()) {
            auto name = field.name();
            std::ranges::transform(name, name.begin(), [](unsigned char c) {
                return char(std::tolower(c));
            });
            if (!names.insert(name).second || (encrypted && field.is_unique()))
                return std::nullopt;
            auto column = encrypted ? Dfs::Field::Blob(field.name()).to_db_column() : field.to_db_column();
            if (!column.has_value())
                return std::nullopt;
            result.add_column(column.value());
        }
        return result;
    }

    std::optional<DbRow> payload(const Row& row) {
        if (row.data.size() > HistoricalCollection::MaxEventBytes
            || !MessagePack::has_bounded_structure(row.data, 1100, 512, 3))
            return std::nullopt;
        auto result = MessagePack::deserialize<DbRow>(row.data);
        if (!result.has_value())
            return std::nullopt;
        return result.value();
    }

    bool projection(DbConnector& db, const Dfs::CollectionTemplate& schema, const Row& row, const DbRow& fields) {
        const auto id = row.target_id.value_or(row.id);
        DbRow      bind { { "id", std::to_string(id) } };
        if (row.operation != CollectionOperation::Add) {
            const auto existing =
                db.select("SELECT id FROM " + schema.name() + " WHERE id = :id", schema.name(), bind);
            if (existing.size() != 1)
                return false;
        }
        if (row.operation == CollectionOperation::Remove)
            return db.delete_row(schema.name(), bind);
        if (row.operation != CollectionOperation::Add && row.operation != CollectionOperation::Update)
            return false;
        std::set<std::string> allowed;
        for (const auto& field : schema.fields())
            allowed.insert(field.name());
        for (const auto& [key, value] : fields)
            if (!allowed.contains(key))
                return false;
        if (row.operation == CollectionOperation::Update && !db.delete_row(schema.name(), bind))
            return false;
        auto values         = fields;
        values["id"]        = std::to_string(id);
        values["timestamp"] = std::to_string(row.timestamp);
        return db.insert(schema.name(), values);
    }

    std::optional<DbRow> read_projection(DbConnector& db, const std::string& table, std::uint32_t id) {
        auto rows =
            db.select("SELECT * FROM " + table + " WHERE id = :id", table, { { "id", std::to_string(id) } });
        if (rows.size() != 1)
            return std::nullopt;
        rows.front().erase("id");
        rows.front().erase("timestamp");
        return rows.front();
    }

    bool append(DbConnector& db, const ActorId& owner, const std::string& file, const Row& row) {
        const auto encoded = MessagePack::serialize(row);
        const auto head    = HistoricalCollection::hash_size(db);
        if ((row.id != 0 && (head.first != row.prev_hash || head.second == 0))
            || encoded.size() > HistoricalCollection::MaxEventBytes + 1024
            || head.second > std::uint64_t(INT64_MAX) - encoded.size())
            return false;
        return db.insert(History,
                         { { "id", std::to_string(row.id) },
                           { "event", encoded },
                           { "hash", HistoricalCollection::row_hash(owner, file, row) },
                           { "bytes", std::to_string(head.second + encoded.size()) } });
    }

    bool consecutive(const ActorId&            owner,
                     const std::string&        file,
                     const std::optional<Row>& previous,
                     const Row&                row) {
        if (!previous.has_value())
            return row.id == 0 && !row.prev_id.has_value() && row.prev_hash.empty();
        return previous.value().id != UINT32_MAX && row.id == previous.value().id + 1
               && row.prev_id == previous.value().id && row.timestamp > previous.value().timestamp
               && row.prev_hash == HistoricalCollection::row_hash(owner, file, previous.value());
    }

    bool create_tables(DbConnector& db, const Schema& schema) {
        auto projection_schema = storage_schema(schema.first, schema.second);
        return projection_schema.has_value()
               && db.query(
                   "CREATE TABLE historical_chain (id INTEGER PRIMARY KEY, event BLOB NOT NULL, "
                   "hash TEXT NOT NULL, bytes INTEGER NOT NULL CHECK(bytes >= 0))")
               && db.create_table(projection_schema.value()).has_value();
    }

    bool writable(ExtraChain::Core::ExtraChainNode* node,
                  const ActorId&                    owner,
                  const std::string&                file,
                  bool                              require_catalog) {
        const auto catalog =
            Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner, file);
        if (!catalog.has_value())
            return !require_catalog;
        return catalog.value().type == Dfs::FileType::Collection
               && catalog.value().state != Dfs::FileState::Removed;
    }
} // namespace

HistoricalCollection::HistoricalCollection(ExtraChain::Core::ExtraChainNode* node,
                                           const Actor<KeyPrivate>&          actor,
                                           const ActorId&                    owner,
                                           const std::string&                file,
                                           Dfs::DataSecurity                 security,
                                           const Dfs::DataSecurityData&      security_data)
    : node(node)
    , actor_(actor)
    , file_actor_id_(owner)
    , file_id_(file)
    , data_security_(security)
    , security_data_(security_data) {
    // Every factory checks this path before construction.
    file_path_ = Dfs::Path::file_path(owner, file).value();
}

std::string HistoricalCollection::row_hash(const ActorId& owner, const std::string& file, const Row& row) {
    return Utils::calculate_hash(std::string("extrachain-history-v2:")
                                 + MessagePack::serialize(std::make_tuple(owner,
                                                                          file,
                                                                          row.id,
                                                                          row.prev_id,
                                                                          row.prev_hash,
                                                                          row.target_id,
                                                                          std::to_underlying(row.operation),
                                                                          row.data,
                                                                          row.timestamp,
                                                                          row.actor_id)));
}

bool HistoricalCollection::verify(ExtraChain::Core::ExtraChainNode* node,
                                  const ActorId&                    owner,
                                  const std::string&                file,
                                  const Row&                        row) {
    if (!node || owner.is_zero() || row.actor_id != owner || !Dfs::Path::file_path(owner, file).has_value()
        || row.data.size() > MaxEventBytes || row.timestamp == 0 || row.timestamp > INT64_MAX
        || row.prev_hash.size() > 64)
        return false;
    if (row.id == 0) {
        const auto schema = schema_from(row);
        if (row.prev_id.has_value() || !row.prev_hash.empty() || row.target_id.has_value() || !schema.has_value()
            || !storage_schema(schema.value().first, schema.value().second).has_value())
            return false;
    } else {
        if (!row.prev_id.has_value() || row.prev_id.value() != row.id - 1 || row.prev_hash.size() != 64)
            return false;
        if (row.operation == CollectionOperation::Add) {
            if (row.target_id.has_value() || !payload(row).has_value())
                return false;
        } else if (row.operation == CollectionOperation::Update || row.operation == CollectionOperation::Remove) {
            if (!row.target_id.has_value() || row.target_id.value() == 0 || row.target_id.value() >= row.id)
                return false;
            if (row.operation == CollectionOperation::Remove ? !row.data.empty() : !payload(row).has_value())
                return false;
        } else
            return false;
    }
    const auto actor = node->actor_index()->read_actor(owner);
    if (!actor.has_value())
        return false;
    auto checked = actor.value().key().verify(row_hash(owner, file, row), row.sign);
    return checked.has_value() && checked.value();
}

std::pair<std::string, std::uint64_t> HistoricalCollection::hash_size(DbConnector& db) {
    const auto rows = db.select("SELECT hash, bytes FROM historical_chain ORDER BY id DESC LIMIT 1");
    if (rows.size() != 1 || !rows.front().contains("hash") || !rows.front().contains("bytes"))
        return { };
    const auto bytes = number(rows.front().at("bytes"));
    if (!bytes.has_value() || rows.front().at("hash").size() != 64 || bytes.value() > INT64_MAX)
        return { };
    return { rows.front().at("hash"), bytes.value() };
}

std::expected<HistoricalCollection, Error> HistoricalCollection::create(
    ExtraChain::Core::ExtraChainNode* node,
    const Actor<KeyPrivate>&          actor,
    const ActorId&                    owner,
    const std::string&                file,
    const ActorId&                    template_owner,
    const std::string&                template_file,
    Dfs::DataSecurity                 security,
    const Dfs::DataSecurityData&      security_data) {
    auto schema =
        Dfs::Tables::DirsFile::ActorSpace::get_collection_template_file_id(template_owner, template_file);
    if (!schema.has_value())
        return std::unexpected(Error::StructuralCreation);
    return create(node, actor, owner, file, schema.value(), security, security_data);
}

std::expected<HistoricalCollection, Error> HistoricalCollection::create(
    ExtraChain::Core::ExtraChainNode* node,
    const Actor<KeyPrivate>&          actor,
    const ActorId&                    owner,
    const std::string&                file,
    const Dfs::CollectionTemplate&    schema,
    Dfs::DataSecurity                 security,
    const Dfs::DataSecurityData&      security_data) {
    if (!node || actor.id() != owner || owner.is_zero() || !Dfs::Path::file_path(owner, file).has_value())
        return std::unexpected(Error::StructuralCreation);
    auto lock = node->dfs()->download_manager().lock_file({ owner, file });
    if (!writable(node, owner, file, false))
        return std::unexpected(Error::StructuralCreation);
    HistoricalCollection chain(node, actor, owner, file, security, security_data);
    if (chain.file_path_.exists_and_size_not_zero())
        return std::unexpected(Error::Conflict);
    Schema descriptor { schema, security != Dfs::DataSecurity::Public };
    if (!storage_schema(schema, descriptor.second).has_value())
        return std::unexpected(Error::StructuralCreation);
    if (descriptor.second
        && !chain.encrypt_data({ { "probe", "key check" } }, security, security_data).has_value())
        return std::unexpected(Error::IncorrectEncryption);
    Row        row { .operation = CollectionOperation::StructuralTemplated,
                     .data      = Json::serialize(descriptor),
                     .timestamp = Utils::current_date_ms(),
                     .actor_id  = owner };
    const auto signature = actor.key().sign(row_hash(owner, file, row));
    if (!signature.has_value())
        return std::unexpected(Error::StructuralCreation);
    row.sign = signature.value();
    DbConnector     db(chain.file_path_);
    std::error_code path_error;
    std::filesystem::create_directories(chain.file_path_.native().parent_path(), path_error);
    if (path_error)
        return std::unexpected(Error::StructuralCreation);
    if (!db.open() || !db.query("BEGIN IMMEDIATE"))
        return std::unexpected(Error::StructuralCreation);
    HistoryTransaction transaction { db };
    if (!create_tables(db, descriptor) || !append(db, owner, file, row) || !transaction.commit())
        return std::unexpected(Error::StructuralCreation);
    chain.schema_     = schema;
    chain.table_name_ = schema.name();
    return chain;
}

std::expected<HistoricalCollection, Error> HistoricalCollection::load(ExtraChain::Core::ExtraChainNode* node,
                                                                      const Actor<KeyPrivate>&          actor,
                                                                      const ActorId&                    owner,
                                                                      const std::string&                file,
                                                                      Dfs::DataSecurity                 security,
                                                                      const Dfs::DataSecurityData& security_data) {
    if (!node || owner.is_zero() || !Dfs::Path::file_path(owner, file).has_value())
        return std::unexpected(Error::CollectionNotFound);
    HistoricalCollection chain(node, actor, owner, file, security, security_data);
    DbConnector          db(chain.file_path_);
    if (!db.open(false))
        return std::unexpected(Error::CollectionNotFound);
    const auto records  = db.select("SELECT event FROM historical_chain WHERE id = 0");
    const auto creation = records.size() == 1 ? decode(records.front()) : std::nullopt;
    if (!creation.has_value() || !verify(node, owner, file, creation.value()))
        return std::unexpected(Error::InvalidHistory);
    const auto descriptor = schema_from(creation.value()).value();
    chain.schema_         = descriptor.first;
    chain.table_name_     = descriptor.first.name();
    if (descriptor.second && security == Dfs::DataSecurity::Public)
        chain.data_security_ = Dfs::DataSecurity::Encrypted;
    if (!descriptor.second && security != Dfs::DataSecurity::Public)
        return std::unexpected(Error::IncorrectEncryption);
    return chain;
}

std::expected<bool, Error> HistoricalCollection::accept(ExtraChain::Core::ExtraChainNode* node,
                                                        const ActorId&                    owner,
                                                        const std::string&                file,
                                                        const std::vector<Row>&           rows) {
    if (rows.empty() || rows.size() > MaxPageRows)
        return std::unexpected(Error::InvalidHistory);
    std::size_t bytes = 0;
    for (const auto& row : rows) {
        if (row.data.size() > MaxPageBytes - bytes || !verify(node, owner, file, row))
            return std::unexpected(Error::InvalidHistory);
        bytes += row.data.size();
    }
    auto lock = node->dfs()->download_manager().lock_file({ owner, file });
    if (!writable(node, owner, file, true))
        return std::unexpected(Error::InvalidHistory);
    const auto      path = Dfs::Path::file_path(owner, file).value();
    DbConnector     db(path);
    std::error_code path_error;
    std::filesystem::create_directories(path.native().parent_path(), path_error);
    if (path_error)
        return std::unexpected(Error::Adding);
    if (!db.open() || !db.query("BEGIN IMMEDIATE"))
        return std::unexpected(Error::Adding);
    HistoryTransaction transaction { db };
    std::optional<Row> previous;
    Schema             schema;
    if (db.table_exists(History)) {
        previous     = last(db);
        auto records = db.select("SELECT event FROM historical_chain WHERE id = 0");
        auto first   = records.size() == 1 ? decode(records.front()) : std::nullopt;
        if (!previous.has_value() || !first.has_value() || !verify(node, owner, file, first.value()))
            return std::unexpected(Error::InvalidHistory);
        schema = schema_from(first.value()).value();
    } else {
        if (rows.front().id != 0)
            return std::unexpected(Error::Conflict);
        if (!db.table_names().empty())
            return std::unexpected(Error::InvalidHistory);
        const auto initial = schema_from(rows.front());
        if (!initial.has_value() || !create_tables(db, initial.value()))
            return std::unexpected(Error::InvalidHistory);
        schema = initial.value();
    }
    const auto catalog =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(node->dfs()->get_db_instance(), owner, file);
    if (!catalog.has_value() || catalog.value().encryption != schema.second)
        return std::unexpected(Error::InvalidHistory);
    bool changed = false;
    for (const auto& row : rows) {
        if (previous.has_value() && row.id <= previous.value().id) {
            auto existing = db.select("SELECT event FROM historical_chain WHERE id = :id",
                                      History,
                                      { { "id", std::to_string(row.id) } });
            auto prior    = existing.size() == 1 ? decode(existing.front()) : std::nullopt;
            if (!prior.has_value() || MessagePack::serialize(prior.value()) != MessagePack::serialize(row))
                return std::unexpected(Error::Conflict);
            continue;
        }
        if (!consecutive(owner, file, previous, row))
            return std::unexpected(Error::Conflict);
        if (row.id != 0) {
            auto data =
                row.operation == CollectionOperation::Remove ? std::optional<DbRow>(DbRow { }) : payload(row);
            if (!data.has_value() || !projection(db, schema.first, row, data.value()))
                return std::unexpected(Error::Adding);
            if (row.operation != CollectionOperation::Remove) {
                auto stored = read_projection(db, schema.first.name(), row.target_id.value_or(row.id));
                if (!stored.has_value() || stored.value() != data.value())
                    return std::unexpected(Error::InvalidHistory);
            }
        }
        if (!append(db, owner, file, row))
            return std::unexpected(Error::Adding);
        previous = row;
        changed  = true;
    }
    if (!transaction.commit())
        return std::unexpected(Error::Adding);
    return changed;
}

std::expected<Row, Error> HistoricalCollection::mutate(CollectionOperation          operation,
                                                       std::optional<std::uint32_t> target,
                                                       const DbRow&                 fields,
                                                       Dfs::DataSecurity            security,
                                                       const Dfs::DataSecurityData& security_data) {
    if (actor_.id() != file_actor_id_)
        return std::unexpected(Error::InvalidHistory);
    auto lock = node->dfs()->download_manager().lock_file({ file_actor_id_, file_id_ });
    if (!writable(node, file_actor_id_, file_id_, true))
        return std::unexpected(Error::InvalidHistory);
    DbConnector db(file_path_);
    if (!db.open(false) || !db.query("BEGIN IMMEDIATE"))
        return std::unexpected(Error::Adding);
    HistoryTransaction transaction { db };
    const auto         previous = last(db);
    if (!previous.has_value() || previous.value().id == UINT32_MAX || previous.value().timestamp >= INT64_MAX)
        return std::unexpected(Error::InvalidHistory);
    Row row { .id        = previous.value().id + 1,
              .prev_id   = previous.value().id,
              .prev_hash = row_hash(file_actor_id_, file_id_, previous.value()),
              .target_id = target,
              .operation = operation,
              .timestamp = std::max(Utils::current_date_ms(), previous.value().timestamp + 1),
              .actor_id  = file_actor_id_ };
    if (target.has_value() && (target.value() == 0 || target.value() >= row.id))
        return std::unexpected(Error::Conflict);
    DbRow canonical;
    if (operation != CollectionOperation::Remove) {
        // Resolve SQL types and defaults in a private in-memory database before signing or encryption.
        DbConnector normalizer(":memory:");
        auto        schema = storage_schema(schema_, false);
        if (!schema.has_value() || !normalizer.open() || !normalizer.create_table(schema.value()).has_value())
            return std::unexpected(Error::Adding);
        auto prepare      = row;
        prepare.operation = CollectionOperation::Add;
        prepare.target_id.reset();
        if (!projection(normalizer, schema_, prepare, fields))
            return std::unexpected(Error::Adding);
        auto normalized = read_projection(normalizer, table_name_, row.id);
        if (!normalized.has_value())
            return std::unexpected(Error::Adding);
        canonical = normalized.value();
        if (data_security_ != Dfs::DataSecurity::Public) {
            auto encrypted = encrypt_data(canonical, security, security_data);
            if (!encrypted.has_value())
                return std::unexpected(encrypted.error());
            canonical = encrypted.value();
        } else if (security != Dfs::DataSecurity::Public)
            return std::unexpected(Error::IncorrectEncryption);
        row.data = MessagePack::serialize(canonical);
        if (row.data.size() > MaxEventBytes)
            return std::unexpected(Error::Adding);
    }
    const auto signature = actor_.key().sign(row_hash(file_actor_id_, file_id_, row));
    if (!signature.has_value())
        return std::unexpected(Error::Adding);
    row.sign = signature.value();
    if (!projection(db, schema_, row, canonical))
        return std::unexpected(Error::Adding);
    if (operation != CollectionOperation::Remove) {
        auto stored = read_projection(db, table_name_, target.value_or(row.id));
        if (!stored.has_value() || stored.value() != canonical)
            return std::unexpected(Error::Adding);
    }
    if (!append(db, file_actor_id_, file_id_, row) || !transaction.commit())
        return std::unexpected(Error::Adding);
    return row;
}

std::expected<Row, Error> HistoricalCollection::add_row(const DbRow&                 row,
                                                        Dfs::DataSecurity            security,
                                                        const Dfs::DataSecurityData& data) {
    return mutate(CollectionOperation::Add, std::nullopt, row, security, data);
}
std::expected<Row, Error> HistoricalCollection::update_row(std::uint32_t                id,
                                                           const DbRow&                 row,
                                                           Dfs::DataSecurity            security,
                                                           const Dfs::DataSecurityData& data) {
    return mutate(CollectionOperation::Update, id, row, security, data);
}
std::expected<Row, Error> HistoricalCollection::remove_row(std::uint32_t id) {
    return mutate(CollectionOperation::Remove, id, { }, data_security_, security_data_);
}
std::expected<void, Error> HistoricalCollection::change_collection(const Row& row) {
    const auto result = accept(node, file_actor_id_, file_id_, { row });
    if (!result.has_value())
        return std::unexpected(result.error());
    return { };
}

std::expected<std::vector<Row>, Error> HistoricalCollection::get_historical_rows(std::uint64_t after,
                                                                                 std::size_t   limit) {
    if (limit == 0 || limit > MaxPageRows || after > std::uint64_t(UINT32_MAX) + 1)
        return std::unexpected(Error::InvalidHistory);
    DbConnector db(file_path_);
    if (!db.open(false))
        return std::unexpected(Error::HistoryNotFound);
    auto iterator = db.select_while("SELECT event FROM historical_chain WHERE id >= :id ORDER BY id LIMIT "
                                        + std::to_string(limit),
                                    History,
                                    { { "id", std::to_string(after) } });
    if (!iterator)
        return std::unexpected(Error::HistoryNotFound);
    std::vector<Row> result;
    std::size_t      bytes = 0;
    while (iterator->next()) {
        auto row = decode(iterator->dbRow());
        if (!row.has_value())
            return std::unexpected(Error::InvalidHistory);
        auto weight = row.value().data.size() + 1024;
        if (weight > MaxPageBytes - bytes)
            break;
        bytes += weight;
        result.push_back(std::move(row.value()));
    }
    if (iterator->failed())
        return std::unexpected(Error::HistoryNotFound);
    return result;
}

std::expected<Row, Error> HistoricalCollection::get_row(const std::string& search, const std::string& field) {
    if (field != "id" || !number(search).has_value())
        return std::unexpected(Error::InvalidHistory);
    DbConnector db(file_path_);
    if (!db.open(false))
        return std::unexpected(Error::HistoryNotFound);
    auto rows = db.select("SELECT event FROM historical_chain WHERE id = :id", History, { { "id", search } });
    auto row  = rows.size() == 1 ? decode(rows.front()) : std::nullopt;
    if (!row.has_value())
        return std::unexpected(Error::HistoryNotFound);
    return row.value();
}
std::expected<Row, Error> HistoricalCollection::get_last_row() {
    DbConnector db(file_path_);
    if (!db.open(false))
        return std::unexpected(Error::HistoryNotFound);
    const auto row = last(db);
    if (!row.has_value())
        return std::unexpected(Error::HistoryNotFound);
    return row.value();
}
std::expected<std::variant<Dfs::CollectionTemplateLink, Dfs::CollectionTemplate>, Error> HistoricalCollection::
    get_creation() {
    return schema_;
}
FsPath HistoricalCollection::get_file_path() const {
    return file_path_;
}
FsPath HistoricalCollection::get_historical_path() const {
    return file_path_;
}

std::expected<std::vector<DbRow>, Error> HistoricalCollection::get_collection_rows(const std::string& where) {
    DbConnector db(file_path_);
    if (!db.open(false))
        return std::unexpected(Error::CollectionNotFound);
    auto rows = db.select("SELECT * FROM " + table_name_ + " " + where + " ORDER BY id");
    if (data_security_ != Dfs::DataSecurity::Public) {
        for (auto& row : rows) {
            auto metadata = DbRow { { "id", row.at("id") }, { "timestamp", row.at("timestamp") } };
            row.erase("id");
            row.erase("timestamp");
            auto decrypted = decrypt_data(row, data_security_, security_data_);
            if (!decrypted.has_value())
                return std::unexpected(decrypted.error());
            row = std::move(decrypted.value());
            row.insert(metadata.begin(), metadata.end());
        }
    }
    return rows;
}

std::expected<DbRow, CollectionError> HistoricalCollection::encrypt_data(
    const DbRow&                 row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData& security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray&)> encryptor;

    if (data_security == Dfs::DataSecurity::Self) {
        if (auto* security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto myself = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!myself.has_value()) {
                return std::unexpected(CollectionError::IncorrectEncryption);
            }
            encryptor = [myself = myself.value()](const ByteArray& data) {
                return myself.get().key().encrypt_self(data.toBytes());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Actor) {
        if (auto* security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
            auto receiver = node->actor_index()->read_actor(security_actor->receiver_id);
            if (!sender.has_value() || !receiver.has_value()) {
                return std::unexpected(CollectionError::IncorrectEncryption);
            }
            encryptor = [s = sender.value(), r = receiver.value(), this](const ByteArray& data) {
                return s.get().key().encrypt(data.toBytes(), r.key().public_key());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Key) {
        if (auto* security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            encryptor = [key = security_key->key, this](const ByteArray& data) {
                return Cryptography::symmetric_encrypt(data.toBytes(), key);
            };
        }
    }

    if (!encryptor) {
        return std::unexpected(CollectionError::IncorrectEncryption);
    }

    DbRow encrypted_row;
    for (const auto& [key, value] : row) {
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
    const DbRow&                 row,
    Dfs::DataSecurity            data_security,
    const Dfs::DataSecurityData& security_data) {
    std::function<Cryptography::CryptoResult(const ByteArray&)> decryptor;
    if (data_security == Dfs::DataSecurity::Self) {
        if (auto* security_self = std::get_if<Dfs::DataSecuritySelf>(&security_data)) {
            auto myself = node->account_controller()->current_profile().get_actor(security_self->my_actor);
            if (!myself.has_value()) {
                return std::unexpected(CollectionError::IncorrectEncryption);
            }
            decryptor = [myself = myself.value()](const ByteArray& data) {
                return myself.get().key().decrypt_self(data.toBytes());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Actor) {
        if (auto* security_actor = std::get_if<Dfs::DataSecurityActor>(&security_data)) {
            auto sender   = node->account_controller()->current_profile().get_actor(security_actor->sender_id);
            auto receiver = node->actor_index()->read_actor(security_actor->receiver_id);
            if (!sender.has_value() || !receiver.has_value()) {
                return std::unexpected(CollectionError::IncorrectEncryption);
            }
            decryptor = [s = sender.value(), r = receiver.value(), this](const ByteArray& data) {
                return s.get().key().decrypt(data.toBytes(), r.key().public_key());
            };
        }
    } else if (data_security == Dfs::DataSecurity::Key) {
        if (auto* security_key = std::get_if<Dfs::DataSecurityKey>(&security_data)) {
            decryptor = [key = security_key->key, this](const ByteArray& data) {
                return Cryptography::symmetric_decrypt(data.toBytes(), key);
            };
        }
    }
    if (!decryptor) {
        return std::unexpected(CollectionError::IncorrectEncryption);
    }
    DbRow decrypted_row;
    for (const auto& [key, value] : row) {
        if (value.empty()) {
            decrypted_row[key] = "";
            continue;
        }

        auto decoded = ByteArray::fromBase64(value);
        if (!decoded.has_value()) {
            return std::unexpected(CollectionError::IncorrectEncryption);
        }
        auto res = decryptor(decoded.value());
        if (!res.has_value()) {
            return std::unexpected(CollectionError::IncorrectEncryption);
        }
        decrypted_row[key] = ByteArray(res.value()).toString();
    }
    return decrypted_row;
}
