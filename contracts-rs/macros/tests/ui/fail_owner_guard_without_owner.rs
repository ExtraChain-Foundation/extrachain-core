extern crate alloc;

use extrachain_contract_sdk::contract;

#[contract(version = 1, upgrade = "owner")]
#[derive(Default)]
struct Counter {
    value: u64,
}

fn main() {}
