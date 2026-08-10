import { resultLength, runContract } from "../sdk/index";
import { CustomContract } from "./contract";

const CONTRACT = new CustomContract();

export function contractAbort(message: usize, fileName: usize, line: u32, column: u32): void {
  unreachable();
}

export function exc_invoke(pointer: i32, length: i32): i32 {
  return runContract(CONTRACT, pointer, length);
}

export function exc_result_len(): i32 {
  return resultLength();
}
