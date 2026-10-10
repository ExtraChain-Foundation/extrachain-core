#include "core/extrachain_node.h"
#include "dfs/dfs_service.h"
#include "managers/account_controller.h"
#include "test_support.h"

#include <algorithm>
#include <filesystem>
#include <memory>

int main() {
    const auto original = std::filesystem::current_path();
    const auto directory =
        std::filesystem::temp_directory_path() / ("extrachain-vector-policy-" + Utils::generate_random_hex(8));
    std::filesystem::create_directories(directory);
    std::filesystem::current_path(directory);
    auto node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->process();
    Actor<KeyPrivate> owner, stranger;
    owner.create(ActorType::User);
    stranger.create(ActorType::User);
    node->account_controller()->create_profile("vector-policy", ActorType::User, owner);
    TEST_REQUIRE(node->actor_index()->save_actor(stranger.to_public()).has_value());
    auto schema = Dfs::CollectionTemplate::create("OwnerRows").value();
    schema.use_id().add_fields({ Dfs::Field::String("payload") });
    const auto owned_file = node->dfs()->store_vector(owner.id(), owner.id(), "OwnerRows", schema);
    TEST_REQUIRE(owned_file.has_value());
    auto  owned        = node->dfs()->make_vector(owner.id(), owned_file.value().file_id).value().second;
    auto  unauthorized = DfsVector::load(node.get(), stranger, owner.id(), owned_file.value().file_id).value();
    DbRow row { { "id", "one" }, { "payload", "original" } };
    TEST_REQUIRE(owned.store_add(row));
    DbRow attempt { { "id", "one" }, { "payload", "replacement" } };
    TEST_REQUIRE(!unauthorized.store_add(attempt));
    const auto sign = [&](DfsVector& vector, DbRow candidate) {
        candidate["actor"]     = stranger.id().to_string();
        candidate["timestamp"] = std::to_string(Utils::current_date_ms() + 1000);
        const auto hash        = vector.calculate_hash(candidate);
        TEST_REQUIRE(!hash.first.empty());
        candidate["sign"] = ByteArray(stranger.key().sign(hash.first).value()).toString();
        return candidate;
    };
    auto forged = sign(owned, row);
    TEST_REQUIRE(!owned.verify(forged));
    TEST_REQUIRE(!owned.local_add(forged, true).has_value());
    TEST_REQUIRE_EQ(owned.read_row("one").value().at("payload"), std::string("original"));
    auto package          = owned.generate_content_package_empty().value();
    package.content       = { forged };
    const auto owned_path = Dfs::Path::file_path(owner.id(), owned_file.value().file_id).value();
    std::filesystem::remove(owned_path.native());
    TEST_REQUIRE(!owned.handle_package(package));
    TEST_REQUIRE(!std::filesystem::exists(owned_path.native()));
    package.content = { row };
    TEST_REQUIRE(owned.handle_package(package));

    schema.set_write_policy(Dfs::VectorWritePolicy::ActorNamespace);
    const auto shared_file = node->dfs()->store_vector(owner.id(), owner.id(), "SharedRows", schema);
    TEST_REQUIRE(shared_file.has_value());
    auto  shared = node->dfs()->make_vector(owner.id(), shared_file.value().file_id).value().second;
    auto  writer = DfsVector::load(node.get(), stranger, owner.id(), shared_file.value().file_id).value();
    DbRow first { { "id", "same" }, { "payload", "owner" } };
    DbRow second { { "id", "same" }, { "payload", "participant" } };
    TEST_REQUIRE(shared.store_add(first));
    TEST_REQUIRE(writer.store_add(second));
    TEST_REQUIRE_EQ(first.at("id"), owner.id().to_string() + ":same");
    TEST_REQUIRE_EQ(second.at("id"), stranger.id().to_string() + ":same");
    TEST_REQUIRE_EQ(shared.read_row("same").value().at("payload"), std::string("owner"));
    TEST_REQUIRE_EQ(writer.read_row("same").value().at("payload"), std::string("participant"));
    TEST_REQUIRE_EQ(shared.read_row(second.at("id")).value().at("payload"), std::string("participant"));
    forged = sign(shared, first);
    TEST_REQUIRE(!shared.local_add(forged, true).has_value());
    DbRow preclaim { { "id", owner.id().to_string() + ":absent" }, { "payload", "preclaim" } };
    TEST_REQUIRE(!writer.store_add(preclaim));
    const auto root = shared.index_root().value().hash;
    package         = shared.generate_content_package().value();
    std::ranges::reverse(package.content);
    const auto shared_path = Dfs::Path::file_path(owner.id(), shared_file.value().file_id).value();
    std::filesystem::remove(shared_path.native());
    auto invalid = package;
    invalid.content.clear();
    invalid.vector_template.set_write_policy(Dfs::VectorWritePolicy::OwnerOnly);
    invalid.vector_file = Json::serialize(invalid.vector_template);
    TEST_REQUIRE(!shared.handle_package(invalid));
    TEST_REQUIRE(!std::filesystem::exists(shared_path.native()));
    invalid         = package;
    invalid.content = { forged };
    TEST_REQUIRE(!shared.handle_package(invalid));
    TEST_REQUIRE(!std::filesystem::exists(shared_path.native()));
    TEST_REQUIRE(shared.handle_package(package));
    TEST_REQUIRE_EQ(shared.index_root().value().hash, root);
    TEST_REQUIRE(!writer.remove(first.at("id")).has_value());
    const auto removed = writer.remove("same");
    TEST_REQUIRE(removed.has_value() && removed.value().at("status") == "0");
    TEST_REQUIRE(shared.verify(removed.value()));
    TEST_REQUIRE(shared.read_row("same").has_value());
    TEST_REQUIRE(!shared.read_row(second.at("id")).has_value());
    invalid = package;
    invalid.vector_template.set_write_policy(Dfs::VectorWritePolicy::OwnerOnly);
    invalid.vector_file = Json::serialize(invalid.vector_template);
    TEST_REQUIRE(!shared.handle_package(invalid));

    auto names = Dfs::CollectionTemplate::create("Names").value();
    names.set_write_policy(Dfs::VectorWritePolicy::ActorNamespace).add_fields({ Dfs::Field::String("name") });
    const auto names_file = node->dfs()->store_vector(owner.id(), owner.id(), "Names", names);
    TEST_REQUIRE(names_file.has_value());
    auto  owner_name = node->dfs()->make_vector(owner.id(), names_file.value().file_id).value().second;
    auto  other_name = DfsVector::load(node.get(), stranger, owner.id(), names_file.value().file_id).value();
    DbRow alias { { "name", "same display name" } }, other_alias = alias;
    TEST_REQUIRE(owner_name.store_add(alias) && other_name.store_add(other_alias));
    TEST_REQUIRE_EQ(owner_name.read_rows().value().size(), std::size_t(2));
    names.add_fields({ Dfs::Field::String("unique_value").unique() });
    TEST_REQUIRE(!Dfs::vector_storage_template(names, false).has_value());
    schema.set_write_policy(static_cast<Dfs::VectorWritePolicy>(99));
    TEST_REQUIRE(!Dfs::vector_storage_template(schema, false).has_value());
    node.reset();
    std::filesystem::current_path(original);
    std::filesystem::remove_all(directory);
    return 0;
}
