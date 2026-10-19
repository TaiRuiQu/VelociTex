// This audit joins immutable registration identities with source-valid input
// mappings and executed artifacts. It never turns input validity into a claim
// that every branch of a registration is compatible.
import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,sha256,writeJson} from './acquire.mjs';
const read=async file=>JSON.parse(await fs.readFile(path.join(root,file),'utf8'));
const inventory=await read('compatibility/core-0.19.0.json');
const symbols=await read('tests/fixtures/p4-symbol-identities.json');
const registrations=await read('tests/fixtures/p4-registrations.json');
const executed=await read('tests/fixtures/p4-executed-identities.json');
const corpus=await read('tests/fixtures/p4.json');
const folder=process.argv.find(x=>x.startsWith('--folder='))?.slice(9)??'artifacts/p4/jit';
let report,differential;
try{report=await read(folder+'/report.json');}catch{}
try{differential=await read(folder+'/differential.json');}catch{}
const sourceMappings=new Map([...symbols.identities,...registrations.identities].map(i=>[i.id,i]));
for(const identity of executed.identities)sourceMappings.set(identity.id,identity);
const corpusCases=new Map(corpus.cases.map(f=>[f.id,f]));
const bySignature=new Map(corpus.cases.map(f=>[JSON.stringify([f.latex,!!f.displayMode,f.macros??null,f.referenceOptions??f.options??null]),f.id]));
const registrationCases=new Map(registrations.cases.map(f=>[f.id,f]));
const extraEdge=await read('tests/fixtures/p4-edge2.json');
const exported=new Set(report?.exportErrors?.map(x=>x.split('/')[0])??[]);
const corpusCasesById=new Map(corpus.cases.map(f=>[f.id,f]));
const checksById=new Map();
for(const c of differential?.checks??[]){if(!checksById.has(c.id))checksById.set(c.id,[]);checksById.get(c.id).push(c);}
const failuresById=new Map();for(const failure of differential?.failures??[]){if(!failuresById.has(failure.id))failuresById.set(failure.id,[]);failuresById.get(failure.id).push(failure.check);}
const optionMapping={
 displayMode:{api:'MathOptions.DisplayMode',kind:'native-option'},
 macros:{api:'MathOptions.Macros / MacroSession',kind:'native-option',limits:'Per-call dictionaries are immutable inputs. Explicit sessions transact only successful calls.'},
 maxExpand:{api:'MathOptions.MaxExpansion',kind:'bounded-native-option',limits:'Finite resource limits are mandatory; unlimited expansion is deliberately not an executable resource policy.'},
 maxSize:{api:'MathOptions.MaxSize',kind:'native-option'},
 minRuleThickness:{api:'MathOptions.MinRuleThickness',kind:'native-option'},
 colorIsTextColor:{api:'MathOptions.ColorIsTextColor',kind:'native-option'},
 leqno:{api:'MathOptions.Leqno',kind:'native-option'},
 fleqn:{api:'MathOptions.Fleqn / DisplayWidth',kind:'host-alignment-option',limits:'A single formula has natural width by default. Host-width alignment requires an explicit DisplayWidth.'},
 globalGroup:{api:'MathOptions.GlobalGroup + explicit MacroSession',kind:'transactional-native-option'},
 strict:{api:'MathOptions.StrictMode / StrictHandler / MathDocument.Warnings',kind:'native-option',limits:'Warning details are retained; unexpected reported caller callback exceptions propagate after cleanup.'},
 throwOnError:{api:'MathEngine.TryCompile / CompileForPresentation',kind:'structured-host-facade',limits:'TryCompile keeps the confirmed false/diagnostic contract. The presentation facade can retain and color literal original source. JavaScript exception APIs are not claimed identical.'},
 errorColor:{api:'MathPresentationOptions.ErrorColor',kind:'structured-host-facade'},
 output:{api:'MathDocument native drawing data + WindowsMathRenderer PNG/SVG + MathMlWriter',kind:'homologous-native-output',limits:'Browser HTML/CSS string modes are replaced by native output; MathML semantics remain included.'},
};
const entries=[];
for(const entry of inventory.entries.filter(e=>e.scope==='included')){
 const mapping=sourceMappings.get(entry.id);
 let fixtureIds=(mapping?.fixtureIds??[]).map(id=>{
  if(corpusCases.has(id))return id;
  const extra=registrationCases.get(id);if(!extra)return id;
  return bySignature.get(JSON.stringify([extra.latex,!!extra.displayMode,extra.macros??null,extra.referenceOptions??extra.options??null]))??id;
 });
 const observations=[];
 for(const id of fixtureIds){
  const actual=report?.results.filter(r=>r.id===id)??[];
  const successful=actual.filter(r=>r.success);
  const expectedInvalid=corpusCasesById.get(id)?.expectation==='invalid';
  const diagnosticPass=expectedInvalid&&actual.length>0&&actual.every(r=>!r.success&&r.diagnostic&&r.diagnostic!=='None');
  observations.push({fixtureId:id,inFrozenMainCorpus:corpusCases.has(id),actualMeasurements:actual.length,successfulMeasurements:successful.length,exportFailure:exported.has(id),hardChecksExecuted:checksById.has(id),structuredErrorPathVerified:!!diagnosticPass,hardFailures:[...new Set(failuresById.get(id)??[])],status:diagnosticPass?'verified-structured-error-use-case':!actual.length?'unverified':!differential||!checksById.has(id)?'executed-awaiting-hard-differential':(failuresById.get(id)?.length??0)>0||exported.has(id)?'failing-use-case':'verified-use-case-only'});
 }
 entries.push({id:entry.id,kind:entry.kind,name:entry.name,...(entry.mode?{mode:entry.mode}:{}),scope:'included',source:entry.source,inputEvidence:mapping?.status??'unexercised',fixtureIds:[...new Set(fixtureIds)],observations,...(entry.kind==='option'?{nativeMapping:optionMapping[entry.name]??{kind:'unimplemented-or-unverified',api:null}}:{}),completeBranchCompatibilityClaim:false});
}
const unmapped=entries.filter(e=>!e.fixtureIds.length);
const byKind={};for(const e of entries){byKind[e.kind]??={total:0,inputMapped:0};byKind[e.kind].total++;if(e.fixtureIds.length)byKind[e.kind].inputMapped++;}
const files=['compatibility/core-0.19.0.json','tests/fixtures/p4-symbol-identities.json','tests/fixtures/p4-registrations.json','tests/fixtures/p4-executed-identities.json','tests/fixtures/p4-edge2.json','tests/fixtures/p4.json',...(report?[folder+'/report.json']:[]),...(differential?[folder+'/differential.json']:[])];
const evidence=[];for(const file of files)evidence.push({path:file,sha256:sha256(await fs.readFile(path.join(root,file)))});
const audit={schemaVersion:1,phase:'P4',reference:lock.source,denominator:inventory.summary,policy:'All 2,939 included identities remain included. Source-valid input identity mappings and actual hard-check observations are recorded separately; neither a generated table nor one use case proves every branch. Browser-specific output values map explicitly to the confirmed native API. No exclusion is inferred from a failure.',summary:{included:entries.length,inputMapped:entries.length-unmapped.length,unmapped:unmapped.length,fullyVerifiedRegistrations:0,byKind},nativeEvidence:{folder,processId:report?.processId??null,nativeAot:report?.nativeAot??null,diagnosticRun:report?.diagnosticRun??null,expectationFailures:report?.expectationFailures??null,exportErrors:report?.exportErrors??[],hardDifferentialExecuted:!!differential,hardFailures:differential?.failures?.length??null},entries,unmapped,evidence,remainingAcceptance:['Run all frozen inputs and source-verified edge sets through independent semantics, geometry, MathML and homologous native outputs in JIT and Native AOT.','Classify every raster difference against unchanged thresholds and retain its original failing result.','Do not promote input mappings to full compatibility until actual hard checks and applicable branch tests pass.','Full old-CPU timing, actual monitor DPI, hardware device loss and complete screen-reader audio acceptance require their separately recorded verification.']};
const target=path.join(root,'compatibility/p4-api-audit.json');
if(process.argv.includes('--check')){if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(audit))throw new Error('P4 API audit changed');}else await writeJson(target,audit);
console.log(JSON.stringify({...audit.summary,nativeProcessId:audit.nativeEvidence.processId,hardFailures:audit.nativeEvidence.hardFailures}));
