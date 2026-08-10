#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractCodec, ContractError, ContractResult, DagProof,
    DfsProof, NonZeroAmount, StateMap, StateSet, is_content_hash, is_dfs_logical_key,
};

pub const MAX_ROLES: usize = 64;
pub const MAX_ROLE_MEMBERS: usize = 256;
pub const MAX_REPLAY_IDS: usize = 4_096;
pub const MAX_LEDGER_ENTRIES: usize = 16_384;
pub const MAX_MULTISIG_SIGNERS: usize = 256;
pub const MAX_DFS_BINDINGS: usize = 4_096;

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct Ownership {
    owner: Option<ActorId>,
}

impl Ownership {
    #[must_use]
    pub fn new(owner: ActorId) -> Self {
        Self { owner: Some(owner) }
    }

    #[must_use]
    pub fn owner(&self) -> Option<&ActorId> {
        self.owner.as_ref()
    }

    pub fn initialize(&mut self, owner: ActorId) -> ContractResult<()> {
        if self.owner.is_some() {
            return Err(ContractError::new("Ownership is already initialized"));
        }
        self.owner = Some(owner);
        Ok(())
    }

    pub fn require_owner(&self, caller: &str) -> ContractResult<()> {
        if self.owner().is_some_and(|owner| owner.as_str() == caller) {
            Ok(())
        } else {
            Err(ContractError::new(
                "Only the owner can perform this operation",
            ))
        }
    }

    pub fn transfer(&mut self, caller: &str, next_owner: ActorId) -> ContractResult<()> {
        self.require_owner(caller)?;
        self.owner = Some(next_owner);
        Ok(())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct Roles {
    members: StateMap<String, StateSet<ActorId, MAX_ROLE_MEMBERS>, MAX_ROLES>,
}

impl Roles {
    pub fn grant(&mut self, role: BoundedString<64>, actor: ActorId) -> ContractResult<bool> {
        let role = role.into_string();
        if let Some(members) = self.members.get_mut(&role) {
            return members.insert(actor);
        }
        let mut members = StateSet::default();
        members.insert(actor)?;
        self.members.insert(role, members)?;
        Ok(true)
    }

    pub fn revoke(&mut self, role: &str, actor: &ActorId) -> bool {
        let removed = self
            .members
            .get_mut(&role.to_string())
            .is_some_and(|members| members.remove(actor));
        if removed
            && self
                .members
                .get(&role.to_string())
                .is_some_and(StateSet::is_empty)
        {
            self.members.remove(&role.to_string());
        }
        removed
    }

    #[must_use]
    pub fn has(&self, role: &str, actor: &ActorId) -> bool {
        self.members
            .get(&role.to_string())
            .is_some_and(|members| members.contains(actor))
    }

    pub fn require(&self, role: &str, actor: &ActorId) -> ContractResult<()> {
        if self.has(role, actor) {
            Ok(())
        } else {
            Err(ContractError::new(
                "The caller does not have the required role",
            ))
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct Pausable {
    paused: bool,
}

impl Pausable {
    #[must_use]
    pub fn is_paused(&self) -> bool {
        self.paused
    }

    pub fn pause(&mut self) {
        self.paused = true;
    }

    pub fn resume(&mut self) {
        self.paused = false;
    }

    pub fn require_active(&self) -> ContractResult<()> {
        if self.paused {
            Err(ContractError::new("The contract is paused"))
        } else {
            Ok(())
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct UpgradePolicy {
    ownership: Ownership,
}

impl UpgradePolicy {
    #[must_use]
    pub fn new(owner: ActorId) -> Self {
        Self {
            ownership: Ownership::new(owner),
        }
    }

    pub fn authorize(&self, caller: &str) -> ContractResult<()> {
        self.ownership.require_owner(caller)
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct ReplayGuard {
    consumed: StateSet<String, MAX_REPLAY_IDS>,
}

impl ReplayGuard {
    pub fn consume(&mut self, operation_id: BoundedString<128>) -> ContractResult<()> {
        if self.consumed.insert(operation_id.into_string())? {
            Ok(())
        } else {
            Err(ContractError::new("The operation was already processed"))
        }
    }

    #[must_use]
    pub fn contains(&self, operation_id: &str) -> bool {
        self.consumed.contains(&operation_id.to_string())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct SectionTimelock {
    unlock_section: u64,
}

impl SectionTimelock {
    #[must_use]
    pub fn new(unlock_section: u64) -> Self {
        Self { unlock_section }
    }

    pub fn require_unlocked(&self, section: u64) -> ContractResult<()> {
        if section >= self.unlock_section {
            Ok(())
        } else {
            Err(ContractError::new("The operation is timelocked"))
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct FungibleLedger {
    balances: StateMap<ActorId, u128, MAX_LEDGER_ENTRIES>,
    total_supply: u128,
}

impl FungibleLedger {
    #[must_use]
    pub fn balance_of(&self, actor: &ActorId) -> u128 {
        self.balances.get(actor).copied().unwrap_or_default()
    }

    #[must_use]
    pub fn total_supply(&self) -> u128 {
        self.total_supply
    }

    pub fn mint(&mut self, actor: ActorId, amount: NonZeroAmount) -> ContractResult<u128> {
        let next_balance = self
            .balance_of(&actor)
            .checked_add(amount.get())
            .ok_or(ContractError::new("The balance is too large"))?;
        let next_supply = self
            .total_supply
            .checked_add(amount.get())
            .ok_or(ContractError::new("The supply is too large"))?;
        self.balances.insert(actor, next_balance)?;
        self.total_supply = next_supply;
        Ok(next_supply)
    }

    pub fn burn(&mut self, actor: &ActorId, amount: NonZeroAmount) -> ContractResult<u128> {
        let balance = self.balance_of(actor);
        if balance < amount.get() {
            return Err(ContractError::new("The balance is too low"));
        }
        self.balances
            .insert(actor.clone(), balance - amount.get())?;
        self.total_supply -= amount.get();
        Ok(self.total_supply)
    }

    pub fn transfer(
        &mut self,
        from: &ActorId,
        to: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<()> {
        if from == &to {
            return Err(ContractError::new("The receiver is the sender"));
        }
        let source = self.balance_of(from);
        if source < amount.get() {
            return Err(ContractError::new("The balance is too low"));
        }
        let target = self
            .balance_of(&to)
            .checked_add(amount.get())
            .ok_or(ContractError::new("The balance is too large"))?;
        self.balances.insert(from.clone(), source - amount.get())?;
        self.balances.insert(to, target)?;
        Ok(())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct NftLedger {
    owners: StateMap<u128, ActorId, MAX_LEDGER_ENTRIES>,
}

impl NftLedger {
    pub fn mint(&mut self, token_id: u128, owner: ActorId) -> ContractResult<()> {
        if self.owners.contains_key(&token_id) {
            return Err(ContractError::new("The token already exists"));
        }
        self.owners.insert(token_id, owner)?;
        Ok(())
    }

    #[must_use]
    pub fn owner_of(&self, token_id: u128) -> Option<&ActorId> {
        self.owners.get(&token_id)
    }

    pub fn transfer(
        &mut self,
        caller: &ActorId,
        token_id: u128,
        receiver: ActorId,
    ) -> ContractResult<()> {
        if self.owner_of(token_id) != Some(caller) {
            return Err(ContractError::new("The caller does not own the token"));
        }
        self.owners.insert(token_id, receiver)?;
        Ok(())
    }

    pub fn burn(&mut self, caller: &ActorId, token_id: u128) -> ContractResult<()> {
        if self.owner_of(token_id) != Some(caller) {
            return Err(ContractError::new("The caller does not own the token"));
        }
        self.owners.remove(&token_id);
        Ok(())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct Escrow {
    payer: Option<ActorId>,
    payee: Option<ActorId>,
    amount: u128,
    released: bool,
}

impl Escrow {
    pub fn new(payer: ActorId, payee: ActorId, amount: NonZeroAmount) -> ContractResult<Self> {
        if payer == payee {
            return Err(ContractError::new("The escrow participants are equal"));
        }
        Ok(Self {
            payer: Some(payer),
            payee: Some(payee),
            amount: amount.get(),
            released: false,
        })
    }

    #[must_use]
    pub fn payer(&self) -> Option<&ActorId> {
        self.payer.as_ref()
    }

    #[must_use]
    pub fn payee(&self) -> Option<&ActorId> {
        self.payee.as_ref()
    }

    #[must_use]
    pub fn amount(&self) -> u128 {
        self.amount
    }

    #[must_use]
    pub fn is_released(&self) -> bool {
        self.released
    }

    pub fn release(&mut self, caller: &ActorId) -> ContractResult<()> {
        if self.payer() != Some(caller) {
            return Err(ContractError::new("Only the payer can release the escrow"));
        }
        if self.released {
            return Err(ContractError::new("The escrow was already released"));
        }
        self.released = true;
        Ok(())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct Multisig {
    signers: StateSet<ActorId, MAX_MULTISIG_SIGNERS>,
    threshold: u16,
}

impl Multisig {
    pub fn new(signers: Vec<ActorId>, threshold: u16) -> ContractResult<Self> {
        let mut unique = StateSet::default();
        for signer in signers {
            if !unique.insert(signer)? {
                return Err(ContractError::new("A multisig signer is duplicated"));
            }
        }
        if threshold == 0 || usize::from(threshold) > unique.len() {
            return Err(ContractError::new("The multisig threshold is invalid"));
        }
        Ok(Self {
            signers: unique,
            threshold,
        })
    }

    pub fn require(&self, approvals: &[ActorId]) -> ContractResult<()> {
        let mut accepted = StateSet::<ActorId, MAX_MULTISIG_SIGNERS>::default();
        for actor in approvals {
            if self.signers.contains(actor) {
                accepted.insert(actor.clone())?;
            }
        }
        if accepted.len() >= usize::from(self.threshold) {
            Ok(())
        } else {
            Err(ContractError::new(
                "The operation does not have enough approvals",
            ))
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct DfsBinding {
    pub file_id: String,
    pub content_hash: String,
}

#[derive(Debug, Clone, Default, PartialEq, Eq, ContractCodec)]
pub struct DfsBindings {
    bindings: StateMap<String, DfsBinding, MAX_DFS_BINDINGS>,
}

impl DfsBindings {
    #[must_use]
    pub fn get(&self, logical_key: &str) -> Option<&DfsBinding> {
        self.bindings.get(&logical_key.to_string())
    }

    pub fn bind(
        &mut self,
        logical_key: BoundedString<128>,
        file_id: BoundedString<256>,
        content_hash: BoundedString<64>,
        previous_content_hash: Option<BoundedString<64>>,
    ) -> ContractResult<()> {
        let logical_key = logical_key.into_string();
        if !is_dfs_logical_key(&logical_key)
            || file_id.as_str().is_empty()
            || !is_content_hash(content_hash.as_str(), false)
            || previous_content_hash
                .as_ref()
                .is_some_and(|hash| !is_content_hash(hash.as_str(), false))
        {
            return Err(ContractError::new("The ExDFS binding is invalid"));
        }
        let current_hash = self
            .get(&logical_key)
            .map_or("", |binding| binding.content_hash.as_str());
        let previous_content_hash = previous_content_hash
            .as_ref()
            .map_or("", BoundedString::as_str);
        if current_hash != previous_content_hash {
            return Err(ContractError::new("The ExDFS binding state is stale"));
        }
        self.bindings.insert(
            logical_key,
            DfsBinding {
                file_id: file_id.into_string(),
                content_hash: content_hash.into_string(),
            },
        )?;
        Ok(())
    }

    pub fn tombstone(
        &mut self,
        logical_key: &str,
        previous_content_hash: &str,
    ) -> ContractResult<()> {
        if self
            .get(logical_key)
            .is_none_or(|binding| binding.content_hash != previous_content_hash)
        {
            return Err(ContractError::new("The ExDFS binding state is stale"));
        }
        self.bindings.remove(&logical_key.to_string());
        Ok(())
    }
}

pub mod dag {
    use super::*;

    pub fn require_transaction<'a>(
        context: &'a Context<'_>,
        transaction_hash: &str,
        minimum_confirmations: u64,
    ) -> ContractResult<&'a DagProof> {
        context
            .dag_proof(transaction_hash, minimum_confirmations)
            .ok_or(ContractError::new(
                "A confirmed DAG transaction is required",
            ))
    }
}

pub mod dfs {
    use super::*;

    pub fn require_file<'a>(
        context: &'a Context<'_>,
        owner_id: &str,
        file_id: &str,
        content_hash: &str,
    ) -> ContractResult<&'a DfsProof> {
        context
            .dfs_proof(owner_id, file_id, content_hash)
            .ok_or(ContractError::new("A verified ExDFS file is required"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use extrachain_contract_sdk::{ContractValue, Decoder, VerifiedInputs, encode_result};

    fn actor(value: &str) -> ActorId {
        ActorId::new(value.to_string()).unwrap()
    }

    #[test]
    fn typed_components_round_trip_as_contract_values() {
        let mut ownership = Ownership::new(actor("1"));
        ownership.transfer(actor("1").as_str(), actor("2")).unwrap();
        let encoded = encode_result(&ownership);
        let mut decoder = Decoder::new(&encoded);
        let decoded = Ownership::decode_value(&mut decoder).unwrap();
        assert_eq!(decoded.owner(), Some(&actor("2")));
        assert!(decoder.is_empty());
    }

    #[test]
    fn ledgers_reject_duplicates_overdraw_and_overflow() {
        let alice = actor("1");
        let bob = actor("2");
        let mut fungible = FungibleLedger::default();
        fungible
            .mint(alice.clone(), NonZeroAmount::new(10).unwrap())
            .unwrap();
        fungible
            .transfer(&alice, bob.clone(), NonZeroAmount::new(4).unwrap())
            .unwrap();
        assert_eq!(fungible.balance_of(&alice), 6);
        assert_eq!(fungible.balance_of(&bob), 4);
        assert!(
            fungible
                .transfer(&alice, bob.clone(), NonZeroAmount::new(7).unwrap())
                .is_err()
        );

        let mut nft = NftLedger::default();
        nft.mint(1, alice.clone()).unwrap();
        assert!(nft.mint(1, bob.clone()).is_err());
        nft.transfer(&alice, 1, bob.clone()).unwrap();
        assert_eq!(nft.owner_of(1), Some(&bob));
    }

    #[test]
    fn roles_replay_and_multisig_are_bounded_and_duplicate_safe() {
        let alice = actor("1");
        let bob = actor("2");
        let mut roles = Roles::default();
        roles
            .grant(
                BoundedString::new("admin".to_string()).unwrap(),
                alice.clone(),
            )
            .unwrap();
        assert!(roles.has("admin", &alice));
        assert!(roles.revoke("admin", &alice));

        let mut replay = ReplayGuard::default();
        let operation = BoundedString::new("operation-1".to_string()).unwrap();
        replay.consume(operation).unwrap();
        assert!(
            replay
                .consume(BoundedString::new("operation-1".to_string()).unwrap())
                .is_err()
        );

        let multisig = Multisig::new(alloc::vec![alice.clone(), bob.clone()], 2).unwrap();
        assert!(multisig.require(&[alice.clone(), bob]).is_ok());
        assert!(multisig.require(&[alice]).is_err());
    }

    #[test]
    fn dfs_bindings_validate_paths_hashes_and_previous_state() {
        let mut bindings = DfsBindings::default();
        let hash = "a".repeat(64);
        bindings
            .bind(
                BoundedString::new("profile/avatar".to_string()).unwrap(),
                BoundedString::new("file-1".to_string()).unwrap(),
                BoundedString::new(hash.clone()).unwrap(),
                None,
            )
            .unwrap();
        assert_eq!(
            bindings
                .get("profile/avatar")
                .map(|binding| binding.content_hash.as_str()),
            Some(hash.as_str())
        );
        assert!(
            bindings
                .bind(
                    BoundedString::new("../avatar".to_string()).unwrap(),
                    BoundedString::new("file-2".to_string()).unwrap(),
                    BoundedString::new(hash.clone()).unwrap(),
                    None,
                )
                .is_err()
        );
        assert!(
            bindings
                .bind(
                    BoundedString::new("profile/avatar".to_string()).unwrap(),
                    BoundedString::new("file-2".to_string()).unwrap(),
                    BoundedString::new("b".repeat(64)).unwrap(),
                    None,
                )
                .is_err()
        );
    }

    #[test]
    fn proof_helpers_read_only_verified_context() {
        let request = extrachain_contract_sdk::InvokeRequest {
            sender: actor("1").into_string(),
            caller: actor("1").into_string(),
            contract_id: actor("2").into_string(),
            method: "run".to_string(),
            arguments: Vec::new(),
            state: Vec::new(),
            block: 4,
            depth: 0,
            verified: VerifiedInputs {
                dag: alloc::vec![DagProof {
                    transaction_hash: "tx".to_string(),
                    section: 2,
                    confirmations: 2,
                }],
                dfs: alloc::vec![DfsProof {
                    file_id: "file".to_string(),
                    owner_id: actor("1").into_string(),
                    content_hash: "a".repeat(64),
                }],
            },
        };
        let context = Context::new(&request);
        assert!(dag::require_transaction(&context, "tx", 2).is_ok());
        assert!(dag::require_transaction(&context, "tx", 3).is_err());
        assert!(dfs::require_file(&context, actor("1").as_str(), "file", &"a".repeat(64)).is_ok());
    }
}
