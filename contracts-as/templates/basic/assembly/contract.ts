import {
  Context,
  ContractResult,
  EmptyValue,
  success,
} from "./generated";

@contract({ version: 1, owner: "owner", upgrade: "owner" })
export class CustomContract {
  @state owner: string = "";

  @init
  initialize(context: Context): ContractResult<EmptyValue> {
    this.owner = context.caller();
    return success(new EmptyValue());
  }
}
