#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_components::Ownership;
use extrachain_contract_sdk::{
    Contract, Decoder, Effect, Encoder, Event, InvokeRequest, InvokeResponse, export_contract,
};

const MAX_MESSAGE_BYTES: usize = 64 * 1024;
const MAX_ACTIVE_CLAIMS: u32 = 16_384;

#[derive(Clone, Debug, PartialEq, Eq)]
struct Claim {
    id: u64,
    owner: String,
    message: String,
    active: bool,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct ClaimState {
    owner: String,
    next_id: u64,
    claims: Vec<Claim>,
}

impl ClaimState {
    fn decode(source: &[u8]) -> Result<Self, &'static str> {
        if source.is_empty() {
            return Ok(Self {
                owner: String::new(),
                next_id: 1,
                claims: Vec::new(),
            });
        }
        let mut decoder = Decoder::new(source);
        if decoder.array().map_err(|_| "Invalid claim state")? != 3 {
            return Err("Invalid claim state");
        }
        let owner = decoder.string().map_err(|_| "Invalid contract owner")?;
        let next_id = decoder.u64().map_err(|_| "Invalid next token ID")?;
        let count = decoder.array().map_err(|_| "Invalid claims")?;
        if count > MAX_ACTIVE_CLAIMS {
            return Err("Too many active claims");
        }
        let mut claims = Vec::with_capacity(count as usize);
        for _ in 0..count {
            if decoder.array().map_err(|_| "Invalid claim")? != 4 {
                return Err("Invalid claim");
            }
            claims.push(Claim {
                id: decoder.u64().map_err(|_| "Invalid token ID")?,
                owner: decoder.string().map_err(|_| "Invalid claim owner")?,
                message: decoder.string().map_err(|_| "Invalid claim message")?,
                active: decoder.boolean().map_err(|_| "Invalid claim status")?,
            });
        }
        if !decoder.is_empty() {
            return Err("Invalid trailing claim state");
        }
        Ok(Self {
            owner,
            next_id,
            claims,
        })
    }

    fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(3);
        encoder.string(&self.owner);
        encoder.u64(self.next_id);
        encoder.array(self.claims.len() as u32);
        for claim in &self.claims {
            encoder.array(4);
            encoder.u64(claim.id);
            encoder.string(&claim.owner);
            encoder.string(&claim.message);
            encoder.boolean(claim.active);
        }
        encoder.finish()
    }

    fn active_claim_index(&self, id: u64) -> Result<usize, &'static str> {
        self.claims
            .iter()
            .position(|claim| claim.id == id && claim.active)
            .ok_or("Claim token does not exist")
    }

    fn active_claim_mut(&mut self, id: u64) -> Result<&mut Claim, &'static str> {
        let index = self.active_claim_index(id)?;
        self.claims
            .get_mut(index)
            .ok_or("Claim token does not exist")
    }
}

pub struct MessageClaim;

impl MessageClaim {
    fn id_data(id: u64) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.u64(id);
        encoder.finish()
    }

    fn event(topic: &str, id: u64, address: &str) -> Event {
        let mut encoder = Encoder::new();
        encoder.array(2);
        encoder.u64(id);
        encoder.string(address);
        Event {
            topic: topic.to_string(),
            data: encoder.finish(),
        }
    }

    fn handle(
        request: &InvokeRequest,
        state: &mut ClaimState,
    ) -> Result<(Vec<u8>, Vec<Event>), &'static str> {
        match request.method.as_str() {
            "init" => {
                if !state.owner.is_empty()
                    || request.caller.is_empty()
                    || !request.arguments.is_empty()
                {
                    return Err("Claim contract is already initialized");
                }
                state.owner = request.caller.clone();
                Ok((Vec::new(), Vec::new()))
            }
            "store" => {
                let mut decoder = Decoder::new(&request.arguments);
                let message = decoder.string().map_err(|_| "Invalid message")?;
                if message.is_empty()
                    || message.len() > MAX_MESSAGE_BYTES
                    || request.caller.is_empty()
                    || !decoder.is_empty()
                {
                    return Err("Message is not valid");
                }
                let id = state.next_id;
                state.next_id = state.next_id.checked_add(1).ok_or("Token ID overflow")?;
                state.claims.push(Claim {
                    id,
                    owner: request.caller.clone(),
                    message,
                    active: true,
                });
                Ok((
                    Self::id_data(id),
                    alloc::vec![Self::event("mint", id, &request.caller)],
                ))
            }
            "transfer" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid arguments")? != 2 {
                    return Err("Invalid arguments");
                }
                let id = decoder.u64().map_err(|_| "Invalid token ID")?;
                let receiver = decoder.string().map_err(|_| "Invalid receiver")?;
                if receiver.is_empty() || !decoder.is_empty() {
                    return Err("Invalid receiver");
                }
                let claim = state.active_claim_mut(id)?;
                if claim.owner != request.caller || receiver == request.caller {
                    return Err("Transfer is not allowed");
                }
                claim.owner = receiver.clone();
                Ok((
                    Vec::new(),
                    alloc::vec![Self::event("transfer", id, &receiver)],
                ))
            }
            "redeem" => {
                let mut decoder = Decoder::new(&request.arguments);
                let id = decoder.u64().map_err(|_| "Invalid token ID")?;
                if !decoder.is_empty() {
                    return Err("Invalid arguments");
                }
                let claim_index = state.active_claim_index(id)?;
                if state.claims[claim_index].owner != request.caller {
                    return Err("Only the current owner can redeem this token");
                }
                let claim = state.claims.remove(claim_index);
                let mut data = Encoder::new();
                data.string(&claim.message);
                Ok((
                    data.finish(),
                    alloc::vec![Self::event("burn", id, &request.caller)],
                ))
            }
            "owner_of" => {
                let mut decoder = Decoder::new(&request.arguments);
                let id = decoder.u64().map_err(|_| "Invalid token ID")?;
                if !decoder.is_empty() {
                    return Err("Invalid arguments");
                }
                let claim = state.active_claim_mut(id)?;
                let mut data = Encoder::new();
                data.string(&claim.owner);
                Ok((data.finish(), Vec::new()))
            }
            "authorize_upgrade" | "migrate" => {
                Ownership::new(&state.owner).require_owner(&request.caller)?;
                Ok((Vec::new(), Vec::new()))
            }
            _ => Err("Unknown claim method"),
        }
    }
}

impl Contract for MessageClaim {
    fn invoke(request: InvokeRequest) -> InvokeResponse {
        let mut state = match ClaimState::decode(&request.state) {
            Ok(state) => state,
            Err(error) => return InvokeResponse::failure(request.state, error),
        };
        if request.method == "forward_store" {
            let mut decoder = Decoder::new(&request.arguments);
            if decoder.array().ok() != Some(2) {
                return InvokeResponse::failure(request.state, "Invalid forward arguments");
            }
            let target = match decoder.string() {
                Ok(value) => value,
                Err(_) => return InvokeResponse::failure(request.state, "Invalid target contract"),
            };
            let message = match decoder.string() {
                Ok(value) => value,
                Err(_) => return InvokeResponse::failure(request.state, "Invalid message"),
            };
            if target.is_empty()
                || message.is_empty()
                || message.len() > MAX_MESSAGE_BYTES
                || !decoder.is_empty()
            {
                return InvokeResponse::failure(request.state, "Invalid forward arguments");
            }
            let id = state.next_id;
            state.next_id = match state.next_id.checked_add(1) {
                Some(value) => value,
                None => return InvokeResponse::failure(request.state, "Token ID overflow"),
            };
            state.claims.push(Claim {
                id,
                owner: request.caller.clone(),
                message: message.clone(),
                active: true,
            });
            let mut child_arguments = Encoder::new();
            child_arguments.string(&message);
            return InvokeResponse::success(
                state.encode(),
                Self::id_data(id),
                alloc::vec![Self::event("mint", id, &request.caller)],
            )
            .with_effects(alloc::vec![Effect::ContractCall {
                contract_id: target,
                method: "store".to_string(),
                arguments: child_arguments.finish(),
            }]);
        }
        match Self::handle(&request, &mut state) {
            Ok((data, events)) => InvokeResponse::success(state.encode(), data, events),
            Err(error) => InvokeResponse::failure(request.state, error),
        }
    }
}

export_contract!(MessageClaim);
