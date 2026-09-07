#![no_std]
extern crate alloc;
use alloc::{string::String, vec::Vec};
use extrachain_contract_sdk::{
    Context, Contract, ContractValue, Decoder, InvokeRequest, InvokeResponse, encode_result,
    export_contract,
};

pub struct DepthFixture;
impl Contract for DepthFixture {
    fn invoke(request: InvokeRequest) -> InvokeResponse {
        if request.method == "init" {
            return InvokeResponse::success(encode_result(&0_u64), Vec::new(), Vec::new());
        }
        if request.method != "walk" {
            return InvokeResponse::failure(request.state, "Unknown method");
        }
        let mut decoder = Decoder::new(&request.arguments);
        let remaining = match Vec::<String>::decode_value(&mut decoder) {
            Ok(value) => value,
            Err(_) => return InvokeResponse::failure(request.state, "Invalid path"),
        };
        let mut context = Context::new(&request);
        if let Some((next, tail)) = remaining.split_first() {
            context.call(next, "walk", &tail.to_vec());
        }
        let state = encode_result(&(context.depth() + 1));
        let (events, effects) = context.finish();
        InvokeResponse::success(state, Vec::new(), events).with_effects(effects)
    }
}
export_contract!(DepthFixture);
