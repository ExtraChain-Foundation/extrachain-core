#![no_std]

extern crate alloc;

use alloc::vec::Vec;
use extrachain_contract_sdk::{Contract, InvokeRequest, InvokeResponse, export_contract};

pub struct ContractTemplate;

impl Contract for ContractTemplate {
    fn invoke(request: InvokeRequest) -> InvokeResponse {
        match request.method.as_str() {
            "init" | "authorize_upgrade" | "migrate" => {
                InvokeResponse::success(request.state, Vec::new(), Vec::new())
            }
            _ => InvokeResponse::failure(request.state, "Unknown contract method"),
        }
    }
}

export_contract!(ContractTemplate);
