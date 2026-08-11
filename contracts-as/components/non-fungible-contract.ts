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

class Item {
  constructor(
    public id: Amount,
    public owner: string,
    public metadataOwner: string,
    public metadataFile: string,
    public metadataHash: string,
  ) {}
}

class Approval {
  constructor(public id: Amount, public actor: string) {}
}

class CollectionState {
  name: string = "";
  symbol: string = "";
  owner: string = "";
  mintEnabled: bool = false;
  items: Array<Item> = new Array<Item>();
  approvals: Array<Approval> = new Array<Approval>();

  static decode(source: Uint8Array): CollectionState | null {
    const state = new CollectionState();
    if (source.length == 0) return state;
    const decoder = new Decoder(source);
    if (decoder.array() != 7 || decoder.u64() != 2) return null;
    state.owner = decoder.string();
    state.name = decoder.string();
    state.symbol = decoder.string();
    state.mintEnabled = decoder.boolean();
    if (decoder.array() != 2) return null;
    const itemCount = decoder.array();
    if (itemCount < 0 || itemCount > MAX_STATE_ENTRIES) return null;
    for (let index = 0; index < itemCount; ++index) {
      if (decoder.array() != 2) return null;
      const id = decoder.amount();
      const owner = decoder.string();
      if (!decoder.valid || id === null || state.itemIndex(id) >= 0) return null;
      state.items.push(new Item(id, owner, "", "", ""));
    }
    const approvalCount = decoder.array();
    if (approvalCount < 0 || approvalCount > MAX_STATE_ENTRIES) return null;
    for (let index = 0; index < approvalCount; ++index) {
      if (decoder.array() != 2) return null;
      const id = decoder.amount();
      const actor = decoder.string();
      if (!decoder.valid || id === null) return null;
      state.approvals.push(new Approval(id, actor));
    }
    const metadataCount = decoder.array();
    if (metadataCount != state.items.length) return null;
    for (let index = 0; index < metadataCount; ++index) {
      if (decoder.array() != 2) return null;
      const id = decoder.amount();
      if (id === null || decoder.array() != 3) return null;
      const itemIndex = state.itemIndex(id);
      if (itemIndex < 0) return null;
      state.items[itemIndex].metadataOwner = decoder.string();
      state.items[itemIndex].metadataFile = decoder.string();
      state.items[itemIndex].metadataHash = decoder.string();
    }
    return decoder.empty() ? state : null;
  }

  encode(): Uint8Array {
    const encoder = new Encoder();
    encoder.array(7);
    encoder.u64(2);
    encoder.string(this.owner);
    encoder.string(this.name);
    encoder.string(this.symbol);
    encoder.boolean(this.mintEnabled);
    encoder.array(2);
    encoder.array(this.items.length);
    for (let index = 0; index < this.items.length; ++index) {
      const item = this.items[index];
      encoder.array(2);
      encoder.amount(item.id);
      encoder.string(item.owner);
    }
    encoder.array(this.approvals.length);
    for (let index = 0; index < this.approvals.length; ++index) {
      encoder.array(2);
      encoder.amount(this.approvals[index].id);
      encoder.string(this.approvals[index].actor);
    }
    encoder.array(this.items.length);
    for (let index = 0; index < this.items.length; ++index) {
      const item = this.items[index];
      encoder.array(2);
      encoder.amount(item.id);
      encoder.array(3);
      encoder.string(item.metadataOwner);
      encoder.string(item.metadataFile);
      encoder.string(item.metadataHash);
    }
    return encoder.finish();
  }

  itemIndex(id: Amount): i32 {
    for (let index = 0; index < this.items.length; ++index) {
      if (this.items[index].id.equals(id)) return index;
    }
    return -1;
  }

  approval(id: Amount): string {
    for (let index = 0; index < this.approvals.length; ++index) {
      if (this.approvals[index].id.equals(id)) return this.approvals[index].actor;
    }
    return "";
  }

  insertItem(item: Item): void {
    let index = 0;
    while (index < this.items.length && this.items[index].id.lessThan(item.id)) ++index;
    this.items.push(item);
    for (let move = this.items.length - 1; move > index; --move) {
      this.items[move] = this.items[move - 1];
    }
    this.items[index] = item;
  }

  setApproval(id: Amount, actor: string): void {
    for (let index = 0; index < this.approvals.length; ++index) {
      if (!this.approvals[index].id.equals(id)) continue;
      if (actor.length == 0) this.approvals.splice(index, 1);
      else this.approvals[index].actor = actor;
      return;
    }
    if (actor.length > 0) {
      let index = 0;
      while (index < this.approvals.length && this.approvals[index].id.lessThan(id)) ++index;
      const approval = new Approval(id.clone(), actor);
      this.approvals.push(approval);
      for (let move = this.approvals.length - 1; move > index; --move) {
        this.approvals[move] = this.approvals[move - 1];
      }
      this.approvals[index] = approval;
    }
  }
}

function itemEvent(topic: string, id: Amount, actors: Array<string>): ContractEvent {
  const encoder = new Encoder();
  encoder.array(actors.length + 1);
  encoder.amount(id);
  for (let index = 0; index < actors.length; ++index) encoder.string(actors[index]);
  return new ContractEvent(topic, encoder.finish());
}

function effect(contractId: string, event: ContractEvent): ContractEffect {
  return new ContractEffect("token_delta", contractId, event.topic, event.data);
}

function parseId(argumentsData: Uint8Array): Amount | null {
  const decoder = new Decoder(argumentsData);
  if (decoder.array() != 1) return null;
  const id = decoder.amount();
  return id !== null && decoder.empty() ? id : null;
}

function unitData(): Uint8Array {
  const encoder = new Encoder();
  encoder.nil();
  return encoder.finish();
}

function receiptData(operation: string, subject: string, tokenId: Amount): Uint8Array {
  const encoder = new Encoder();
  encoder.array(3);
  encoder.string(operation);
  encoder.string(subject);
  encoder.amount(tokenId);
  return encoder.finish();
}

export class StandardNonFungibleContract implements Contract {
  constructor(
    private configuredName: string = "",
    private configuredSymbol: string = "",
  ) {}

  invoke(request: InvokeRequest): InvokeResponse {
    const state = CollectionState.decode(request.state);
    if (state === null) return InvokeResponse.failure(request.state, "Invalid collection state");
    const response = this.handle(request, state);
    if (response.ok) response.state = state.encode();
    return response;
  }

  private handle(request: InvokeRequest, state: CollectionState): InvokeResponse {
    if (request.method == "init") return this.init(request, state);
    if (request.method == "mint") return this.mint(request, state);
    if (request.method == "approve") return this.approve(request, state);
    if (request.method == "transfer") return this.transferCall(request, state);
    if (request.method == "transfer_from") return this.transferFrom(request, state);
    if (request.method == "burn") return this.burn(request, state);
    if (request.method == "owner_of") return this.ownerOf(request, state);
    if (request.method == "metadata_of") return this.metadataOf(request, state);
    if (request.method == "revoke_mint") {
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
    if (request.method == "authorize_upgrade") {
      const decoder = new Decoder(request.argumentsData);
      if (decoder.array() != 1) {
        return InvokeResponse.failure(request.state, "Only the owner can update the collection");
      }
      const moduleHash = decoder.string();
      if (moduleHash.length != 64 || !decoder.empty() || request.caller != state.owner) {
        return InvokeResponse.failure(request.state, "Only the owner can update the collection");
      }
      const response = InvokeResponse.success(request.state);
      response.data = unitData();
      return response;
    }
    if (request.method == "migrate") {
      const decoder = new Decoder(request.argumentsData);
      if (decoder.array() != 0 || !decoder.empty() || request.caller != state.owner) {
        return InvokeResponse.failure(request.state, "Invalid collection migration");
      }
      const response = InvokeResponse.success(request.state);
      response.data = unitData();
      return response;
    }
    return InvokeResponse.failure(request.state, "Unknown collection method");
  }

  private init(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    const configured = this.configuredName.length > 0;
    if (state.owner.length != 0 || request.caller.length == 0
        || decoder.array() != (configured ? 0 : 2)) {
      return InvokeResponse.failure(request.state, "Collection is already initialized");
    }
    const name = configured ? this.configuredName : decoder.string();
    const symbol = configured ? this.configuredSymbol : decoder.string();
    if (name.length == 0 || name.length > MAX_NAME_BYTES || symbol.length == 0
        || symbol.length > MAX_SYMBOL_BYTES || !decoder.empty()) {
      return InvokeResponse.failure(request.state, "Invalid collection metadata");
    }
    state.name = name;
    state.symbol = symbol;
    state.owner = request.caller;
    state.mintEnabled = true;
    const response = InvokeResponse.success(request.state);
    response.data = unitData();
    return response;
  }

  private mint(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (request.caller != state.owner || !state.mintEnabled || decoder.array() != 5) {
      return InvokeResponse.failure(request.state, "Mint is not allowed");
    }
    const id = decoder.amount();
    const receiver = decoder.string();
    const metadataOwner = decoder.string();
    const metadataFile = decoder.string();
    const metadataHash = decoder.string();
    if (id === null || receiver.length == 0 || metadataOwner.length == 0 || metadataFile.length == 0
        || metadataHash.length != 64 || !decoder.empty() || state.itemIndex(id) >= 0) {
      return InvokeResponse.failure(request.state, "NFT metadata is not verified");
    }
    let verified = false;
    for (let index = 0; index < request.verified.dfs.length; ++index) {
      const proof = request.verified.dfs[index];
      if (proof.ownerId == metadataOwner && proof.fileId == metadataFile && proof.contentHash == metadataHash) {
        verified = true;
        break;
      }
    }
    if (!verified) return InvokeResponse.failure(request.state, "NFT metadata is not verified");
    state.insertItem(new Item(id, receiver, metadataOwner, metadataFile, metadataHash));
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("nft_mint", receiver, id);
    const created = itemEvent("nft_mint", id, [receiver]);
    response.events.push(created);
    response.effects.push(effect(request.contractId, created));
    return response;
  }

  private approve(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 2) return InvokeResponse.failure(request.state, "Invalid approval");
    const id = decoder.amount();
    const actor = decoder.string();
    if (id === null || actor.length == 0 || actor == request.caller || !decoder.empty()) {
      return InvokeResponse.failure(request.state, "Approval is not allowed");
    }
    const index = state.itemIndex(id);
    if (index < 0 || state.items[index].owner != request.caller) return InvokeResponse.failure(request.state, "Approval is not allowed");
    state.setApproval(id, actor);
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("nft_approval", actor, id);
    response.events.push(itemEvent("nft_approval", id, [actor]));
    return response;
  }

  private move(state: CollectionState, caller: string, owner: string, receiver: string, id: Amount): bool {
    const index = state.itemIndex(id);
    if (index < 0 || receiver.length == 0 || receiver == owner || state.items[index].owner != owner
        || (caller != owner && state.approval(id) != caller)) return false;
    state.items[index].owner = receiver;
    state.setApproval(id, "");
    return true;
  }

  private transferCall(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 2) return InvokeResponse.failure(request.state, "Invalid transfer");
    const id = decoder.amount();
    const receiver = decoder.string();
    if (id === null || !decoder.empty()) return InvokeResponse.failure(request.state, "Invalid transfer");
    const index = state.itemIndex(id);
    if (index < 0) return InvokeResponse.failure(request.state, "Item does not exist");
    const owner = state.items[index].owner;
    if (!this.move(state, request.caller, owner, receiver, id)) return InvokeResponse.failure(request.state, "Transfer is not allowed");
    return this.transferResponse(request, id, owner, receiver);
  }

  private transferFrom(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 3) return InvokeResponse.failure(request.state, "Invalid transfer");
    const id = decoder.amount();
    const owner = decoder.string();
    const receiver = decoder.string();
    if (id === null || !decoder.empty() || !this.move(state, request.caller, owner, receiver, id)) {
      return InvokeResponse.failure(request.state, "Transfer is not allowed");
    }
    return this.transferResponse(request, id, owner, receiver);
  }

  private transferResponse(request: InvokeRequest, id: Amount, owner: string, receiver: string): InvokeResponse {
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("nft_transfer", receiver, id);
    const moved = itemEvent("nft_transfer", id, [owner, receiver]);
    response.events.push(moved);
    response.effects.push(effect(request.contractId, moved));
    return response;
  }

  private burn(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const id = parseId(request.argumentsData);
    if (id === null) return InvokeResponse.failure(request.state, "Invalid item ID");
    const index = state.itemIndex(id);
    if (index < 0) return InvokeResponse.failure(request.state, "Item does not exist");
    const owner = state.items[index].owner;
    if (request.caller != owner && state.approval(id) != request.caller) return InvokeResponse.failure(request.state, "Burn is not allowed");
    state.items.splice(index, 1);
    state.setApproval(id, "");
    const response = InvokeResponse.success(request.state);
    response.data = receiptData("nft_burn", owner, id);
    const burned = itemEvent("nft_burn", id, [owner]);
    response.events.push(burned);
    response.effects.push(effect(request.contractId, burned));
    return response;
  }

  private ownerOf(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const id = parseId(request.argumentsData);
    if (id === null) return InvokeResponse.failure(request.state, "Invalid item ID");
    const index = state.itemIndex(id);
    if (index < 0) return InvokeResponse.failure(request.state, "Item does not exist");
    const encoder = new Encoder();
    encoder.string(state.items[index].owner);
    const response = InvokeResponse.success(request.state);
    response.data = encoder.finish();
    return response;
  }

  private metadataOf(request: InvokeRequest, state: CollectionState): InvokeResponse {
    const id = parseId(request.argumentsData);
    if (id === null) return InvokeResponse.failure(request.state, "Invalid item ID");
    const index = state.itemIndex(id);
    if (index < 0) return InvokeResponse.failure(request.state, "Item does not exist");
    const item = state.items[index];
    const encoder = new Encoder();
    encoder.array(3);
    encoder.string(item.metadataOwner);
    encoder.string(item.metadataFile);
    encoder.string(item.metadataHash);
    const response = InvokeResponse.success(request.state);
    response.data = encoder.finish();
    return response;
  }
}
