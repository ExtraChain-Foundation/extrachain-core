import {
  Context, ContractResult, EmptyValue, failure, success,
} from "./generated";

const MIGRATED_LIMIT: u64 = 0;

@contract({ version: 1, owner: "owner", upgrade: "owner" })
export class StudioCounter {
  @state owner: string = "";
  @state value: u64 = 0;
  @state limit: u64 = 0;
  @state touches: u64 = 0;
  @state last_section: u64 = 0;

  @init
  initialize(context: Context): ContractResult<EmptyValue> {
    this.owner = context.caller();
    this.last_section = context.block();
    return success(new EmptyValue());
  }

  @call({ access: "owner" })
  add(context: Context, amount: u64): ContractResult<u64> {
    const next = this.value + amount;
    if (amount == 0 || next < this.value || (this.limit > 0 && next >= this.limit)) {
      return failure(this.value, "Counter limit reached");
    }
    this.value = next;
    this.last_section = context.block();
    return success(next);
  }

  @query
  get(_context: Context): ContractResult<u64> {
    return success(this.value);
  }

  @call({ access: "owner" })
  touch(context: Context): ContractResult<u64> {
    this.touches += 1;
    this.last_section = context.block();
    return success(this.value);
  }

  @migrate({ from: 1, to: 2, access: "owner" })
  migrate(context: Context): ContractResult<EmptyValue> {
    this.limit = MIGRATED_LIMIT;
    this.last_section = context.block();
    return success(new EmptyValue());
  }
}
