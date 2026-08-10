import { GeneratedContract, InvokeRequest, InvokeResponse } from "./generated";

export class CustomContract extends GeneratedContract {
  invokeCustom(request: InvokeRequest): InvokeResponse | null {
    return null;
  }
}
