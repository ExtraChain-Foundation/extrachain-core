#include <array>
#include <atomic>
#include <barrier>
#include <filesystem>
#include <memory>
#include <thread>

#include "dfs/catalog_metadata.h"
#include "dfs/legacy_catalog.h"
#include "test_support.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto home =
        std::filesystem::temp_directory_path() / ("extrachain-catalog-metadata-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    Actor<KeyPrivate> owner, outsider;
    owner.create(ActorType::User);
    outsider.create(ActorType::User);
    auto database = std::make_shared<DbConnector>(home / "catalog.sqlite");
    TEST_REQUIRE(database->open());
    TEST_REQUIRE(database->query(Dfs::Tables::DirsFile::CreateTableQueryActorsFiles));
    const auto sign = [&](Dfs::DirRow &row) {
        const auto signature = owner.key().sign(row.calculate_hash(owner.id()));
        TEST_REQUIRE(signature.has_value());
        row.sign = signature.value();
    };
    Dfs::DirRow first {
        .actor_id          = owner.id(),
        .owner_id          = owner.id(),
        .file_id           = std::string(64, 'a'),
        .prev_file_id      = std::string(64, 'c'),
        .hash              = std::string(64, 'b'),
        .metadata_revision = 100,
        .folder            = "ab",
        .name              = "c",
        .size              = 17,
        .created           = 100,
        .last_modified     = 100,
        .type              = Dfs::FileType::File,
        .state             = Dfs::FileState::Ready,
    };
    sign(first);
    TEST_REQUIRE(Dfs::valid_catalog_metadata(first, owner.to_public()));
    const auto legacy_export = Dfs::legacy_public_file(first, owner);
    TEST_REQUIRE(legacy_export.has_value());
    TEST_REQUIRE(owner.key().verify(first.calculate_legacy_hash(owner.id()), legacy_export.value().sign).value());
    TEST_REQUIRE(!Dfs::legacy_public_file(first, outsider).has_value());
    auto invalid_export = first;
    invalid_export.name = "tampered";
    TEST_REQUIRE(!Dfs::legacy_public_file(invalid_export, owner).has_value());
    for (const auto state : { Dfs::FileState::Known, Dfs::FileState::Removed }) {
        invalid_export       = first;
        invalid_export.state = state;
        sign(invalid_export);
        TEST_REQUIRE(!Dfs::legacy_public_file(invalid_export, owner).has_value());
    }
    invalid_export            = first;
    invalid_export.encryption = true;
    sign(invalid_export);
    TEST_REQUIRE(!Dfs::legacy_public_file(invalid_export, owner).has_value());
    invalid_export      = first;
    invalid_export.sign = legacy_export.value().sign;
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(invalid_export, owner.to_public()));
    auto ambiguous   = first;
    ambiguous.folder = "a";
    ambiguous.name   = "bc";
    TEST_REQUIRE(first.calculate_legacy_hash(owner.id()) == ambiguous.calculate_legacy_hash(owner.id()));
    TEST_REQUIRE(first.calculate_hash(owner.id()) != ambiguous.calculate_hash(owner.id()));
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(ambiguous, owner.to_public()));
    const auto initial = Dfs::store_catalog_metadata(database, first, owner.to_public(), true);
    TEST_REQUIRE(initial.has_value() && initial->changed);
    auto second    = first;
    second.file_id = std::string(64, 'd');
    second.name    = "second";
    sign(second);
    TEST_REQUIRE(Dfs::store_catalog_metadata(database, second, owner.to_public()).value().changed);
    TEST_REQUIRE(database->count("ActorsFiles") == 2);
    auto forged              = first;
    forged.name              = "forged";
    forged.metadata_revision = 200;
    forged.sign              = outsider.key().sign(forged.calculate_hash(owner.id())).value();
    TEST_REQUIRE(!Dfs::store_catalog_metadata(database, forged, outsider.to_public()).has_value());
    TEST_REQUIRE(!Dfs::store_catalog_metadata(database, forged, owner.to_public()).has_value());
    auto renamed              = first;
    renamed.name              = "renamed";
    renamed.folder            = "new-folder";
    renamed.metadata_revision = 101;
    renamed.last_modified     = 101;
    sign(renamed);
    TEST_REQUIRE(Dfs::store_catalog_metadata(database, renamed, owner.to_public()).value().changed);
    const auto persisted = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(database, owner.id(), first.file_id);
    TEST_REQUIRE(persisted.has_value() && persisted->name == renamed.name && persisted->folder == renamed.folder);
    TEST_REQUIRE(Dfs::valid_catalog_metadata(persisted.value(), owner.to_public()));
    TEST_REQUIRE(!Dfs::store_catalog_metadata(database, first, owner.to_public()).value().changed);

    auto authored     = second;
    authored.file_id  = std::string(64, '1');
    authored.actor_id = outsider.id();
    sign(authored);
    TEST_REQUIRE(Dfs::valid_catalog_metadata(authored, owner.to_public()));
    authored.sign = outsider.key().sign(authored.calculate_hash(owner.id())).value();
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(authored, outsider.to_public()));
    authored.folder     = ":DApp:Chat:Invite";
    authored.encryption = true;
    authored.sign       = outsider.key().sign(authored.calculate_hash(owner.id())).value();
    TEST_REQUIRE(Dfs::valid_catalog_metadata(authored, outsider.to_public()));
    authored.size = 1024 * 1024 + 1;
    authored.sign = outsider.key().sign(authored.calculate_hash(owner.id())).value();
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(authored, outsider.to_public()));

    auto vector          = first;
    vector.file_id       = std::string(64, 'e');
    vector.type          = Dfs::FileType::Vector;
    vector.template_hash = Utils::calculate_hash("schema and write policy");
    sign(vector);
    auto hint          = vector;
    hint.hash          = std::string(64, 'f');
    hint.size          = 1000;
    hint.last_modified = 200;
    TEST_REQUIRE(Dfs::valid_catalog_metadata(hint, owner.to_public()));
    hint.template_hash = std::string(64, 'f');
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(hint, owner.to_public()));
    auto removed = Dfs::catalog_tombstone(owner.id(), first.file_id, 102, first.sign);
    TEST_REQUIRE(!Dfs::valid_catalog_metadata(removed, owner.to_public()));
    sign(removed);
    TEST_REQUIRE(Dfs::store_catalog_metadata(database, removed, owner.to_public()).value().changed);
    TEST_REQUIRE(!Dfs::store_catalog_metadata(database, removed, owner.to_public()).value().changed);
    renamed.metadata_revision = 1000;
    renamed.created           = 0;
    sign(renamed);
    TEST_REQUIRE(!Dfs::store_catalog_metadata(database, renamed, owner.to_public()).value().changed);
    removed.file_id = std::string(64, 'f');
    sign(removed);
    TEST_REQUIRE(Dfs::store_catalog_metadata(database, removed, owner.to_public()).value().changed);
    const auto tombstone = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(database, owner.id(), first.file_id);
    TEST_REQUIRE(tombstone.has_value() && !tombstone->prev_file_id.has_value() && tombstone->name.empty());
    TEST_REQUIRE(database->count("ActorsFiles") == 3);
    Dfs::Tables::DirsFile::ActorSpace::update_file_state(database,
                                                         owner.id(),
                                                         first.file_id,
                                                         Dfs::FileState::Ready);
    TEST_REQUIRE(Dfs::Tables::DirsFile::ActorSpace::get_dir_row(database, owner.id(), first.file_id).value().state
                 == Dfs::FileState::Removed);

    std::filesystem::create_directories(Dfs::Basic::DFS_FOLDER);
    auto legacy = Dfs::Tables::DirsFile::DirsSpace::database();
    TEST_REQUIRE(legacy.has_value());
    auto old_schema = Dfs::Tables::DirsFile::CreateTableQueryActorsFiles;
    auto begin      = old_schema.find("template_hash");
    old_schema.erase(begin, old_schema.find("folder", begin) - begin);
    begin = old_schema.find("prev_file_id");
    old_schema.replace(begin, old_schema.find("actor_id", begin) - begin, "prev_file_id TEXT UNIQUE,");
    TEST_REQUIRE(legacy.value()->query(old_schema));
    auto legacy_row = Utils::to_dbrow(first);
    legacy_row.erase("metadata_revision");
    legacy_row.erase("template_hash");
    TEST_REQUIRE(legacy.value()->insert("ActorsFiles", legacy_row));
    TEST_REQUIRE(legacy.value()->close());
    auto migrated = Dfs::Tables::DirsFile::DirsSpace::create_file();
    TEST_REQUIRE(migrated.has_value());
    const auto old_record =
        Dfs::Tables::DirsFile::ActorSpace::get_dir_row(migrated.value(), owner.id(), first.file_id);
    TEST_REQUIRE(old_record.has_value() && old_record->metadata_revision == 0 && old_record->name == first.name);
    TEST_REQUIRE(Dfs::store_catalog_metadata(migrated.value(), second, owner.to_public()).value().changed);
    TEST_REQUIRE(migrated.value()->count("ActorsFiles") == 2);
    TEST_REQUIRE(migrated.value()->close());
    constexpr std::size_t                              writer_count = 6;
    constexpr std::size_t                              revisions    = 64;
    std::array<std::vector<Dfs::DirRow>, writer_count> writes;
    for (std::size_t writer = 0; writer < writer_count; ++writer) {
        auto row    = first;
        row.file_id = Utils::calculate_hash("parallel catalog writer " + std::to_string(writer));
        row.prev_file_id.reset();
        for (std::size_t revision = 1; revision <= revisions; ++revision) {
            row.metadata_revision = revision;
            row.name              = "revision " + std::to_string(revision);
            sign(row);
            writes[writer].push_back(row);
        }
    }
    std::barrier             ready(static_cast<std::ptrdiff_t>(writer_count));
    std::atomic<std::size_t> failures = 0;
    const auto               signer   = owner.to_public();
    {
        std::vector<std::jthread> workers;
        for (std::size_t writer = 0; writer < writer_count; ++writer) {
            workers.emplace_back([&, writer] {
                ready.arrive_and_wait();
                for (const auto &row : writes[writer]) {
                    const auto stored = Dfs::store_catalog_metadata(database, row, signer);
                    if (!stored.has_value() || !stored.value().changed)
                        ++failures;
                }
            });
        }
    }
    TEST_REQUIRE_EQ(failures.load(), 0U);
    for (const auto &writer : writes) {
        const auto &expected = writer.back();
        const auto stored = Dfs::Tables::DirsFile::ActorSpace::get_dir_row(database, owner.id(), expected.file_id);
        TEST_REQUIRE(stored.has_value());
        TEST_REQUIRE_EQ(stored.value().metadata_revision, revisions);
        TEST_REQUIRE_EQ(stored.value().name, expected.name);
        TEST_REQUIRE(Dfs::valid_catalog_metadata(stored.value(), signer));
    }
    TEST_REQUIRE(database->close());
    database.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(home);
    return 0;
}
