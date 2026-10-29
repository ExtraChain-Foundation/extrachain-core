#include "chain/transaction.h"
#include "consensus/balance_snapshot.h"
#include "contracts/contract_codec.h"
#include "contracts/contract_hash.h"
#include "contracts/contract_manager.h"
#include "contracts/contract_transaction.h"
#include "contracts/standard_token.h"
#include "test_support.h"
#include "utils/exc_utils_base64.h"
#include "utils/serialization.h"

using namespace ExtraChain;

namespace {
    Consensus::LightContractSnapshotV1 snapshot_for(const Contracts::PreparedContractChange& change,
                                                    const Actor<KeyPrivate>&                 owner,
                                                    TransactionType                          type) {
        const auto&             record  = change.record;
        const auto&             version = record.versions.at(record.active_version - 1);
        const auto&             state   = version.revisions.back();
        ContractTransactionData metadata {
            .kind             = record.kind,
            .language         = record.language,
            .method           = type == TransactionType::ContractDeploy ? "init" : "migrate",
            .arguments_base64 = Utils::to_base64(
                Contracts::Codec::encode_json(
                    type == TransactionType::ContractDeploy ? R"(["Snapshot Token","SNP",0,1000,[]])" : "[]")
                    .value()),
            .module_hash         = version.module_hash,
            .previous_state_hash = state.previous_hash,
            .state_hash          = state.state_hash,
            .effects_hash        = Contracts::Codec::effect_hash(change.output.effects),
            .effects_base64      = Utils::to_base64(Contracts::Codec::encode_effects(change.output.effects)),
            .version             = version.version,
            .revision            = state.revision,
            .checkpoint          = change.checkpoint,
            .checkpoint_revision = state.checkpoint_revision,
        };
        Transaction transaction;
        transaction.set_type(type);
        transaction.set_sender(owner.id());
        transaction.set_receiver(ActorId::create(record.contract_id).value());
        transaction.set_amount(BigNumberFloat(0));
        transaction.set_section(SectionId(state.block));
        transaction.set_timestamp(1);
        transaction.set_meta(Json::serialize(metadata));
        TEST_REQUIRE(transaction.sign(owner));
        return {
            .contract_id      = record.contract_id,
            .owner_id         = record.owner_id,
            .kind             = record.kind,
            .language         = record.language,
            .version          = version.version,
            .revision         = state.revision,
            .module_hash      = version.module_hash,
            .state_hash       = state.state_hash,
            .transaction_hash = transaction.hash(),
            .section          = state.block,
            .module           = version.module,
            .state            = state.state,
            .transaction      = transaction,
        };
    }

    std::string attach_proof(Consensus::LightContractSnapshotV1& snapshot) {
        const std::vector<std::pair<std::string, std::string>> entries {
            { snapshot.contract_id, Consensus::contract_snapshot_value(snapshot) },
        };
        snapshot.proof = Consensus::make_state_entry_proof("contracts", entries, snapshot.contract_id).value();
        return Consensus::segmented_state_root("contracts", entries);
    }
} // namespace

int main() {
    Actor<KeyPrivate> owner;
    Actor<KeyPrivate> contract;
    owner.create(ActorType::User);
    contract.create(ActorType::Service);
    const auto arguments = Contracts::Codec::encode_json(R"(["Snapshot Token","SNP",0,1000,[]])").value();
    for (const auto language :
         { Contracts::ToolchainLanguage::Rust, Contracts::ToolchainLanguage::AssemblyScript }) {
        Contracts::ContractManager manager;
        const auto module     = Contracts::standard_token_module(Contracts::FungibleTokenKind, language).value();
        auto       deployment = manager.prepare_deploy(contract.id().to_string(),
                                                 owner.id().to_string(),
                                                 std::string(Contracts::FungibleTokenKind),
                                                 module,
                                                 arguments,
                                                 20);
        TEST_REQUIRE(deployment.has_value());
        auto       snapshot = snapshot_for(deployment.value(), owner, TransactionType::ContractDeploy);
        const auto root     = attach_proof(snapshot);
        const auto verified = Consensus::verify_contract_snapshot(snapshot, root);
        TEST_REQUIRE(verified.has_value());
        TEST_REQUIRE_EQ(verified.value().versions.back().revisions.back().state, snapshot.state);
        TEST_REQUIRE_EQ(verified.value().versions.back().module, module);
        TEST_REQUIRE_EQ(Consensus::light_contract_summary(snapshot).checkpoint_revision, 1U);

        auto changed = snapshot;
        changed.state.front() ^= 1;
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, root).has_value());
        changed = snapshot;
        changed.module.back() ^= 1;
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, root).has_value());
        changed          = snapshot;
        changed.owner_id = contract.id().to_string();
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, root).has_value());
        changed = snapshot;
        changed.transaction.set_amount(BigNumberFloat(1));
        TEST_REQUIRE_EQ(changed.transaction.hash(), snapshot.transaction_hash);
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, root).has_value());
        changed         = snapshot;
        changed.version = static_cast<std::uint32_t>(Consensus::MaximumLightContractVersions + 1);
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, root).has_value());
        changed       = snapshot;
        auto metadata = Json::deserialize<ContractTransactionData>(changed.transaction.meta().value()).value();
        metadata.checkpoint_revision = 0;
        changed.transaction.set_meta(Json::serialize(metadata));
        TEST_REQUIRE(changed.transaction.sign(owner));
        changed.transaction_hash = changed.transaction.hash();
        const auto bad_root      = attach_proof(changed);
        TEST_REQUIRE(!Consensus::verify_contract_snapshot(changed, bad_root).has_value());

        TEST_REQUIRE(manager.commit(std::move(deployment.value()), snapshot.transaction_hash).has_value());
        const auto upgrade = manager.prepare_upgrade(contract.id().to_string(),
                                                     owner.id().to_string(),
                                                     module,
                                                     Contracts::Codec::encode_json("[]").value(),
                                                     40);
        TEST_REQUIRE(upgrade.has_value());
        auto       updated        = snapshot_for(upgrade.value(), owner, TransactionType::ContractUpgrade);
        const auto updated_root   = attach_proof(updated);
        const auto updated_record = Consensus::verify_contract_snapshot(updated, updated_root);
        TEST_REQUIRE(updated_record.has_value());
        TEST_REQUIRE_EQ(updated_record.value().active_version, 2U);
        TEST_REQUIRE(updated_record.value().versions.front().module.empty());
        TEST_REQUIRE(updated_record.value().versions.front().revisions.empty());
        TEST_REQUIRE_EQ(updated_record.value().versions.back().module, module);
    }
    const std::map<std::pair<ActorId, ActorId>, BigNumberFloat> balances {
        { { owner.id(), ActorId() }, BigNumberFloat(10) },
    };
    const auto without_nonce = Consensus::balance_snapshot_root(balances);
    TEST_REQUIRE(Consensus::balance_snapshot_root(balances, { { owner.id(), 1 } }) != without_nonce);
    TEST_REQUIRE(Consensus::balance_snapshot_root(balances, { { owner.id(), 1 } })
                 != Consensus::balance_snapshot_root(balances, { { owner.id(), 2 } }));
}
