#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use extrachain_contract_sdk::{Context, ContractResult, contract};

#[contract(version = 1, owner = "owner", upgrade = "owner")]
#[derive(Default)]
pub struct ContractTemplate {
    owner: String,
}

#[contract]
impl ContractTemplate {
    #[init]
    fn initialize(&mut self, context: &Context<'_>) -> ContractResult<()> {
        self.owner = context.caller().to_string();
        Ok(())
    }
}
