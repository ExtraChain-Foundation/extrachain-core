import { readFileSync, writeFileSync } from "node:fs";

const [input, output] = process.argv.slice(2);
let source = readFileSync(input, "utf8");
const getter = '(import "env" "getTempRet0" (func $fimport$0 (result i32)))';
const setter = '(import "env" "setTempRet0" (func $fimport$1 (param i32)))';
if (!source.includes(getter) || !source.includes(setter)) {
  throw new Error("Emscripten temporary-return imports changed");
}
source = source.replace(
  getter,
  '(global $exco_temp_ret (mut i32) (i32.const 0))\n (func $fimport$0 (result i32) (global.get $exco_temp_ret))',
);
source = source.replace(
  setter,
  '(func $fimport$1 (param $value i32) (global.set $exco_temp_ret (local.get $value)))',
);
writeFileSync(output, source);
