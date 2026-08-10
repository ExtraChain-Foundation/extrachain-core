import { resultLength, runContract } from "../../../sdk/index";
import { RouterConformanceContract } from "./contract";

const CONTRACT = new RouterConformanceContract();

export function contractAbort(_message: usize, _fileName: usize, _line: u32, _column: u32): void {
  unreachable();
}

export function exc_invoke(pointer: i32, length: i32): i32 {
  return runContract(CONTRACT, pointer, length);
}

export function exc_result_len(): i32 {
  return resultLength();
}
