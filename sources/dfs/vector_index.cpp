#include "dfs/vector_index.h"

#include <sqlite3.h>

namespace {
    using Summary                          = Dfs::VectorIndexSummary;
    constexpr std::string_view EmptyDomain = "EXC_DFS_MERKLE_EMPTY_V1";
    constexpr std::uint64_t    MaxCount    = std::numeric_limits<std::int64_t>::max();
    struct Node {
        Summary              summary;
        std::string          primary;
        std::string          value_hash;
        std::vector<Summary> children;
    };
    struct Statement {
        sqlite3_stmt* value = nullptr;
        explicit Statement(sqlite3* database, const std::string& sql) {
            if (sqlite3_prepare_v2(database, sql.c_str(), -1, &value, nullptr) != SQLITE_OK) {
                sqlite3_finalize(value);
                throw std::runtime_error("Cannot prepare vector index query");
            }
        }
        Statement(const Statement&)            = delete;
        Statement& operator=(const Statement&) = delete;
        ~Statement() {
            sqlite3_finalize(value);
        }
        void bind(int index, std::string_view text) {
            if (text.size() > std::numeric_limits<int>::max()
                || sqlite3_bind_text(value,
                                     index,
                                     text.empty() ? "" : text.data(),
                                     static_cast<int>(text.size()),
                                     SQLITE_TRANSIENT)
                       != SQLITE_OK) {
                throw std::runtime_error("Cannot bind vector index value");
            }
        }
        void bind(int index, std::uint64_t number) {
            if (number > MaxCount
                || sqlite3_bind_int64(value, index, static_cast<sqlite3_int64>(number)) != SQLITE_OK) {
                throw std::runtime_error("Vector index counter is outside its range");
            }
        }
        bool step() {
            const auto result = sqlite3_step(value);
            if (result != SQLITE_ROW && result != SQLITE_DONE) {
                throw std::runtime_error("Cannot execute vector index query");
            }
            return result == SQLITE_ROW;
        }
        std::string text(int index) {
            const auto* data   = sqlite3_column_text(value, index);
            const auto  length = sqlite3_column_bytes(value, index);
            return data == nullptr ? std::string { } : std::string(reinterpret_cast<const char*>(data), length);
        }
        std::uint64_t number(int index) {
            const auto number = sqlite3_column_int64(value, index);
            if (number < 0)
                throw std::runtime_error("Invalid vector index counter");
            return static_cast<std::uint64_t>(number);
        }
    };
    bool hex_prefix(std::string_view value) {
        return value.size() <= 64 && std::ranges::all_of(value, [](char c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    }
    std::string hash(std::string_view data) {
        return Utils::calculate_hash(std::string(data), Utils::HashAlgorithm::Blake3);
    }
    Summary empty() {
        return { .hash = hash(EmptyDomain) };
    }
    std::uint64_t add(std::uint64_t left, std::uint64_t right) {
        if (left > MaxCount || right > MaxCount - left)
            throw std::runtime_error("Vector index counter overflow");
        return left + right;
    }
    Summary summarize(Node& node) {
        Summary     result { .prefix = node.summary.prefix };
        std::string preimage;
        if (node.children.empty()) {
            if (result.prefix.size() != 64 || !hex_prefix(result.prefix) || node.value_hash.size() != 64
                || !hex_prefix(node.value_hash) || node.summary.bytes > MaxCount) {
                throw std::runtime_error("Invalid vector index leaf");
            }
            result.rows  = 1;
            result.bytes = node.summary.bytes;
            preimage =
                "EXC_DFS_MERKLE_LEAF_V1:" + result.prefix + node.value_hash + ':' + std::to_string(result.bytes);
        } else {
            if (node.children.size() < 2 || node.children.size() > 16 || result.prefix.size() >= 64) {
                throw std::runtime_error("Invalid vector index branch");
            }
            std::ranges::sort(node.children, { }, &Summary::prefix);
            preimage = "EXC_DFS_MERKLE_BRANCH_V1:" + std::to_string(result.prefix.size()) + ':' + result.prefix;
            char previous = 0;
            for (const auto& child : node.children) {
                if (!hex_prefix(child.prefix) || !child.prefix.starts_with(result.prefix)
                    || child.prefix.size() <= result.prefix.size() || child.hash.size() != 64
                    || !hex_prefix(child.hash) || child.rows == 0
                    || child.prefix[result.prefix.size()] == previous) {
                    throw std::runtime_error("Invalid vector index child");
                }
                previous     = child.prefix[result.prefix.size()];
                result.rows  = add(result.rows, child.rows);
                result.bytes = add(result.bytes, child.bytes);
                preimage += Json::serialize(child);
            }
        }
        result.hash = hash(preimage);
        return result;
    }
    Node load(sqlite3* database, const Summary& expected) {
        if (!hex_prefix(expected.prefix))
            throw std::runtime_error("Invalid vector node prefix");
        Statement query(database,
                        "SELECT primary_value,value_hash,children,hash,row_count,byte_size FROM ExVectorNodes "
                        "WHERE prefix=?1");
        query.bind(1, expected.prefix);
        if (!query.step() || sqlite3_column_bytes(query.value, 2) > 64 * 1024) {
            throw std::runtime_error("Vector index node is unavailable");
        }
        Node result;
        result.summary      = { .prefix = expected.prefix,
                                .hash   = query.text(3),
                                .rows   = query.number(4),
                                .bytes  = query.number(5) };
        result.primary      = query.text(0);
        result.value_hash   = query.text(1);
        const auto children = Json::deserialize<std::vector<Summary>>(query.text(2));
        if (!children.has_value())
            throw std::runtime_error("Invalid vector index children");
        result.children = children.value();
        if (result.summary != expected || summarize(result) != expected) {
            throw std::runtime_error("Vector index node hash mismatch");
        }
        return result;
    }
    Summary store(sqlite3* database, Node node) {
        node.summary = summarize(node);
        Statement query(database,
                        "INSERT OR REPLACE INTO "
                        "ExVectorNodes(prefix,primary_value,value_hash,children,hash,row_count,byte_size) "
                        "VALUES(?1,?2,?3,?4,?5,?6,?7)");
        query.bind(1, node.summary.prefix);
        query.bind(2, node.primary);
        query.bind(3, node.value_hash);
        query.bind(4, Json::serialize(node.children));
        query.bind(5, node.summary.hash);
        query.bind(6, node.summary.rows);
        query.bind(7, node.summary.bytes);
        if (query.step())
            throw std::runtime_error("Unexpected vector index write result");
        return node.summary;
    }
    DbRow read_row(Statement& statement) {
        DbRow       row;
        std::size_t bytes = 0;
        for (int i = 0; i < sqlite3_column_count(statement.value); ++i) {
            const std::string name = sqlite3_column_name(statement.value, i);
            bytes                  = add(bytes, name.size());
            bytes = add(bytes, static_cast<std::uint64_t>(sqlite3_column_bytes(statement.value, i)));
            if (bytes > 64 * 1024 * 1024)
                throw std::runtime_error("Vector row exceeds the transfer limit");
            row.emplace(name, statement.text(i));
        }
        return row;
    }
    std::string upper_bound(std::string prefix) {
        for (auto i = prefix.size(); i > 0; --i) {
            auto& value = prefix[i - 1];
            if (value == 'f')
                continue;
            value = value == '9' ? 'a' : static_cast<char>(value + 1);
            prefix.resize(i);
            return prefix;
        }
        return "g";
    }
} // namespace

Dfs::VectorIndex::VectorIndex(DbConnector& database, std::string primary)
    : database_(database)
    , primary_(std::move(primary)) {
    quoted_primary_ = "\"";
    for (char value : primary_) {
        quoted_primary_ += value;
        if (value == '"')
            quoted_primary_ += '"';
    }
    quoted_primary_ += '"';
}

std::string Dfs::VectorIndex::key_hash(std::string_view primary) {
    return hash("EXC_DFS_MERKLE_KEY_V1:" + std::string(primary));
}

std::string Dfs::VectorIndex::row_hash(const DbRow& row) {
    std::vector<std::pair<std::string_view, std::string_view>> ordered(row.begin(), row.end());
    std::ranges::sort(ordered);
    std::string data = "EXC_DFS_MERKLE_ROW_V1:";
    for (const auto& [name, value] : ordered) {
        data += std::to_string(name.size()) + ':';
        data += name;
        data += std::to_string(value.size()) + ':';
        data += value;
    }
    return hash(data);
}

std::string Dfs::VectorIndex::schema_hash() {
    if (primary_.empty() || primary_.size() > 128 || primary_.find('\0') != std::string::npos) {
        throw std::invalid_argument("Invalid vector primary column");
    }
    const auto columns = database_.table_columns("Vector");
    if (columns.empty() || columns.front().name != primary_) {
        throw std::runtime_error("Vector primary column differs from its schema");
    }
    return hash("EXC_DFS_VECTOR_SCHEMA_V1:" + Json::serialize(columns) + ':' + primary_);
}

bool Dfs::VectorIndex::valid_root(const VectorIndexRoot& root) {
    if (root.version != 1 || root.schema.size() != 64 || !hex_prefix(root.schema) || root.hash.size() != 64
        || !hex_prefix(root.hash) || !hex_prefix(root.tree.prefix) || root.tree.hash.size() != 64
        || !hex_prefix(root.tree.hash) || root.tree.rows > MaxCount || root.tree.bytes > MaxCount) {
        return false;
    }
    if (root.tree.rows == 0 && root.tree != empty())
        return false;
    if (root.tree.rows == 1 && root.tree.prefix.size() != 64)
        return false;
    if (root.tree.rows > 1 && root.tree.prefix.size() >= 64)
        return false;
    return root.hash == hash("EXC_DFS_VECTOR_ROOT_V1:" + root.schema + Json::serialize(root.tree));
}

std::expected<Dfs::VectorIndexSummary, std::string> Dfs::VectorIndex::summarize_children(
    std::string_view                       prefix,
    const std::vector<VectorIndexSummary>& children) {
    try {
        if (!hex_prefix(prefix) || children.size() < 2 || children.size() > 16)
            throw std::runtime_error("Invalid vector branch proof");
        Node node { .summary = { .prefix = std::string(prefix) }, .children = children };
        return summarize(node);
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<Dfs::VectorIndexSummary, std::string> Dfs::VectorIndex::summarize_rows(
    std::string_view          primary,
    const std::vector<DbRow>& rows) {
    try {
        if (primary.empty() || primary.size() > 128 || rows.size() > 256)
            throw std::runtime_error("Invalid vector row proof");
        if (rows.empty())
            return empty();
        std::vector<Summary> leaves;
        std::uint64_t        bytes = 0;
        for (const auto& row : rows) {
            const auto found = row.find(std::string(primary));
            if (found == row.end() || found->second.size() > 4096)
                throw std::runtime_error("Invalid vector row primary value");
            Node leaf { .summary = { .prefix = key_hash(found->second) } };
            for (const auto& [name, value] : row) {
                leaf.summary.bytes = add(leaf.summary.bytes, add(name.size(), value.size()));
            }
            bytes = add(bytes, leaf.summary.bytes);
            if (bytes > 64 * 1024 * 1024)
                throw std::runtime_error("Vector row proof exceeds the transfer limit");
            leaf.value_hash = row_hash(row);
            leaves.push_back(summarize(leaf));
        }
        std::ranges::sort(leaves, { }, &Summary::prefix);
        for (std::size_t i = 1; i < leaves.size(); ++i) {
            if (leaves[i - 1].prefix == leaves[i].prefix)
                throw std::runtime_error("Duplicate vector proof primary value");
        }
        const auto tree = [&](this auto&& self, std::size_t begin, std::size_t end) -> Summary {
            if (end - begin == 1)
                return leaves[begin];
            const auto& first  = leaves[begin].prefix;
            const auto& last   = leaves[end - 1].prefix;
            const auto  common = std::mismatch(first.begin(), first.end(), last.begin()).first - first.begin();
            Node        branch { .summary = { .prefix = first.substr(0, common) } };
            for (auto i = begin; i < end;) {
                auto next = i + 1;
                while (next < end && leaves[next].prefix[common] == leaves[i].prefix[common])
                    ++next;
                branch.children.push_back(self(i, next));
                i = next;
            }
            return summarize(branch);
        };
        return tree(0, leaves.size());
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

void Dfs::VectorIndex::prepare() {
    for (const auto* sql :
         { "CREATE TABLE IF NOT EXISTS ExVectorNodes(prefix TEXT PRIMARY KEY,primary_value TEXT NOT "
           "NULL,value_hash TEXT NOT NULL,children TEXT NOT NULL,hash TEXT NOT NULL,row_count INTEGER NOT "
           "NULL,byte_size INTEGER NOT NULL)",
           "CREATE TABLE IF NOT EXISTS ExVectorIndex(version INTEGER PRIMARY KEY CHECK(version=1),primary_name "
           "TEXT NOT NULL,schema_hash TEXT NOT NULL,dirty INTEGER NOT NULL,root_prefix TEXT NOT NULL,root_hash "
           "TEXT NOT NULL,row_count INTEGER NOT NULL,byte_size INTEGER NOT NULL,legacy_hash TEXT NOT NULL)",
           "CREATE TRIGGER IF NOT EXISTS ExVectorInsert AFTER INSERT ON Vector BEGIN UPDATE ExVectorIndex SET "
           "dirty=dirty+1,legacy_hash=''; END",
           "CREATE TRIGGER IF NOT EXISTS ExVectorUpdate AFTER UPDATE ON Vector BEGIN UPDATE ExVectorIndex SET "
           "dirty=dirty+1,legacy_hash=''; END",
           "CREATE TRIGGER IF NOT EXISTS ExVectorDelete AFTER DELETE ON Vector BEGIN UPDATE ExVectorIndex SET "
           "dirty=dirty+1,legacy_hash=''; END" }) {
        if (!database_.query(sql))
            throw std::runtime_error("Cannot create vector index schema");
    }
}

Dfs::VectorIndexRoot Dfs::VectorIndex::read_root() {
    Statement
        query(database_.getDb(),
              "SELECT primary_name,schema_hash,dirty,root_prefix,root_hash,row_count,byte_size,legacy_hash FROM "
              "ExVectorIndex WHERE version=1");
    if (!query.step() || query.text(0) != primary_ || query.number(2) != 0) {
        throw std::runtime_error("Vector index requires a rebuild");
    }
    VectorIndexRoot result;
    result.schema      = query.text(1);
    result.legacy_hash = query.text(7);
    result.tree        = { .prefix = query.text(3),
                           .hash   = query.text(4),
                           .rows   = query.number(5),
                           .bytes  = query.number(6) };
    if (result.tree.rows == 0) {
        if (result.tree != empty())
            throw std::runtime_error("Invalid empty vector index");
    } else {
        static_cast<void>(load(database_.getDb(), result.tree));
    }
    result.hash = hash("EXC_DFS_VECTOR_ROOT_V1:" + result.schema + Json::serialize(result.tree));
    return result;
}

void Dfs::VectorIndex::rebuild(const std::string& legacy_hash) {
    if (!database_.query("DELETE FROM ExVectorNodes"))
        throw std::runtime_error("Cannot reset vector index");
    const auto initial = empty();
    Statement  reset(database_.getDb(), "INSERT OR REPLACE INTO ExVectorIndex VALUES(1,?1,?2,0,'',?3,0,0,?4)");
    reset.bind(1, primary_);
    reset.bind(2, schema_hash());
    reset.bind(3, initial.hash);
    reset.bind(4, legacy_hash);
    reset.step();
    // The Vector table is unchanged while this transaction builds the derived index.
    Statement     rows(database_.getDb(), "SELECT * FROM Vector");
    std::uint64_t count = 0;
    while (rows.step()) {
        update_row(read_row(rows));
        ++count;
    }
    if (read_root().tree.rows != count)
        throw std::runtime_error("Duplicate vector primary values");
}

std::expected<Dfs::VectorIndexRoot, std::string> Dfs::VectorIndex::root() {
    try {
        std::string legacy_hash;
        if (database_.table_exists("ExVectorIndex")) {
            Statement
                dirty(database_.getDb(),
                      "SELECT dirty,primary_name,schema_hash,legacy_hash FROM ExVectorIndex WHERE version=1");
            const bool found = dirty.step();
            if (found)
                legacy_hash = dirty.text(3);
            if (found && dirty.number(0) == 0 && dirty.text(1) == primary_ && dirty.text(2) == schema_hash()) {
                try {
                    return read_root();
                } catch (const std::exception&) {
                    // A damaged derived index can be rebuilt from the Vector table.
                }
            }
        }
        if (!database_.query("SAVEPOINT vector_index_rebuild"))
            throw std::runtime_error("Cannot start vector index rebuild");
        try {
            if (!database_.table_exists("ExVectorIndex")) {
                legacy_hash = database_.hash_size(primary_).first;
            }
            prepare();
            rebuild(legacy_hash);
            auto result = read_root();
            if (!database_.query("RELEASE vector_index_rebuild"))
                throw std::runtime_error("Cannot commit vector index rebuild");
            return result;
        } catch (...) {
            database_.query("ROLLBACK TO vector_index_rebuild");
            database_.query("RELEASE vector_index_rebuild");
            throw;
        }
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

Dfs::VectorIndexSummary Dfs::VectorIndex::insert(const Summary& current, const DbRow& row) {
    const auto& primary = row.at(primary_);
    if (primary.size() > 4096)
        throw std::runtime_error("Vector primary value is too large");
    const auto key = key_hash(primary);
    Node       leaf { .summary = { .prefix = key }, .primary = primary, .value_hash = row_hash(row) };
    for (const auto& [name, value] : row)
        leaf.summary.bytes = add(leaf.summary.bytes, name.size() + value.size());
    if (current.rows == 0)
        return store(database_.getDb(), std::move(leaf));
    auto       node     = load(database_.getDb(), current);
    const auto mismatch = std::mismatch(key.begin(), key.end(), current.prefix.begin(), current.prefix.end());
    const auto common   = static_cast<std::size_t>(mismatch.second - current.prefix.begin());
    if (common != current.prefix.size()) {
        const auto added = store(database_.getDb(), std::move(leaf));
        Node       parent { .summary = { .prefix = key.substr(0, common) }, .children = { current, added } };
        return store(database_.getDb(), std::move(parent));
    }
    if (node.children.empty()) {
        if (node.primary != primary)
            throw std::runtime_error("Vector primary key hash collision");
        return store(database_.getDb(), std::move(leaf));
    }
    const auto child = std::ranges::find_if(node.children, [&](const Summary& item) {
        return item.prefix[current.prefix.size()] == key[current.prefix.size()];
    });
    if (child == node.children.end()) {
        node.children.push_back(store(database_.getDb(), std::move(leaf)));
    } else {
        *child = insert(*child, row);
    }
    return store(database_.getDb(), std::move(node));
}

void Dfs::VectorIndex::update_row(const DbRow& row) {
    Statement query(database_.getDb(),
                    "SELECT root_prefix,root_hash,row_count,byte_size FROM ExVectorIndex WHERE version=1");
    if (!query.step())
        throw std::runtime_error("Missing vector index root");
    const Summary current { .prefix = query.text(0),
                            .hash   = query.text(1),
                            .rows   = query.number(2),
                            .bytes  = query.number(3) };
    const auto    updated = insert(current, row);
    Statement write(database_.getDb(),
                    "UPDATE ExVectorIndex SET dirty=0,root_prefix=?1,root_hash=?2,row_count=?3,byte_size=?4 WHERE "
                    "version=1");
    write.bind(1, updated.prefix);
    write.bind(2, updated.hash);
    write.bind(3, updated.rows);
    write.bind(4, updated.bytes);
    write.step();
}

std::expected<void, std::string> Dfs::VectorIndex::update(std::string_view primary) {
    try {
        if (sqlite3_get_autocommit(database_.getDb()) != 0) {
            throw std::runtime_error("Vector index update requires the row transaction");
        }
        bool rebuild_needed = !database_.table_exists("ExVectorIndex");
        if (!rebuild_needed) {
            Statement state(database_.getDb(), "SELECT dirty,primary_name FROM ExVectorIndex WHERE version=1");
            rebuild_needed = !state.step() || state.number(0) > 1 || state.text(1) != primary_;
        }
        if (rebuild_needed) {
            const auto rebuilt = root();
            if (!rebuilt.has_value())
                return std::unexpected(rebuilt.error());
            return { };
        }
        Statement stored(database_.getDb(), "SELECT * FROM Vector WHERE " + quoted_primary_ + "=?1");
        stored.bind(1, primary);
        if (!stored.step())
            throw std::runtime_error("Vector row is unavailable for indexing");
        const auto row = read_row(stored);
        if (stored.step())
            throw std::runtime_error("Duplicate vector primary values");
        update_row(row);
        return { };
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<std::vector<Dfs::VectorIndexSummary>, std::string> Dfs::VectorIndex::children(
    std::string_view prefix) {
    const auto node = subtree(prefix);
    if (!node.has_value())
        return std::unexpected(node.error());
    try {
        if (node.value().rows == 0)
            return std::vector<Summary> { };
        const auto loaded = load(database_.getDb(), node.value());
        if (loaded.children.empty() || node.value().prefix != prefix)
            return std::vector<Summary> { node.value() };
        return loaded.children;
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<Dfs::VectorIndexSummary, std::string> Dfs::VectorIndex::subtree(std::string_view prefix) {
    try {
        if (!hex_prefix(prefix))
            throw std::runtime_error("Invalid vector prefix");
        const auto current = root();
        if (!current.has_value())
            return std::unexpected(current.error());
        auto node = current.value().tree;
        while (node.rows != 0 && node.prefix.size() < prefix.size() && prefix.starts_with(node.prefix)) {
            const auto loaded = load(database_.getDb(), node);
            const auto child  = std::ranges::find_if(loaded.children, [&](const Summary& value) {
                return value.prefix.starts_with(prefix) || prefix.starts_with(value.prefix);
            });
            if (child == loaded.children.end())
                return empty();
            node = *child;
        }
        if (node.rows == 0 || !node.prefix.starts_with(prefix))
            return empty();
        static_cast<void>(load(database_.getDb(), node));
        return node;
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}

std::expected<std::vector<DbRow>, std::string> Dfs::VectorIndex::page(std::string_view prefix,
                                                                      std::string_view after,
                                                                      std::size_t      limit) {
    try {
        if (!hex_prefix(prefix) || !hex_prefix(after) || (!after.empty() && after.size() != 64) || limit == 0
            || limit > 256) {
            throw std::runtime_error("Invalid vector page range");
        }
        const auto current = root();
        if (!current.has_value())
            return std::unexpected(current.error());
        Statement leaves(database_.getDb(),
                         "SELECT primary_value,byte_size,value_hash,prefix FROM ExVectorNodes WHERE "
                         "length(prefix)=64 AND prefix>=?1 AND "
                         "prefix<?2 AND prefix>?3 ORDER BY prefix LIMIT ?4");
        leaves.bind(1, prefix);
        leaves.bind(2, upper_bound(std::string(prefix)));
        leaves.bind(3, after);
        leaves.bind(4, limit);
        std::vector<DbRow> result;
        std::uint64_t      bytes = 0;
        while (leaves.step()) {
            const auto next_bytes = add(bytes, leaves.number(1));
            if (next_bytes > 64 * 1024 * 1024) {
                if (result.empty())
                    throw std::runtime_error("Vector row exceeds the transfer limit");
                break;
            }
            Statement row(database_.getDb(), "SELECT * FROM Vector WHERE " + quoted_primary_ + "=?1");
            row.bind(1, leaves.text(0));
            if (!row.step())
                throw std::runtime_error("Vector index points to an absent row");
            auto value = read_row(row);
            if (row_hash(value) != leaves.text(2) || key_hash(value.at(primary_)) != leaves.text(3)) {
                throw std::runtime_error("Vector row differs from its index");
            }
            result.push_back(std::move(value));
            bytes = next_bytes;
        }
        return result;
    } catch (const std::exception& error) {
        return std::unexpected(error.what());
    }
}
