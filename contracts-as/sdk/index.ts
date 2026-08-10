import { u128 } from "as-bignum/assembly/integer/u128";

export const ABI_VERSION: u64 = 3;
export const MAX_PROOFS: i32 = 64;
export const MAX_U128_DECIMAL = "340282366920938463463374607431768211455";

export class Amount {
  private constructor(public value: u128) {}

  static zero(): Amount { return new Amount(u128.Zero); }
  static one(): Amount { return new Amount(u128.One); }
  static fromU64(value: u64): Amount { return new Amount(u128.fromU64(value)); }

  static parse(value: string): Amount | null {
    if (value.length == 0 || value.length > MAX_U128_DECIMAL.length) return null;
    if (value.length > 1 && value.charCodeAt(0) == 48) return null;
    for (let index = 0; index < value.length; ++index) {
      const digit = value.charCodeAt(index);
      if (digit < 48 || digit > 57) return null;
    }
    if (value.length == MAX_U128_DECIMAL.length && value > MAX_U128_DECIMAL) return null;
    return new Amount(u128.fromString(value));
  }

  clone(): Amount { return new Amount(this.value.clone()); }
  isZero(): bool { return this.value.isZero(); }
  equals(other: Amount): bool { return this.value == other.value; }
  lessThan(other: Amount): bool { return this.value < other.value; }
  greaterThan(other: Amount): bool { return this.value > other.value; }

  checkedAdd(other: Amount): Amount | null {
    const value = this.value + other.value;
    return value < this.value ? null : new Amount(value);
  }

  checkedSub(other: Amount): Amount | null {
    return this.value < other.value ? null : new Amount(this.value - other.value);
  }

  toString(): string { return this.value.toString(); }
}

export class Decoder {
  private source: Uint8Array;
  private offset: i32 = 0;
  valid: bool = true;

  constructor(source: Uint8Array) {
    this.source = source;
  }

  private byte(): u8 {
    if (this.offset >= this.source.length) {
      this.valid = false;
      return 0;
    }
    return this.source[this.offset++];
  }

  private peek(): u8 {
    if (this.offset >= this.source.length) {
      this.valid = false;
      return 0;
    }
    return this.source[this.offset];
  }

  private readBigEndian(length: i32): u64 {
    let value: u64 = 0;
    if (length < 0 || this.offset + length > this.source.length) {
      this.valid = false;
      return 0;
    }
    for (let index = 0; index < length; ++index) {
      value = (value << 8) | <u64>this.byte();
    }
    return value;
  }

  array(): i32 {
    const marker = this.byte();
    if ((marker & 0xf0) == 0x90) return <i32>(marker & 0x0f);
    if (marker == 0xdc) return <i32>this.readBigEndian(2);
    if (marker == 0xdd) {
      const value = this.readBigEndian(4);
      if (value > <u64>i32.MAX_VALUE) this.valid = false;
      return <i32>value;
    }
    this.valid = false;
    return 0;
  }

  boolean(): bool {
    const marker = this.byte();
    if (marker == 0xc2) return false;
    if (marker == 0xc3) return true;
    this.valid = false;
    return false;
  }

  u64(): u64 {
    const marker = this.byte();
    if (marker <= 0x7f) return marker;
    if (marker == 0xcc) return this.readBigEndian(1);
    if (marker == 0xcd) return this.readBigEndian(2);
    if (marker == 0xce) return this.readBigEndian(4);
    if (marker == 0xcf) return this.readBigEndian(8);
    this.valid = false;
    return 0;
  }

  amount(): Amount | null {
    const marker = this.peek();
    if (!this.valid) return null;
    if ((marker & 0xe0) == 0xa0 || marker == 0xd9 || marker == 0xda || marker == 0xdb) {
      const value = this.string();
      return this.valid ? Amount.parse(value) : null;
    }
    const value = this.u64();
    return this.valid ? Amount.fromU64(value) : null;
  }

  private length(marker: u8, fixedMask: u8, fixedValue: u8): i32 {
    if ((marker & fixedMask) == fixedValue) return <i32>(marker & ~fixedMask);
    if (marker == 0xd9 || marker == 0xc4) return <i32>this.readBigEndian(1);
    if (marker == 0xda || marker == 0xc5) return <i32>this.readBigEndian(2);
    if (marker == 0xdb || marker == 0xc6) {
      const value = this.readBigEndian(4);
      if (value > <u64>i32.MAX_VALUE) this.valid = false;
      return <i32>value;
    }
    this.valid = false;
    return 0;
  }

  string(): string {
    const marker = this.byte();
    const length = this.length(marker, 0xe0, 0xa0);
    if (!this.valid || length < 0 || this.offset + length > this.source.length) {
      this.valid = false;
      return "";
    }
    const buffer = new ArrayBuffer(length);
    const bytes = Uint8Array.wrap(buffer);
    for (let index = 0; index < length; ++index) bytes[index] = this.byte();
    return String.UTF8.decode(buffer, false);
  }

  bytes(): Uint8Array {
    const marker = this.byte();
    let length: i32 = 0;
    if (marker == 0xc4) length = <i32>this.readBigEndian(1);
    else if (marker == 0xc5) length = <i32>this.readBigEndian(2);
    else if (marker == 0xc6) {
      const value = this.readBigEndian(4);
      if (value > <u64>i32.MAX_VALUE) this.valid = false;
      length = <i32>value;
    } else {
      this.valid = false;
    }
    if (!this.valid || length < 0 || this.offset + length > this.source.length) {
      this.valid = false;
      return new Uint8Array(0);
    }
    const result = new Uint8Array(length);
    for (let index = 0; index < length; ++index) result[index] = this.byte();
    return result;
  }

  empty(): bool {
    return this.valid && this.offset == this.source.length;
  }
}

export class Encoder {
  private output: Array<u8> = new Array<u8>();

  private byte(value: u8): void {
    this.output.push(value);
  }

  private bigEndian(value: u64, length: i32): void {
    for (let shift = length - 1; shift >= 0; --shift) {
      this.byte(<u8>(value >> (<u64>shift * 8)));
    }
  }

  array(length: i32): void {
    if (length >= 0 && length <= 15) {
      this.byte(<u8>(0x90 | length));
    } else if (length <= 0xffff) {
      this.byte(0xdc);
      this.bigEndian(<u64>length, 2);
    } else {
      this.byte(0xdd);
      this.bigEndian(<u64>length, 4);
    }
  }

  boolean(value: bool): void {
    this.byte(value ? 0xc3 : 0xc2);
  }

  u64(value: u64): void {
    this.byte(0xcf);
    this.bigEndian(value, 8);
  }

  amount(value: Amount): void {
    this.string(value.toString());
  }

  string(value: string): void {
    const buffer = String.UTF8.encode(value, false);
    const bytes = Uint8Array.wrap(buffer);
    const length = bytes.length;
    if (length <= 31) {
      this.byte(<u8>(0xa0 | length));
    } else if (length <= 0xff) {
      this.byte(0xd9);
      this.bigEndian(<u64>length, 1);
    } else if (length <= 0xffff) {
      this.byte(0xda);
      this.bigEndian(<u64>length, 2);
    } else {
      this.byte(0xdb);
      this.bigEndian(<u64>length, 4);
    }
    for (let index = 0; index < bytes.length; ++index) this.byte(bytes[index]);
  }

  bytes(value: Uint8Array): void {
    const length = value.length;
    if (length <= 0xff) {
      this.byte(0xc4);
      this.bigEndian(<u64>length, 1);
    } else if (length <= 0xffff) {
      this.byte(0xc5);
      this.bigEndian(<u64>length, 2);
    } else {
      this.byte(0xc6);
      this.bigEndian(<u64>length, 4);
    }
    for (let index = 0; index < value.length; ++index) this.byte(value[index]);
  }

  nil(): void {
    this.byte(0xc0);
  }

  finish(): Uint8Array {
    const result = new Uint8Array(this.output.length);
    for (let index = 0; index < this.output.length; ++index) result[index] = this.output[index];
    return result;
  }
}

export class DagProof {
  constructor(
    public transactionHash: string,
    public section: u64,
    public confirmations: u64,
  ) {}
}

export class DfsProof {
  constructor(
    public fileId: string,
    public ownerId: string,
    public contentHash: string,
  ) {}
}

export class VerifiedInputs {
  dag: Array<DagProof> = new Array<DagProof>();
  dfs: Array<DfsProof> = new Array<DfsProof>();
}

export class InvokeRequest {
  sender: string = "";
  caller: string = "";
  contractId: string = "";
  method: string = "";
  argumentsData: Uint8Array = new Uint8Array(0);
  state: Uint8Array = new Uint8Array(0);
  block: u64 = 0;
  depth: u64 = 0;
  verified: VerifiedInputs = new VerifiedInputs();

  static decode(source: Uint8Array): InvokeRequest | null {
    const decoder = new Decoder(source);
    if (decoder.array() != 6 || decoder.array() != 5) return null;
    const result = new InvokeRequest();
    result.sender = decoder.string();
    result.caller = decoder.string();
    result.contractId = decoder.string();
    result.block = decoder.u64();
    result.depth = decoder.u64();
    result.method = decoder.string();
    result.argumentsData = decoder.bytes();
    result.state = decoder.bytes();
    if (decoder.array() != 2) return null;
    const dagCount = decoder.array();
    if (dagCount < 0 || dagCount > MAX_PROOFS) return null;
    for (let index = 0; index < dagCount; ++index) {
      if (decoder.array() != 3) return null;
      result.verified.dag.push(new DagProof(decoder.string(), decoder.u64(), decoder.u64()));
    }
    const dfsCount = decoder.array();
    if (dfsCount < 0 || dfsCount > MAX_PROOFS || dagCount + dfsCount > MAX_PROOFS) return null;
    for (let index = 0; index < dfsCount; ++index) {
      if (decoder.array() != 3) return null;
      result.verified.dfs.push(new DfsProof(decoder.string(), decoder.string(), decoder.string()));
    }
    if (decoder.u64() != ABI_VERSION || !decoder.empty()) return null;
    return result;
  }
}

export class ContractEvent {
  constructor(public topic: string, public data: Uint8Array) {}
}

export class ContractEffect {
  constructor(
    public kind: string,
    public target: string,
    public operation: string,
    public argumentsData: Uint8Array,
  ) {}
}

export class InvokeResponse {
  ok: bool = true;
  state: Uint8Array = new Uint8Array(0);
  data: Uint8Array = new Uint8Array(0);
  events: Array<ContractEvent> = new Array<ContractEvent>();
  effects: Array<ContractEffect> = new Array<ContractEffect>();
  error: string | null = null;

  static success(state: Uint8Array): InvokeResponse {
    const result = new InvokeResponse();
    result.state = state;
    return result;
  }

  static failure(state: Uint8Array, error: string): InvokeResponse {
    const result = new InvokeResponse();
    result.ok = false;
    result.state = state;
    result.error = error;
    return result;
  }

  encode(): Uint8Array {
    const encoder = new Encoder();
    encoder.array(6);
    encoder.boolean(this.ok);
    encoder.bytes(this.state);
    encoder.bytes(this.data);
    encoder.array(this.events.length);
    for (let index = 0; index < this.events.length; ++index) {
      const event = this.events[index];
      encoder.array(2);
      encoder.string(event.topic);
      encoder.bytes(event.data);
    }
    encoder.array(this.effects.length);
    for (let index = 0; index < this.effects.length; ++index) {
      const effect = this.effects[index];
      encoder.array(4);
      encoder.string(effect.kind);
      encoder.string(effect.target);
      encoder.string(effect.operation);
      encoder.bytes(effect.argumentsData);
    }
    if (this.error === null) encoder.nil();
    else encoder.string(this.error!);
    return encoder.finish();
  }
}

export interface Contract {
  invoke(request: InvokeRequest): InvokeResponse;
}

let result = new Uint8Array(0);

export function runContract(contract: Contract, pointer: i32, length: i32): i32 {
  if (pointer < 0 || length < 0) {
    result = InvokeResponse.failure(new Uint8Array(0), "Invalid contract request").encode();
    return <i32>result.dataStart;
  }
  const input = new Uint8Array(length);
  for (let index = 0; index < length; ++index) input[index] = load<u8>(pointer + index);
  const request = InvokeRequest.decode(input);
  result = request === null
    ? InvokeResponse.failure(new Uint8Array(0), "Invalid contract request").encode()
    : contract.invoke(request).encode();
  return <i32>result.dataStart;
}

export function resultLength(): i32 {
  return result.length;
}

export function callContract(contractId: string, method: string, argumentsData: Uint8Array): ContractEffect {
  return new ContractEffect("contract_call", contractId, method, argumentsData);
}
