import { readFile, writeFile } from "node:fs/promises";

const SECTION_NAME = new TextEncoder().encode("extrachain.language");
const ALLOWED = new Set(["assemblyscript"]);

function unsignedLeb(value) {
  const result = [];
  do {
    let byte = value & 0x7f;
    value >>>= 7;
    if (value !== 0) byte |= 0x80;
    result.push(byte);
  } while (value !== 0);
  return Uint8Array.from(result);
}

const [fileName, language] = process.argv.slice(2);
if (!fileName || !ALLOWED.has(language)) {
  throw new Error("Usage: node mark-wasm.mjs <module.wasm> assemblyscript");
}

const module = new Uint8Array(await readFile(fileName));
const payload = new TextEncoder().encode(language);
const body = new Uint8Array(unsignedLeb(SECTION_NAME.length).length + SECTION_NAME.length + payload.length);
let offset = 0;
const nameLength = unsignedLeb(SECTION_NAME.length);
body.set(nameLength, offset);
offset += nameLength.length;
body.set(SECTION_NAME, offset);
offset += SECTION_NAME.length;
body.set(payload, offset);

const size = unsignedLeb(body.length);
const marked = new Uint8Array(module.length + 1 + size.length + body.length);
marked.set(module, 0);
marked[module.length] = 0;
marked.set(size, module.length + 1);
marked.set(body, module.length + 1 + size.length);
await writeFile(fileName, marked);
