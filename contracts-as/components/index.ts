import {
  ContractEffect,
  DagProof,
  Decoder,
  DfsProof,
  Encoder,
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
  private members: Map<string, Set<string>> = new Map<string, Set<string>>();

  grant(role: string, actor: string): bool {
    if (role.length == 0 || actor.length == 0) return false;
    if (!this.members.has(role)) this.members.set(role, new Set<string>());
    this.members.get(role).add(actor);
    return true;
  }

  revoke(role: string, actor: string): bool {
    return this.members.has(role) && this.members.get(role).delete(actor);
  }

  has(role: string, actor: string): bool {
    return this.members.has(role) && this.members.get(role).has(actor);
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
  private consumed: Set<string> = new Set<string>();

  consume(operationId: string): bool {
    if (operationId.length == 0 || this.consumed.has(operationId)) return false;
    this.consumed.add(operationId);
    return true;
  }
}

export class SectionTimelock {
  constructor(public unlockSection: u64) {}
  unlocked(section: u64): bool { return section >= this.unlockSection; }
}

export class FungibleLedger {
  private balances: Map<string, u64> = new Map<string, u64>();
  totalSupply: u64 = 0;

  balanceOf(actor: string): u64 {
    return this.balances.has(actor) ? this.balances.get(actor) : 0;
  }

  mint(actor: string, amount: u64): bool {
    if (actor.length == 0 || amount == 0) return false;
    const balance = this.balanceOf(actor);
    const nextBalance = balance + amount;
    const nextSupply = this.totalSupply + amount;
    if (nextBalance < balance || nextSupply < this.totalSupply) return false;
    this.balances.set(actor, nextBalance);
    this.totalSupply = nextSupply;
    return true;
  }

  burn(actor: string, amount: u64): bool {
    const balance = this.balanceOf(actor);
    if (actor.length == 0 || amount == 0 || balance < amount) return false;
    this.balances.set(actor, balance - amount);
    this.totalSupply -= amount;
    return true;
  }

  transfer(from: string, to: string, amount: u64): bool {
    const source = this.balanceOf(from);
    const target = this.balanceOf(to);
    if (from.length == 0 || to.length == 0 || from == to || amount == 0 || source < amount) return false;
    const nextTarget = target + amount;
    if (nextTarget < target) return false;
    this.balances.set(from, source - amount);
    this.balances.set(to, nextTarget);
    return true;
  }
}

export class NftLedger {
  private owners: Map<u64, string> = new Map<u64, string>();

  mint(tokenId: u64, owner: string): bool {
    if (owner.length == 0 || this.owners.has(tokenId)) return false;
    this.owners.set(tokenId, owner);
    return true;
  }

  ownerOf(tokenId: u64): string {
    return this.owners.has(tokenId) ? this.owners.get(tokenId) : "";
  }

  transfer(caller: string, tokenId: u64, receiver: string): bool {
    if (receiver.length == 0 || this.ownerOf(tokenId) != caller) return false;
    this.owners.set(tokenId, receiver);
    return true;
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
  private signers: Set<string> = new Set<string>();

  constructor(signers: Array<string>, public threshold: i32) {
    for (let index = 0; index < signers.length; ++index) this.signers.add(signers[index]);
  }

  valid(): bool {
    return this.threshold > 0 && this.threshold <= this.signers.size;
  }

  approved(approvals: Array<string>): bool {
    if (!this.valid()) return false;
    const accepted = new Set<string>();
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

export function requireDfsFile(proofs: Array<DfsProof>, fileId: string, contentHash: string = ""): bool {
  for (let index = 0; index < proofs.length; ++index) {
    if (proofs[index].fileId == fileId
        && (contentHash.length == 0 || proofs[index].contentHash == contentHash)) return true;
  }
  return false;
}

export class DfsBinding {
  constructor(public fileId: string, public contentHash: string) {}
}

export class DfsBindings {
  private bindings: Map<string, DfsBinding> = new Map<string, DfsBinding>();

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
    const keys = this.bindings.keys();
    encoder.array(keys.length);
    for (let index = 0; index < keys.length; ++index) {
      const binding = this.bindings.get(keys[index]);
      encoder.array(3);
      encoder.string(keys[index]);
      encoder.string(binding.fileId);
      encoder.string(binding.contentHash);
    }
    return encoder.finish();
  }

  get(logicalKey: string): DfsBinding | null {
    return this.bindings.has(logicalKey) ? this.bindings.get(logicalKey) : null;
  }

  bind(logicalKey: string, fileId: string, contentHash: string, previousContentHash: string): bool {
    if (!validLogicalKey(logicalKey) || fileId.length == 0 || !validContentHash(contentHash)) {
      return false;
    }
    const previous = this.get(logicalKey);
    const currentHash = previous === null ? "" : previous.contentHash;
    if (currentHash != previousContentHash) return false;
    this.bindings.set(logicalKey, new DfsBinding(fileId, contentHash));
    return true;
  }

  tombstone(logicalKey: string, previousContentHash: string): bool {
    const previous = this.get(logicalKey);
    if (previous === null || previous.contentHash != previousContentHash) return false;
    return this.bindings.delete(logicalKey);
  }
}

function validLogicalKey(value: string): bool {
  if (value.length == 0 || value.length > 128 || value.startsWith("/") || value.endsWith("/")) return false;
  if (value == ".." || value.startsWith("../") || value.endsWith("/..") || value.includes("/../")) return false;
  for (let index = 0; index < value.length; ++index) {
    const character = value.charCodeAt(index);
    const valid = (character >= 48 && character <= 57)
      || (character >= 65 && character <= 90)
      || (character >= 97 && character <= 122)
      || character == 45
      || character == 46
      || character == 47
      || character == 95;
    if (!valid) return false;
  }
  return true;
}

function validContentHash(value: string): bool {
  if (value.length != 64) return false;
  for (let index = 0; index < value.length; ++index) {
    const character = value.charCodeAt(index);
    if (!((character >= 48 && character <= 57) || (character >= 97 && character <= 102))) return false;
  }
  return true;
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
): ContractEffect {
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
): ContractEffect {
  return new ContractEffect(
    "dfs_write",
    ownerId,
    "tombstone",
    bindingArguments(logicalKey, "", "", previousContentHash),
  );
}
