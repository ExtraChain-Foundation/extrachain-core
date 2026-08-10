#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};
use alloc::vec::Vec;

use extrachain_contract_sdk::{
    Contract, Decoder, Effect, Encoder, Event, InvokeRequest, InvokeResponse, export_contract,
};

const MAX_NAME_BYTES: usize = 64;
const MAX_SYMBOL_BYTES: usize = 12;
const MAX_STATE_ENTRIES: u32 = 16_384;

#[derive(Clone, Debug, PartialEq, Eq)]
struct Item {
    id: u128,
    owner: String,
    metadata_owner: String,
    metadata_file: String,
    metadata_hash: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq)]
struct CollectionState {
    name: String,
    symbol: String,
    owner: String,
    mint_enabled: bool,
    items: Vec<Item>,
    approvals: Vec<(u128, String)>,
}

impl CollectionState {
    fn decode(source: &[u8]) -> Result<Self, &'static str> {
        if source.is_empty() {
            return Ok(Self::default());
        }
        let mut decoder = Decoder::new(source);
        if decoder.array().map_err(|_| "Invalid collection state")? != 6 {
            return Err("Invalid collection state");
        }
        let name = decoder.string().map_err(|_| "Invalid collection name")?;
        let symbol = decoder.string().map_err(|_| "Invalid collection symbol")?;
        let owner = decoder.string().map_err(|_| "Invalid collection owner")?;
        let mint_enabled = decoder.boolean().map_err(|_| "Invalid mint state")?;
        let item_count = decoder.array().map_err(|_| "Invalid items")?;
        if item_count > MAX_STATE_ENTRIES {
            return Err("Too many items");
        }
        let mut items = Vec::with_capacity(item_count as usize);
        for _ in 0..item_count {
            if decoder.array().map_err(|_| "Invalid item")? != 5 {
                return Err("Invalid item");
            }
            items.push(Item {
                id: decoder.amount().map_err(|_| "Invalid item ID")?,
                owner: decoder.string().map_err(|_| "Invalid item owner")?,
                metadata_owner: decoder.string().map_err(|_| "Invalid metadata owner")?,
                metadata_file: decoder.string().map_err(|_| "Invalid metadata file")?,
                metadata_hash: decoder.string().map_err(|_| "Invalid metadata hash")?,
            });
        }
        let approval_count = decoder.array().map_err(|_| "Invalid approvals")?;
        if approval_count > MAX_STATE_ENTRIES {
            return Err("Too many approvals");
        }
        let mut approvals = Vec::with_capacity(approval_count as usize);
        for _ in 0..approval_count {
            if decoder.array().map_err(|_| "Invalid approval")? != 2 {
                return Err("Invalid approval");
            }
            approvals.push((
                decoder.amount().map_err(|_| "Invalid approval item")?,
                decoder.string().map_err(|_| "Invalid approved actor")?,
            ));
        }
        if !decoder.is_empty() {
            return Err("Invalid trailing collection state");
        }
        Ok(Self {
            name,
            symbol,
            owner,
            mint_enabled,
            items,
            approvals,
        })
    }

    fn encode(&self) -> Vec<u8> {
        let mut encoder = Encoder::new();
        encoder.array(6);
        encoder.string(&self.name);
        encoder.string(&self.symbol);
        encoder.string(&self.owner);
        encoder.boolean(self.mint_enabled);
        encoder.array(self.items.len() as u32);
        for item in &self.items {
            encoder.array(5);
            encoder.amount(item.id);
            encoder.string(&item.owner);
            encoder.string(&item.metadata_owner);
            encoder.string(&item.metadata_file);
            encoder.string(&item.metadata_hash);
        }
        encoder.array(self.approvals.len() as u32);
        for (id, actor) in &self.approvals {
            encoder.array(2);
            encoder.amount(*id);
            encoder.string(actor);
        }
        encoder.finish()
    }

    fn item(&self, id: u128) -> Option<&Item> {
        self.items.iter().find(|item| item.id == id)
    }

    fn item_mut(&mut self, id: u128) -> Option<&mut Item> {
        self.items.iter_mut().find(|item| item.id == id)
    }

    fn approval(&self, id: u128) -> Option<&str> {
        self.approvals
            .iter()
            .find(|(candidate, _)| *candidate == id)
            .map(|(_, actor)| actor.as_str())
    }

    fn set_approval(&mut self, id: u128, actor: Option<String>) {
        self.approvals.retain(|(candidate, _)| *candidate != id);
        if let Some(actor) = actor {
            self.approvals.push((id, actor));
        }
    }
}

pub struct NonFungibleToken;

impl NonFungibleToken {
    fn event(topic: &str, id: u128, actors: &[&str]) -> Event {
        let mut encoder = Encoder::new();
        encoder.array(actors.len() as u32 + 1);
        encoder.amount(id);
        for actor in actors {
            encoder.string(actor);
        }
        Event {
            topic: topic.to_string(),
            data: encoder.finish(),
        }
    }

    fn effect(contract_id: &str, event: &Event) -> Effect {
        Effect::TokenDelta {
            token_id: contract_id.to_string(),
            operation: event.topic.clone(),
            arguments: event.data.clone(),
        }
    }

    fn success(
        state: &CollectionState,
        data: Vec<u8>,
        events: Vec<Event>,
        effects: Vec<Effect>,
    ) -> InvokeResponse {
        InvokeResponse::success(state.encode(), data, events).with_effects(effects)
    }

    fn parse_id(arguments: &[u8]) -> Result<u128, &'static str> {
        let mut decoder = Decoder::new(arguments);
        let id = decoder.amount().map_err(|_| "Invalid item ID")?;
        if !decoder.is_empty() {
            return Err("Invalid item ID");
        }
        Ok(id)
    }

    fn transfer(
        state: &mut CollectionState,
        caller: &str,
        owner: &str,
        receiver: &str,
        id: u128,
    ) -> Result<(), &'static str> {
        if receiver.is_empty() || receiver == owner {
            return Err("Invalid receiver");
        }
        let current = state.item(id).ok_or("Item does not exist")?;
        if current.owner != owner
            || (caller != owner && state.approval(id) != Some(caller))
        {
            return Err("Transfer is not allowed");
        }
        state.item_mut(id).ok_or("Item does not exist")?.owner = receiver.to_string();
        state.set_approval(id, None);
        Ok(())
    }

    fn handle(
        request: &InvokeRequest,
        state: &mut CollectionState,
    ) -> Result<(Vec<u8>, Vec<Event>, Vec<Effect>), &'static str> {
        match request.method.as_str() {
            "init" => {
                if !state.owner.is_empty() || request.caller.is_empty() {
                    return Err("Collection is already initialized");
                }
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid init arguments")? != 2 {
                    return Err("Invalid init arguments");
                }
                let name = decoder.string().map_err(|_| "Invalid collection name")?;
                let symbol = decoder.string().map_err(|_| "Invalid collection symbol")?;
                if name.is_empty()
                    || name.len() > MAX_NAME_BYTES
                    || symbol.is_empty()
                    || symbol.len() > MAX_SYMBOL_BYTES
                    || !decoder.is_empty()
                {
                    return Err("Invalid collection metadata");
                }
                state.name = name;
                state.symbol = symbol;
                state.owner = request.caller.clone();
                state.mint_enabled = true;
                Ok((Vec::new(), Vec::new(), Vec::new()))
            }
            "mint" => {
                if request.caller != state.owner || !state.mint_enabled {
                    return Err("Mint is not allowed");
                }
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid mint arguments")? != 5 {
                    return Err("Invalid mint arguments");
                }
                let id = decoder.amount().map_err(|_| "Invalid item ID")?;
                let receiver = decoder.string().map_err(|_| "Invalid receiver")?;
                let metadata_owner = decoder.string().map_err(|_| "Invalid metadata owner")?;
                let metadata_file = decoder.string().map_err(|_| "Invalid metadata file")?;
                let metadata_hash = decoder.string().map_err(|_| "Invalid metadata hash")?;
                if receiver.is_empty()
                    || metadata_owner.is_empty()
                    || metadata_file.is_empty()
                    || metadata_hash.len() != 64
                    || !decoder.is_empty()
                    || state.item(id).is_some()
                    || !request.verified.dfs.iter().any(|proof| {
                        proof.owner_id == metadata_owner
                            && proof.file_id == metadata_file
                            && proof.content_hash == metadata_hash
                    })
                {
                    return Err("NFT metadata is not verified");
                }
                state.items.push(Item {
                    id,
                    owner: receiver.clone(),
                    metadata_owner,
                    metadata_file,
                    metadata_hash,
                });
                let event = Self::event("nft_mint", id, &[&receiver]);
                let effect = Self::effect(&request.contract_id, &event);
                Ok((Vec::new(), alloc::vec![event], alloc::vec![effect]))
            }
            "approve" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid approval")? != 2 {
                    return Err("Invalid approval");
                }
                let id = decoder.amount().map_err(|_| "Invalid item ID")?;
                let actor = decoder.string().map_err(|_| "Invalid approved actor")?;
                if actor.is_empty()
                    || actor == request.caller
                    || !decoder.is_empty()
                    || state.item(id).map(|item| item.owner.as_str()) != Some(request.caller.as_str())
                {
                    return Err("Approval is not allowed");
                }
                state.set_approval(id, Some(actor.clone()));
                Ok((Vec::new(), alloc::vec![Self::event("nft_approval", id, &[&actor])], Vec::new()))
            }
            "transfer" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid transfer")? != 2 {
                    return Err("Invalid transfer");
                }
                let id = decoder.amount().map_err(|_| "Invalid item ID")?;
                let receiver = decoder.string().map_err(|_| "Invalid receiver")?;
                if !decoder.is_empty() {
                    return Err("Invalid transfer");
                }
                let owner = state.item(id).ok_or("Item does not exist")?.owner.clone();
                Self::transfer(state, &request.caller, &owner, &receiver, id)?;
                let event = Self::event("nft_transfer", id, &[&owner, &receiver]);
                let effect = Self::effect(&request.contract_id, &event);
                Ok((Vec::new(), alloc::vec![event], alloc::vec![effect]))
            }
            "transfer_from" => {
                let mut decoder = Decoder::new(&request.arguments);
                if decoder.array().map_err(|_| "Invalid transfer")? != 3 {
                    return Err("Invalid transfer");
                }
                let id = decoder.amount().map_err(|_| "Invalid item ID")?;
                let owner = decoder.string().map_err(|_| "Invalid owner")?;
                let receiver = decoder.string().map_err(|_| "Invalid receiver")?;
                if !decoder.is_empty() {
                    return Err("Invalid transfer");
                }
                Self::transfer(state, &request.caller, &owner, &receiver, id)?;
                let event = Self::event("nft_transfer", id, &[&owner, &receiver]);
                let effect = Self::effect(&request.contract_id, &event);
                Ok((Vec::new(), alloc::vec![event], alloc::vec![effect]))
            }
            "burn" => {
                let id = Self::parse_id(&request.arguments)?;
                let index = state
                    .items
                    .iter()
                    .position(|item| item.id == id)
                    .ok_or("Item does not exist")?;
                let owner = state.items[index].owner.clone();
                if request.caller != owner && state.approval(id) != Some(request.caller.as_str()) {
                    return Err("Burn is not allowed");
                }
                state.items.remove(index);
                state.set_approval(id, None);
                let event = Self::event("nft_burn", id, &[&owner]);
                let effect = Self::effect(&request.contract_id, &event);
                Ok((Vec::new(), alloc::vec![event], alloc::vec![effect]))
            }
            "owner_of" => {
                let id = Self::parse_id(&request.arguments)?;
                let mut encoder = Encoder::new();
                encoder.string(&state.item(id).ok_or("Item does not exist")?.owner);
                Ok((encoder.finish(), Vec::new(), Vec::new()))
            }
            "metadata_of" => {
                let id = Self::parse_id(&request.arguments)?;
                let item = state.item(id).ok_or("Item does not exist")?;
                let mut encoder = Encoder::new();
                encoder.array(3);
                encoder.string(&item.metadata_owner);
                encoder.string(&item.metadata_file);
                encoder.string(&item.metadata_hash);
                Ok((encoder.finish(), Vec::new(), Vec::new()))
            }
            "revoke_mint" => {
                if request.caller != state.owner || !state.mint_enabled {
                    return Err("Mint control is not available");
                }
                state.mint_enabled = false;
                Ok((Vec::new(), Vec::new(), Vec::new()))
            }
            "authorize_upgrade" => {
                if request.caller != state.owner {
                    return Err("Only the owner can update the collection");
                }
                Ok((Vec::new(), Vec::new(), Vec::new()))
            }
            "migrate" => {
                if request.caller != state.owner || !request.arguments.is_empty() {
                    return Err("Invalid collection migration");
                }
                Ok((Vec::new(), Vec::new(), Vec::new()))
            }
            _ => Err("Unknown collection method"),
        }
    }
}

impl Contract for NonFungibleToken {
    fn invoke(request: InvokeRequest) -> InvokeResponse {
        let mut state = match CollectionState::decode(&request.state) {
            Ok(state) => state,
            Err(error) => return InvokeResponse::failure(request.state, error),
        };
        match Self::handle(&request, &mut state) {
            Ok((data, events, effects)) => Self::success(&state, data, events, effects),
            Err(error) => InvokeResponse::failure(request.state, error),
        }
    }
}

export_contract!(NonFungibleToken);
