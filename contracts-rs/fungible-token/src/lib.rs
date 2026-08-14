#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};

use extrachain_contract_components::{FreezeLastUnit, FungibleLedger};
use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractCodec, ContractResult, NonZeroAmount,
    OperationReceipt, contract, require,
};

#[derive(Clone, Debug, Default, PartialEq, Eq, ContractCodec)]
struct LegacyMigration {
    ready: bool,
    source: String,
}

#[contract(version = 3, owner = "owner", upgrade = "owner")]
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct FungibleToken {
    owner: String,
    name: String,
    symbol: String,
    decimals: u8,
    mint_enabled: bool,
    ledger: FungibleLedger,
    transfers: FreezeLastUnit,
    migration: LegacyMigration,
}

#[contract]
impl FungibleToken {
    #[init]
    fn init(
        &mut self,
        context: &mut Context<'_>,
        name: BoundedString<64>,
        symbol: BoundedString<12>,
        decimals: u8,
        supply: u128,
        initial_balances: alloc::vec::Vec<(ActorId, NonZeroAmount)>,
    ) -> ContractResult<OperationReceipt> {
        require!(decimals <= 18, "Token decimals are out of range");
        self.owner = context.caller().to_string();
        self.name = name.into_string();
        self.symbol = symbol.into_string();
        self.decimals = decimals;
        self.mint_enabled = supply != 0;
        self.migration.ready = supply == 0;
        self.transfers = FreezeLastUnit::new(decimals)?;
        self.transfers.disable();

        if supply == 0 {
            require!(
                initial_balances.is_empty(),
                "Migration target must be empty"
            );
            context.emit("migration_ready", &());
            return Ok(OperationReceipt::new("migration_ready", &self.owner, 0));
        }

        let supply = NonZeroAmount::new(supply)?;

        if initial_balances.is_empty() {
            let owner = ActorId::new(self.owner.clone())?;
            return self.ledger.mint(context, owner, supply);
        }

        let mut distributed = 0_u128;
        for (actor, amount) in initial_balances {
            distributed = distributed
                .checked_add(amount.get())
                .ok_or("Migration supply overflow")?;
            self.ledger.restore_balance(actor, amount)?;
        }
        require!(
            distributed == supply.get(),
            "Migration supply does not match balances"
        );
        context.emit("migrated", &distributed);
        Ok(OperationReceipt::new(
            "migrated",
            self.owner.as_str(),
            distributed,
        ))
    }

    #[call(access = "owner")]
    fn import_legacy(
        &mut self,
        context: &mut Context<'_>,
        legacy_source: ActorId,
        supply: NonZeroAmount,
        balances: alloc::vec::Vec<(ActorId, NonZeroAmount)>,
    ) -> ContractResult<OperationReceipt> {
        require!(
            self.migration.ready
                && self.migration.source.is_empty()
                && self.ledger.total_supply() == 0,
            "Legacy import is not allowed"
        );
        require!(!balances.is_empty(), "Legacy balances are empty");
        let mut distributed = 0_u128;
        for (actor, amount) in balances {
            distributed = distributed
                .checked_add(amount.get())
                .ok_or("Migration supply overflow")?;
            self.ledger.restore_balance(actor, amount)?;
        }
        require!(
            distributed == supply.get(),
            "Migration supply does not match balances"
        );
        self.migration.ready = false;
        self.migration.source = legacy_source.as_str().to_string();
        self.mint_enabled = false;
        context.emit("migrated", &self.migration.source);
        Ok(OperationReceipt::new(
            "migrated",
            self.migration.source.as_str(),
            distributed,
        ))
    }

    #[query]
    fn migration_ready(&self) -> ContractResult<bool> {
        Ok(self.migration.ready)
    }

    #[query]
    fn legacy_source(&self) -> ContractResult<String> {
        Ok(self.migration.source.clone())
    }

    #[query]
    fn token_name(&self) -> ContractResult<String> {
        Ok(self.name.clone())
    }

    #[query]
    fn token_symbol(&self) -> ContractResult<String> {
        Ok(self.symbol.clone())
    }

    #[query]
    fn token_decimals(&self) -> ContractResult<u8> {
        Ok(self.decimals)
    }

    #[call]
    fn transfer(
        &mut self,
        context: &mut Context<'_>,
        receiver: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<OperationReceipt> {
        require!(!self.migration.ready, "Migration target is not active");
        let sender = ActorId::new(context.caller().to_string())?;
        self.ledger
            .transfer(context, &sender, receiver, amount, &self.transfers)
    }

    #[call]
    fn approve(
        &mut self,
        context: &mut Context<'_>,
        spender: ActorId,
        amount: u128,
    ) -> ContractResult<OperationReceipt> {
        require!(!self.migration.ready, "Migration target is not active");
        let owner = ActorId::new(context.caller().to_string())?;
        self.ledger.approve(context, &owner, spender, amount)
    }

    #[call]
    fn transfer_from(
        &mut self,
        context: &mut Context<'_>,
        owner: ActorId,
        receiver: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<OperationReceipt> {
        require!(!self.migration.ready, "Migration target is not active");
        let spender = ActorId::new(context.caller().to_string())?;
        self.ledger
            .transfer_from(context, &spender, &owner, receiver, amount, &self.transfers)
    }

    #[call(access = "owner")]
    fn mint(
        &mut self,
        context: &mut Context<'_>,
        receiver: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<OperationReceipt> {
        require!(!self.migration.ready, "Migration target is not active");
        require!(self.mint_enabled, "Mint is disabled");
        self.ledger.mint(context, receiver, amount)
    }

    #[call(access = "owner")]
    fn revoke_mint(&mut self, context: &mut Context<'_>) -> ContractResult<()> {
        require!(!self.migration.ready, "Migration target is not active");
        require!(self.mint_enabled, "Mint control is not available");
        self.mint_enabled = false;
        context.emit("mint_revoked", &());
        Ok(())
    }

    #[call]
    fn burn(
        &mut self,
        context: &mut Context<'_>,
        amount: NonZeroAmount,
    ) -> ContractResult<OperationReceipt> {
        require!(!self.migration.ready, "Migration target is not active");
        let owner = ActorId::new(context.caller().to_string())?;
        self.ledger.burn(context, &owner, amount)
    }

    #[query]
    fn balance_of(&self, owner: ActorId) -> ContractResult<u128> {
        Ok(self.ledger.balance_of(&owner))
    }

    #[query]
    fn allowance(&self, owner: ActorId, spender: ActorId) -> ContractResult<u128> {
        Ok(self.ledger.allowance(&owner, &spender))
    }

    #[query]
    fn locked_balance_of(&self, owner: ActorId) -> ContractResult<u128> {
        Ok(self.ledger.locked_balance(&owner))
    }

    #[migrate(from = 1, to = 2, access = "owner")]
    fn enable_freeze_last_unit(&mut self, _context: &mut Context<'_>) -> ContractResult<()> {
        self.transfers = FreezeLastUnit::new(self.decimals)?;
        Ok(())
    }
}
