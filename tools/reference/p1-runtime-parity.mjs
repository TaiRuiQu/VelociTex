import fs from 'node:fs/promises';
import path from 'node:path';
import {root,sha256,writeJson} from './acquire.mjs';
const phase=process.argv.find(x=>x.startsWith('--phase='))?.slice(8)??'p1';
if(!['p1','p2','p4'].includes(phase))throw new Error('Unknown phase');
const jit=JSON.parse(await fs.readFile(path.join(root,`artifacts/${phase}/jit/report.json`),'utf8'));
const aot=JSON.parse(await fs.readFile(path.join(root,`artifacts/${phase}/aot/report.json`),'utf8'));
if(jit.nativeAot||!aot.nativeAot)throw new Error('Tests must run in real JIT and Native AOT processes');
if(JSON.stringify(jit.results)!==JSON.stringify(aot.results))throw new Error('JIT/AOT semantics, diagnostics or geometry differ');
const outputs=[];
for(const result of jit.results.filter(x=>x.success))for(const file of ['formula.png','formula.svg','formula.mathml']) {
  const relative=path.join(result.id,result.scenario,file),j=await fs.readFile(path.join(root,`artifacts/${phase}/jit`,relative)),a=await fs.readFile(path.join(root,`artifacts/${phase}/aot`,relative));
  if(!j.equals(a))throw new Error(`Runtime output differs ${relative}`);
  outputs.push({path:relative.replaceAll('\\','/'),sha256:sha256(a),bytes:a.length});
}
await writeJson(path.join(root,`artifacts/${phase}/runtime-parity.json`),{schemaVersion:1,jitProcessId:jit.processId,aotProcessId:aot.processId,semanticsGeometryAndDiagnosticResultsEqual:true,identicalExports:outputs.length,outputs});
console.log(JSON.stringify({semanticsGeometryAndDiagnostics:'identical',identicalExports:outputs.length,jitProcessId:jit.processId,aotProcessId:aot.processId}));
