// Validate JSON files against the real grammar. PowerShell's ConvertFrom-Json
// rejects (or silently mis-parses) deeply nested fixtures, so the source lint
// uses this script for every tracked .json file.
import fs from 'node:fs';

let failed = false;
for (const file of process.argv.slice(2)) {
  try {
    JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    failed = true;
    console.error(`${file}: ${error.message}`);
  }
}
if (failed) process.exit(1);
