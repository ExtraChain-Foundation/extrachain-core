import { StandardFungibleContract } from "./generated";

@contract({ standard: "fungible" })
export class TokenX extends StandardFungibleContract {
  constructor() {
    super("Token X", "X", 0, true);
  }
}
