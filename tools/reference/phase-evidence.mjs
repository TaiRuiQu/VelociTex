import fs from 'node:fs/promises';
import path from 'node:path';
import {execFileSync} from 'node:child_process';
import {root,sha256,writeJson} from './acquire.mjs';
const phase=process.argv.find(x=>x.startsWith('--phase='))?.slice(8)??'p2';
if(phase!=='p2')throw new Error('This frozen evidence writer currently owns P2 only');
const target=`docs/reference/${phase}.evidence.json`;
const sourcePaths=execFileSync('git',['ls-files','--cached','--others','--exclude-standard','-z'],{cwd:root,maxBuffer:32*1024*1024}).toString('utf8').split('\0').filter(p=>p&&p!==target&&!p.startsWith('docs/reference/p1.evidence')&&!p.startsWith('docs/reference/p0-'));
const sources=[];
for(const p of sourcePaths.sort()){
  const b=await fs.readFile(path.join(root,p)),binary=p.endsWith('.ttf');
  const data=binary?b:Buffer.from(b.toString('utf8').replace(/^\uFEFF/,'').replaceAll('\r\n','\n'));
  sources.push({path:p,sha256:sha256(data),encoding:binary?'raw':'UTF-8 LF without BOM'});
}
const outputs=new Map();
async function add(p){const absolute=path.isAbsolute(p)?p:path.join(root,p);const relative=path.relative(root,absolute).replaceAll('\\','/');if(relative.startsWith('../'))throw new Error('Evidence outside repository');const b=await fs.readFile(absolute);outputs.set(relative,{path:relative,sha256:sha256(b),bytes:b.length});}
async function walk(dir,filter=()=>true){for(const e of await fs.readdir(path.join(root,dir),{withFileTypes:true})){const p=dir+'/'+e.name;if(e.isDirectory())await walk(p,filter);else if(filter(p))await add(p);}}
for(const dir of ['artifacts/p2/jit','artifacts/p2/aot','artifacts/p2/reference','artifacts/p2/p1-regression','artifacts/p2/workload-aot','artifacts/p2/workload-jit'])await walk(dir);
await walk('artifacts/p2/review/jit',p=>!path.basename(p).startsWith('pair-'));
await walk('artifacts/packages/p2',p=>!p.includes('/repeat/'));
await walk('artifacts/package-consumer/winui/aot');
await add('artifacts/package-consumer/winui/package-consumer-report.json');
const headless=JSON.parse(await fs.readFile(path.join(root,'artifacts/p2/package-consumer/verification.json'),'utf8'));
await add('artifacts/p2/package-consumer/verification.json');
await walk(path.relative(root,headless.runPath).replaceAll('\\','/')+'/aot');
await walk(path.relative(root,headless.runPath).replaceAll('\\','/')+'/jit');
await walk(path.relative(root,headless.runPath).replaceAll('\\','/')+'/review');
for(const record of headless.files??[])await add(record.path);
await add('artifacts/p2/publish/VelociTex.P1.Tests.exe');
for(const p of ['artifacts/p2/differential-summary.json','artifacts/p2/image-summary.json','artifacts/p2/runtime-parity.json'])await add(p);
const manifest={schemaVersion:1,phase:'P2',sourceNormalization:'Committed text canonical UTF-8 LF without BOM; font/output files raw bytes. No timestamps included in equality.',sources,outputs:[...outputs.values()].sort((a,b)=>a.path.localeCompare(b.path))};
if(process.argv.includes('--check')){const previous=JSON.parse(await fs.readFile(path.join(root,target),'utf8'));if(JSON.stringify(previous)!==JSON.stringify(manifest))throw new Error('Phase evidence source or output drift');}
else await writeJson(path.join(root,target),manifest);
console.log(JSON.stringify({sources:sources.length,outputs:outputs.size,check:process.argv.includes('--check')}));
