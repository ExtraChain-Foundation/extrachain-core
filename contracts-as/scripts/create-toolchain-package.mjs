import { cp, mkdir, readFile, rm, stat, writeFile } from "node:fs/promises";
import { basename, dirname, join, resolve } from "node:path";
import { platform } from "node:os";
import { spawnSync } from "node:child_process";

function argument(name) {
  const index = process.argv.indexOf(name);
  return index >= 0 && index + 1 < process.argv.length ? process.argv[index + 1] : "";
}

const root = resolve(import.meta.dirname, "..");
const nodeSource = resolve(argument("--node"));
const output = resolve(argument("--output"));
if (!argument("--node") || !argument("--output")) {
  throw new Error("Use --node <Node.js 24 executable> --output <archive.tar>");
}
if (!(await stat(nodeSource)).isFile()) throw new Error("The Node.js executable does not exist");

const version = spawnSync(nodeSource, ["--version"], { encoding: "utf8" });
if (version.status !== 0 || !/^v24\./.test(version.stdout.trim())) {
  throw new Error("The toolchain package requires a Node.js 24 executable");
}
const packageJson = JSON.parse(await readFile(join(root, "package.json"), "utf8"));
if (packageJson.devDependencies.assemblyscript !== "0.28.20") {
  throw new Error("AssemblyScript must stay pinned to 0.28.20");
}

const staging = join(dirname(output), `.extrachain-as-toolchain-${process.pid}`);
await rm(staging, { recursive: true, force: true });
await mkdir(join(staging, "bin"), { recursive: true });
await mkdir(join(staging, "compiler", "node_modules"), { recursive: true });
await mkdir(join(staging, "dependencies"), { recursive: true });
await cp(nodeSource, join(staging, "bin", platform() === "win32" ? "node.exe" : "node"));
for (const dependency of ["assemblyscript", "binaryen", "long"]) {
  await cp(join(root, "node_modules", dependency), join(staging, "compiler", "node_modules", dependency), {
    recursive: true,
    dereference: true,
  });
}
await writeFile(
  join(staging, "compiler", "asc.js"),
  'import "./node_modules/assemblyscript/bin/asc.js";\n',
);
await cp(join(root, "scripts", "mark-wasm.mjs"), join(staging, "compiler", "mark-wasm.mjs"));
await cp(join(root, "scripts", "generate-contract.mjs"), join(staging, "compiler", "generate-contract.mjs"));
await cp(join(root, "node_modules", "as-bignum"), join(staging, "dependencies", "as-bignum"), {
  recursive: true,
  dereference: true,
});
for (const directory of ["sdk", "components", "catalog", "templates"]) {
  await cp(join(root, directory), join(staging, directory), { recursive: true, dereference: true });
}
await writeFile(
  join(staging, "toolchain.json"),
  `${JSON.stringify({
    schema: 1,
    language: "assemblyscript",
    version: packageJson.version,
    compiler_version: packageJson.devDependencies.assemblyscript,
    runtime_version: version.stdout.trim().slice(1),
  }, null, 2)}\n`,
);

await mkdir(dirname(output), { recursive: true });
await rm(output, { force: true });
const archiveFlag = output.endsWith(".tar.xz")
  ? "-cJf"
  : output.endsWith(".tar.gz")
    ? "-czf"
    : "-cf";
const archive = spawnSync("tar", [archiveFlag, output, "-C", staging, "."], { stdio: "inherit" });
await rm(staging, { recursive: true, force: true });
if (archive.status !== 0) throw new Error(`Cannot create ${basename(output)}`);
console.log(output);
