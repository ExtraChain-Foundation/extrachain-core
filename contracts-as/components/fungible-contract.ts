import {
  Amount,
  Contract,
  ContractEffect,
  ContractEvent,
  Decoder,
  Encoder,
  InvokeRequest,
  InvokeResponse,
} from "../sdk/index";

const MAX_NAME_BYTES: i32 = 64;
const MAX_SYMBOL_BYTES: i32 = 12;
const MAX_STATE_ENTRIES: i32 = 16384;

class BalanceEntry {
  constructor(public actor: string, public amount: Amount) {}
}

class AllowanceEntry {
  constructor(public owner: string, public spender: string, public amount: Amount) {}
}

class TokenState {
  name: string = "";
  symbol: string = "";
  decimals: u8 = 0;
  owner: string = "";
  mintEnabled: bool = false;
  totalSupply: Amount = Amount.zero();
  balances: Array<BalanceEntry> = new Array<BalanceEntry>();
  allowances: Array<AllowanceEntry> = new Array<AllowanceEntry>();
  freezeLastEnabled: bool = false;
  freezeLastUnit: Amount = Amount.one();
  locked: Array<BalanceEntry> = new Array<BalanceEntry>();

  static decode(source: Uint8Array): TokenState | null {
    const state = new TokenState();
    if (source.length == 0) return state;
    const decoder = new Decoder(source);
    if (decoder.array() != 8 || decoder.u64() != 2) return null;
    state.owner = decoder.string();
    state.name = decoder.string();
    state.symbol = decoder.string();
    const decimals = decoder.u64();
    state.mintEnabled = decoder.boolean();
    if (decoder.array() != 4) return null;
    if (!decoder.valid || decimals > 255) return null;
    state.decimals = <u8>decimals;

    const balanceCount = decoder.array();
    if (balanceCount < 0 || balanceCount > MAX_STATE_ENTRIES) return null;
    for (let index = 0; index < balanceCount; ++index) {
      if (decoder.array() != 2) return null;
      const actor = decoder.string();
      const amount = decoder.amount();
      if (!decoder.valid || amount === null) return null;
      state.balances.push(new BalanceEntry(actor, amount));
    }

    const allowanceCount = decoder.array();
    if (allowanceCount < 0 || allowanceCount > MAX_STATE_ENTRIES) return null;
    for (let index = 0; index < allowanceCount; ++index) {
      if (decoder.array() != 2 || decoder.array() != 2) return null;
      const owner = decoder.string();
      const spender = decoder.string();
      const amount = decoder.amount();
      if (!decoder.valid || amount === null) return null;
      state.allowances.push(new AllowanceEntry(owner, spender, amount));
    }

    const lockedCount = decoder.array();
    if (lockedCount < 0 || lockedCount > MAX_STATE_ENTRIES) return null;
    for (let index = 0; index < lockedCount; ++index) {
      if (decoder.array() != 2) return null;
      const actor = decoder.string();
      const amount = decoder.amount();
      if (!decoder.valid || amount === null) return null;
      state.locked.push(new BalanceEntry(actor, amount));
    }
    const supply = decoder.amount();
    if (supply === null || decoder.array() != 2) return null;
    state.totalSupply = supply;
    const freezeLastUnit = decoder.amount();
    if (freezeLastUnit === null) return null;
    state.freezeLastUnit = freezeLastUnit;
    state.freezeLastEnabled = decoder.boolean();
    return decoder.empty() ? state : null;
  }

  encode(): Uint8Array {
    const encoder = new Encoder();
    encoder.array(8);
    encoder.u64(2);
    encoder.string(this.owner);
    encoder.string(this.name);
    encoder.string(this.symbol);
    encoder.u64(this.decimals);
    encoder.boolean(this.mintEnabled);
    encoder.array(4);
    encoder.array(this.balances.length);
    for (let index = 0; index < this.balances.length; ++index) {
      encoder.array(2);
      encoder.string(this.balances[index].actor);
      encoder.amount(this.balances[index].amount);
    }
    encoder.array(this.allowances.length);
    for (let index = 0; index < this.allowances.length; ++index) {
      encoder.array(2);
      encoder.array(2);
      encoder.string(this.allowances[index].owner);
      encoder.string(this.allowances[index].spender);
      encoder.amount(this.allowances[index].amount);
    }
    encoder.array(this.locked.length);
    for (let index = 0; index < this.locked.length; ++index) {
      encoder.array(2);
      encoder.string(this.locked[index].actor);
      encoder.amount(this.locked[index].amount);
    }
    encoder.amount(this.totalSupply);
    encoder.array(2);
    encoder.amount(this.freezeLastUnit);
    encoder.boolean(this.freezeLastEnabled);
    return encoder.finish();
  }

  balance(actor: string): Amount {
    for (let index = 0; index < this.balances.length; ++index) {
      if (this.balances[index].actor == actor) return this.balances[index].amount.clone();
    }
    return Amount.zero();
  }

  setBalance(actor: string, amount: Amount): void {
    for (let index = 0; index < this.balances.length; ++index) {
      if (this.balances[index].actor != actor) continue;
      if (amount.isZero()) this.balances.splice(index, 1);
      else this.balances[index].amount = amount;
      return;
    }
    if (!amount.isZero()) {
      let index = 0;
      while (index < this.balances.length && this.balances[index].actor < actor) ++index;
      this.balances.push(new BalanceEntry(actor, amount));
      for (let move = this.balances.length - 1; move > index; --move) {
        this.balances[move] = this.balances[move - 1];
      }
      this.balances[index] = new BalanceEntry(actor, amount);
    }
  }

  allowance(owner: string, spender: string): Amount {
    for (let index = 0; index < this.allowances.length; ++index) {
      const entry = this.allowances[index];
      if (entry.owner == owner && entry.spender == spender) return entry.amount.clone();
    }
    return Amount.zero();
  }

  setAllowance(owner: string, spender: string, amount: Amount): void {
    for (let index = 0; index < this.allowances.length; ++index) {
      const entry = this.allowances[index];
      if (entry.owner != owner || entry.spender != spender) continue;
      if (amount.isZero()) this.allowances.splice(index, 1);
      else entry.amount = amount;
      return;
    }
    if (!amount.isZero()) {
      let index = 0;
      while (index < this.allowances.length) {
        const current = this.allowances[index];
        if (current.owner > owner || (current.owner == owner && current.spender >= spender)) break;
        ++index;
      }
      this.allowances.push(new AllowanceEntry(owner, spender, amount));
      for (let move = this.allowances.length - 1; move > index; --move) {
        this.allowances[move] = this.allowances[move - 1];
      }
      this.allowances[index] = new AllowanceEntry(owner, spender, amount);
    }
  }

  lockedBalance(actor: string): Amount {
    for (let index = 0; index < this.locked.length; ++index) {
      if (this.locked[index].actor == actor) return this.locked[index].amount.clone();
    }
    return Amount.zero();
  }

  setLockedBalance(actor: string, amount: Amount): void {
    for (let index = 0; index < this.locked.length; ++index) {
      if (this.locked[index].actor != actor) continue;
      if (amount.isZero()) this.locked.splice(index, 1);
      else this.locked[index].amount = amount;
      return;
    }
    if (!amount.isZero()) {
      let index = 0;
      while (index < this.locked.length && this.locked[index].actor < actor) ++index;
      this.locked.push(new BalanceEntry(actor, amount));
      for (let move = this.locked.length - 1; move > index; --move) {
        this.locked[move] = this.locked[move - 1];
      }
      this.locked[index] = new BalanceEntry(actor, amount);
    }
  }

  spendable(actor: string): Amount | null {
    return this.balance(actor).checkedSub(this.lockedBalance(actor));
  }
}

class Pair {
  constructor(public actor: string, public amount: Amount) {}
}

function parsePair(argumentsData: Uint8Array): Pair | null {
  const decoder = new Decoder(argumentsData);
  if (decoder.array() != 2) return null;
  const actor = decoder.string();
  const amount = decoder.amount();
  return actor.length > 0 && amount !== null && decoder.empty() ? new Pair(actor, amount) : null;
}

function amountData(amount: Amount): Uint8Array {
  const encoder = new Encoder();
  encoder.amount(amount);
  return encoder.finish();
}

function receiptData(operation: string, subject: string, amount: Amount): Uint8Array {
  const encoder = new Encoder();
  encoder.array(3);
  encoder.string(operation);
  encoder.string(subject);
  encoder.amount(amount);
  return encoder.finish();
}

function unitData(): Uint8Array {
  const encoder = new Encoder();
  encoder.nil();
  return encoder.finish();
}

function event(topic: string, actors: Array<string>, amounts: Array<Amount>): ContractEvent {
  const encoder = new Encoder();
  encoder.array(actors.length);
  for (let index = 0; index < actors.length; ++index) {
    encoder.array(2);
    encoder.string(actors[index]);
    encoder.amount(amounts[index]);
  }
  return new ContractEvent(topic, encoder.finish());
}

function tokenEffect(contractId: string, source: ContractEvent): ContractEffect {
  return new ContractEffect("token_delta", contractId, source.topic, source.data);
}

function unit(decimals: u8): Amount | null {
  let value: u64 = 1;
  for (let index: u8 = 0; index < decimals; ++index) {
    if (value > u64.MAX_VALUE / 10) return null;
    value *= 10;
  }
  return Amount.fromU64(value);
}

class TransferResult {
  constructor(public frozen: bool) {}
}

function transfer(state: TokenState, from: string, to: string, amount: Amount): TransferResult | null {
  const spendable = state.spendable(from);
  if (amount.isZero() || from == to || spendable === null || spendable.lessThan(amount)) return null;
  const target = state.balance(to).checkedAdd(amount);
  const source = state.balance(from).checkedSub(amount);
  if (target === null || source === null) return null;
  state.setBalance(from, source);
  state.setBalance(to, target);
  const frozen = state.freezeLastEnabled
    && state.balance(from).equals(state.freezeLastUnit)
    && state.lockedBalance(from).isZero();
  if (frozen) state.setLockedBalance(from, state.freezeLastUnit);
  return new TransferResult(frozen);
}

export class StandardFungibleContract implements Contract {
  constructor(
    private configuredName: string = "",
    private configuredSymbol: string = "",
    private configuredDecimals: u8 = 0,
    private configuredFreezeLastUnit: bool = false,
  ) {}

  invoke(request: InvokeRequest): InvokeResponse {
    const state = TokenState.decode(request.state);
    if (state === null) return InvokeResponse.failure(request.state, "Invalid token state");
    const response = this.handle(request, state);
    if (!response.ok) return response;
    response.state = state.encode();
    return response;
  }

  private handle(request: InvokeRequest, state: TokenState): InvokeResponse {
    if (request.method == "init") return this.init(request, state);
    if (request.method == "transfer") return this.transferCall(request, state);
    if (request.method == "approve") return this.approve(request, state);
    if (request.method == "transfer_from") return this.transferFrom(request, state);
    if (request.method == "mint") return this.mint(request, state);
    if (request.method == "revoke_mint") return this.revokeMint(request, state);
    if (request.method == "burn") return this.burn(request, state);
    if (request.method == "balance_of") return this.balanceOf(request, state);
    if (request.method == "allowance") return this.allowance(request, state);
    if (request.method == "locked_balance_of") return this.lockedBalanceOf(request, state);
    if (request.method == "authorize_upgrade") {
      const decoder = new Decoder(request.argumentsData);
      if (decoder.array() != 1) {
        return InvokeResponse.failure(request.state, "Invalid contract arguments");
      }
      const moduleHash = decoder.string();
      if (moduleHash.length != 64 || !decoder.empty()) {
        return InvokeResponse.failure(request.state, "Invalid contract arguments");
      }
      if (request.caller != state.owner) {
        return InvokeResponse.failure(request.state, "Only the owner can update the token");
      }
      const response = InvokeResponse.success(request.state);
      response.data = unitData();
      return response;
    }
    if (request.method == "migrate") return this.migrate(request, state);
    return InvokeResponse.failure(request.state, "Unknown token method");
  }

  private init(request: InvokeRequest, state: TokenState): InvokeResponse {
    if (state.owner.length != 0 || request.caller.length == 0) {
      return InvokeResponse.failure(request.state, "Token is already initialized");
    }
    const decoder = new Decoder(request.argumentsData);
    const configured = this.configuredName.length > 0;
    if (decoder.array() != (configured ? 2 : 5)) {
      return InvokeResponse.failure(request.state, "Invalid init arguments");
    }
    const name = configured ? this.configuredName : decoder.string();
    const symbol = configured ? this.configuredSymbol : decoder.string();
    const decimals = configured ? <u64>this.configuredDecimals : decoder.u64();
    const supply = decoder.amount();
    if (!decoder.valid || supply === null || name.length == 0 || name.length > MAX_NAME_BYTES
        || symbol.length == 0 || symbol.length > MAX_SYMBOL_BYTES || decimals > 18) {
      return InvokeResponse.failure(request.state, "Invalid token metadata");
    }
    state.name = name;
    state.symbol = symbol;
    state.decimals = <u8>decimals;
    state.owner = request.caller;
    state.mintEnabled = true;
    const whole = unit(state.decimals);
    if (whole === null) return InvokeResponse.failure(request.state, "Invalid token decimals");
    state.freezeLastUnit = whole;
    state.freezeLastEnabled = this.configuredFreezeLastUnit;
    state.totalSupply = supply;
    const response = InvokeResponse.success(request.state);
    const count = decoder.array();
    if (count < 0 || count > MAX_STATE_ENTRIES) return InvokeResponse.failure(request.state, "Invalid migration balances");
    response.data = receiptData(count > 0 ? "migrated" : "mint", request.caller, supply);
    if (count > 0) {
      let migrated = Amount.zero();
      for (let index = 0; index < count; ++index) {
        if (decoder.array() != 2) return InvokeResponse.failure(request.state, "Invalid migration balance");
        const actor = decoder.string();
        const amount = decoder.amount();
        if (actor.length == 0 || amount === null || amount.isZero() || !state.balance(actor).isZero()) {
          return InvokeResponse.failure(request.state, "Invalid migration balance");
        }
        const next = migrated.checkedAdd(amount);
        if (next === null) return InvokeResponse.failure(request.state, "Migration supply overflow");
        migrated = next;
        state.setBalance(actor, amount);
      }
      if (!decoder.empty() || !migrated.equals(supply)) return InvokeResponse.failure(request.state, "Migration supply does not match balances");
      response.events.push(new ContractEvent("migrated", unitData()));
      return response;
    }
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Invalid init arguments");
    state.setBalance(request.caller, supply);
    const minted = event("mint", [request.caller], [supply]);
    response.events.push(minted);
    response.effects.push(tokenEffect(request.contractId, minted));
    return response;
  }

  private transferCall(request: InvokeRequest, state: TokenState): InvokeResponse {
    const pair = parsePair(request.argumentsData);
    if (pair === null) return InvokeResponse.failure(request.state, "Invalid arguments");
    const result = transfer(state, request.caller, pair.actor, pair.amount);
    if (result === null) return InvokeResponse.failure(request.state, "Transfer is not allowed");
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("transfer", pair.actor, pair.amount);
    const moved = event("transfer", [request.caller, pair.actor], [pair.amount, pair.amount]);
    response.events.push(moved);
    response.effects.push(tokenEffect(request.contractId, moved));
    if (result.frozen) {
      const locked = event("lock", [request.caller], [state.lockedBalance(request.caller)]);
      response.events.push(locked);
      response.effects.push(tokenEffect(request.contractId, locked));
    }
    return response;
  }

  private approve(request: InvokeRequest, state: TokenState): InvokeResponse {
    const pair = parsePair(request.argumentsData);
    if (pair === null || pair.actor == request.caller) return InvokeResponse.failure(request.state, "Self approval is not allowed");
    state.setAllowance(request.caller, pair.actor, pair.amount);
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("approval", pair.actor, pair.amount);
    response.events.push(event("approval", [pair.actor], [pair.amount]));
    return response;
  }

  private transferFrom(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 3) return InvokeResponse.failure(request.state, "Invalid arguments");
    const owner = decoder.string();
    const receiver = decoder.string();
    const amount = decoder.amount();
    if (amount === null || !decoder.empty() || state.allowance(owner, request.caller).lessThan(amount)) {
      return InvokeResponse.failure(request.state, "Allowance is too small");
    }
    const result = transfer(state, owner, receiver, amount);
    if (result === null) return InvokeResponse.failure(request.state, "Transfer is not allowed");
    const allowance = state.allowance(owner, request.caller).checkedSub(amount);
    if (allowance === null) return InvokeResponse.failure(request.state, "Allowance is too small");
    state.setAllowance(owner, request.caller, allowance);
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("transfer", receiver, amount);
    const moved = event("transfer", [owner, receiver], [amount, amount]);
    response.events.push(moved);
    response.effects.push(tokenEffect(request.contractId, moved));
    if (result.frozen) {
      const locked = event("lock", [owner], [state.lockedBalance(owner)]);
      response.events.push(locked);
      response.effects.push(tokenEffect(request.contractId, locked));
    }
    return response;
  }

  private mint(request: InvokeRequest, state: TokenState): InvokeResponse {
    const pair = parsePair(request.argumentsData);
    if (pair === null || request.caller != state.owner || !state.mintEnabled || pair.amount.isZero()) {
      return InvokeResponse.failure(request.state, "Mint is not allowed");
    }
    const supply = state.totalSupply.checkedAdd(pair.amount);
    const balance = state.balance(pair.actor).checkedAdd(pair.amount);
    if (supply === null || balance === null) return InvokeResponse.failure(request.state, "Supply overflow");
    state.totalSupply = supply;
    state.setBalance(pair.actor, balance);
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("mint", pair.actor, pair.amount);
    const minted = event("mint", [pair.actor], [pair.amount]);
    response.events.push(minted);
    response.effects.push(tokenEffect(request.contractId, minted));
    return response;
  }

  private revokeMint(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 0 || !decoder.empty() || request.caller != state.owner || !state.mintEnabled) {
      return InvokeResponse.failure(request.state, "Mint control is not available");
    }
    state.mintEnabled = false;
    const response = InvokeResponse.success(request.state);
    response.data = unitData();
    response.events.push(new ContractEvent("mint_revoked", unitData()));
    return response;
  }

  private burn(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 1) return InvokeResponse.failure(request.state, "Invalid arguments");
    const amount = decoder.amount();
    const spendable = state.spendable(request.caller);
    if (amount === null || amount.isZero() || !decoder.empty() || spendable === null || spendable.lessThan(amount)) {
      return InvokeResponse.failure(request.state, "Burn is not allowed");
    }
    const balance = state.balance(request.caller).checkedSub(amount);
    const supply = state.totalSupply.checkedSub(amount);
    if (balance === null || supply === null) return InvokeResponse.failure(request.state, "Burn is not allowed");
    state.setBalance(request.caller, balance);
    state.totalSupply = supply;
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("burn", request.caller, amount);
    const burned = event("burn", [request.caller], [amount]);
    response.events.push(burned);
    response.effects.push(tokenEffect(request.contractId, burned));
    return response;
  }

  private balanceOf(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 1) return InvokeResponse.failure(request.state, "Invalid arguments");
    const actor = decoder.string();
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Invalid arguments");
    const response = InvokeResponse.success(request.state);
    response.data = amountData(state.balance(actor));
    return response;
  }

  private allowance(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 2) return InvokeResponse.failure(request.state, "Invalid arguments");
    const owner = decoder.string();
    const spender = decoder.string();
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Invalid arguments");
    const response = InvokeResponse.success(request.state);
    response.data = amountData(state.allowance(owner, spender));
    return response;
  }

  private lockedBalanceOf(request: InvokeRequest, state: TokenState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 1) return InvokeResponse.failure(request.state, "Invalid arguments");
    const actor = decoder.string();
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Invalid arguments");
    const response = InvokeResponse.success(request.state);
    response.data = amountData(state.lockedBalance(actor));
    return response;
  }

  private migrate(request: InvokeRequest, state: TokenState): InvokeResponse {
    if (request.caller != state.owner) return InvokeResponse.failure(request.state, "Only the owner can update the token");
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 0) return InvokeResponse.failure(request.state, "Invalid freeze policy");
    state.freezeLastEnabled = true;
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Invalid freeze policy");
    const response = InvokeResponse.success(request.state);
    response.data = unitData();
    return response;
  }
}
