import {
  Context,
  ContractResult,
  ContractRouter,
  Decoder,
  Encoder,
  RouteKind,
  VersionedStateCodec,
  success,
  u64Codec,
} from "../../../sdk/index";

class State { value: u64 = 0; }

class StateCodec extends VersionedStateCodec<State> {
  constructor() { super(1, 1); }
  create(): State { return new State(); }
  decodeFields(decoder: Decoder, state: State): void { state.value = decoder.u64(); }
  encodeFields(encoder: Encoder, state: State): void { encoder.u64(state.value); }
}

function route0(state: State, _context: Context): ContractResult<u64> { return success(state.value); }
function route1(_state: State, _context: Context, a: u64): ContractResult<u64> { return success(a); }
function route2(_state: State, _context: Context, a: u64, b: u64): ContractResult<u64> {
  return success(a + b);
}
function route3(_state: State, _context: Context, a: u64, b: u64, c: u64): ContractResult<u64> {
  return success(a + b + c);
}
function route4(
  _state: State,
  _context: Context,
  a: u64,
  b: u64,
  c: u64,
  d: u64,
): ContractResult<u64> { return success(a + b + c + d); }
function route5(
  _state: State,
  _context: Context,
  a: u64,
  b: u64,
  c: u64,
  d: u64,
  e: u64,
): ContractResult<u64> { return success(a + b + c + d + e); }

export class RouterConformanceContract extends ContractRouter<State> {
  constructor() {
    super(new StateCodec());
    this.route0<u64>(RouteKind.Query, "route0", u64Codec, route0);
    this.route1<u64, u64>(RouteKind.Query, "route1", u64Codec, u64Codec, route1);
    this.route2<u64, u64, u64>(RouteKind.Query, "route2", u64Codec, u64Codec, u64Codec, route2);
    this.route3<u64, u64, u64, u64>(
      RouteKind.Query, "route3", u64Codec, u64Codec, u64Codec, u64Codec, route3,
    );
    this.route4<u64, u64, u64, u64, u64>(
      RouteKind.Query, "route4", u64Codec, u64Codec, u64Codec, u64Codec, u64Codec, route4,
    );
    this.route5<u64, u64, u64, u64, u64, u64>(
      RouteKind.Query,
      "route5",
      u64Codec,
      u64Codec,
      u64Codec,
      u64Codec,
      u64Codec,
      u64Codec,
      route5,
    );
  }
}
