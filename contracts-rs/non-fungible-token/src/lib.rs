#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};

use extrachain_contract_components::NftLedger;
use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractCodec, ContractResult, OperationReceipt, StateMap,
    contract, require,
};

const MAX_TOKENS: usize = 16_384;

#[derive(Clone, Debug, PartialEq, Eq, ContractCodec)]
struct Metadata {
    owner: ActorId,
    file: String,
    hash: String,
}

#[contract(version = 2, owner = "owner", upgrade = "owner")]
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct NonFungibleToken {
    owner: String,
    name: String,
    symbol: String,
    mint_enabled: bool,
    ledger: NftLedger,
    metadata: StateMap<u128, Metadata, MAX_TOKENS>,
}

#[contract]
impl NonFungibleToken {
    #[init]
    fn init(
        &mut self,
        context: &Context<'_>,
        name: BoundedString<64>,
        symbol: BoundedString<12>,
    ) -> ContractResult<()> {
        self.owner = context.caller().to_string();
        self.name = name.into_string();
        self.symbol = symbol.into_string();
        self.mint_enabled = true;
        Ok(())
    }

    #[call(access = "owner")]
    fn mint(
        &mut self,
        context: &mut Context<'_>,
        token_id: u128,
        receiver: ActorId,
        metadata_owner: ActorId,
        metadata_file: BoundedString<256>,
        metadata_hash: BoundedString<64>,
    ) -> ContractResult<OperationReceipt> {
        require!(self.mint_enabled, "Mint is disabled");
        require!(
            context.has_dfs_proof(
                metadata_owner.as_str(),
                metadata_file.as_str(),
                metadata_hash.as_str(),
            ),
            "NFT metadata is not verified"
        );
        let receipt = self.ledger.mint(context, token_id, receiver)?;
        self.metadata.insert(
            token_id,
            Metadata {
                owner: metadata_owner,
                file: metadata_file.into_string(),
                hash: metadata_hash.into_string(),
            },
        )?;
        Ok(receipt)
    }

    #[call]
    fn approve(
        &mut self,
        context: &mut Context<'_>,
        token_id: u128,
        actor: ActorId,
    ) -> ContractResult<OperationReceipt> {
        let caller = ActorId::new(context.caller().to_string())?;
        self.ledger.approve(context, &caller, token_id, actor)
    }

    #[call]
    fn transfer(
        &mut self,
        context: &mut Context<'_>,
        token_id: u128,
        receiver: ActorId,
    ) -> ContractResult<OperationReceipt> {
        let caller = ActorId::new(context.caller().to_string())?;
        self.ledger.transfer(context, &caller, token_id, receiver)
    }

    #[call]
    fn transfer_from(
        &mut self,
        context: &mut Context<'_>,
        token_id: u128,
        owner: ActorId,
        receiver: ActorId,
    ) -> ContractResult<OperationReceipt> {
        require!(
            self.ledger.owner_of(token_id) == Some(&owner),
            "The expected owner does not own the token"
        );
        let caller = ActorId::new(context.caller().to_string())?;
        self.ledger.transfer(context, &caller, token_id, receiver)
    }

    #[call]
    fn burn(
        &mut self,
        context: &mut Context<'_>,
        token_id: u128,
    ) -> ContractResult<OperationReceipt> {
        let caller = ActorId::new(context.caller().to_string())?;
        let receipt = self.ledger.burn(context, &caller, token_id)?;
        self.metadata.remove(&token_id);
        Ok(receipt)
    }

    #[query]
    fn owner_of(&self, token_id: u128) -> ContractResult<String> {
        Ok(self
            .ledger
            .owner_of(token_id)
            .ok_or("The token does not exist")?
            .as_str()
            .to_string())
    }

    #[query]
    fn metadata_of(&self, token_id: u128) -> ContractResult<(ActorId, String, String)> {
        let metadata = self
            .metadata
            .get(&token_id)
            .ok_or("Metadata does not exist")?;
        Ok((
            metadata.owner.clone(),
            metadata.file.clone(),
            metadata.hash.clone(),
        ))
    }

    #[call(access = "owner")]
    fn revoke_mint(&mut self, context: &mut Context<'_>) -> ContractResult<()> {
        require!(self.mint_enabled, "Mint control is not available");
        self.mint_enabled = false;
        context.emit("mint_revoked", &());
        Ok(())
    }

    #[migrate(from = 1, to = 2, access = "owner")]
    fn migrate(&mut self, _context: &mut Context<'_>) -> ContractResult<()> {
        Ok(())
    }
}
