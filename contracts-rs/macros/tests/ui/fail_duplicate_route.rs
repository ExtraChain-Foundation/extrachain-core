extern crate alloc;

use extrachain_contract_sdk::{ContractResult, ContractState, contract};

#[derive(Default, ContractState)]
#[state(version = 1)]
struct Counter {
    value: u64,
}

#[contract]
impl Counter {
    #[call]
    #[query]
    fn get(&mut self) -> ContractResult<u64> {
        Ok(self.value)
    }
}

fn main() {}
