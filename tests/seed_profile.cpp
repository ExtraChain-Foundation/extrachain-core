#include <filesystem>

#include "chain/private_profile.h"
#include "encryption/encryption_tools.h"
#include "test_support.h"
#include "utils/file_io.h"
#include "utils/exc_utils.h"

int main() {
    TEST_REQUIRE(sodium_init() >= 0);
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-seed-profile-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    std::filesystem::create_directories(Profiles::folder);
    MasterSeed seed;
    seed.fill(7);
    SeedProfile profile;
    profile.set(seed);
    profile.generate();
    const std::string password = "seed-profile-test-password";
    TEST_REQUIRE(profile.save(password).has_value());
    const auto filename = profile.filename();
    const auto first    = FileIo::read_all(filename).value();
    TEST_REQUIRE(profile.save(password).has_value());
    const auto second = FileIo::read_all(filename).value();
    TEST_REQUIRE(first != second);
    TEST_REQUIRE(first.starts_with("ECP2") && second.starts_with("ECP2"));
    TEST_REQUIRE_EQ(first.size(), std::size_t(98));
    TEST_REQUIRE(first.substr(10, 16) != second.substr(10, 16));
    TEST_REQUIRE(first.substr(26, 24) != second.substr(26, 24));
    const auto actor_id = profile.actors().front().id().to_string();
    auto       loaded   = SeedProfile::load(actor_id, password);
    TEST_REQUIRE(loaded.has_value() && loaded.value().seed() == seed);
    TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), second);
    TEST_REQUIRE(!SeedProfile::load(actor_id, "wrong-password").has_value());
    TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), second);
    TEST_REQUIRE(!SeedProfile::load("../invalid", password).has_value());
#ifndef _WIN32
    const auto permissions = std::filesystem::status(filename).permissions();
    TEST_REQUIRE((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all))
                 == std::filesystem::perms::none);
#endif
    for (std::size_t field : { 0U, 3U, 4U, 5U, 6U, 9U, 10U, 26U, 50U, 97U }) {
        auto invalid = second;
        invalid[field] ^= 1;
        TEST_REQUIRE(FileIo::write_atomic(filename, invalid).has_value());
        TEST_REQUIRE(!SeedProfile::load(actor_id, password).has_value());
        TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), invalid);
    }
    for (std::size_t length : { std::size_t(0), std::size_t(1), second.size() - 1 }) {
        TEST_REQUIRE(FileIo::write_atomic(filename, second.substr(0, length)).has_value());
        TEST_REQUIRE(!SeedProfile::load(actor_id, password).has_value());
    }
    const auto legacy_key = Cryptography::key_from_password(password);
    TEST_REQUIRE(legacy_key.has_value());
    const auto legacy_cipher =
        Cryptography::symmetric_encrypt(Bytes(seed.begin(), seed.end()), legacy_key.value(), true);
    TEST_REQUIRE(legacy_cipher.has_value());
    const auto legacy = Utils::to_base64(legacy_cipher.value());
    TEST_REQUIRE(FileIo::write_atomic(filename, legacy).has_value());
    TEST_REQUIRE(!SeedProfile::load(actor_id, "wrong-password").has_value());
    TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), legacy);
    auto migrated = SeedProfile::load(actor_id, password);
    TEST_REQUIRE(migrated.has_value() && migrated.value().seed() == seed);
    const auto modern = FileIo::read_all(filename).value();
    TEST_REQUIRE(modern != legacy && modern.starts_with("ECP2"));
    TEST_REQUIRE(SeedProfile::load(actor_id, password).has_value());
    TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), modern);
#ifndef _WIN32
    const auto parent_permissions = std::filesystem::status(Profiles::folder).permissions();
    std::filesystem::permissions(Profiles::folder,
                                 std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    const auto failed_write = profile.save(password);
    std::filesystem::permissions(Profiles::folder, parent_permissions);
    TEST_REQUIRE(!failed_write.has_value());
    TEST_REQUIRE_EQ(FileIo::read_all(filename).value(), modern);
#endif
    auto wallet_profile    = PrivateProfile::create(profile.actors()[0], profile.actors()[1], password, nullptr);
    const auto wallet_data = FileIo::read_all(filename).value();
    TEST_REQUIRE(wallet_data.starts_with("ECP2"));
    auto wallet_reload = PrivateProfile::read(profile.actors()[0].id(), password, nullptr, legacy_key.value());
    TEST_REQUIRE(wallet_reload.has_value());
    TEST_REQUIRE_EQ(wallet_reload.value().main_id(), profile.actors()[1].id());
    wallet_profile.add_wallet(profile.actors()[2]);
    const auto updated_wallet_data = FileIo::read_all(filename).value();
    TEST_REQUIRE(updated_wallet_data.starts_with("ECP2") && updated_wallet_data != wallet_data);
    wallet_reload = PrivateProfile::read(profile.actors()[0].id(), password, nullptr, legacy_key.value());
    TEST_REQUIRE(wallet_reload.has_value());
    TEST_REQUIRE(wallet_reload.value().get_actor(profile.actors()[2].id()).has_value());
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    std::puts("Seed profile encryption and migration: PASS");
}
