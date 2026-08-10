import { u128 } from "as-bignum/assembly/integer/u128";

export const ABI_VERSION: u64 = 4;
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

  reject(): void {
    this.valid = false;
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

  i64(): i64 {
    const marker = this.byte();
    if (marker <= 0x7f) return marker;
    if (marker >= 0xe0) return <i8>marker;
    if (marker == 0xcc) return <i64>this.readBigEndian(1);
    if (marker == 0xcd) return <i64>this.readBigEndian(2);
    if (marker == 0xce) return <i64>this.readBigEndian(4);
    if (marker == 0xcf) {
      const value = this.readBigEndian(8);
      if (value > <u64>i64.MAX_VALUE) this.valid = false;
      return <i64>value;
    }
    if (marker == 0xd0) return <i8>this.readBigEndian(1);
    if (marker == 0xd1) return <i16>this.readBigEndian(2);
    if (marker == 0xd2) return <i32>this.readBigEndian(4);
    if (marker == 0xd3) return <i64>this.readBigEndian(8);
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

  nil(): bool {
    if (this.byte() == 0xc0) return true;
    this.valid = false;
    return false;
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

  i64(value: i64): void {
    if (value >= 0) {
      this.u64(<u64>value);
      return;
    }
    this.byte(0xd3);
    this.bigEndian(<u64>value, 8);
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

export abstract class ValueCodec<T> {
  abstract defaultValue(): T;
  abstract decode(decoder: Decoder): T;
  abstract encode(encoder: Encoder, value: T): void;
}

export class EmptyValue {}

export class EmptyCodec extends ValueCodec<EmptyValue> {
  defaultValue(): EmptyValue { return new EmptyValue(); }
  decode(decoder: Decoder): EmptyValue {
    decoder.nil();
    return new EmptyValue();
  }
  encode(encoder: Encoder, _value: EmptyValue): void { encoder.nil(); }
}

export class BoolCodec extends ValueCodec<bool> {
  defaultValue(): bool { return false; }
  decode(decoder: Decoder): bool { return decoder.boolean(); }
  encode(encoder: Encoder, value: bool): void { encoder.boolean(value); }
}

export class U64Codec extends ValueCodec<u64> {
  defaultValue(): u64 { return 0; }
  decode(decoder: Decoder): u64 { return decoder.u64(); }
  encode(encoder: Encoder, value: u64): void { encoder.u64(value); }
}

export class U8Codec extends ValueCodec<u8> {
  defaultValue(): u8 { return 0; }
  decode(decoder: Decoder): u8 {
    const value = decoder.u64();
    if (value > u8.MAX_VALUE) decoder.reject();
    return <u8>value;
  }
  encode(encoder: Encoder, value: u8): void { encoder.u64(value); }
}

export class U16Codec extends ValueCodec<u16> {
  defaultValue(): u16 { return 0; }
  decode(decoder: Decoder): u16 {
    const value = decoder.u64();
    if (value > u16.MAX_VALUE) decoder.reject();
    return <u16>value;
  }
  encode(encoder: Encoder, value: u16): void { encoder.u64(value); }
}

export class U32Codec extends ValueCodec<u32> {
  defaultValue(): u32 { return 0; }
  decode(decoder: Decoder): u32 {
    const value = decoder.u64();
    if (value > u32.MAX_VALUE) decoder.reject();
    return <u32>value;
  }
  encode(encoder: Encoder, value: u32): void { encoder.u64(value); }
}

export class I64Codec extends ValueCodec<i64> {
  defaultValue(): i64 { return 0; }
  decode(decoder: Decoder): i64 { return decoder.i64(); }
  encode(encoder: Encoder, value: i64): void { encoder.i64(value); }
}

export class I8Codec extends ValueCodec<i8> {
  defaultValue(): i8 { return 0; }
  decode(decoder: Decoder): i8 {
    const value = decoder.i64();
    if (value < i8.MIN_VALUE || value > i8.MAX_VALUE) decoder.reject();
    return <i8>value;
  }
  encode(encoder: Encoder, value: i8): void { encoder.i64(value); }
}

export class I16Codec extends ValueCodec<i16> {
  defaultValue(): i16 { return 0; }
  decode(decoder: Decoder): i16 {
    const value = decoder.i64();
    if (value < i16.MIN_VALUE || value > i16.MAX_VALUE) decoder.reject();
    return <i16>value;
  }
  encode(encoder: Encoder, value: i16): void { encoder.i64(value); }
}

export class I32Codec extends ValueCodec<i32> {
  defaultValue(): i32 { return 0; }
  decode(decoder: Decoder): i32 {
    const value = decoder.i64();
    if (value < i32.MIN_VALUE || value > i32.MAX_VALUE) decoder.reject();
    return <i32>value;
  }
  encode(encoder: Encoder, value: i32): void { encoder.i64(value); }
}

export class AmountCodec extends ValueCodec<Amount> {
  defaultValue(): Amount { return Amount.zero(); }
  decode(decoder: Decoder): Amount {
    const value = decoder.amount();
    if (value === null) {
      decoder.reject();
      return Amount.zero();
    }
    return value;
  }
  encode(encoder: Encoder, value: Amount): void { encoder.amount(value); }
}

export class StringCodec extends ValueCodec<string> {
  defaultValue(): string { return ""; }
  decode(decoder: Decoder): string { return decoder.string(); }
  encode(encoder: Encoder, value: string): void { encoder.string(value); }
}

export class BytesCodec extends ValueCodec<Uint8Array> {
  defaultValue(): Uint8Array { return new Uint8Array(0); }
  decode(decoder: Decoder): Uint8Array { return decoder.bytes(); }
  encode(encoder: Encoder, value: Uint8Array): void { encoder.bytes(value); }
}

export class ActorId {
  private constructor(public value: string) {}

  static parse(value: string): ActorId | null {
    if (value.length == 0 || value.length > 40) return null;
    for (let index = 0; index < value.length; ++index) {
      const character = value.charCodeAt(index);
      if (!((character >= 48 && character <= 57) || (character >= 97 && character <= 102))) {
        return null;
      }
    }
    let normalized = "";
    for (let index = value.length; index < 40; ++index) normalized += "0";
    return new ActorId(normalized + value);
  }

  toString(): string { return this.value; }
}

export class ActorIdCodec extends ValueCodec<ActorId> {
  defaultValue(): ActorId { return ActorId.parse("0")!; }
  decode(decoder: Decoder): ActorId {
    const value = ActorId.parse(decoder.string());
    if (value === null) {
      decoder.reject();
      return this.defaultValue();
    }
    return value;
  }
  encode(encoder: Encoder, value: ActorId): void { encoder.string(value.value); }
}

export class NonZeroAmount {
  private constructor(public value: Amount) {}

  static create(value: Amount): NonZeroAmount | null {
    return value.isZero() ? null : new NonZeroAmount(value);
  }
}

export class NonZeroAmountCodec extends ValueCodec<NonZeroAmount> {
  defaultValue(): NonZeroAmount { return NonZeroAmount.create(Amount.one())!; }
  decode(decoder: Decoder): NonZeroAmount {
    const value = decoder.amount();
    const result = value === null ? null : NonZeroAmount.create(value);
    if (result === null) {
      decoder.reject();
      return this.defaultValue();
    }
    return result;
  }
  encode(encoder: Encoder, value: NonZeroAmount): void { encoder.amount(value.value); }
}

export class BoundedString {
  private constructor(public value: string) {}

  static create(value: string, maximumBytes: i32): BoundedString | null {
    if (value.length == 0 || String.UTF8.byteLength(value) > maximumBytes) return null;
    return new BoundedString(value);
  }

  toString(): string { return this.value; }
}

export class BoundedStringCodec extends ValueCodec<BoundedString> {
  constructor(public maximumBytes: i32) { super(); }
  defaultValue(): BoundedString { return BoundedString.create("0", this.maximumBytes)!; }
  decode(decoder: Decoder): BoundedString {
    const value = BoundedString.create(decoder.string(), this.maximumBytes);
    if (value === null) {
      decoder.reject();
      return this.defaultValue();
    }
    return value;
  }
  encode(encoder: Encoder, value: BoundedString): void { encoder.string(value.value); }
}

export class OptionalValue<T> {
  constructor(public present: bool, public value: T) {}
}

export class OptionalCodec<T> extends ValueCodec<OptionalValue<T>> {
  constructor(public inner: ValueCodec<T>) { super(); }
  defaultValue(): OptionalValue<T> { return new OptionalValue<T>(false, this.inner.defaultValue()); }
  decode(decoder: Decoder): OptionalValue<T> {
    const length = decoder.array();
    const tag = decoder.u64();
    if (length == 1 && tag == 0) return this.defaultValue();
    if (length == 2 && tag == 1) return new OptionalValue<T>(true, this.inner.decode(decoder));
    decoder.reject();
    return this.defaultValue();
  }
  encode(encoder: Encoder, value: OptionalValue<T>): void {
    encoder.array(value.present ? 2 : 1);
    encoder.u64(value.present ? 1 : 0);
    if (value.present) this.inner.encode(encoder, value.value);
  }
}

export class ArrayCodec<T> extends ValueCodec<Array<T>> {
  constructor(public inner: ValueCodec<T>, public maximumEntries: i32 = 16384) { super(); }
  defaultValue(): Array<T> { return new Array<T>(); }
  decode(decoder: Decoder): Array<T> {
    const length = decoder.array();
    if (!decoder.valid || length < 0 || length > this.maximumEntries) {
      decoder.reject();
      return new Array<T>();
    }
    const result = new Array<T>();
    for (let index = 0; index < length; ++index) result.push(this.inner.decode(decoder));
    return result;
  }
  encode(encoder: Encoder, value: Array<T>): void {
    encoder.array(value.length);
    for (let index = 0; index < value.length; ++index) this.inner.encode(encoder, value[index]);
  }
}

export type Comparator<T> = (left: T, right: T) => i32;

export function compareString(left: string, right: string): i32 {
  return left < right ? -1 : left > right ? 1 : 0;
}

export function compareActorId(left: ActorId, right: ActorId): i32 {
  return compareString(left.value, right.value);
}

export function compareU64(left: u64, right: u64): i32 {
  return left < right ? -1 : left > right ? 1 : 0;
}

export class StateEntry<K, V> {
  constructor(public key: K, public value: V) {}
}

export class StateMap<K, V> {
  private items: Array<StateEntry<K, V>> = new Array<StateEntry<K, V>>();

  constructor(public compare: Comparator<K>, public maximumEntries: i32 = 16384) {}

  private find(key: K): i32 {
    let low = 0;
    let high = this.items.length - 1;
    while (low <= high) {
      const middle = low + ((high - low) >> 1);
      const order = this.compare(this.items[middle].key, key);
      if (order == 0) return middle;
      if (order < 0) low = middle + 1;
      else high = middle - 1;
    }
    return ~low;
  }

  has(key: K): bool { return this.find(key) >= 0; }

  get(key: K): V | null {
    const index = this.find(key);
    return index < 0 ? null : this.items[index].value;
  }

  set(key: K, value: V): i32 {
    const index = this.find(key);
    if (index >= 0) {
      this.items[index].value = value;
      return 1;
    }
    if (this.items.length >= this.maximumEntries) return -1;
    const insertion = ~index;
    this.items.push(new StateEntry<K, V>(key, value));
    for (let current = this.items.length - 1; current > insertion; --current) {
      this.items[current] = this.items[current - 1];
    }
    this.items[insertion] = new StateEntry<K, V>(key, value);
    return 0;
  }

  delete(key: K): bool {
    const index = this.find(key);
    if (index < 0) return false;
    this.items.splice(index, 1);
    return true;
  }

  get size(): i32 { return this.items.length; }
  isEmpty(): bool { return this.items.length == 0; }

  entries(): Array<StateEntry<K, V>> {
    const result = new Array<StateEntry<K, V>>();
    for (let index = 0; index < this.items.length; ++index) {
      result.push(new StateEntry<K, V>(this.items[index].key, this.items[index].value));
    }
    return result;
  }
}

export class StateMapCodec<K, V> extends ValueCodec<StateMap<K, V>> {
  constructor(
    public keyCodec: ValueCodec<K>,
    public valueCodec: ValueCodec<V>,
    public compare: Comparator<K>,
    public maximumEntries: i32 = 16384,
  ) { super(); }

  defaultValue(): StateMap<K, V> { return new StateMap<K, V>(this.compare, this.maximumEntries); }

  decode(decoder: Decoder): StateMap<K, V> {
    const count = decoder.array();
    const result = this.defaultValue();
    if (!decoder.valid || count < 0 || count > this.maximumEntries) {
      decoder.reject();
      return result;
    }
    for (let index = 0; index < count; ++index) {
      if (decoder.array() != 2) {
        decoder.reject();
        return result;
      }
      const key = this.keyCodec.decode(decoder);
      const value = this.valueCodec.decode(decoder);
      if (!decoder.valid || result.set(key, value) != 0) {
        decoder.reject();
        return result;
      }
    }
    return result;
  }

  encode(encoder: Encoder, value: StateMap<K, V>): void {
    const entries = value.entries();
    encoder.array(entries.length);
    for (let index = 0; index < entries.length; ++index) {
      encoder.array(2);
      this.keyCodec.encode(encoder, entries[index].key);
      this.valueCodec.encode(encoder, entries[index].value);
    }
  }
}

export class StateSet<T> {
  private values: StateMap<T, bool>;

  constructor(compare: Comparator<T>, maximumEntries: i32 = 16384) {
    this.values = new StateMap<T, bool>(compare, maximumEntries);
  }

  has(value: T): bool { return this.values.has(value); }
  add(value: T): i32 { return this.values.set(value, true); }
  delete(value: T): bool { return this.values.delete(value); }
  get size(): i32 { return this.values.size; }
  entries(): Array<StateEntry<T, bool>> { return this.values.entries(); }
}

export class StateSetCodec<T> extends ValueCodec<StateSet<T>> {
  constructor(
    public valueCodec: ValueCodec<T>,
    public compare: Comparator<T>,
    public maximumEntries: i32 = 16384,
  ) { super(); }

  defaultValue(): StateSet<T> { return new StateSet<T>(this.compare, this.maximumEntries); }
  decode(decoder: Decoder): StateSet<T> {
    const count = decoder.array();
    const result = this.defaultValue();
    if (!decoder.valid || count < 0 || count > this.maximumEntries) {
      decoder.reject();
      return result;
    }
    for (let index = 0; index < count; ++index) {
      if (result.add(this.valueCodec.decode(decoder)) != 0) {
        decoder.reject();
        return result;
      }
    }
    return result;
  }
  encode(encoder: Encoder, value: StateSet<T>): void {
    const entries = value.entries();
    encoder.array(entries.length);
    for (let index = 0; index < entries.length; ++index) {
      this.valueCodec.encode(encoder, entries[index].key);
    }
  }
}

export abstract class VersionedStateCodec<S> {
  constructor(public version: u64, public fieldCount: i32) {}
  abstract create(): S;
  abstract decodeFields(decoder: Decoder, state: S): void;
  abstract encodeFields(encoder: Encoder, state: S): void;

  decode(source: Uint8Array): S | null {
    const state = this.create();
    if (source.length == 0) return state;
    const decoder = new Decoder(source);
    if (decoder.array() != this.fieldCount + 1 || decoder.u64() != this.version) return null;
    this.decodeFields(decoder, state);
    return decoder.empty() ? state : null;
  }

  encode(state: S): Uint8Array {
    const encoder = new Encoder();
    encoder.array(this.fieldCount + 1);
    encoder.u64(this.version);
    this.encodeFields(encoder, state);
    return encoder.finish();
  }
}

export const emptyCodec = new EmptyCodec();
export const boolCodec = new BoolCodec();
export const u8Codec = new U8Codec();
export const u16Codec = new U16Codec();
export const u32Codec = new U32Codec();
export const u64Codec = new U64Codec();
export const i8Codec = new I8Codec();
export const i16Codec = new I16Codec();
export const i32Codec = new I32Codec();
export const i64Codec = new I64Codec();
export const amountCodec = new AmountCodec();
export const stringCodec = new StringCodec();
export const bytesCodec = new BytesCodec();
export const actorIdCodec = new ActorIdCodec();
export const nonZeroAmountCodec = new NonZeroAmountCodec();

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

export class ContractResult<T> {
  constructor(public value: T, public error: string | null = null) {}
  get ok(): bool { return this.error === null; }
}

export function success<T>(value: T): ContractResult<T> {
  return new ContractResult<T>(value);
}

export function failure<T>(fallback: T, error: string): ContractResult<T> {
  return new ContractResult<T>(fallback, error);
}

export function isContentHash(value: string, allowEmpty: bool = false): bool {
  if (allowEmpty && value.length == 0) return true;
  if (value.length != 64) return false;
  for (let index = 0; index < value.length; ++index) {
    const character = value.charCodeAt(index);
    if (!((character >= 48 && character <= 57) || (character >= 97 && character <= 102))) {
      return false;
    }
  }
  return true;
}

export function isDfsLogicalKey(value: string): bool {
  if (value.length == 0 || value.length > 128 || value.startsWith("/") || value.endsWith("/")) {
    return false;
  }
  if (value == ".." || value.startsWith("../") || value.endsWith("/..") || value.includes("/../")) {
    return false;
  }
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

export class Context {
  events: Array<ContractEvent> = new Array<ContractEvent>();
  effects: Array<ContractEffect> = new Array<ContractEffect>();

  constructor(public request: InvokeRequest) {}

  sender(): string { return this.request.sender; }
  caller(): string { return this.request.caller; }
  contractId(): string { return this.request.contractId; }
  block(): u64 { return this.request.block; }
  depth(): u64 { return this.request.depth; }

  dagProof(transactionHash: string, minimumConfirmations: u64): DagProof | null {
    for (let index = 0; index < this.request.verified.dag.length; ++index) {
      const proof = this.request.verified.dag[index];
      if (proof.transactionHash == transactionHash && proof.confirmations >= minimumConfirmations) {
        return proof;
      }
    }
    return null;
  }

  dfsProof(ownerId: string, fileId: string, contentHash: string): DfsProof | null {
    for (let index = 0; index < this.request.verified.dfs.length; ++index) {
      const proof = this.request.verified.dfs[index];
      if (proof.ownerId == ownerId && proof.fileId == fileId && proof.contentHash == contentHash) {
        return proof;
      }
    }
    return null;
  }

  hasDfsProof(ownerId: string, fileId: string, contentHash: string): bool {
    return this.dfsProof(ownerId, fileId, contentHash) !== null;
  }

  emit<T>(topic: string, codec: ValueCodec<T>, value: T): void {
    const encoder = new Encoder();
    codec.encode(encoder, value);
    this.events.push(new ContractEvent(topic, encoder.finish()));
  }

  call<T>(contractId: ActorId, method: BoundedString, codec: ValueCodec<T>, argumentsData: T): void {
    const encoder = new Encoder();
    codec.encode(encoder, argumentsData);
    this.effects.push(new ContractEffect("contract_call", contractId.value, method.value, encoder.finish()));
  }

  private tokenEvent(operation: string, data: Uint8Array): void {
    this.events.push(new ContractEvent(operation, data));
    this.effects.push(new ContractEffect("token_delta", this.request.contractId, operation, data));
  }

  private fungibleEntries(
    first: ActorId,
    amount: NonZeroAmount,
    second: ActorId | null = null,
  ): Uint8Array {
    const encoder = new Encoder();
    encoder.array(second === null ? 1 : 2);
    encoder.array(2);
    encoder.string(first.value);
    encoder.amount(amount.value);
    if (second !== null) {
      encoder.array(2);
      encoder.string(second.value);
      encoder.amount(amount.value);
    }
    return encoder.finish();
  }

  fungibleMint(receiver: ActorId, amount: NonZeroAmount): void {
    this.tokenEvent("mint", this.fungibleEntries(receiver, amount));
  }

  fungibleBurn(owner: ActorId, amount: NonZeroAmount): void {
    this.tokenEvent("burn", this.fungibleEntries(owner, amount));
  }

  fungibleTransfer(sender: ActorId, receiver: ActorId, amount: NonZeroAmount): void {
    this.tokenEvent("transfer", this.fungibleEntries(sender, amount, receiver));
  }

  fungibleLock(owner: ActorId, amount: NonZeroAmount): void {
    this.tokenEvent("lock", this.fungibleEntries(owner, amount));
  }

  private nftData(tokenId: Amount, first: ActorId, second: ActorId | null = null): Uint8Array {
    const encoder = new Encoder();
    encoder.array(second === null ? 2 : 3);
    encoder.amount(tokenId);
    encoder.string(first.value);
    if (second !== null) encoder.string(second.value);
    return encoder.finish();
  }

  nftMint(tokenId: Amount, receiver: ActorId): void {
    this.tokenEvent("nft_mint", this.nftData(tokenId, receiver));
  }

  nftTransfer(tokenId: Amount, sender: ActorId, receiver: ActorId): void {
    this.tokenEvent("nft_transfer", this.nftData(tokenId, sender, receiver));
  }

  nftBurn(tokenId: Amount, owner: ActorId): void {
    this.tokenEvent("nft_burn", this.nftData(tokenId, owner));
  }

  dfsBind(
    ownerId: ActorId,
    logicalKey: string,
    fileId: string,
    contentHash: string,
    previousContentHash: string = "",
  ): bool {
    if (!isDfsLogicalKey(logicalKey)
        || fileId.length == 0
        || !isContentHash(contentHash)
        || !isContentHash(previousContentHash, true)
        || this.dfsProof(ownerId.value, fileId, contentHash) === null) return false;
    const encoder = new Encoder();
    encoder.array(4);
    encoder.string(logicalKey);
    encoder.string(fileId);
    encoder.string(contentHash);
    encoder.string(previousContentHash);
    this.effects.push(new ContractEffect("dfs_write", ownerId.value, "bind", encoder.finish()));
    return true;
  }

  dfsTombstone(ownerId: ActorId, logicalKey: string, previousContentHash: string): bool {
    if (!isDfsLogicalKey(logicalKey) || !isContentHash(previousContentHash)) return false;
    const encoder = new Encoder();
    encoder.array(4);
    encoder.string(logicalKey);
    encoder.string("");
    encoder.string("");
    encoder.string(previousContentHash);
    this.effects.push(new ContractEffect("dfs_write", ownerId.value, "tombstone", encoder.finish()));
    return true;
  }
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

export enum RouteKind {
  Init,
  Call,
  Query,
  AuthorizeUpgrade,
  Migrate,
}

export class RouteExecution {
  constructor(
    public ok: bool,
    public data: Uint8Array = new Uint8Array(0),
    public error: string = "",
  ) {}

  static rejected(error: string): RouteExecution {
    return new RouteExecution(false, new Uint8Array(0), error);
  }
}

export type RouteGuard<S> = (state: S, context: Context) => string | null;
export type Handler0<S, R> = (state: S, context: Context) => ContractResult<R>;
export type Handler1<S, A, R> = (state: S, context: Context, first: A) => ContractResult<R>;
export type Handler2<S, A, B, R> = (
  state: S,
  context: Context,
  first: A,
  second: B,
) => ContractResult<R>;
export type Handler3<S, A, B, C, R> = (
  state: S,
  context: Context,
  first: A,
  second: B,
  third: C,
) => ContractResult<R>;
export type Handler4<S, A, B, C, D, R> = (
  state: S,
  context: Context,
  first: A,
  second: B,
  third: C,
  fourth: D,
) => ContractResult<R>;
export type Handler5<S, A, B, C, D, E, R> = (
  state: S,
  context: Context,
  first: A,
  second: B,
  third: C,
  fourth: D,
  fifth: E,
) => ContractResult<R>;

export abstract class ContractRoute<S> {
  constructor(
    public kind: RouteKind,
    public name: string,
    public guard: RouteGuard<S> | null = null,
  ) {}

  protected checkGuard(state: S, context: Context): string | null {
    return this.guard === null ? null : this.guard!(state, context);
  }

  protected encodeResult<R>(codec: ValueCodec<R>, result: ContractResult<R>): RouteExecution {
    if (!result.ok) return RouteExecution.rejected(result.error || "Contract method failed");
    const encoder = new Encoder();
    codec.encode(encoder, result.value);
    return new RouteExecution(true, encoder.finish());
  }

  abstract invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution;
}

class Route0<S, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public resultCodec: ValueCodec<R>,
    public handler: Handler0<S, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 0 || !decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context))
      : RouteExecution.rejected(guardError);
  }
}

class Route1<S, A, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public firstCodec: ValueCodec<A>,
    public resultCodec: ValueCodec<R>,
    public handler: Handler1<S, A, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 1) return RouteExecution.rejected("Invalid contract arguments");
    const first = this.firstCodec.decode(decoder);
    if (!decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context, first))
      : RouteExecution.rejected(guardError);
  }
}

class Route2<S, A, B, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public firstCodec: ValueCodec<A>,
    public secondCodec: ValueCodec<B>,
    public resultCodec: ValueCodec<R>,
    public handler: Handler2<S, A, B, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 2) return RouteExecution.rejected("Invalid contract arguments");
    const first = this.firstCodec.decode(decoder);
    const second = this.secondCodec.decode(decoder);
    if (!decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context, first, second))
      : RouteExecution.rejected(guardError);
  }
}

class Route3<S, A, B, C, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public firstCodec: ValueCodec<A>,
    public secondCodec: ValueCodec<B>,
    public thirdCodec: ValueCodec<C>,
    public resultCodec: ValueCodec<R>,
    public handler: Handler3<S, A, B, C, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 3) return RouteExecution.rejected("Invalid contract arguments");
    const first = this.firstCodec.decode(decoder);
    const second = this.secondCodec.decode(decoder);
    const third = this.thirdCodec.decode(decoder);
    if (!decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context, first, second, third))
      : RouteExecution.rejected(guardError);
  }
}

class Route4<S, A, B, C, D, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public firstCodec: ValueCodec<A>,
    public secondCodec: ValueCodec<B>,
    public thirdCodec: ValueCodec<C>,
    public fourthCodec: ValueCodec<D>,
    public resultCodec: ValueCodec<R>,
    public handler: Handler4<S, A, B, C, D, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 4) return RouteExecution.rejected("Invalid contract arguments");
    const first = this.firstCodec.decode(decoder);
    const second = this.secondCodec.decode(decoder);
    const third = this.thirdCodec.decode(decoder);
    const fourth = this.fourthCodec.decode(decoder);
    if (!decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context, first, second, third, fourth))
      : RouteExecution.rejected(guardError);
  }
}

class Route5<S, A, B, C, D, E, R> extends ContractRoute<S> {
  constructor(
    kind: RouteKind,
    name: string,
    public firstCodec: ValueCodec<A>,
    public secondCodec: ValueCodec<B>,
    public thirdCodec: ValueCodec<C>,
    public fourthCodec: ValueCodec<D>,
    public fifthCodec: ValueCodec<E>,
    public resultCodec: ValueCodec<R>,
    public handler: Handler5<S, A, B, C, D, E, R>,
    guard: RouteGuard<S> | null,
  ) { super(kind, name, guard); }

  invoke(state: S, context: Context, argumentsData: Uint8Array): RouteExecution {
    const decoder = new Decoder(argumentsData);
    if (decoder.array() != 5) return RouteExecution.rejected("Invalid contract arguments");
    const first = this.firstCodec.decode(decoder);
    const second = this.secondCodec.decode(decoder);
    const third = this.thirdCodec.decode(decoder);
    const fourth = this.fourthCodec.decode(decoder);
    const fifth = this.fifthCodec.decode(decoder);
    if (!decoder.empty()) return RouteExecution.rejected("Invalid contract arguments");
    const guardError = this.checkGuard(state, context);
    return guardError === null
      ? this.encodeResult<R>(this.resultCodec, this.handler(state, context, first, second, third, fourth, fifth))
      : RouteExecution.rejected(guardError);
  }
}

function equalBytes(left: Uint8Array, right: Uint8Array): bool {
  if (left.length != right.length) return false;
  for (let index = 0; index < left.length; ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

export class ContractRouter<S> implements Contract {
  private routes: Array<ContractRoute<S>> = new Array<ContractRoute<S>>();
  private valid: bool = true;

  constructor(public stateCodec: VersionedStateCodec<S>) {}

  private add(route: ContractRoute<S>): void {
    if (route.name.length == 0 || route.name.length > 64) {
      this.valid = false;
      return;
    }
    if ((route.kind == RouteKind.Init && route.name != "init")
        || (route.kind == RouteKind.AuthorizeUpgrade && route.name != "authorize_upgrade")
        || (route.kind == RouteKind.Migrate && route.name != "migrate")) {
      this.valid = false;
      return;
    }
    for (let index = 0; index < this.routes.length; ++index) {
      if (this.routes[index].name == route.name) {
        this.valid = false;
        return;
      }
    }
    this.routes.push(route);
  }

  route0<R>(
    kind: RouteKind,
    name: string,
    resultCodec: ValueCodec<R>,
    handler: Handler0<S, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route0<S, R>(kind, name, resultCodec, handler, guard));
  }

  route1<A, R>(
    kind: RouteKind,
    name: string,
    firstCodec: ValueCodec<A>,
    resultCodec: ValueCodec<R>,
    handler: Handler1<S, A, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route1<S, A, R>(kind, name, firstCodec, resultCodec, handler, guard));
  }

  route2<A, B, R>(
    kind: RouteKind,
    name: string,
    firstCodec: ValueCodec<A>,
    secondCodec: ValueCodec<B>,
    resultCodec: ValueCodec<R>,
    handler: Handler2<S, A, B, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route2<S, A, B, R>(kind, name, firstCodec, secondCodec, resultCodec, handler, guard));
  }

  route3<A, B, C, R>(
    kind: RouteKind,
    name: string,
    firstCodec: ValueCodec<A>,
    secondCodec: ValueCodec<B>,
    thirdCodec: ValueCodec<C>,
    resultCodec: ValueCodec<R>,
    handler: Handler3<S, A, B, C, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route3<S, A, B, C, R>(
      kind, name, firstCodec, secondCodec, thirdCodec, resultCodec, handler, guard,
    ));
  }

  route4<A, B, C, D, R>(
    kind: RouteKind,
    name: string,
    firstCodec: ValueCodec<A>,
    secondCodec: ValueCodec<B>,
    thirdCodec: ValueCodec<C>,
    fourthCodec: ValueCodec<D>,
    resultCodec: ValueCodec<R>,
    handler: Handler4<S, A, B, C, D, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route4<S, A, B, C, D, R>(
      kind, name, firstCodec, secondCodec, thirdCodec, fourthCodec, resultCodec, handler, guard,
    ));
  }

  route5<A, B, C, D, E, R>(
    kind: RouteKind,
    name: string,
    firstCodec: ValueCodec<A>,
    secondCodec: ValueCodec<B>,
    thirdCodec: ValueCodec<C>,
    fourthCodec: ValueCodec<D>,
    fifthCodec: ValueCodec<E>,
    resultCodec: ValueCodec<R>,
    handler: Handler5<S, A, B, C, D, E, R>,
    guard: RouteGuard<S> | null = null,
  ): void {
    this.add(new Route5<S, A, B, C, D, E, R>(
      kind, name, firstCodec, secondCodec, thirdCodec, fourthCodec, fifthCodec,
      resultCodec, handler, guard,
    ));
  }

  invoke(request: InvokeRequest): InvokeResponse {
    if (!this.valid) return InvokeResponse.failure(request.state, "The contract route table is invalid");
    let route: ContractRoute<S> | null = null;
    for (let index = 0; index < this.routes.length; ++index) {
      if (this.routes[index].name == request.method) {
        route = this.routes[index];
        break;
      }
    }
    if (route === null) return InvokeResponse.failure(request.state, "Unknown contract method");
    if (route.kind == RouteKind.Init && request.state.length != 0) {
      return InvokeResponse.failure(request.state, "Contract is already initialized");
    }
    const state = this.stateCodec.decode(request.state);
    if (state === null) return InvokeResponse.failure(request.state, "Invalid contract state");
    const context = new Context(request);
    const execution = route.invoke(state, context, request.argumentsData);
    if (!execution.ok) return InvokeResponse.failure(request.state, execution.error);
    const nextState = this.stateCodec.encode(state);
    const readOnly = route.kind == RouteKind.Query || route.kind == RouteKind.AuthorizeUpgrade;
    if (readOnly && (!equalBytes(nextState, request.state) || context.effects.length != 0)) {
      return InvokeResponse.failure(request.state, "The method must not change contract state");
    }
    const response = InvokeResponse.success(readOnly ? request.state : nextState);
    response.data = execution.data;
    response.events = context.events;
    response.effects = context.effects;
    return response;
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
