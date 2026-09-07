#include "chain/dag.h"
#include "contracts/contract_codec.h"
#include "contracts/dfs_contract_storage.h"
#include "core/extrachain_node.h"
#include "managers/account_controller.h"
#include "network/network_runtime.h"
#include "test_support.h"
#include <filesystem>
#include <fstream>
#include <memory>

using namespace ExtraChain::Contracts;

ContractTransactionData metadata(const PreparedContractChange    &change,
                                 std::string_view                 method,
                                 const std::vector<std::uint8_t> &arguments) {
    const auto             &version  = change.record.versions.at(change.record.active_version - 1);
    const auto             &revision = version.revisions.back();
    ContractTransactionData result {
        .kind                = change.record.kind,
        .language            = change.record.language,
        .method              = std::string(method),
        .arguments_base64    = Utils::to_base64(arguments),
        .module_hash         = version.module_hash,
        .previous_state_hash = revision.previous_hash,
        .state_hash          = revision.state_hash,
        .effects_hash        = Codec::effect_hash(change.output.effects),
        .effects_base64      = Utils::to_base64(Codec::encode_effects(change.output.effects)),
        .version             = version.version,
        .revision            = revision.revision,
        .checkpoint          = change.checkpoint,
        .checkpoint_revision = revision.checkpoint_revision,
    };
    std::size_t index = 0;
    for (const auto &effect : change.output.effects) {
        if (effect.kind != ContractEffectKind::ContractCall)
            continue;
        const auto &child  = change.children.at(index++);
        const auto  nested = metadata(child, effect.operation, effect.arguments);
        result.transitions.push_back(ContractTransitionData {
            .contract_id         = child.record.contract_id,
            .caller_contract_id  = change.record.contract_id,
            .kind                = nested.kind,
            .language            = nested.language,
            .method              = nested.method,
            .arguments_base64    = nested.arguments_base64,
            .module_hash         = nested.module_hash,
            .previous_state_hash = nested.previous_state_hash,
            .state_hash          = nested.state_hash,
            .effects_hash        = nested.effects_hash,
            .effects_base64      = nested.effects_base64,
            .version             = nested.version,
            .revision            = nested.revision,
            .checkpoint          = nested.checkpoint,
            .checkpoint_revision = nested.checkpoint_revision,
        });
        result.transitions.insert(result.transitions.end(), nested.transitions.begin(), nested.transitions.end());
    }
    return result;
}

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3)
        return 64;
    const auto module_path = std::filesystem::absolute(argv[1]);
    const auto original    = std::filesystem::current_path();
    const auto home        = argc == 3 ? std::filesystem::absolute(argv[2])
                                       : std::filesystem::temp_directory_path()
                                      / ("extrachain-depth-replay-" + Utils::generate_random_hex(8));
    TEST_REQUIRE(!std::filesystem::exists(home));
    std::filesystem::create_directories(home);
    std::filesystem::current_path(home);
    std::ifstream input(module_path, std::ios::binary);
    TEST_REQUIRE(input.good());
    std::vector<std::uint8_t> module((std::istreambuf_iterator<char>(input)), {});
    auto                      node = std::make_unique<ExtraChain::Core::ExtraChainNode>(false, true, 0);
    node->network_runtime().stop();
    node->process();
    node->dag()->set_mode(DagMode::Light);
    {
        Actor<KeyPrivate> owner;
        owner.create(ActorType::User);
        node->account_controller()->create_profile("dfs-depth-regression", ActorType::User, owner);
        std::vector<std::string> contracts;
        for (int index = 0; index < 3; ++index)
            contracts.push_back(node->account_controller()->create_service().id().to_string());
        ContractManager manager(std::make_unique<DfsContractStorage>(node->dfs(), node->dag()));
        const auto      commit = [&](PreparedContractChange           change,
                                std::string_view                 method,
                                const std::vector<std::uint8_t> &arguments,
                                std::uint64_t                    block) {
            auto staged = manager.stage(change);
            TEST_REQUIRE_MESSAGE(staged.has_value(), staged.has_value() ? "" : staged.error().detail);
            Transaction transaction;
            transaction.set_sender(owner.id());
            transaction.set_receiver(ActorId::create(change.record.contract_id).value());
            transaction.set_type(change.kind == ContractChangeKind::Create ? TransactionType::ContractDeploy
                                                                           : TransactionType::ContractCall);
            transaction.set_section(SectionId(block));
            transaction.set_timestamp(block);
            transaction.set_meta(Json::serialize(metadata(change, method, arguments)));
            TEST_REQUIRE(transaction.sign(owner));
            TEST_REQUIRE(node->dag()->save_transaction(transaction));
            node->dag()->set_current_section(SectionId(block));
            const auto committed = manager.commit(std::move(change), transaction.hash());
            TEST_REQUIRE_MESSAGE(committed.has_value(), committed.has_value() ? "" : committed.error().detail);
        };
        const std::vector<std::uint8_t> empty_arguments { 0x90 };
        for (std::size_t index = 0; index < contracts.size(); ++index) {
            auto deployed = manager.prepare_deploy(contracts[index],
                                                   owner.id().to_string(),
                                                   "depth-fixture",
                                                   module,
                                                   empty_arguments,
                                                   index + 1);
            TEST_REQUIRE_MESSAGE(deployed.has_value(), deployed.has_value() ? "" : deployed.error().detail);
            commit(std::move(deployed.value()), "init", empty_arguments, index + 1);
        }
        for (std::size_t root = 0; root < 2; ++root) {
            const std::vector<std::string>  tail(contracts.begin() + root + 1, contracts.end());
            const auto                      packed = MessagePack::serialize(tail);
            const std::vector<std::uint8_t> arguments(packed.begin(), packed.end());
            auto                            called =
                manager.prepare_call(contracts[root], owner.id().to_string(), "walk", arguments, root + 4);
            TEST_REQUIRE_MESSAGE(called.has_value(), called.has_value() ? "" : called.error().detail);
            commit(std::move(called.value()), "walk", arguments, root + 4);
            node->dag()->flush_admission();
            std::filesystem::remove_all(home / "contract-heads");
            DfsContractStorage replayed(node->dfs(), node->dag());
            for (std::size_t index = 0; index < contracts.size(); ++index) {
                const auto live = manager.inspect(contracts[index]);
                TEST_REQUIRE(live.has_value());
                const auto restored = replayed.load(contracts[index]);
                TEST_REQUIRE_MESSAGE(restored.has_value(), restored.has_value() ? "" : restored.error().detail);
                const auto &expected = live.value().versions.back().revisions.back();
                const auto &actual   = restored.value().versions.back().revisions.back();
                TEST_REQUIRE_EQ(actual.state, expected.state);
                TEST_REQUIRE_EQ(actual.state_hash, expected.state_hash);
                TEST_REQUIRE_EQ(actual.revision, expected.revision);
                const auto stored_depth = index < root ? 1 : index - root + 1;
                const auto decoded =
                    msgpack::unpack(reinterpret_cast<const char *>(actual.state.data()), actual.state.size());
                TEST_REQUIRE_EQ(decoded.get().as<std::uint64_t>(), stored_depth);
                std::printf("REPLAY root=%zu contract=%zu depth-plus-one=%zu revision=%llu PASS\n",
                            root,
                            index,
                            stored_depth,
                            static_cast<unsigned long long>(actual.revision));
            }
        }
    }
    node->cleanUp();
    std::puts("DFS-backed nested depth replay: PASS");
    node.reset();
    std::filesystem::current_path(original);
    if (argc == 2) {
        std::filesystem::remove_all(home);
    }
}
