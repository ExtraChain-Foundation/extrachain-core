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
    fn add(&mut self, value: f64) -> ContractResult<u64> {
        self.value += value as u64;
        Ok(self.value)
    }
}

fn main() {}
