import fs from 'node:fs/promises';
import path from 'node:path';
import {root,writeJson,sha256} from './acquire.mjs';
import {deriveFormulaGeometry} from './geometry.mjs';
const directory=path.join(root,'artifacts/reference');
const summary=JSON.parse(await fs.readFile(path.join(directory,'summary.json'),'utf8'));
let previous;
try{previous=JSON.parse(await fs.readFile(path.join(directory,'geometry-derivation.json'),'utf8'));}catch(error){if(error.code!=='ENOENT')throw error;}
const derived=[];
for(const item of summary.geometries) {
  const file=path.join(directory,item.id,item.scenario,'reference.geometry.json');
  const bytes=await fs.readFile(file);
  const original=JSON.parse(bytes);
  const {htmlBox,advanceDip,ascentDip,depthDip,tagBoxes,logicalMeasurement,...raw}=original;
  if(htmlBox)raw.logical=htmlBox;
  const rawBytes=Buffer.from(JSON.stringify(raw,null,2)+'\n');
  const prior=previous?.fixtures.find(x=>x.fixtureId===item.id&&x.scenario===item.scenario);
  if(prior&&sha256(rawBytes)!==prior.rawMeasurementSha256)throw new Error('Reconstructed raw data differs from original captured hash');
  await fs.writeFile(path.join(directory,item.id,item.scenario,'reference.geometry.raw.json'),rawBytes);
  const geometry=deriveFormulaGeometry(original);
  await writeJson(file,geometry);
  if(item.scenario==='24dip-100pct-light')await writeJson(path.join(directory,item.id,'reference.geometry.json'),geometry);
  item.widthDip=geometry.logical.width;item.heightDip=geometry.logical.height;
  derived.push({fixtureId:item.id,scenario:item.scenario,rawMeasurementSha256:sha256(rawBytes),derivedSha256:sha256(await fs.readFile(file)),screenshotSha256:geometry.screenshot.sha256});
}
await writeJson(path.join(directory,'summary.json'),summary);
await writeJson(path.join(directory,'geometry-derivation.json'),{geometryMeasurementVersion:2,reason:'P1 correction: natural vertical TeX extents use measured direct .katex-strut rectangles; horizontal extents use .katex-base width. A base inline line box can exceed its strut. Preserve original htmlBox, baseline, raw node/glyph geometry and screenshot bytes; separate tags and advance/ascent/depth.',generatorSha256:sha256(await fs.readFile(new URL('geometry.mjs',import.meta.url))),derivationScriptSha256:sha256(await fs.readFile(new URL('derive-geometries.mjs',import.meta.url))),fixtures:derived});
console.log(`${derived.length} geometry records derived from existing real DOM data; screenshots unchanged`);
