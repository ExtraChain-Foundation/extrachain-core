#![no_std]

extern crate alloc;

use extrachain_contract_sdk::nft_collection;

#[nft_collection(name = "ExtraChain Collection", symbol = "EXNFT")]
pub struct Collection;
