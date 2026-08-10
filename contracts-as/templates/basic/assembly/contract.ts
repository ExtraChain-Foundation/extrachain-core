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
} from "./generated";

class State { owner: string = ""; }

class StateCodec extends VersionedStateCodec<State> {
  constructor() { super(1, 1); }
  create(): State { return new State(); }
  decodeFields(decoder: Decoder, state: State): void { state.owner = decoder.string(); }
  encodeFields(encoder: Encoder, state: State): void { encoder.string(state.owner); }
}

function initialize(state: State, context: Context): ContractResult<EmptyValue> {
  if (context.caller().length == 0) return failure(new EmptyValue(), "Contract owner is invalid");
  state.owner = context.caller();
  return success(new EmptyValue());
}

function ownerGuard(state: State, context: Context): string | null {
  return state.owner == context.caller() ? null : "Only the owner can perform this operation";
}

function authorize(
  _state: State,
  _context: Context,
  _moduleHash: BoundedString,
): ContractResult<EmptyValue> {
  return success(new EmptyValue());
}

function migrate(_state: State, _context: Context): ContractResult<EmptyValue> {
  return success(new EmptyValue());
}

export class CustomContract extends ContractRouter<State> {
  constructor() {
    super(new StateCodec());
    this.route0<EmptyValue>(RouteKind.Init, "init", emptyCodec, initialize);
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
