#![no_std]

extern crate alloc;

use extrachain_contract_sdk::fungible_token;

#[fungible_token(name = "Token X", symbol = "X", decimals = 0, freeze_last_unit = true)]
pub struct TokenX;
