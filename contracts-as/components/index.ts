import {
  ActorId,
  Amount,
  Context,
  ContractEffect,
  ContractEvent,
  DagProof,
  Decoder,
  DfsProof,
  Encoder,
  NonZeroAmount,
  OperationReceipt,
  StateMap,
  StateSet,
  compareString,
  compareU64,
  isContentHash,
  isDfsLogicalKey,
} from "../sdk/index";

export class Ownership {
  constructor(public owner: string) {}

  requireOwner(caller: string): bool {
    return caller == this.owner;
  }

  transfer(caller: string, nextOwner: string): bool {
    if (!this.requireOwner(caller) || nextOwner.length == 0) return false;
    this.owner = nextOwner;
    return true;
  }
}

export class Roles {
  private members: StateMap<string, StateSet<string>> =
    new StateMap<string, StateSet<string>>(compareString, 64);

  grant(role: string, actor: string): bool {
    if (role.length == 0 || actor.length == 0) return false;
    let members = this.members.get(role);
    if (members === null) {
      members = new StateSet<string>(compareString, 256);
      if (this.members.set(role, members) < 0) return false;
    }
    return members.add(actor) >= 0;
  }

  revoke(role: string, actor: string): bool {
    const members = this.members.get(role);
    if (members === null) return false;
    const removed = members.delete(actor);
    if (removed && members.size == 0) this.members.delete(role);
    return removed;
  }

  has(role: string, actor: string): bool {
    const members = this.members.get(role);
    return members !== null && members.has(actor);
  }
}

export class Pausable {
  paused: bool = false;

  pause(): void { this.paused = true; }
  resume(): void { this.paused = false; }
  active(): bool { return !this.paused; }
}

export class UpgradePolicy {
  constructor(public owner: string) {}
  authorize(caller: string): bool { return caller == this.owner; }
}

export class ReplayGuard {
  private consumed: StateSet<string> = new StateSet<string>(compareString, 4096);

  consume(operationId: string): bool {
    return operationId.length > 0 && this.consumed.add(operationId) == 0;
  }
}

export class SectionTimelock {
  constructor(public unlockSection: u64) {}
  unlocked(section: u64): bool { return section >= this.unlockSection; }
}

export class FungibleLedger {
  private balances: StateMap<string, Amount> = new StateMap<string, Amount>(compareString);
  private allowances: StateMap<string, Amount> = new StateMap<string, Amount>(compareString);
  private locked: StateMap<string, Amount> = new StateMap<string, Amount>(compareString);
  totalSupply: Amount = Amount.zero();

  balanceOf(actor: ActorId): Amount {
    const balance = this.balances.get(actor.value);
    return balance === null ? Amount.zero() : balance.clone();
  }

  allowance(owner: ActorId, spender: ActorId): Amount {
    const value = this.allowances.get(owner.value + ":" + spender.value);
    return value === null ? Amount.zero() : value.clone();
  }

  lockedBalance(actor: ActorId): Amount {
    const value = this.locked.get(actor.value);
    return value === null ? Amount.zero() : value.clone();
  }

  spendableBalance(actor: ActorId): Amount | null {
    return this.balanceOf(actor).checkedSub(this.lockedBalance(actor));
  }

  private setBalance(actor: ActorId, amount: Amount): bool {
    if (amount.isZero()) return this.balances.delete(actor.value) || true;
    return this.balances.set(actor.value, amount) >= 0;
  }

  private setAllowance(owner: ActorId, spender: ActorId, amount: Amount): bool {
    const key = owner.value + ":" + spender.value;
    if (amount.isZero()) return this.allowances.delete(key) || true;
    return this.allowances.set(key, amount) >= 0;
  }

  setLocked(actor: ActorId, amount: Amount): bool {
    if (amount.isZero()) return this.locked.delete(actor.value) || true;
    return this.locked.set(actor.value, amount) >= 0;
  }

  mint(context: Context, actor: ActorId, amount: NonZeroAmount): OperationReceipt | null {
    const nextBalance = this.balanceOf(actor).checkedAdd(amount.value);
    const nextSupply = this.totalSupply.checkedAdd(amount.value);
    if (nextBalance === null || nextSupply === null || !this.setBalance(actor, nextBalance)) return null;
    this.totalSupply = nextSupply;
    context.fungibleMint(actor, amount);
    return new OperationReceipt("mint", actor.value, amount.value);
  }

  burn(context: Context, actor: ActorId, amount: NonZeroAmount): OperationReceipt | null {
    const spendable = this.spendableBalance(actor);
    const balance = this.balanceOf(actor).checkedSub(amount.value);
    const supply = this.totalSupply.checkedSub(amount.value);
    if (spendable === null || spendable.lessThan(amount.value) || balance === null || supply === null) return null;
    if (!this.setBalance(actor, balance)) return null;
    this.totalSupply = supply;
    context.fungibleBurn(actor, amount);
    return new OperationReceipt("burn", actor.value, amount.value);
  }

  approve(context: Context, owner: ActorId, spender: ActorId, amount: Amount): OperationReceipt | null {
    if (owner.value == spender.value || !this.setAllowance(owner, spender, amount)) return null;
    const encoder = new Encoder();
    encoder.array(3);
    encoder.string(owner.value);
    encoder.string(spender.value);
    encoder.amount(amount);
    context.events.push(new ContractEvent("approval", encoder.finish()));
    return new OperationReceipt("approval", spender.value, amount);
  }

  transfer(
    context: Context,
    sender: ActorId,
    receiver: ActorId,
    amount: NonZeroAmount,
    policy: FungiblePolicy,
  ): OperationReceipt | null {
    if (!this.move(sender, receiver, amount)) return null;
    context.fungibleTransfer(sender, receiver, amount);
    if (!policy.afterTransfer(this, context, sender)) return null;
    return new OperationReceipt("transfer", receiver.value, amount.value);
  }

  transferFrom(
    context: Context,
    spender: ActorId,
    owner: ActorId,
    receiver: ActorId,
    amount: NonZeroAmount,
    policy: FungiblePolicy,
  ): OperationReceipt | null {
    const allowance = this.allowance(owner, spender);
    const remaining = allowance.checkedSub(amount.value);
    if (remaining === null || !this.move(owner, receiver, amount)) return null;
    if (!this.setAllowance(owner, spender, remaining)) return null;
    context.fungibleTransfer(owner, receiver, amount);
    if (!policy.afterTransfer(this, context, owner)) return null;
    return new OperationReceipt("transfer", receiver.value, amount.value);
  }

  private move(sender: ActorId, receiver: ActorId, amount: NonZeroAmount): bool {
    if (sender.value == receiver.value) return false;
    const spendable = this.spendableBalance(sender);
    if (spendable === null || spendable.lessThan(amount.value)) return false;
    const source = this.balanceOf(sender).checkedSub(amount.value);
    const target = this.balanceOf(receiver).checkedAdd(amount.value);
    if (source === null || target === null || !this.setBalance(receiver, target)) return false;
    return this.setBalance(sender, source);
  }
}

export abstract class FungiblePolicy {
  abstract afterTransfer(ledger: FungibleLedger, context: Context, sender: ActorId): bool;
}

export class OpenTransfers extends FungiblePolicy {
  afterTransfer(_ledger: FungibleLedger, _context: Context, _sender: ActorId): bool { return true; }
}

export class FreezeLastUnit extends FungiblePolicy {
  constructor(public unit: Amount, public enabled: bool = true) { super(); }

  afterTransfer(ledger: FungibleLedger, context: Context, sender: ActorId): bool {
    if (!this.enabled || !ledger.balanceOf(sender).equals(this.unit) || !ledger.lockedBalance(sender).isZero()) {
      return true;
    }
    const amount = NonZeroAmount.create(this.unit);
    if (amount === null || !ledger.setLocked(sender, this.unit)) return false;
    context.fungibleLock(sender, amount);
    return true;
  }
}

export class NftLedger {
  private owners: StateMap<string, ActorId> = new StateMap<string, ActorId>(compareString);
  private approvals: StateMap<string, ActorId> = new StateMap<string, ActorId>(compareString);

  mint(context: Context, tokenId: Amount, owner: ActorId): OperationReceipt | null {
    const key = tokenId.toString();
    if (this.owners.has(key) || this.owners.set(key, owner) < 0) return null;
    context.nftMint(tokenId, owner);
    return new OperationReceipt("nft_mint", owner.value, tokenId);
  }

  ownerOf(tokenId: Amount): ActorId | null {
    return this.owners.get(tokenId.toString());
  }

  approved(tokenId: Amount): ActorId | null {
    return this.approvals.get(tokenId.toString());
  }

  approve(context: Context, caller: ActorId, tokenId: Amount, actor: ActorId): OperationReceipt | null {
    const owner = this.ownerOf(tokenId);
    if (owner === null || owner.value != caller.value || actor.value == caller.value) return null;
    if (this.approvals.set(tokenId.toString(), actor) < 0) return null;
    const encoder = new Encoder();
    encoder.array(2);
    encoder.amount(tokenId);
    encoder.string(actor.value);
    context.events.push(new ContractEvent("nft_approval", encoder.finish()));
    return new OperationReceipt("nft_approval", actor.value, tokenId);
  }

  transfer(
    context: Context,
    caller: ActorId,
    tokenId: Amount,
    receiver: ActorId,
  ): OperationReceipt | null {
    const owner = this.ownerOf(tokenId);
    const approval = this.approved(tokenId);
    if (owner === null || owner.value == receiver.value
        || (owner.value != caller.value && (approval === null || approval.value != caller.value))) return null;
    if (this.owners.set(tokenId.toString(), receiver) < 0) return null;
    this.approvals.delete(tokenId.toString());
    context.nftTransfer(tokenId, owner, receiver);
    return new OperationReceipt("nft_transfer", receiver.value, tokenId);
  }

  burn(context: Context, caller: ActorId, tokenId: Amount): OperationReceipt | null {
    const owner = this.ownerOf(tokenId);
    const approval = this.approved(tokenId);
    if (owner === null
        || (owner.value != caller.value && (approval === null || approval.value != caller.value))) return null;
    this.owners.delete(tokenId.toString());
    this.approvals.delete(tokenId.toString());
    context.nftBurn(tokenId, owner);
    return new OperationReceipt("nft_burn", owner.value, tokenId);
  }
}

export class Escrow {
  released: bool = false;

  constructor(public payer: string, public payee: string, public amount: u64) {}

  valid(): bool {
    return this.payer.length > 0 && this.payee.length > 0 && this.payer != this.payee && this.amount > 0;
  }

  release(caller: string): bool {
    if (!this.valid() || this.released || caller != this.payer) return false;
    this.released = true;
    return true;
  }
}

export class Multisig {
  private signers: StateSet<string> = new StateSet<string>(compareString, 256);

  constructor(signers: Array<string>, public threshold: i32) {
    for (let index = 0; index < signers.length; ++index) this.signers.add(signers[index]);
  }

  valid(): bool {
    return this.threshold > 0 && this.threshold <= this.signers.size;
  }

  approved(approvals: Array<string>): bool {
    if (!this.valid()) return false;
    const accepted = new StateSet<string>(compareString, 256);
    for (let index = 0; index < approvals.length; ++index) {
      if (this.signers.has(approvals[index])) accepted.add(approvals[index]);
    }
    return accepted.size >= this.threshold;
  }
}

export function requireDagTransaction(
  proofs: Array<DagProof>,
  transactionHash: string,
  minimumConfirmations: u64,
): bool {
  for (let index = 0; index < proofs.length; ++index) {
    if (proofs[index].transactionHash == transactionHash
        && proofs[index].confirmations >= minimumConfirmations) return true;
  }
  return false;
}

export function requireDfsFile(
  proofs: Array<DfsProof>,
  ownerId: string,
  fileId: string,
  contentHash: string,
): bool {
  for (let index = 0; index < proofs.length; ++index) {
    if (proofs[index].ownerId == ownerId
        && proofs[index].fileId == fileId
        && proofs[index].contentHash == contentHash) return true;
  }
  return false;
}

export class DfsBinding {
  constructor(public fileId: string, public contentHash: string) {}
}

export class DfsBindings {
  private bindings: StateMap<string, DfsBinding> = new StateMap<string, DfsBinding>(compareString, 4096);

  static decode(state: Uint8Array): DfsBindings | null {
    const result = new DfsBindings();
    if (state.length == 0) return result;
    const decoder = new Decoder(state);
    const count = decoder.array();
    if (!decoder.valid || count < 0 || count > 4096) return null;
    for (let index = 0; index < count; ++index) {
      if (decoder.array() != 3) return null;
      const logicalKey = decoder.string();
      const fileId = decoder.string();
      const contentHash = decoder.string();
      if (!decoder.valid || !result.bind(logicalKey, fileId, contentHash, "")) return null;
    }
    return decoder.empty() ? result : null;
  }

  encode(): Uint8Array {
    const encoder = new Encoder();
    const entries = this.bindings.entries();
    encoder.array(entries.length);
    for (let index = 0; index < entries.length; ++index) {
      const binding = entries[index].value;
      encoder.array(3);
      encoder.string(entries[index].key);
      encoder.string(binding.fileId);
      encoder.string(binding.contentHash);
    }
    return encoder.finish();
  }

  get(logicalKey: string): DfsBinding | null {
    return this.bindings.get(logicalKey);
  }

  bind(logicalKey: string, fileId: string, contentHash: string, previousContentHash: string): bool {
    if (!isDfsLogicalKey(logicalKey) || fileId.length == 0 || !isContentHash(contentHash)) {
      return false;
    }
    const previous = this.get(logicalKey);
    const currentHash = previous === null ? "" : previous.contentHash;
    if (currentHash != previousContentHash) return false;
    return this.bindings.set(logicalKey, new DfsBinding(fileId, contentHash)) >= 0;
  }

  tombstone(logicalKey: string, previousContentHash: string): bool {
    if (!isDfsLogicalKey(logicalKey) || !isContentHash(previousContentHash)) return false;
    const previous = this.get(logicalKey);
    if (previous === null || previous.contentHash != previousContentHash) return false;
    return this.bindings.delete(logicalKey);
  }
}

function bindingArguments(
  logicalKey: string,
  fileId: string,
  contentHash: string,
  previousContentHash: string,
): Uint8Array {
  const encoder = new Encoder();
  encoder.array(4);
  encoder.string(logicalKey);
  encoder.string(fileId);
  encoder.string(contentHash);
  encoder.string(previousContentHash);
  return encoder.finish();
}

export function bindDfsFile(
  ownerId: string,
  logicalKey: string,
  fileId: string,
  contentHash: string,
  previousContentHash: string = "",
): ContractEffect | null {
  if (ownerId.length == 0 || !isDfsLogicalKey(logicalKey) || fileId.length == 0
      || !isContentHash(contentHash) || !isContentHash(previousContentHash, true)) return null;
  return new ContractEffect(
    "dfs_write",
    ownerId,
    "bind",
    bindingArguments(logicalKey, fileId, contentHash, previousContentHash),
  );
}

export function tombstoneDfsFile(
  ownerId: string,
  logicalKey: string,
  previousContentHash: string,
): ContractEffect | null {
  if (ownerId.length == 0 || !isDfsLogicalKey(logicalKey)
      || !isContentHash(previousContentHash)) return null;
  return new ContractEffect(
    "dfs_write",
    ownerId,
    "tombstone",
    bindingArguments(logicalKey, "", "", previousContentHash),
  );
}
