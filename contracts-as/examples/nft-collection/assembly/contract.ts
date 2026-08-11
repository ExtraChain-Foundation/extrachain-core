import { StandardNonFungibleContract } from "./generated";

@contract({ standard: "nft" })
export class Collection extends StandardNonFungibleContract {
  constructor() {
    super("ExtraChain Collection", "EXNFT");
  }
}
