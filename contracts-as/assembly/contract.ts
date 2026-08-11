import {
  Context,
  ContractResult,
  EmptyValue,
  failure,
  success,
} from "./generated";

@contract({ version: 1, owner: "owner", upgrade: "owner" })
export class Counter {
  @state owner: string = "";
  @state value: u64 = 0;

  @init
  initialize(context: Context): ContractResult<EmptyValue> {
    this.owner = context.caller();
    return success(new EmptyValue());
  }

  @call({ access: "owner" })
  add(_context: Context, amount: u64): ContractResult<u64> {
    const next = this.value + amount;
    if (amount == 0 || next < this.value) {
      return failure(this.value, "Counter value is invalid");
    }
    this.value = next;
    return success(next);
  }

  @query
  get(_context: Context): ContractResult<u64> {
    return success(this.value);
  }
}
