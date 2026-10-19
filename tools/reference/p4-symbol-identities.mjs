import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const source=loadSource();
const inventory=JSON.parse(await fs.readFile(path.join(root,'compatibility/core-0.19.0.json'),'utf8'));
const symbols=inventory.entries.filter(e=>e.kind==='symbol'&&e.scope==='included');
const main=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p4.json'),'utf8'));
const extra=[], identities=[],bySignature=new Map();
const normalizeName=name=>name;
function uses(tree,entry){
 let found=false;
 function visit(n){
  if(!n||typeof n!=='object')return;if(Array.isArray(n)){for(const child of n)visit(child);return;}
  if(n.mode===entry.mode&&['mathord','textord','atom','accent','spacing','op'].includes(n.type)){
   if(n.text===entry.name||n.type==='accent'&&n.label===entry.name||n.type==='op'&&n.name===entry.name)found=true;
  }
  for(const [key,value]of Object.entries(n))if(key!=='loc')visit(value);
 }
 visit(tree);return found;
}
const parsedMain=[];
for(const f of main.cases.filter(f=>f.expectation==='valid')){
 try{parsedMain.push({fixture:f,tree:source.katex.__parse(f.latex,{displayMode:!!f.displayMode,strict:'ignore',trust:false,throwOnError:true,macros:{...f.macros}})});}catch(error){throw new Error('Frozen main no longer valid: '+f.id+': '+error);}
}
for(const entry of symbols){
 const matches=parsedMain.filter(p=>uses(p.tree,entry)).map(p=>p.fixture.id);
 let acceptedExtra;
 if(matches.length===0){
  let body=entry.name;
  if(entry.upstream.group==='accent-token')body+='{x}';
  const candidates=entry.mode==='text'?['\\text{'+body+'}', '\\text{'+entry.name+' x}']: [body, body+'{x}', '\\mathord{'+body+'}'];
  // Some symbol registrations are function syntax tokens. Their real grammar
  // must exercise the exact symbol name, not merely an alias with the same glyph.
  if(entry.name==='$')candidates.push('\\text{a $x$ b}');
  for(const latex of candidates){
   try{
    const tree=source.katex.__parse(latex,{displayMode:false,strict:'ignore',trust:false,throwOnError:true});
    if(!uses(tree,entry))continue;
    const signature=JSON.stringify([latex,false]);let fixture=bySignature.get(signature);
    if(!fixture){fixture={id:'p4-symbol-extra-'+sha256(Buffer.from(signature)).slice(0,16),category:'symbol-identity-extra',latex,displayMode:false,expectation:'valid',registrationIds:[],provenance:'Exact mode/name symbol identity occurs in the locked upstream parse tree; no native-result filtering.'};bySignature.set(signature,fixture);extra.push(fixture);}
    fixture.registrationIds.push(entry.id);matches.push(fixture.id);acceptedExtra=fixture;break;
   }catch{}
  }
 }
 identities.push({id:entry.id,name:normalizeName(entry.name),mode:entry.mode,font:entry.upstream.font,group:entry.upstream.group,fixtureIds:[...new Set(matches)],status:matches.length?'actual-upstream-symbol-use':'unexercised',...(!matches.length?{reason:'No accepted input produced this exact mode/name symbol registration in the upstream parse tree. The identity remains included and unverified.'}:{})});
}
const unexercised=identities.filter(x=>!x.fixtureIds.length);
const result={schemaVersion:1,phase:'P4',reference:lock.source,denominator:symbols.length,policy:'Mappings require actual exact-mode/name source symbol nodes. Macro aliases yielding another node do not automatically exercise overwritten symbol registrations. Unexercised identities remain included.',counts:{symbols:symbols.length,exercised:symbols.length-unexercised.length,unexercised:unexercised.length,extraFixtures:extra.length},identities,unexercised};
const target=path.join(root,'tests/fixtures/p4-symbol-identities.json'),extraTarget=path.join(root,'tests/fixtures/p4-symbol-extra.json');
const additional={schemaVersion:1,phase:'P4',reference:lock.source,counts:{valid:extra.length,invalid:0},cases:extra};
if(process.argv.includes('--check')){
 if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(result)||JSON.stringify(JSON.parse(await fs.readFile(extraTarget,'utf8')))!==JSON.stringify(additional))throw new Error('Symbol identity fixture drift');
}else{await writeJson(target,result);await writeJson(extraTarget,additional);}
console.log(JSON.stringify({...result.counts,sha256:sha256(await fs.readFile(target)),unexercisedNames:unexercised.map(x=>({mode:x.mode,name:x.name}))}));
