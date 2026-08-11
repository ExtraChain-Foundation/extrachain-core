extern crate alloc;

use alloc::string::{String, ToString};
use extrachain_contract_sdk::{Context, ContractResult, contract};

#[contract(version = 1, owner = "owner", upgrade = "owner")]
#[derive(Default)]
struct Counter {
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

    #[call(access = "owner")]
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
