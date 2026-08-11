extern crate alloc;

use extrachain_contract_sdk::{ContractResult, contract};

#[contract(version = 1)]
#[derive(Default)]
struct Counter {
    value: u64,
}

#[contract]
impl Counter {
    #[query]
    fn get(&mut self) -> ContractResult<u64> {
        Ok(self.value)
    }
}

fn main() {}
