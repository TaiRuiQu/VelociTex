import fs from 'node:fs/promises';
import path from 'node:path';
import {root,writeJson,sha256} from './acquire.mjs';
import {deriveFormulaGeometry} from './geometry.mjs';

const reference=path.join(root,'artifacts/reference');
const summary=JSON.parse(await fs.readFile(path.join(reference,'summary.json'),'utf8'));
const inventory=JSON.parse(await fs.readFile(path.join(root,'compatibility/core-0.19.0.json'),'utf8'));
const noise=JSON.parse(await fs.readFile(path.join(reference,'reference-noise.json'),'utf8'));
const baseline=JSON.parse(await fs.readFile(path.join(reference,'baseline-check.json'),'utf8'));
if(baseline.maxGeometryDriftDip!==0||baseline.maxBaselineDifferenceDip>0.02)throw new Error('Baseline instrumentation differs from independent measured strut');
if(inventory.entries.some(x=>!x.scope||!x.source||!x.fixtureId||!x.status))throw new Error('Incomplete inventory');
if(noise.maxGeometryDeltaDip!==0||noise.nonidenticalScreenshotFixtures!==0)throw new Error('Reference noise requires investigation');
const byFixture=new Map();
for(const geometry of summary.geometries) {
  const directory=path.join(reference,geometry.id,geometry.scenario);
  const mathml=await fs.readFile(path.join(directory,'reference.mathml.normalized.json'),'utf8');
  const previous=byFixture.get(geometry.id);
  if(previous&&previous!==mathml)throw new Error(`MathML changed across visual scenarios: ${geometry.id}`);
  byFixture.set(geometry.id,mathml);
  const data=JSON.parse(await fs.readFile(path.join(directory,'reference.geometry.json'),'utf8'));
  const raw=JSON.parse(await fs.readFile(path.join(directory,'reference.geometry.raw.json'),'utf8'));
  if(JSON.stringify(data)!==JSON.stringify(deriveFormulaGeometry(raw)))throw new Error(`Logical geometry differs from original measured bases: ${geometry.id}`);
  if(!data.inkBoundsDevicePixels||data.glyphRanges.length===0)throw new Error(`Missing ink/glyphs: ${geometry.id}`);
  const screenshot=await fs.readFile(path.join(directory,'reference.png'));
  if(sha256(screenshot)!==data.screenshot.sha256)throw new Error(`Stale screenshot ${geometry.id}`);
}
const trackedFiles=['compatibility/core-0.19.0.json','docs/reference/katex-0.19.0.files.json','docs/reference/development-toolchain.lock.json','tools/reference/package-lock.json'];
trackedFiles.push('benchmarks/corpus.normal.json','docs/reference/normal-corpus-0.19.0.check.json');
for(const name of await fs.readdir(new URL('.',import.meta.url)))if(name.endsWith('.mjs')||name==='package.json')trackedFiles.push('tools/reference/'+name);
const evidenceFiles=['artifacts/reference/run-manifest.json','artifacts/reference/summary.json','artifacts/reference/reference-noise.json','artifacts/reference/browser-timing.samples.json','artifacts/reference/platform-fonts.json','artifacts/reference/source-dist-check.json','artifacts/reference/baseline-check.json'];
for(const optional of ['artifacts/reference/geometry-derivation.json','artifacts/reference/native-svg-check.json','artifacts/reference/reference.capture-source.mjs'])try{await fs.access(path.join(root,optional));evidenceFiles.push(optional);}catch(error){if(error.code!=='ENOENT')throw error;}
const manifest={schemaVersion:1,recordedDate:'2026-10-19',inventory:inventory.summary,fixtures:{valid:summary.semantic.filter(x=>x.result==='valid').length,invalid:summary.semantic.filter(x=>x.result==='invalid').length,excluded:summary.semantic.filter(x=>x.result==='excluded').length},screenshots:summary.referenceScreenshots,scenarios:summary.scenarios,normalizedMathmlScenarioInvariance:true,allScreenshotDigestsVerified:true,noise:{repetitions:noise.repetitions,geometryMaxDeltaDip:noise.maxGeometryDeltaDip,nonidenticalScreenshotFixtures:noise.nonidenticalScreenshotFixtures},files:Object.fromEntries(await Promise.all([...trackedFiles,...evidenceFiles].map(async relative=>[relative,sha256(await fs.readFile(path.join(root,relative)))]))),limitations:['Local Chrome binary is locked and checked; automatic acquisition of a redistributable pinned browser remains unverified.','The complete denominator is not a passing-test count. P1 overlays only partially verified use cases; unimplemented registrations retain planned fixture IDs.','The P0 seed set is reference-tool evidence, not a full core corpus. Its natural strut geometry was corrected in P1 from retained raw DOM measurements without changing screenshot bytes.','This P0 reference manifest does not claim native differential, old-CPU acceptance or actual drawing/presentation timing; P1 engine evidence is recorded separately.']};
manifest.baseline={independentStrutMaxDifferenceDip:baseline.maxBaselineDifferenceDip,instrumentationGeometryDriftDip:baseline.maxGeometryDriftDip};
await writeJson(path.join(root,'docs/reference/p0-reference.evidence.json'),manifest);
console.log(JSON.stringify({checkedScreenshots:summary.referenceScreenshots,invariantMathmlFixtures:byFixture.size,...manifest.noise}));
