extern crate alloc;

use alloc::string::{String, ToString};
use extrachain_contract_sdk::{Context, ContractResult, ContractState, contract};

#[derive(Default, ContractState)]
#[state(version = 1)]
struct Counter {
    #[owner]
    owner: String,
    value: u64,
}

#[contract]
impl Counter {
    #[init]
    fn init(&mut self, ctx: &Context<'_>) -> ContractResult<()> {
        self.owner = ctx.caller().to_string();
        Ok(())
    }

    #[call]
    #[owner_only]
    fn add(&mut self, _ctx: &Context<'_>, amount: u64) -> ContractResult<u64> {
        self.value += amount;
        Ok(self.value)
    }

    #[query]
    fn get(&self) -> ContractResult<u64> {
        Ok(self.value)
    }
}

fn main() {}
