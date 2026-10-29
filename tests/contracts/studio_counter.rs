#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use extrachain_contract_sdk::{Context, ContractError, ContractResult, contract};

const MIGRATED_LIMIT: u64 = 0;

#[contract(version = 1, owner = "owner", upgrade = "owner")]
#[derive(Default)]
pub struct StudioCounter {
    owner: String,
    value: u64,
    limit: u64,
    touches: u64,
}

#[contract]
impl StudioCounter {
    #[init]
    fn init(&mut self, context: &Context<'_>) -> ContractResult<()> {
        self.owner = context.caller().to_string();
        Ok(())
    }

    #[call(access = "owner")]
    fn add(&mut self, _context: &Context<'_>, amount: u64) -> ContractResult<u64> {
        let next = self
            .value
            .checked_add(amount)
            .ok_or(ContractError::new("Counter overflow"))?;
        if amount == 0 || (self.limit > 0 && next >= self.limit) {
            return Err(ContractError::new("Counter limit reached"));
        }
        self.value = next;
        Ok(next)
    }

    #[query]
    fn get(&self) -> ContractResult<u64> {
        Ok(self.value)
    }

    #[call(access = "owner")]
    fn touch(&mut self) -> ContractResult<u64> {
        self.touches += 1;
        Ok(self.value)
    }

    #[migrate(from = 1, to = 2, access = "owner")]
    fn migrate(&mut self) -> ContractResult<()> {
        self.limit = MIGRATED_LIMIT;
        Ok(())
    }
}
