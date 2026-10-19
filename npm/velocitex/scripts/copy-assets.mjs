#!/usr/bin/env node
import fs from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const source = fileURLToPath(new URL('../dist/runtime/', import.meta.url));
const destination = process.argv[2];
if (!destination) throw new Error('Usage: velocitex-copy-assets public/velocitex');
const target = path.resolve(destination);
if (target === source || source.startsWith(target + path.sep)) throw new Error('Choose a separate destination directory.');
await fs.mkdir(target, { recursive: true });
await fs.cp(source, target, { recursive: true });
console.log(`VelociTex runtime copied to ${target}`);
