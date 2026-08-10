import {
  Decoder,
  Encoder,
  GeneratedContract,
  InvokeRequest,
  InvokeResponse,
  Ownership,
} from "./generated";

export class CustomContract extends GeneratedContract {
  invokeCustom(request: InvokeRequest): InvokeResponse | null {
    if (request.method == "init") {
      if (request.state.length != 0 || request.caller.length == 0) {
        return InvokeResponse.failure(request.state, "Counter is already initialized");
      }
      return InvokeResponse.success(this.state(request.caller, 0));
    }

    const decoder = new Decoder(request.state);
    if (decoder.array() != 2) return InvokeResponse.failure(request.state, "Counter state is invalid");
    const owner = decoder.string();
    const value = decoder.u64();
    if (!decoder.empty()) return InvokeResponse.failure(request.state, "Counter state is invalid");

    if (request.method == "get") {
      const response = InvokeResponse.success(request.state);
      response.data = this.value(value);
      return response;
    }
    if (request.method == "add") {
      if (!new Ownership(owner).requireOwner(request.caller)) {
        return InvokeResponse.failure(request.state, "Only the owner can add a value");
      }
      const argumentsDecoder = new Decoder(request.argumentsData);
      const amount = argumentsDecoder.u64();
      const next = value + amount;
      if (!argumentsDecoder.empty() || amount == 0 || next < value) {
        return InvokeResponse.failure(request.state, "Counter value is invalid");
      }
      const response = InvokeResponse.success(this.state(owner, next));
      response.data = this.value(next);
      return response;
    }
    if (request.method == "authorize_upgrade") {
      return new Ownership(owner).requireOwner(request.caller)
        ? InvokeResponse.success(request.state)
        : InvokeResponse.failure(request.state, "Only the owner can update the contract");
    }
    if (request.method == "migrate") return InvokeResponse.success(request.state);
    return null;
  }

  private state(owner: string, value: u64): Uint8Array {
    const encoder = new Encoder();
    encoder.array(2);
    encoder.string(owner);
    encoder.u64(value);
    return encoder.finish();
  }

  private value(value: u64): Uint8Array {
    const encoder = new Encoder();
    encoder.u64(value);
    return encoder.finish();
  }
}
