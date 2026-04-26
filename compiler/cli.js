#!/usr/bin/env node
// Bidule 01 compiler CLI
// Usage: bdcc <input.bdcart> [output.bdbin]
//        bdcc --check <input.bdcart>   (validate only, no output)

import { compile } from './compiler.js';
import { readFileSync, writeFileSync } from 'fs';
import { basename, extname } from 'path';
import process from 'process';

const args  = process.argv.slice(2);
const check = args[0] === '--check';
if (check) args.shift();

const input = args[0];
if (!input) {
  console.error('Usage: bdcc [--check] <input.bdcart> [output.bdbin]');
  process.exit(1);
}

let source;
try {
  source = readFileSync(input, 'utf8');
} catch (e) {
  console.error(`error: cannot read '${input}': ${e.message}`);
  process.exit(1);
}

const { binary, errors, warnings } = compile(source);

for (const w of warnings) console.warn(`warning: ${w}`);
for (const e of errors)   console.error(`error: ${e}`);

if (errors.length > 0) {
  console.error(`\n${errors.length} error(s). Compilation failed.`);
  process.exit(1);
}

if (check) {
  console.log(`ok: '${input}' — ${binary.length} bytes`);
  process.exit(0);
}

const stem   = basename(input, extname(input));
const output = args[1] ?? `${stem}.bdbin`;

try {
  writeFileSync(output, binary);
} catch (e) {
  console.error(`error: cannot write '${output}': ${e.message}`);
  process.exit(1);
}

console.log(`ok: '${input}' → '${output}' (${binary.length} bytes)`);
