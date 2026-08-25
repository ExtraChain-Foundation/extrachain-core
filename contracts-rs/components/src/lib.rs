#![no_std]

extern crate alloc;

use alloc::collections::{BTreeMap, BTreeSet};
use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_sdk::{DagProof, Decoder, DfsProof, Effect, Encoder, Event, InvokeRequest};

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ComponentOutput {
    pub data: Vec<u8>,
    pub events: Vec<Event>,
    pub effects: Vec<Effect>,
}

pub trait Component {
    fn handles(&self, method: &str) -> bool;
    fn call(&mut self, request: &InvokeRequest) -> Result<ComponentOutput, &'static str>;
}

#[macro_export]
macro_rules! dispatch_components {
    ($request:expr, $($component:expr),+ $(,)?) => {{
        let mut result = None;
        $(
            if result.is_none() && $component.handles(&$request.method) {
                result = Some($component.call($request));
            }
        )+
        result
    }};
}

pub trait Hooks {
    fn before_call(&self, _request: &InvokeRequest) -> Result<(), &'static str> {
        Ok(())
    }

    fn after_call(&self, _request: &InvokeRequest) -> Result<(), &'static str> {
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Ownership {
    owner: String,
}

impl Ownership {
    #[must_use]
    pub fn new(owner: impl ToString) -> Self {
        Self {
            owner: owner.to_string(),
        }
    }

    #[must_use]
    pub fn owner(&self) -> &str {
        &self.owner
    }

    pub fn require_owner(&self, caller: &str) -> Result<(), &'static str> {
        if caller == self.owner {
            Ok(())
        } else {
            Err("Only the owner can perform this operation")
        }
    }

    pub fn transfer(
        &mut self,
        caller: &str,
        next_owner: impl ToString,
    ) -> Result<(), &'static str> {
        self.require_owner(caller)?;
        let next_owner = next_owner.to_string();
        if next_owner.is_empty() {
            return Err("The new owner is empty");
        }
        self.owner = next_owner;
        Ok(())
    }
}

impl Component for Ownership {
    fn handles(&self, method: &str) -> bool {
        matches!(method, "owner" | "transfer_owner")
    }

    fn call(&mut self, request: &InvokeRequest) -> Result<ComponentOutput, &'static str> {
        match request.method.as_str() {
            "owner" => {
                let mut data = Encoder::new();
                data.string(&self.owner);
                Ok(ComponentOutput {
                    data: data.finish(),
                    ..ComponentOutput::default()
                })
            }
            "transfer_owner" => {
                let mut decoder = Decoder::new(&request.arguments);
                let next_owner = decoder.string().map_err(|_| "The new owner is invalid")?;
                if !decoder.is_empty() {
                    return Err("The new owner is invalid");
                }
                self.transfer(&request.caller, &next_owner)?;
                let mut event = Encoder::new();
                event.string(&next_owner);
                Ok(ComponentOutput {
                    events: alloc::vec![Event {
                        topic: "owner_transferred".to_string(),
                        data: event.finish(),
                    }],
                    ..ComponentOutput::default()
                })
            }
            _ => Err("The ownership method is not supported"),
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Roles {
    members: BTreeMap<String, BTreeSet<String>>,
}

impl Roles {
    pub fn grant(&mut self, role: impl ToString, actor: impl ToString) -> Result<(), &'static str> {
        let role = role.to_string();
        let actor = actor.to_string();
        if role.is_empty() || actor.is_empty() {
            return Err("The role and actor are required");
        }
        self.members.entry(role).or_default().insert(actor);
        Ok(())
    }

    pub fn revoke(&mut self, role: &str, actor: &str) -> bool {
        self.members
            .get_mut(role)
            .is_some_and(|members| members.remove(actor))
    }

    #[must_use]
    pub fn has(&self, role: &str, actor: &str) -> bool {
        self.members
            .get(role)
            .is_some_and(|members| members.contains(actor))
    }

    pub fn require(&self, role: &str, actor: &str) -> Result<(), &'static str> {
        if self.has(role, actor) {
            Ok(())
        } else {
            Err("The caller does not have the required role")
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
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

    pub fn require_active(&self) -> Result<(), &'static str> {
        if self.paused {
            Err("The contract is paused")
        } else {
            Ok(())
        }
    }
}

impl Hooks for Pausable {
    fn before_call(&self, _request: &InvokeRequest) -> Result<(), &'static str> {
        self.require_active()
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct UpgradePolicy {
    owner: String,
}

impl UpgradePolicy {
    #[must_use]
    pub fn new(owner: impl ToString) -> Self {
        Self {
            owner: owner.to_string(),
        }
    }

    pub fn authorize(&self, sender: &str) -> Result<(), &'static str> {
        if sender == self.owner {
            Ok(())
        } else {
            Err("Only the owner can upgrade this contract")
        }
    }
}

impl Component for UpgradePolicy {
    fn handles(&self, method: &str) -> bool {
        method == "authorize_upgrade"
    }

    fn call(&mut self, request: &InvokeRequest) -> Result<ComponentOutput, &'static str> {
        self.authorize(&request.caller)?;
        Ok(ComponentOutput::default())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct ReplayGuard {
    consumed: BTreeSet<String>,
}

impl ReplayGuard {
    pub fn consume(&mut self, operation_id: impl ToString) -> Result<(), &'static str> {
        let operation_id = operation_id.to_string();
        if operation_id.is_empty() {
            return Err("The operation ID is empty");
        }
        if !self.consumed.insert(operation_id) {
            return Err("The operation was already processed");
        }
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SectionTimelock {
    unlock_section: u64,
}

impl SectionTimelock {
    #[must_use]
    pub fn new(unlock_section: u64) -> Self {
        Self { unlock_section }
    }

    pub fn require_unlocked(&self, section: u64) -> Result<(), &'static str> {
        if section >= self.unlock_section {
            Ok(())
        } else {
            Err("The operation is timelocked")
        }
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct FungibleLedger {
    balances: BTreeMap<String, u64>,
    total_supply: u64,
}

impl FungibleLedger {
    #[must_use]
    pub fn balance_of(&self, actor: &str) -> u64 {
        self.balances.get(actor).copied().unwrap_or_default()
    }

    #[must_use]
    pub fn total_supply(&self) -> u64 {
        self.total_supply
    }

    pub fn mint(&mut self, actor: impl ToString, amount: u64) -> Result<(), &'static str> {
        let actor = actor.to_string();
        if actor.is_empty() || amount == 0 {
            return Err("The receiver and a positive amount are required");
        }
        let balance = self.balance_of(&actor);
        self.balances.insert(
            actor,
            balance
                .checked_add(amount)
                .ok_or("The balance is too large")?,
        );
        self.total_supply = self
            .total_supply
            .checked_add(amount)
            .ok_or("The supply is too large")?;
        Ok(())
    }

    pub fn burn(&mut self, actor: &str, amount: u64) -> Result<(), &'static str> {
        if actor.is_empty() || amount == 0 {
            return Err("The owner and a positive amount are required");
        }
        let balance = self.balance_of(actor);
        if balance < amount {
            return Err("The balance is too low");
        }
        self.balances.insert(actor.to_string(), balance - amount);
        self.total_supply -= amount;
        Ok(())
    }

    pub fn transfer(
        &mut self,
        from: &str,
        to: impl ToString,
        amount: u64,
    ) -> Result<(), &'static str> {
        let to = to.to_string();
        if from.is_empty() || to.is_empty() || amount == 0 {
            return Err("The sender, receiver, and a positive amount are required");
        }
        if to == from {
            return Err("The receiver is the sender");
        }
        let source_balance = self.balance_of(from);
        if source_balance < amount {
            return Err("The balance is too low");
        }
        let balance = self.balance_of(&to);
        let target_balance = balance
            .checked_add(amount)
            .ok_or("The balance is too large")?;
        self.balances
            .insert(from.to_string(), source_balance - amount);
        self.balances.insert(to, target_balance);
        Ok(())
    }
}

#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct NftLedger {
    owners: BTreeMap<u64, String>,
}

impl NftLedger {
    pub fn mint(&mut self, token_id: u64, owner: impl ToString) -> Result<(), &'static str> {
        if self.owners.contains_key(&token_id) {
            return Err("The token already exists");
        }
        let owner = owner.to_string();
        if owner.is_empty() {
            return Err("The token owner is empty");
        }
        self.owners.insert(token_id, owner);
        Ok(())
    }

    #[must_use]
    pub fn owner_of(&self, token_id: u64) -> Option<&str> {
        self.owners.get(&token_id).map(String::as_str)
    }

    pub fn transfer(
        &mut self,
        caller: &str,
        token_id: u64,
        to: impl ToString,
    ) -> Result<(), &'static str> {
        if self.owner_of(token_id) != Some(caller) {
            return Err("The caller does not own the token");
        }
        let to = to.to_string();
        if to.is_empty() {
            return Err("The receiver is empty");
        }
        self.owners.insert(token_id, to);
        Ok(())
    }

    pub fn burn(&mut self, caller: &str, token_id: u64) -> Result<(), &'static str> {
        if self.owner_of(token_id) != Some(caller) {
            return Err("The caller does not own the token");
        }
        self.owners.remove(&token_id);
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Escrow {
    pub payer: String,
    pub payee: String,
    pub amount: u64,
    pub released: bool,
}

impl Escrow {
    pub fn new(
        payer: impl ToString,
        payee: impl ToString,
        amount: u64,
    ) -> Result<Self, &'static str> {
        let payer = payer.to_string();
        let payee = payee.to_string();
        if payer.is_empty() || payee.is_empty() || payer == payee || amount == 0 {
            return Err("The escrow participants or amount are invalid");
        }
        Ok(Self {
            payer,
            payee,
            amount,
            released: false,
        })
    }

    pub fn release(&mut self, caller: &str) -> Result<(), &'static str> {
        if caller != self.payer {
            return Err("Only the payer can release the escrow");
        }
        if self.released {
            return Err("The escrow was already released");
        }
        self.released = true;
        Ok(())
    }
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Multisig {
    signers: BTreeSet<String>,
    threshold: usize,
}

impl Multisig {
    pub fn new(
        signers: impl IntoIterator<Item = String>,
        threshold: usize,
    ) -> Result<Self, &'static str> {
        let signers: BTreeSet<_> = signers.into_iter().collect();
        if threshold == 0 || threshold > signers.len() || signers.iter().any(String::is_empty) {
            return Err("The multisig threshold is invalid");
        }
        Ok(Self { signers, threshold })
    }

    pub fn require(&self, approvals: &[String]) -> Result<(), &'static str> {
        let approved = approvals
            .iter()
            .filter(|actor| self.signers.contains(actor.as_str()))
            .collect::<BTreeSet<_>>()
            .len();
        if approved >= self.threshold {
            Ok(())
        } else {
            Err("The operation does not have enough approvals")
        }
    }
}

pub mod dag {
    use super::*;

    pub fn require_transaction<'a>(
        proofs: &'a [DagProof],
        transaction_hash: &str,
        minimum_confirmations: u64,
    ) -> Result<&'a DagProof, &'static str> {
        proofs
            .iter()
            .find(|proof| {
                proof.transaction_hash == transaction_hash
                    && proof.confirmations >= minimum_confirmations
            })
            .ok_or("A confirmed DAG transaction is required")
    }
}

pub mod dfs {
    use super::*;

    pub fn require_file<'a>(
        proofs: &'a [DfsProof],
        file_id: &str,
    ) -> Result<&'a DfsProof, &'static str> {
        proofs
            .iter()
            .find(|proof| proof.file_id == file_id)
            .ok_or("A verified DFS file is required")
    }
}

#[must_use]
pub fn call_contract(
    contract_id: impl ToString,
    method: impl ToString,
    arguments: Vec<u8>,
) -> Effect {
    Effect::ContractCall {
        contract_id: contract_id.to_string(),
        method: method.to_string(),
        arguments,
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ownership_and_pause_apply_clear_rules() {
        let mut ownership = Ownership::new("alice");
        assert!(ownership.require_owner("alice").is_ok());
        assert!(ownership.transfer("bob", "carol").is_err());
        ownership.transfer("alice", "carol").unwrap();
        assert_eq!(ownership.owner(), "carol");

        let mut pause = Pausable::default();
        pause.pause();
        assert!(pause.require_active().is_err());
        pause.resume();
        assert!(pause.require_active().is_ok());
    }

    #[test]
    fn ledgers_do_not_duplicate_or_overdraw_assets() {
        let mut fungible = FungibleLedger::default();
        assert!(fungible.mint("", 10).is_err());
        assert!(fungible.mint("alice", 0).is_err());
        fungible.mint("alice", 10).unwrap();
        fungible.transfer("alice", "bob", 4).unwrap();
        assert_eq!(fungible.total_supply(), 10);
        assert_eq!(fungible.balance_of("alice"), 6);
        assert_eq!(fungible.balance_of("bob"), 4);
        assert!(fungible.transfer("alice", "bob", 7).is_err());
        assert!(fungible.transfer("alice", "alice", 1).is_err());
        assert!(fungible.transfer("alice", "bob", 0).is_err());

        let mut nft = NftLedger::default();
        nft.mint(1, "alice").unwrap();
        assert!(nft.mint(1, "bob").is_err());
        nft.transfer("alice", 1, "bob").unwrap();
        assert_eq!(nft.owner_of(1), Some("bob"));
    }

    #[test]
    fn proofs_and_effects_use_verified_inputs() {
        let proofs = [DagProof {
            transaction_hash: "tx".to_string(),
            section: 8,
            confirmations: 2,
        }];
        assert!(dag::require_transaction(&proofs, "tx", 2).is_ok());
        assert!(dag::require_transaction(&proofs, "tx", 3).is_err());
        let dfs_proofs = [DfsProof {
            file_id: "file".to_string(),
            owner_id: "alice".to_string(),
            content_hash: "hash".to_string(),
        }];
        assert!(dfs::require_file(&dfs_proofs, "file").is_ok());
    }

    #[test]
    fn component_dispatch_can_be_extended_by_a_contract() {
        let request = InvokeRequest {
            sender: "alice".to_string(),
            caller: "alice".to_string(),
            contract_id: "contract".to_string(),
            method: "owner".to_string(),
            arguments: Vec::new(),
            state: Vec::new(),
            block: 1,
            depth: 0,
            verified: Default::default(),
        };
        let mut ownership = Ownership::new("alice");
        let result = dispatch_components!(&request, ownership);
        assert!(matches!(result, Some(Ok(_))));

        let mut arguments = Encoder::new();
        arguments.string("bob");
        let transfer = InvokeRequest {
            caller: "parent-contract".to_string(),
            method: "transfer_owner".to_string(),
            arguments: arguments.finish(),
            ..request
        };
        let result = dispatch_components!(&transfer, ownership);
        assert!(matches!(result, Some(Err(_))));
    }
}
