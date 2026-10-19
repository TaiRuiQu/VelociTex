// Conservative per-registration use-case evidence, never a complete-coverage
// claim. The upstream denominator and exclusions remain fixed at P0.
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {root,writeJson,sha256} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const source=loadSource(),require=createRequire(import.meta.url);
const phase=process.argv.find(x=>x.startsWith('--phase='))?.slice(8)??'p1';
if(!['p1','p2','p4'].includes(phase))throw new Error('Unknown phase');
const Lexer=require(path.join(root,'artifacts/upstream/source/src/Lexer.ts')).default;
const corpus=JSON.parse(await fs.readFile(path.join(root,`tests/fixtures/${phase}.json`),'utf8'));
const inventory=JSON.parse(await fs.readFile(path.join(root,'compatibility/core-0.19.0.json'),'utf8'));
const coverage=new Map();
const add=(kind,name,mode,id)=>{const key=kind+':'+(mode??'')+':'+name;if(!coverage.has(key))coverage.set(key,new Set());coverage.get(key).add(id);};
for(const fixture of corpus.cases.filter(x=>x.expectation==='valid')) {
  const lexer=new Lexer(fixture.latex,{});
  let environmentPending=false;
  for(;;) {
    const token=lexer.lex();if(token.text==='EOF')break;
    if(source.functions._functions[token.text])add('function',token.text,null,fixture.id);
    if(source.macros[token.text])add('macro',token.text,null,fixture.id);
    if(token.text==='\\begin')environmentPending=true;
    else if(environmentPending&&token.text!=='{') {
      const start=token.loc.start,tail=fixture.latex.slice(start),end=tail.indexOf('}');
      if(end>=0&&source.environments._environments[tail.slice(0,end)])add('environment',tail.slice(0,end),null,fixture.id);
      environmentPending=false;
    }
  }
  const tree=source.katex.__parse(fixture.latex,{displayMode:!!fixture.displayMode,macros:{...fixture.macros},throwOnError:true});
  const walk=n=>{
    if(!n||typeof n!=='object')return;
    if(['mathord','textord','atom'].includes(n.type)) {
      add('symbol',n.text,n.mode,fixture.id);
      // Builder registration identities are separate from symbol identities.
      add('builder',n.type,null,fixture.id);
    }
    for(const [key,v]of Object.entries(n))if(key!=='loc')if(Array.isArray(v))v.forEach(walk);else if(v&&typeof v==='object')walk(v);
  };walk(tree);
}
for(const runtime of ['jit','aot']) {
  const diff=JSON.parse(await fs.readFile(path.join(root,`artifacts/${phase}`,runtime,'differential.json'),'utf8'));
  const images=JSON.parse(await fs.readFile(path.join(root,`artifacts/${phase}`,runtime,'image-check.json'),'utf8'));
  if(diff.failures.length||!images.unreviewedFailures||images.unreviewedFailures.length||images.checks.some(x=>x.nativeToSvg<.95||x.svgToNative<.95||x.edgeClipping))throw new Error('Phase evidence has unresolved semantic, geometry, output or unreviewed raster failure');
}
const entries=[];
for(const item of inventory.entries) {
  const ids=coverage.get(item.kind+':'+(item.mode??'')+':'+item.name);
  if(ids&&item.scope==='included')entries.push({id:item.id,fixtureIds:[...ids].sort(),status:'partially-verified',reason:`These ${phase.toUpperCase()} fixtures passed normalized semantics, geometry, MathML and homologous native/PNG/SVG checks under JIT and Native AOT. Other arguments, styles, modes, aliases or interactions of this registration are not claimed complete.`});
}
await writeJson(path.join(root,`compatibility/${phase}-coverage.json`),{schemaVersion:1,phase:phase.toUpperCase(),denominator:inventory.summary,fixtureCorpusSha256:sha256(await fs.readFile(path.join(root,`tests/fixtures/${phase}.json`))),fullyVerifiedRegistrations:0,partiallyVerifiedRegistrations:entries.length,policy:'Use-case evidence only. Generated symbol/macro table entries alone earn no coverage. Core scope and exclusions are unchanged. Browser raster anti-aliasing/hinting exceptions are recorded separately in the phase delivery; native/SVG output consistency and hard semantic/geometric checks must pass.',entries});
console.log(JSON.stringify({partiallyVerifiedRegistrations:entries.length,fullyVerifiedRegistrations:0,includedDenominator:inventory.summary.included}));
