#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_sdk::{
    Contract, Decoder, Encoder, Event, InvokeRequest, InvokeResponse, export_contract,
};

const MAX_NAME_BYTES: usize = 64;
const MAX_SYMBOL_BYTES: usize = 12;
const MAX_STATE_ENTRIES: u32 = 16_384;

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct TokenState {
    name: String,
    symbol: String,
    decimals: u8,
    owner: String,
    mint_enabled: bool,
    total_supply: u64,
    balances: Vec<(String, u64)>,
    allowances: Vec<(String, String, u64)>,
}

impl TokenState {
    fn decode(source: &[u8]) -> Result<Self, &'static str> {
        if source.is_empty() {
            return Ok(Self::default());
        }

        let mut decoder = Decoder::new(source);
        if decoder.array().map_err(|_| "Invalid token state")? != 8 {
            return Err("Invalid token state");
        }
        let name = decoder.string().map_err(|_| "Invalid token name")?;
        let symbol = decoder.string().map_err(|_| "Invalid token symbol")?;
        let decimals_raw = decoder.u64().map_err(|_| "Invalid token decimals")?;
        let decimals = u8::try_from(decimals_raw).map_err(|_| "Invalid token decimals")?;
        let owner = decoder.string().map_err(|_| "Invalid token owner")?;
        let mint_enabled = decoder.boolean().map_err(|_| "Invalid mint state")?;
        let total_supply = decoder.u64().map_err(|_| "Invalid token supply")?;

        let balance_count = decoder.array().map_err(|_| "Invalid balances")?;
        if balance_count > MAX_STATE_ENTRIES {
            return Err("Too many balances");
        }
        let mut balances = Vec::with_capacity(balance_count as usize);
        for _ in 0..balance_count {
            if decoder.array().map_err(|_| "Invalid balance")? != 2 {
                return Err("Invalid balance");
            }
            balances.push((
                decoder.string().map_err(|_| "Invalid balance owner")?,
                decoder.u64().map_err(|_| "Invalid balance amount")?,
            ));
        }

        let allowance_count = decoder.array().map_err(|_| "Invalid allowances")?;
        if allowance_count > MAX_STATE_ENTRIES {
            return Err("Too many allowances");
        }
        let mut allowances = Vec::with_capacity(allowance_count as usize);
        for _ in 0..allowance_count {
            if decoder.array().map_err(|_| "Invalid allowance")? != 3 {
                return Err("Invalid allowance");
            }
            allowances.push((
                decoder.string().map_err(|_| "Invalid allowance owner")?,
                decoder.string().map_err(|_| "Invalid allowance spender")?,
                decoder.u64().map_err(|_| "Invalid allowance amount")?,
            ));
        }
        if !decoder.is_empty() {
            return Err("Invalid trailing token state");
        }

        Ok(Self {
            name,
            symbol,
            decimals,
            owner,
            mint_enabled,
            total_supply,
            balances,
            allowances,
        })
    }

    fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(8);
        encoder.string(&self.name);
        encoder.string(&self.symbol);
        encoder.u64(u64::from(self.decimals));
        encoder.string(&self.owner);
        encoder.boolean(self.mint_enabled);
        encoder.u64(self.total_supply);
        encoder.array(self.balances.len() as u32);
        for (owner, amount) in &self.balances {
            encoder.array(2);
            encoder.string(owner);
            encoder.u64(*amount);
        }
        encoder.array(self.allowances.len() as u32);
        for (owner, spender, amount) in &self.allowances {
            encoder.array(3);
            encoder.string(owner);
            encoder.string(spender);
            encoder.u64(*amount);
        }
        encoder.finish()
    }

    fn balance(&self, owner: &str) -> u64 {
        self.balances
            .iter()
            .find(|(candidate, _)| candidate == owner)
            .map_or(0, |(_, amount)| *amount)
    }

    fn set_balance(&mut self, owner: &str, amount: u64) {
        if let Some((_, current)) = self
            .balances
            .iter_mut()
            .find(|(candidate, _)| candidate == owner)
        {
            *current = amount;
        } else if amount != 0 {
            self.balances.push((owner.to_string(), amount));
        }
        self.balances.retain(|(_, value)| *value != 0);
    }

    fn allowance(&self, owner: &str, spender: &str) -> u64 {
        self.allowances
            .iter()
            .find(|(candidate_owner, candidate_spender, _)| {
                candidate_owner == owner && candidate_spender == spender
            })
            .map_or(0, |(_, _, amount)| *amount)
    }

    fn set_allowance(&mut self, owner: &str, spender: &str, amount: u64) {
        if let Some((_, _, current)) =
            self.allowances
                .iter_mut()
                .find(|(candidate_owner, candidate_spender, _)| {
                    candidate_owner == owner && candidate_spender == spender
                })
        {
            *current = amount;
        } else if amount != 0 {
            self.allowances
                .push((owner.to_string(), spender.to_string(), amount));
        }
        self.allowances.retain(|(_, _, value)| *value != 0);
    }
}

pub struct FungibleToken;

impl FungibleToken {
    fn event(topic: &str, fields: &[(&str, u64)]) -> Event {
        let mut encoder = Encoder::new();
        encoder.array(fields.len() as u32);
        for (address, amount) in fields {
            encoder.array(2);
            encoder.string(address);
            encoder.u64(*amount);
        }
        Event {
            topic: topic.to_string(),
            data: encoder.finish(),
        }
    }

    fn success(state: &TokenState, data: Vec<u8>, events: Vec<Event>) -> InvokeResponse {
        InvokeResponse::success(state.encode(), data, events)
    }

    fn amount_data(amount: u64) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.u64(amount);
        encoder.finish()
    }

    fn parse_pair(arguments: &[u8]) -> Result<(String, u64), &'static str> {
        let mut decoder = Decoder::new(arguments);
        if decoder.array().map_err(|_| "Invalid arguments")? != 2 {
            return Err("Invalid arguments");
        }
        let address = decoder.string().map_err(|_| "Invalid address")?;
        let amount = decoder.u64().map_err(|_| "Invalid amount")?;
        if address.is_empty() || !decoder.is_empty() {
            return Err("Invalid arguments");
        }
        Ok((address, amount))
    }

    fn transfer(
        state: &mut TokenState,
        from: &str,
        to: &str,
        amount: u64,
    ) -> Result<(), &'static str> {
        if amount == 0 || from == to || state.balance(from) < amount {
            return Err("Transfer is not allowed");
        }
        let receiver_balance = state
            .balance(to)
            .checked_add(amount)
            .ok_or("Balance overflow")?;
        state.set_balance(from, state.balance(from) - amount);
        state.set_balance(to, receiver_balance);
        Ok(())
    }

    fn handle(
        request: &InvokeRequest,
        state: &mut TokenState,
    ) -> Result<(Vec<u8>, Vec<Event>), &'static str> {
        match request.method.as_str() {
            "init" => {
                if !state.owner.is_empty() {
                    return Err("Token is already initialized");
                }
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid init arguments")? != 4 {
                    return Err("Invalid init arguments");
                }
                let name = decoder.string().map_err(|_| "Invalid token name")?;
                let symbol = decoder.string().map_err(|_| "Invalid token symbol")?;
                let decimals = u8::try_from(decoder.u64().map_err(|_| "Invalid decimals")?)
                    .map_err(|_| "Invalid decimals")?;
                let supply = decoder.u64().map_err(|_| "Invalid supply")?;
                if name.is_empty()
                    || name.len() > MAX_NAME_BYTES
                    || symbol.is_empty()
                    || symbol.len() > MAX_SYMBOL_BYTES
                    || decimals > 18
                    || request.sender.is_empty()
                    || !decoder.is_empty()
                {
                    return Err("Invalid token metadata");
                }
                state.name = name;
                state.symbol = symbol;
                state.decimals = decimals;
                state.owner = request.sender.clone();
                state.mint_enabled = true;
                state.total_supply = supply;
                state.set_balance(&request.sender, supply);
                Ok((
                    Self::amount_data(supply),
                    alloc::vec![Self::event("mint", &[(&request.sender, supply)])],
                ))
            }
            "transfer" => {
                let (to, amount) = Self::parse_pair(&request.arguments)?;
                Self::transfer(state, &request.sender, &to, amount)?;
                Ok((
                    Vec::new(),
                    alloc::vec![Self::event(
                        "transfer",
                        &[(&request.sender, amount), (&to, amount)]
                    )],
                ))
            }
            "approve" => {
                let (spender, amount) = Self::parse_pair(&request.arguments)?;
                if spender == request.sender {
                    return Err("Self approval is not allowed");
                }
                state.set_allowance(&request.sender, &spender, amount);
                Ok((
                    Vec::new(),
                    alloc::vec![Self::event("approval", &[(&spender, amount)])],
                ))
            }
            "transfer_from" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid arguments")? != 3 {
                    return Err("Invalid arguments");
                }
                let owner = decoder.string().map_err(|_| "Invalid owner")?;
                let to = decoder.string().map_err(|_| "Invalid receiver")?;
                let amount = decoder.u64().map_err(|_| "Invalid amount")?;
                if !decoder.is_empty() || state.allowance(&owner, &request.sender) < amount {
                    return Err("Allowance is too small");
                }
                Self::transfer(state, &owner, &to, amount)?;
                state.set_allowance(
                    &owner,
                    &request.sender,
                    state.allowance(&owner, &request.sender) - amount,
                );
                Ok((
                    Vec::new(),
                    alloc::vec![Self::event("transfer", &[(&owner, amount), (&to, amount)])],
                ))
            }
            "mint" => {
                let (to, amount) = Self::parse_pair(&request.arguments)?;
                if request.sender != state.owner || !state.mint_enabled || amount == 0 {
                    return Err("Mint is not allowed");
                }
                state.total_supply = state
                    .total_supply
                    .checked_add(amount)
                    .ok_or("Supply overflow")?;
                state.set_balance(
                    &to,
                    state
                        .balance(&to)
                        .checked_add(amount)
                        .ok_or("Balance overflow")?,
                );
                Ok((
                    Self::amount_data(state.total_supply),
                    alloc::vec![Self::event("mint", &[(&to, amount)])],
                ))
            }
            "revoke_mint" => {
                if request.sender != state.owner || !state.mint_enabled {
                    return Err("Mint control is not available");
                }
                state.mint_enabled = false;
                Ok((Vec::new(), alloc::vec![Self::event("mint_revoked", &[])]))
            }
            "burn" => {
                let mut decoder = Decoder::new(&request.arguments);
                let amount = decoder.u64().map_err(|_| "Invalid amount")?;
                if amount == 0 || !decoder.is_empty() || state.balance(&request.sender) < amount {
                    return Err("Burn is not allowed");
                }
                state.set_balance(&request.sender, state.balance(&request.sender) - amount);
                state.total_supply -= amount;
                Ok((
                    Self::amount_data(state.total_supply),
                    alloc::vec![Self::event("burn", &[(&request.sender, amount)])],
                ))
            }
            "balance_of" => {
                let mut decoder = Decoder::new(&request.arguments);
                let owner = decoder.string().map_err(|_| "Invalid owner")?;
                if !decoder.is_empty() {
                    return Err("Invalid arguments");
                }
                Ok((Self::amount_data(state.balance(&owner)), Vec::new()))
            }
            "allowance" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid arguments")? != 2 {
                    return Err("Invalid arguments");
                }
                let owner = decoder.string().map_err(|_| "Invalid owner")?;
                let spender = decoder.string().map_err(|_| "Invalid spender")?;
                if !decoder.is_empty() {
                    return Err("Invalid arguments");
                }
                Ok((
                    Self::amount_data(state.allowance(&owner, &spender)),
                    Vec::new(),
                ))
            }
            "authorize_upgrade" | "migrate" => {
                if request.sender != state.owner {
                    return Err("Only the token owner can upgrade this contract");
                }
                Ok((Vec::new(), Vec::new()))
            }
            _ => Err("Unknown token method"),
        }
    }
}

impl Contract for FungibleToken {
    fn invoke(request: InvokeRequest) -> InvokeResponse {
        let mut state = match TokenState::decode(&request.state) {
            Ok(state) => state,
            Err(error) => return InvokeResponse::failure(request.state, error),
        };
        match Self::handle(&request, &mut state) {
            Ok((data, events)) => Self::success(&state, data, events),
            Err(error) => InvokeResponse::failure(request.state, error),
        }
    }
}

export_contract!(FungibleToken);
