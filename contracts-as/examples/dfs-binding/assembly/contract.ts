import { ContractEffect, Decoder, Encoder } from "../../../sdk/index";
import { DfsBindings } from "../../../components/index";
import { GeneratedContract, InvokeRequest, InvokeResponse } from "./generated";

export class CustomContract extends GeneratedContract {
  invokeCustom(request: InvokeRequest): InvokeResponse | null {
    if (request.method == "init") {
      const argumentsDecoder = new Decoder(request.argumentsData);
      if (request.state.length != 0 || argumentsDecoder.array() != 0 || !argumentsDecoder.empty()) {
        return InvokeResponse.failure(request.state, "DFS binding initialization is invalid");
      }
      return InvokeResponse.success(new DfsBindings().encode());
    }
    const bindings = DfsBindings.decode(request.state);
    if (bindings === null) return InvokeResponse.failure(request.state, "DFS binding state is invalid");
    if (request.method == "binding") {
      const keyDecoder = new Decoder(request.argumentsData);
      if (keyDecoder.array() != 1) {
        return InvokeResponse.failure(request.state, "Logical key is invalid");
      }
      const key = keyDecoder.string();
      if (!keyDecoder.empty()) return InvokeResponse.failure(request.state, "Logical key is invalid");
      const binding = bindings.get(key);
      const response = InvokeResponse.success(request.state);
      if (binding !== null) {
        const encoder = new Encoder();
        encoder.array(2);
        encoder.string(binding.fileId);
        encoder.string(binding.contentHash);
        response.data = encoder.finish();
      }
      return response;
    }
    if (request.method != "bind" && request.method != "tombstone") return null;
    const decoder = new Decoder(request.argumentsData);
    if (decoder.array() != 4) return InvokeResponse.failure(request.state, "DFS binding is invalid");
    const logicalKey = decoder.string();
    const fileId = decoder.string();
    const contentHash = decoder.string();
    const previousContentHash = decoder.string();
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "DFS binding is invalid");
    const changed = request.method == "bind"
      ? bindings.bind(logicalKey, fileId, contentHash, previousContentHash)
      : bindings.tombstone(logicalKey, previousContentHash);
    if (!changed) return InvokeResponse.failure(request.state, "DFS binding conflicts with current state");
    const response = InvokeResponse.success(bindings.encode());
    response.effects.push(new ContractEffect(
      "dfs_write",
      request.contractId,
      request.method,
      request.argumentsData,
    ));
    return response;
  }
}
