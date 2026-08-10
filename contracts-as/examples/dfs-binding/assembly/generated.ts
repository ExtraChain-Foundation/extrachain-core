import {
  Contract,
  InvokeRequest,
  InvokeResponse,
} from "../../../sdk/index";

export {
  ContractEffect,
  ContractEvent,
  Decoder,
  Encoder,
  InvokeRequest,
  InvokeResponse,
  callContract,
} from "../../../sdk/index";

export class GeneratedContract implements Contract {
  invoke(request: InvokeRequest): InvokeResponse {
    const response = this.invokeCustom(request);
    if (response !== null) return response;
    return InvokeResponse.failure(request.state, "Unknown contract method");
  }

  invokeCustom(request: InvokeRequest): InvokeResponse | null {
    return null;
  }
}
