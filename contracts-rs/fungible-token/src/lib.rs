#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractResult, ContractState, NonZeroAmount, StateMap,
    contract, require,
};

const MAX_STATE_ENTRIES: usize = 16_384;

#[derive(Clone, Debug, Default, PartialEq, Eq, ContractState)]
#[state(version = 1)]
pub struct FungibleToken {
    name: String,
    symbol: String,
    decimals: u8,
    #[owner]
    owner: String,
    mint_enabled: bool,
    total_supply: u128,
    balances: StateMap<String, u128, MAX_STATE_ENTRIES>,
    allowances: StateMap<(String, String), u128, MAX_STATE_ENTRIES>,
    freeze_last_enabled: bool,
    locked: StateMap<String, u128, MAX_STATE_ENTRIES>,
}

#[contract]
impl FungibleToken {
    fn balance(&self, owner: &str) -> u128 {
        self.balances.get(&owner.to_string()).copied().unwrap_or(0)
    }

    fn set_balance(&mut self, owner: &str, amount: u128) -> ContractResult<()> {
        let owner = owner.to_string();
        if amount == 0 {
            self.balances.remove(&owner);
        } else {
            self.balances.insert(owner, amount)?;
        }
        Ok(())
    }

    fn allowance_value(&self, owner: &str, spender: &str) -> u128 {
        self.allowances
            .get(&(owner.to_string(), spender.to_string()))
            .copied()
            .unwrap_or(0)
    }

    fn set_allowance(&mut self, owner: &str, spender: &str, amount: u128) -> ContractResult<()> {
        let key = (owner.to_string(), spender.to_string());
        if amount == 0 {
            self.allowances.remove(&key);
        } else {
            self.allowances.insert(key, amount)?;
        }
        Ok(())
    }

    fn locked_balance(&self, owner: &str) -> u128 {
        self.locked.get(&owner.to_string()).copied().unwrap_or(0)
    }

    fn set_locked_balance(&mut self, owner: &str, amount: u128) -> ContractResult<()> {
        let owner = owner.to_string();
        if amount == 0 {
            self.locked.remove(&owner);
        } else {
            self.locked.insert(owner, amount)?;
        }
        Ok(())
    }

    fn spendable_balance(&self, owner: &str) -> u128 {
        self.balance(owner)
            .saturating_sub(self.locked_balance(owner))
    }

    fn move_balance(&mut self, from: &str, to: &str, amount: u128) -> ContractResult<bool> {
        require!(from != to, "Self transfer is not allowed");
        require!(
            self.spendable_balance(from) >= amount,
            "Balance is too small"
        );
        let receiver_balance = self
            .balance(to)
            .checked_add(amount)
            .ok_or("Balance overflow")?;
        self.set_balance(from, self.balance(from) - amount)?;
        self.set_balance(to, receiver_balance)?;

        let unit = 10_u128
            .checked_pow(u32::from(self.decimals))
            .ok_or("Invalid decimals")?;
        let freeze = self.freeze_last_enabled
            && self.balance(from) == unit
            && self.locked_balance(from) == 0;
        if freeze {
            self.set_locked_balance(from, unit)?;
        }
        Ok(freeze)
    }

    fn emit_transfer(ctx: &mut Context<'_>, from: &str, to: &str, amount: u128) {
        ctx.token_event(
            "transfer",
            &alloc::vec![(from.to_string(), amount), (to.to_string(), amount)],
        );
    }

    #[init]
    fn init(
        &mut self,
        ctx: &mut Context<'_>,
        name: BoundedString<64>,
        symbol: BoundedString<12>,
        decimals: u8,
        supply: u128,
        initial_balances: Vec<(ActorId, u128)>,
    ) -> ContractResult<u128> {
        require!(decimals <= 18, "Token decimals are out of range");
        self.name = name.into_string();
        self.symbol = symbol.into_string();
        self.decimals = decimals;
        self.owner = ctx.caller().to_string();
        self.mint_enabled = true;
        self.total_supply = supply;

        if initial_balances.is_empty() {
            self.set_balance(ctx.caller(), supply)?;
            ctx.token_event("mint", &alloc::vec![(ctx.caller().to_string(), supply)]);
            return Ok(supply);
        }

        let mut migrated_supply = 0_u128;
        for (actor, amount) in initial_balances {
            require!(amount != 0, "Migration balance is zero");
            let actor = actor.into_string();
            require!(self.balance(&actor) == 0, "Migration actor is duplicated");
            migrated_supply = migrated_supply
                .checked_add(amount)
                .ok_or("Migration supply overflow")?;
            self.set_balance(&actor, amount)?;
        }
        require!(
            migrated_supply == supply,
            "Migration supply does not match balances"
        );
        ctx.emit("migrated", &());
        Ok(supply)
    }

    #[call]
    fn transfer(
        &mut self,
        ctx: &mut Context<'_>,
        to: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<()> {
        let to = to.into_string();
        let amount = amount.get();
        let caller = ctx.caller().to_string();
        let freeze = self.move_balance(&caller, &to, amount)?;
        Self::emit_transfer(ctx, &caller, &to, amount);
        if freeze {
            ctx.token_event(
                "lock",
                &alloc::vec![(caller.clone(), self.locked_balance(&caller))],
            );
        }
        Ok(())
    }

    #[call]
    fn approve(
        &mut self,
        ctx: &mut Context<'_>,
        spender: ActorId,
        amount: u128,
    ) -> ContractResult<()> {
        let spender = spender.into_string();
        require!(spender != ctx.caller(), "Self approval is not allowed");
        self.set_allowance(ctx.caller(), &spender, amount)?;
        ctx.emit("approval", &alloc::vec![(spender, amount)]);
        Ok(())
    }

    #[call]
    fn transfer_from(
        &mut self,
        ctx: &mut Context<'_>,
        owner: ActorId,
        to: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<()> {
        let owner = owner.into_string();
        let to = to.into_string();
        let amount = amount.get();
        let allowance = self.allowance_value(&owner, ctx.caller());
        require!(allowance >= amount, "Allowance is too small");
        let freeze = self.move_balance(&owner, &to, amount)?;
        self.set_allowance(&owner, ctx.caller(), allowance - amount)?;
        Self::emit_transfer(ctx, &owner, &to, amount);
        if freeze {
            ctx.token_event(
                "lock",
                &alloc::vec![(owner.clone(), self.locked_balance(&owner))],
            );
        }
        Ok(())
    }

    #[call]
    #[owner_only]
    fn mint(
        &mut self,
        ctx: &mut Context<'_>,
        to: ActorId,
        amount: NonZeroAmount,
    ) -> ContractResult<u128> {
        require!(self.mint_enabled, "Mint is disabled");
        let to = to.into_string();
        let amount = amount.get();
        self.total_supply = self
            .total_supply
            .checked_add(amount)
            .ok_or("Supply overflow")?;
        let next = self
            .balance(&to)
            .checked_add(amount)
            .ok_or("Balance overflow")?;
        self.set_balance(&to, next)?;
        ctx.token_event("mint", &alloc::vec![(to, amount)]);
        Ok(self.total_supply)
    }

    #[call]
    #[owner_only]
    fn revoke_mint(&mut self, ctx: &mut Context<'_>) -> ContractResult<()> {
        require!(self.mint_enabled, "Mint control is not available");
        self.mint_enabled = false;
        ctx.emit("mint_revoked", &());
        Ok(())
    }

    #[call]
    fn burn(&mut self, ctx: &mut Context<'_>, amount: NonZeroAmount) -> ContractResult<u128> {
        let amount = amount.get();
        require!(
            self.spendable_balance(ctx.caller()) >= amount,
            "Balance is too small"
        );
        self.set_balance(ctx.caller(), self.balance(ctx.caller()) - amount)?;
        self.total_supply -= amount;
        ctx.token_event("burn", &alloc::vec![(ctx.caller().to_string(), amount)]);
        Ok(self.total_supply)
    }

    #[query]
    fn balance_of(&self, owner: ActorId) -> ContractResult<u128> {
        Ok(self.balance(owner.as_str()))
    }

    #[query]
    fn allowance(&self, owner: ActorId, spender: ActorId) -> ContractResult<u128> {
        Ok(self.allowance_value(owner.as_str(), spender.as_str()))
    }

    #[authorize_upgrade]
    #[owner_only]
    fn authorize_upgrade(
        &self,
        _ctx: &Context<'_>,
        _module_hash: BoundedString<64>,
    ) -> ContractResult<()> {
        Ok(())
    }

    #[migrate]
    #[owner_only]
    fn migrate(&mut self, _ctx: &Context<'_>) -> ContractResult<()> {
        self.freeze_last_enabled = true;
        Ok(())
    }
}
