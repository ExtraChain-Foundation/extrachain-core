#include "dfs/vector_snapshot.h"

#include <sqlite3.h>

Dfs::VectorSnapshot::VectorSnapshot(const FsPath& path, const std::string& primary)
    : database_(path)
    , index_(database_, primary)
    , wal_path_(path.native().string() + "-wal")
    , deadline_(std::chrono::steady_clock::now() + std::chrono::seconds(120)) {
}

Dfs::VectorSnapshot::~VectorSnapshot() {
    if (database_.is_open())
        database_.query("ROLLBACK");
}

std::expected<std::unique_ptr<Dfs::VectorSnapshot>, std::string> Dfs::VectorSnapshot::open(
    const FsPath&      path,
    const std::string& primary) {
    // Older SQLite releases can corrupt a WAL when a writer races a checkpoint.
    if (sqlite3_libversion_number() < 3051003)
        return std::unexpected("Vector snapshots require SQLite 3.51.3 or later");
    auto snapshot = std::unique_ptr<VectorSnapshot>(new VectorSnapshot(path, primary));
    if (!snapshot->database_.open(false))
        return std::unexpected("Cannot open vector snapshot");
    const auto journal = snapshot->database_.select("PRAGMA journal_mode=WAL");
    if (journal.size() != 1 || !journal.front().contains("journal_mode")
        || journal.front().at("journal_mode") != "wal")
        return std::unexpected("Vector snapshots require WAL storage");
    if (!snapshot->index_.root().has_value() || !snapshot->database_.query("PRAGMA query_only=ON")
        || !snapshot->database_.query("BEGIN"))
        return std::unexpected("Cannot start vector snapshot");
    const auto root = snapshot->index_.root();
    if (!root.has_value())
        return std::unexpected(root.error());
    snapshot->root_ = root.value();
    return snapshot;
}

const Dfs::VectorIndexRoot& Dfs::VectorSnapshot::root() const {
    return root_;
}

bool Dfs::VectorSnapshot::expired(std::chrono::steady_clock::time_point now) const {
    std::error_code error;
    const auto      bytes = std::filesystem::file_size(wal_path_, error);
    return now >= deadline_ || error || bytes > 256ULL * 1024 * 1024;
}

std::expected<Dfs::VectorIndexSlice, std::string> Dfs::VectorSnapshot::read(std::string_view prefix) {
    if (expired(std::chrono::steady_clock::now()))
        return std::unexpected("Vector snapshot has expired");
    const auto subtree = index_.subtree(prefix);
    if (!subtree.has_value())
        return std::unexpected(subtree.error());
    VectorIndexSlice slice { .summary = subtree.value() };
    if (slice.summary.rows <= 256 && (slice.summary.bytes <= 1024 * 1024 || slice.summary.rows == 1)) {
        const auto rows = index_.page(prefix, { }, 256);
        if (!rows.has_value())
            return std::unexpected(rows.error());
        slice.rows = rows.value();
    } else {
        const auto children = index_.children(slice.summary.prefix);
        if (!children.has_value())
            return std::unexpected(children.error());
        slice.children = children.value();
    }
    return slice;
}

bool Dfs::VectorSnapshot::verify(const VectorIndexSummary& expected,
                                 std::string_view          primary,
                                 const VectorIndexSlice&   slice) {
    if (slice.summary != expected)
        return false;
    if (!slice.children.empty()) {
        if (!slice.rows.empty())
            return false;
        const auto summary = VectorIndex::summarize_children(slice.summary.prefix, slice.children);
        return summary.has_value() && summary.value() == expected;
    }
    const auto summary = VectorIndex::summarize_rows(primary, slice.rows);
    return summary.has_value() && summary.value() == expected;
}
