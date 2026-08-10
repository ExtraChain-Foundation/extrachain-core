#![no_std]

extern crate alloc;

use alloc::string::{String, ToString};

use extrachain_contract_sdk::{
    ActorId, BoundedString, Context, ContractCodec, ContractResult, ContractState, StateMap,
    contract, require,
};

const MAX_ACTIVE_CLAIMS: usize = 16_384;
const MAX_MESSAGE_BYTES: usize = 64 * 1024;

#[derive(Clone, Debug, PartialEq, Eq, ContractCodec)]
struct Claim {
    id: u64,
    owner: String,
    message: String,
}

#[derive(Clone, Debug, PartialEq, Eq, ContractState)]
#[state(version = 1)]
pub struct MessageClaim {
    #[owner]
    owner: String,
    next_id: u64,
    claims: StateMap<u64, Claim, MAX_ACTIVE_CLAIMS>,
}

impl Default for MessageClaim {
    fn default() -> Self {
        Self {
            owner: String::new(),
            next_id: 1,
            claims: StateMap::default(),
        }
    }
}

#[contract]
impl MessageClaim {
    fn issue(
        &mut self,
        ctx: &mut Context<'_>,
        owner: &str,
        message: String,
    ) -> ContractResult<u64> {
        let id = self.next_id;
        self.next_id = self.next_id.checked_add(1).ok_or("Token ID overflow")?;
        self.claims.insert(
            id,
            Claim {
                id,
                owner: owner.to_string(),
                message,
            },
        )?;
        ctx.emit("mint", &(id, owner.to_string()));
        Ok(id)
    }

    #[init]
    fn init(&mut self, ctx: &Context<'_>) -> ContractResult<()> {
        self.owner = ctx.caller().to_string();
        Ok(())
    }

    #[call]
    fn store(
        &mut self,
        ctx: &mut Context<'_>,
        message: BoundedString<MAX_MESSAGE_BYTES>,
    ) -> ContractResult<u64> {
        let caller = ctx.caller().to_string();
        self.issue(ctx, &caller, message.into_string())
    }

    #[call]
    fn forward_store(
        &mut self,
        ctx: &mut Context<'_>,
        target: ActorId,
        message: BoundedString<MAX_MESSAGE_BYTES>,
    ) -> ContractResult<u64> {
        let target = target.into_string();
        let message = message.into_string();
        let caller = ctx.caller().to_string();
        let id = self.issue(ctx, &caller, message.clone())?;
        ctx.call(&target, "store", &(message,));
        Ok(id)
    }

    #[call]
    fn transfer(
        &mut self,
        ctx: &mut Context<'_>,
        id: u64,
        receiver: ActorId,
    ) -> ContractResult<()> {
        let mut claim = self
            .claims
            .remove(&id)
            .ok_or("Claim token does not exist")?;
        let receiver = receiver.into_string();
        require!(claim.owner == ctx.caller(), "Transfer is not allowed");
        require!(receiver != ctx.caller(), "Self transfer is not allowed");
        claim.owner = receiver.clone();
        self.claims.insert(id, claim)?;
        ctx.emit("transfer", &(id, receiver));
        Ok(())
    }

    #[call]
    fn redeem(&mut self, ctx: &mut Context<'_>, id: u64) -> ContractResult<String> {
        let claim = self
            .claims
            .remove(&id)
            .ok_or("Claim token does not exist")?;
        require!(
            claim.owner == ctx.caller(),
            "Only the current owner can redeem this token"
        );
        ctx.emit("burn", &(id, ctx.caller().to_string()));
        Ok(claim.message)
    }

    #[query]
    fn owner_of(&self, id: u64) -> ContractResult<String> {
        Ok(self
            .claims
            .get(&id)
            .ok_or("Claim token does not exist")?
            .owner
            .clone())
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
