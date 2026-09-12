#include <filesystem>
#include <memory>

#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "managers/thoth_manager.h"
#include "test_support.h"
#include "utils/file_io.h"

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-push-token-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner;
    owner.create(ActorType::User);
    const auto        path  = fmt::format(".thoth_device_token.{}", owner.id());
    const std::string token = "test-device-token-that-must-not-be-stored-as-plaintext";
    ThothManager      before_login(node.get(), "test");
    before_login.set_device_token(token);
    TEST_REQUIRE(!std::filesystem::exists(".thoth_device_token"));
    TEST_REQUIRE(!std::filesystem::exists(path));
    node->account_controller()->create_profile("push-token-profile", ActorType::User, owner);
    TEST_REQUIRE(FileIo::write_atomic(".thoth_device_token", "legacy-token").has_value());
    before_login.reconcile_tokens_for_chats({ });
    const auto stored = FileIo::read_all(path).value();
    TEST_REQUIRE(stored.starts_with("ECTH1"));
    TEST_REQUIRE(stored.find(token) == std::string::npos);
    TEST_REQUIRE(!std::filesystem::exists(".thoth_device_token"));
    const auto decrypted = owner.key().decrypt_self(ByteArray(stored.substr(5)).toBytes());
    TEST_REQUIRE(decrypted.has_value());
    const auto record = boost::json::parse(ByteArray(decrypted.value()).toString()).as_object();
    TEST_REQUIRE_EQ(record.at("profile").as_string(), owner.id().to_string());
    TEST_REQUIRE_EQ(record.at("token").as_string(), token);
#ifndef _WIN32
    const auto permissions = std::filesystem::status(path).permissions();
    TEST_REQUIRE((permissions & (std::filesystem::perms::group_all | std::filesystem::perms::others_all))
                 == std::filesystem::perms::none);
#endif
    before_login.prepare_shutdown();
    ThothManager restored(node.get(), "test");
    restored.set_device_token(token);
    restored.set_device_token(std::string(4097, 'x'));
    restored.set_device_token(std::string("invalid\0token", 13));
    TEST_REQUIRE_EQ(FileIo::read_all(path).value(), stored);
    restored.prepare_shutdown();
    auto corrupt = stored;
    corrupt.back() ^= 1;
    TEST_REQUIRE(FileIo::write_atomic(path, corrupt).has_value());
    ThothManager repaired(node.get(), "test");
    repaired.reconcile_tokens_for_chats({ });
    TEST_REQUIRE_EQ(FileIo::read_all(path).value(), corrupt);
    repaired.set_device_token(token);
    const auto repaired_data = FileIo::read_all(path).value();
    TEST_REQUIRE(repaired_data != corrupt);
    TEST_REQUIRE(owner.key().decrypt_self(ByteArray(repaired_data.substr(5)).toBytes()).has_value());
    repaired.prepare_shutdown();

    Actor<KeyPrivate> other;
    other.create(ActorType::User);
    node->account_controller()->create_profile("other-push-token-profile", ActorType::User, other);
    const auto other_path = fmt::format(".thoth_device_token.{}", other.id());
    TEST_REQUIRE(FileIo::write_atomic(other_path, repaired_data).has_value());
    ThothManager separate(node.get(), "test");
    separate.reconcile_tokens_for_chats({ });
    TEST_REQUIRE_EQ(FileIo::read_all(other_path).value(), repaired_data);
    separate.set_device_token("other-device-token");
    const auto separate_data = FileIo::read_all(other_path).value();
    TEST_REQUIRE(other.key().decrypt_self(ByteArray(separate_data.substr(5)).toBytes()).has_value());
    TEST_REQUIRE(!owner.key().decrypt_self(ByteArray(separate_data.substr(5)).toBytes()).has_value());
    TEST_REQUIRE_EQ(FileIo::read_all(path).value(), repaired_data);
    separate.prepare_shutdown();
    node->cleanUp();
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    std::puts("Protected device token persistence: PASS");
}
