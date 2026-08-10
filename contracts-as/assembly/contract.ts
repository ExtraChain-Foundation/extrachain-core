import {
  BoundedString,
  BoundedStringCodec,
  Context,
  ContractResult,
  ContractRouter,
  Decoder,
  EmptyValue,
  Encoder,
  RouteKind,
  VersionedStateCodec,
  emptyCodec,
  failure,
  success,
  u64Codec,
} from "./generated";

class CounterState {
  owner: string = "";
  value: u64 = 0;
}

class CounterStateCodec extends VersionedStateCodec<CounterState> {
  constructor() { super(1, 2); }
  create(): CounterState { return new CounterState(); }
  decodeFields(decoder: Decoder, state: CounterState): void {
    state.owner = decoder.string();
    state.value = decoder.u64();
  }
  encodeFields(encoder: Encoder, state: CounterState): void {
    encoder.string(state.owner);
    encoder.u64(state.value);
  }
}

function initialize(state: CounterState, context: Context): ContractResult<EmptyValue> {
  if (context.caller().length == 0) return failure(new EmptyValue(), "Counter owner is invalid");
  state.owner = context.caller();
  return success(new EmptyValue());
}

function ownerGuard(state: CounterState, context: Context): string | null {
  return state.owner == context.caller() ? null : "Only the owner can perform this operation";
}

function add(state: CounterState, _context: Context, amount: u64): ContractResult<u64> {
  const next = state.value + amount;
  if (amount == 0 || next < state.value) return failure(state.value, "Counter value is invalid");
  state.value = next;
  return success(next);
}

function get(state: CounterState, _context: Context): ContractResult<u64> {
  return success(state.value);
}

function authorize(
  _state: CounterState,
  _context: Context,
  _moduleHash: BoundedString,
): ContractResult<EmptyValue> {
  return success(new EmptyValue());
}

function migrate(_state: CounterState, _context: Context): ContractResult<EmptyValue> {
  return success(new EmptyValue());
}

export class CustomContract extends ContractRouter<CounterState> {
  constructor() {
    super(new CounterStateCodec());
    this.route0<EmptyValue>(RouteKind.Init, "init", emptyCodec, initialize);
    this.route1<u64, u64>(RouteKind.Call, "add", u64Codec, u64Codec, add, ownerGuard);
    this.route0<u64>(RouteKind.Query, "get", u64Codec, get);
    this.route1<BoundedString, EmptyValue>(
      RouteKind.AuthorizeUpgrade,
      "authorize_upgrade",
      new BoundedStringCodec(64),
      emptyCodec,
      authorize,
      ownerGuard,
    );
    this.route0<EmptyValue>(RouteKind.Migrate, "migrate", emptyCodec, migrate, ownerGuard);
  }
}
