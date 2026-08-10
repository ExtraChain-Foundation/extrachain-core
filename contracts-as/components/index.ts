import {
  ContractEffect,
  DagProof,
  Decoder,
  DfsProof,
  Encoder,
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
  private balances: StateMap<string, u64> = new StateMap<string, u64>(compareString);
  totalSupply: u64 = 0;

  balanceOf(actor: string): u64 {
    const balance = this.balances.get(actor);
    return balance === null ? 0 : balance;
  }

  mint(actor: string, amount: u64): bool {
    if (actor.length == 0 || amount == 0) return false;
    const balance = this.balanceOf(actor);
    const nextBalance = balance + amount;
    const nextSupply = this.totalSupply + amount;
    if (nextBalance < balance || nextSupply < this.totalSupply) return false;
    if (this.balances.set(actor, nextBalance) < 0) return false;
    this.totalSupply = nextSupply;
    return true;
  }

  burn(actor: string, amount: u64): bool {
    const balance = this.balanceOf(actor);
    if (actor.length == 0 || amount == 0 || balance < amount) return false;
    if (balance == amount) this.balances.delete(actor);
    else this.balances.set(actor, balance - amount);
    this.totalSupply -= amount;
    return true;
  }

  transfer(from: string, to: string, amount: u64): bool {
    const source = this.balanceOf(from);
    const target = this.balanceOf(to);
    if (from.length == 0 || to.length == 0 || from == to || amount == 0 || source < amount) return false;
    const nextTarget = target + amount;
    if (nextTarget < target) return false;
    if (this.balances.set(to, nextTarget) < 0) return false;
    if (source == amount) this.balances.delete(from);
    else this.balances.set(from, source - amount);
    return true;
  }
}

export class NftLedger {
  private owners: StateMap<u64, string> = new StateMap<u64, string>(compareU64);

  mint(tokenId: u64, owner: string): bool {
    if (owner.length == 0 || this.owners.has(tokenId)) return false;
    return this.owners.set(tokenId, owner) == 0;
  }

  ownerOf(tokenId: u64): string {
    const owner = this.owners.get(tokenId);
    return owner === null ? "" : owner;
  }

  transfer(caller: string, tokenId: u64, receiver: string): bool {
    if (receiver.length == 0 || this.ownerOf(tokenId) != caller) return false;
    return this.owners.set(tokenId, receiver) >= 0;
  }

  burn(caller: string, tokenId: u64): bool {
    return this.ownerOf(tokenId) == caller && this.owners.delete(tokenId);
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
