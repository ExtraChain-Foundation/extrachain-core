extern crate alloc;

use extrachain_contract_sdk::{Context, ContractResult, ContractState, contract};

#[derive(Default, ContractState)]
#[state(version = 1)]
struct Counter {
    value: u64,
}

#[contract]
impl Counter {
    #[call]
    #[owner_only]
    fn set(&mut self, _ctx: &Context<'_>, value: u64) -> ContractResult<()> {
        self.value = value;
        Ok(())
    }
}

fn main() {}
