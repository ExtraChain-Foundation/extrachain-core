import { mkdir, readFile, writeFile } from "node:fs/promises";
import path from "node:path";

const sourcePath = process.argv[2];
if (!sourcePath) throw new Error("Usage: generate-contract.mjs <assembly/contract.ts>");

const source = await readFile(sourcePath, "utf8");
const contractMatch = source.match(/@contract\s*\(\s*\{([\s\S]*?)\}\s*\)\s*export\s+class\s+(\w+)/);
if (!contractMatch) throw new Error("The source needs @contract({...}) on an exported class");

const className = contractMatch[2];
const optionText = contractMatch[1];
const versionMatch = optionText.match(/version\s*:\s*(\d+)/);
const ownerMatch = optionText.match(/owner\s*:\s*["'](\w+)["']/);
const upgradeMatch = optionText.match(/upgrade\s*:\s*["'](owner|locked)["']/);
const standardMatch = optionText.match(/standard\s*:\s*["'](fungible|nft)["']/);
const standard = standardMatch?.[1] || "";
const outputDirectory = path.join(path.dirname(sourcePath), ".exco");
await mkdir(outputDirectory, { recursive: true });

if (standard) {
  const baseClass = standard === "fungible" ? "StandardFungibleContract" : "StandardNonFungibleContract";
  const inheritance = new RegExp(`export\\s+class\\s+${className}\\s+extends\\s+${baseClass}\\b`);
  if (!inheritance.test(source)) {
    throw new Error(`The ${standard} standard requires ${className} to extend ${baseClass}`);
  }
  const cleanSource = source.replace(/@contract\s*\(\s*\{[\s\S]*?\}\s*\)\s*/m, "");
  await writeFile(path.join(outputDirectory, "contract.ts"), rewriteImports(cleanSource), "utf8");
  await writeFile(path.join(outputDirectory, "index.ts"), standardEntrySource(), "utf8");
  process.exit(0);
}

if (!versionMatch) throw new Error("The contract needs a numeric version");
if (upgradeMatch?.[1] === "owner" && !ownerMatch) {
  throw new Error("Owner-controlled upgrades need an owner state field");
}

const stateFields = [];
const fieldPattern = /@state\s+(?:public\s+)?(\w+)\s*:\s*([^=;\n]+)(?:\s*=\s*([^;]+))?\s*;/g;
for (const match of source.matchAll(fieldPattern)) {
  stateFields.push({ name: match[1], type: match[2].trim() });
}
if (stateFields.length === 0) throw new Error("The contract needs at least one @state field");
if (ownerMatch && !stateFields.some((field) => field.name === ownerMatch[1])) {
  throw new Error(`Owner field '${ownerMatch[1]}' is not marked with @state`);
}

const routes = [];
const routePattern = /@(init|call|query|migrate)(?:\s*\(\s*\{([\s\S]*?)\}\s*\))?\s*(?:public\s+)?(\w+)\s*\(([^)]*)\)\s*:\s*ContractResult<([^>]+)>\s*\{/g;
for (const match of source.matchAll(routePattern)) {
  const kind = match[1];
  const options = match[2] || "";
  const access = options.match(/access\s*:\s*["'](public|owner)["']/)?.[1] || "public";
  const fromVersion = options.match(/from\s*:\s*(\d+)/)?.[1] || "";
  const toVersion = options.match(/to\s*:\s*(\d+)/)?.[1] || "";
  const parameters = splitParameters(match[4]);
  if (parameters.length === 0 || parameters[0].type !== "Context") {
    throw new Error(`${match[3]} must receive Context as its first argument`);
  }
  if (kind === "query" && options.includes("mutable")) {
    throw new Error(`${match[3]} is a query and cannot be mutable`);
  }
  if (kind === "migrate" && (!fromVersion || !toVersion || Number(fromVersion) >= Number(toVersion))) {
    throw new Error(`${match[3]} needs an increasing migration range`);
  }
  if (kind !== "migrate" && (fromVersion || toVersion)) {
    throw new Error(`${match[3]} cannot declare a migration range`);
  }
  routes.push({
    kind,
    name: match[3],
    externalName: kind === "init" ? "init" : kind === "migrate" ? "migrate" : match[3],
    access,
    parameters: parameters.slice(1),
    resultType: match[5].trim(),
  });
}
if (routes.length === 0) throw new Error("The contract needs at least one exported route");

const names = new Set();
for (const route of routes) {
  if (names.has(route.externalName)) throw new Error(`Duplicate contract route '${route.externalName}'`);
  names.add(route.externalName);
}

const cleanSource = source
  .replace(/@contract\s*\(\s*\{[\s\S]*?\}\s*\)\s*/m, "")
  .replace(/@state\s+/g, "")
  .replace(/@(init|call|query|migrate)(?:\s*\(\s*\{[\s\S]*?\}\s*\))?\s*/g, "");
await writeFile(path.join(outputDirectory, "contract.ts"), rewriteImports(cleanSource), "utf8");
await writeFile(path.join(outputDirectory, "binding.ts"), bindingSource(), "utf8");
await writeFile(path.join(outputDirectory, "index.ts"), entrySource(), "utf8");

function splitParameters(value) {
  const result = [];
  let start = 0;
  let depth = 0;
  for (let index = 0; index <= value.length; ++index) {
    const character = value[index];
    if (character === "<" || character === "(" || character === "[") ++depth;
    if (character === ">" || character === ")" || character === "]") --depth;
    if (index !== value.length && (character !== "," || depth !== 0)) continue;
    const part = value.slice(start, index).trim();
    start = index + 1;
    if (!part) continue;
    const separator = part.indexOf(":");
    if (separator < 1) throw new Error(`Invalid parameter '${part}'`);
    result.push({ name: part.slice(0, separator).trim(), type: part.slice(separator + 1).trim() });
  }
  return result;
}

function rewriteImports(value) {
  return value.replace(/from\s+(["'])\.\/generated\1/g, "from $1../generated$1");
}

function codec(type) {
  const builtins = new Map([
    ["EmptyValue", "emptyCodec"],
    ["bool", "boolCodec"],
    ["u8", "u8Codec"],
    ["u16", "u16Codec"],
    ["u32", "u32Codec"],
    ["u64", "u64Codec"],
    ["i8", "i8Codec"],
    ["i16", "i16Codec"],
    ["i32", "i32Codec"],
    ["i64", "i64Codec"],
    ["string", "stringCodec"],
    ["Amount", "amountCodec"],
    ["ActorId", "actorIdCodec"],
    ["NonZeroAmount", "nonZeroAmountCodec"],
    ["OperationReceipt", "operationReceiptCodec"],
  ]);
  if (builtins.has(type)) return builtins.get(type);
  const bounded = type.match(/^BoundedString<(\d+)>$/);
  if (bounded) return `new BoundedStringCodec(${bounded[1]})`;
  throw new Error(`No generated codec is available for '${type}'`);
}

function stateDecode(field) {
  const decoders = new Map([
    ["bool", "boolean()"], ["u8", "u64()"], ["u16", "u64()"], ["u32", "u64()"],
    ["u64", "u64()"], ["i8", "i64()"], ["i16", "i64()"], ["i32", "i64()"],
    ["i64", "i64()"], ["string", "string()"],
  ]);
  const expression = decoders.get(field.type);
  if (!expression) throw new Error(`No generated state codec is available for '${field.type}'`);
  const cast = ["u8", "u16", "u32", "i8", "i16", "i32"].includes(field.type) ? `<${field.type}>` : "";
  return `    state.${field.name} = ${cast}decoder.${expression};`;
}

function stateEncode(field) {
  const encoders = new Map([
    ["bool", "boolean"], ["u8", "u64"], ["u16", "u64"], ["u32", "u64"],
    ["u64", "u64"], ["i8", "i64"], ["i16", "i64"], ["i32", "i64"],
    ["i64", "i64"], ["string", "string"],
  ]);
  const encoder = encoders.get(field.type);
  if (!encoder) throw new Error(`No generated state codec is available for '${field.type}'`);
  return `    encoder.${encoder}(state.${field.name});`;
}

function routeKind(kind) {
  return new Map([
    ["init", "RouteKind.Init"],
    ["call", "RouteKind.Call"],
    ["query", "RouteKind.Query"],
    ["migrate", "RouteKind.Migrate"],
  ]).get(kind);
}

function bindingSource() {
  const imports = new Set([
    "ActorId", "Amount", "BoundedString", "BoundedStringCodec", "Context", "ContractResult",
    "ContractRouter", "Decoder", "Encoder", "EmptyValue", "NonZeroAmount", "OperationReceipt", "RouteKind",
    "VersionedStateCodec", "emptyCodec", "boolCodec", "u8Codec",
    "u16Codec", "u32Codec", "u64Codec", "i8Codec", "i16Codec", "i32Codec", "i64Codec",
    "stringCodec", "amountCodec", "actorIdCodec", "nonZeroAmountCodec", "operationReceiptCodec",
  ]);
  const wrappers = routes.map((route) => {
    const parameters = route.parameters.map((parameter) => `${parameter.name}: ${parameter.type}`).join(", ");
    const call = route.parameters.map((parameter) => parameter.name).join(", ");
    return `function route_${route.name}(state: ${className}, context: Context${parameters ? `, ${parameters}` : ""}): ContractResult<${route.resultType}> {\n  return state.${route.name}(context${call ? `, ${call}` : ""});\n}`;
  }).join("\n\n");
  const ownerGuard = ownerMatch
    ? `function ownerGuard(state: ${className}, context: Context): string | null {\n  return state.${ownerMatch[1]} == context.caller() ? null : "Only the owner can perform this operation";\n}`
    : "";
  const registrations = routes.map((route) => {
    const codecs = route.parameters.map((parameter) => codec(parameter.type));
    const guard = route.access === "owner" ? ", ownerGuard" : "";
    return `    this.route${route.parameters.length}<${[...route.parameters.map((parameter) => parameter.type), route.resultType].join(", ")}>(${routeKind(route.kind)}, "${route.externalName}", ${[...codecs, codec(route.resultType), `route_${route.name}`].join(", ")}${guard});`;
  }).join("\n");
  const automaticUpgrade = upgradeMatch?.[1] === "owner" && !names.has("authorize_upgrade")
    ? `\n    this.route1<BoundedString, EmptyValue>(RouteKind.AuthorizeUpgrade, "authorize_upgrade", new BoundedStringCodec(64), emptyCodec, authorizeUpgrade, ownerGuard);`
    : "";
  const upgradeFunction = upgradeMatch?.[1] === "owner" && !names.has("authorize_upgrade")
    ? `\n\nfunction authorizeUpgrade(_state: ${className}, _context: Context, _moduleHash: BoundedString): ContractResult<EmptyValue> {\n  return success(new EmptyValue());\n}`
    : "";
  const automaticMigrate = !names.has("migrate")
    ? `\n    this.route0<EmptyValue>(RouteKind.Migrate, "migrate", emptyCodec, automaticMigrate${ownerMatch ? ", ownerGuard" : ""});`
    : "";
  const migrateFunction = !names.has("migrate")
    ? `\n\nfunction automaticMigrate(_state: ${className}, _context: Context): ContractResult<EmptyValue> {\n  return success(new EmptyValue());\n}`
    : "";
  return `import {\n  ${[...imports].sort().join(",\n  ")},\n  success,\n} from "../generated";\nimport { ${className} } from "./contract";\n\nclass StateCodec extends VersionedStateCodec<${className}> {\n  constructor() { super(${versionMatch[1]}, ${stateFields.length}); }\n  create(): ${className} { return new ${className}(); }\n  decodeFields(decoder: Decoder, state: ${className}): void {\n${stateFields.map(stateDecode).join("\n")}\n  }\n  encodeFields(encoder: Encoder, state: ${className}): void {\n${stateFields.map(stateEncode).join("\n")}\n  }\n}\n\n${ownerGuard}\n\n${wrappers}${upgradeFunction}${migrateFunction}\n\nexport class GeneratedContract extends ContractRouter<${className}> {\n  constructor() {\n    super(new StateCodec());\n${registrations}${automaticUpgrade}${automaticMigrate}\n  }\n}\n`;
}

function entrySource() {
  return `import { resultLength, runContract } from "../generated";
import { GeneratedContract } from "./binding";

const CONTRACT = new GeneratedContract();

export function contractAbort(_message: usize, _fileName: usize, _line: u32, _column: u32): void {
  unreachable();
}

export function exc_invoke(pointer: i32, length: i32): i32 {
  return runContract(CONTRACT, pointer, length);
}

export function exc_result_len(): i32 {
  return resultLength();
}
`;
}

function standardEntrySource() {
  return `import { resultLength, runContract } from "../generated";
import { ${className} } from "./contract";

const CONTRACT = new ${className}();

export function contractAbort(_message: usize, _fileName: usize, _line: u32, _column: u32): void {
  unreachable();
}

export function exc_invoke(pointer: i32, length: i32): i32 {
  return runContract(CONTRACT, pointer, length);
}

export function exc_result_len(): i32 {
  return resultLength();
}
`;
}
