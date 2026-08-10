#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};

use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractCodec, ContractResult, ContractState, StateMap,
    contract, require,
};

const MAX_STATE_ENTRIES: usize = 16_384;

#[derive(Clone, Debug, PartialEq, Eq, ContractCodec)]
struct Item {
    id: u128,
    owner: String,
    metadata_owner: String,
    metadata_file: String,
    metadata_hash: String,
}

#[derive(Clone, Debug, Default, PartialEq, Eq, ContractState)]
#[state(version = 1)]
pub struct NonFungibleToken {
    name: String,
    symbol: String,
    #[owner]
    owner: String,
    mint_enabled: bool,
    items: StateMap<u128, Item, MAX_STATE_ENTRIES>,
    approvals: StateMap<u128, String, MAX_STATE_ENTRIES>,
}

#[contract]
impl NonFungibleToken {
    fn item(&self, id: u128) -> ContractResult<&Item> {
        self.items.get(&id).ok_or("Item does not exist".into())
    }

    fn approval(&self, id: u128) -> Option<&str> {
        self.approvals.get(&id).map(String::as_str)
    }

    fn transfer_item(
        &mut self,
        caller: &str,
        owner: &str,
        receiver: &str,
        id: u128,
    ) -> ContractResult<()> {
        require!(receiver != owner, "Self transfer is not allowed");
        let item = self.item(id)?;
        require!(item.owner == owner, "Item owner does not match");
        require!(
            caller == owner || self.approval(id) == Some(caller),
            "Transfer is not allowed"
        );
        let mut item = self.items.remove(&id).ok_or("Item does not exist")?;
        item.owner = receiver.to_string();
        self.items.insert(id, item)?;
        self.approvals.remove(&id);
        Ok(())
    }

    #[init]
    fn init(
        &mut self,
        ctx: &Context<'_>,
        name: BoundedString<64>,
        symbol: BoundedString<12>,
    ) -> ContractResult<()> {
        self.name = name.into_string();
        self.symbol = symbol.into_string();
        self.owner = ctx.caller().to_string();
        self.mint_enabled = true;
        Ok(())
    }

    #[call]
    #[owner_only]
    fn mint(
        &mut self,
        ctx: &mut Context<'_>,
        id: u128,
        receiver: ActorId,
        metadata_owner: ActorId,
        metadata_file: BoundedString<256>,
        metadata_hash: BoundedString<64>,
    ) -> ContractResult<()> {
        require!(self.mint_enabled, "Mint is disabled");
        require!(self.items.get(&id).is_none(), "Item already exists");
        let receiver = receiver.into_string();
        let metadata_owner = metadata_owner.into_string();
        let metadata_file = metadata_file.into_string();
        let metadata_hash = metadata_hash.into_string();
        require!(metadata_hash.len() == 64, "Metadata hash is invalid");
        require!(
            ctx.has_dfs_proof(&metadata_owner, &metadata_file, &metadata_hash),
            "NFT metadata is not verified"
        );
        self.items.insert(
            id,
            Item {
                id,
                owner: receiver.clone(),
                metadata_owner,
                metadata_file,
                metadata_hash,
            },
        )?;
        ctx.token_event("nft_mint", &(id, receiver));
        Ok(())
    }

    #[call]
    fn approve(&mut self, ctx: &mut Context<'_>, id: u128, actor: ActorId) -> ContractResult<()> {
        let actor = actor.into_string();
        require!(actor != ctx.caller(), "Self approval is not allowed");
        require!(
            self.item(id)?.owner == ctx.caller(),
            "Approval is not allowed"
        );
        self.approvals.insert(id, actor.clone())?;
        ctx.emit("nft_approval", &(id, actor));
        Ok(())
    }

    #[call]
    fn transfer(
        &mut self,
        ctx: &mut Context<'_>,
        id: u128,
        receiver: ActorId,
    ) -> ContractResult<()> {
        let receiver = receiver.into_string();
        let owner = self.item(id)?.owner.clone();
        self.transfer_item(ctx.caller(), &owner, &receiver, id)?;
        ctx.token_event("nft_transfer", &(id, owner, receiver));
        Ok(())
    }

    #[call]
    fn transfer_from(
        &mut self,
        ctx: &mut Context<'_>,
        id: u128,
        owner: ActorId,
        receiver: ActorId,
    ) -> ContractResult<()> {
        let owner = owner.into_string();
        let receiver = receiver.into_string();
        self.transfer_item(ctx.caller(), &owner, &receiver, id)?;
        ctx.token_event("nft_transfer", &(id, owner, receiver));
        Ok(())
    }

    #[call]
    fn burn(&mut self, ctx: &mut Context<'_>, id: u128) -> ContractResult<()> {
        let owner = self.item(id)?.owner.clone();
        require!(
            ctx.caller() == owner || self.approval(id) == Some(ctx.caller()),
            "Burn is not allowed"
        );
        self.items.remove(&id);
        self.approvals.remove(&id);
        ctx.token_event("nft_burn", &(id, owner));
        Ok(())
    }

    #[query]
    fn owner_of(&self, id: u128) -> ContractResult<String> {
        Ok(self.item(id)?.owner.clone())
    }

    #[query]
    fn metadata_of(&self, id: u128) -> ContractResult<(String, String, String)> {
        let item = self.item(id)?;
        Ok((
            item.metadata_owner.clone(),
            item.metadata_file.clone(),
            item.metadata_hash.clone(),
        ))
    }

    #[call]
    #[owner_only]
    fn revoke_mint(&mut self, ctx: &mut Context<'_>) -> ContractResult<()> {
        require!(self.mint_enabled, "Mint control is not available");
        self.mint_enabled = false;
        ctx.emit("mint_revoked", &());
        Ok(())
    }

    #[authorize_upgrade]
    #[owner_only]
    fn authorize_upgrade(
        &self,
        _ctx: &Context<'_>,
        _module_hash: BoundedString<64>,
    ) -> ContractResult<()> {
        Ok(())
    }

    #[migrate]
    #[owner_only]
    fn migrate(&mut self, _ctx: &Context<'_>) -> ContractResult<()> {
        Ok(())
    }
}
