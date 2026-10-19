// Observe actual exact-source calls. Aliases share handler objects, so events
// use Parser.callFunction(context name) and environment context.envName rather
// than the property through which a shared function was wrapped.
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {root,sourceRoot,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const source=loadSource(),require=createRequire(import.meta.url);
const Parser=require(path.join(sourceRoot,'src/Parser.ts')).default;
const Expander=require(path.join(sourceRoot,'src/MacroExpander.ts')).default;
const inventory=JSON.parse(await fs.readFile(path.join(root,'compatibility/core-0.19.0.json'),'utf8'));
const files=['tests/fixtures/p4.json','tests/fixtures/p4-edge2.json'];
const cases=[],seen=new Set();for(const file of files){const corpus=JSON.parse(await fs.readFile(path.join(root,file),'utf8'));for(const c of corpus.cases)if(!seen.has(c.id)){seen.add(c.id);cases.push({...c,corpus:file});}}
const manifest=JSON.parse(await fs.readFile(path.join(root,'docs/reference/katex-0.19.0.files.json'),'utf8'));
for(const module of source.moduleSources){const entry=manifest.sourceFiles.find(x=>x.path===module);if(!entry||sha256(await fs.readFile(path.join(sourceRoot,module)))!==entry.sha256)throw new Error('Exact source drift: '+module);}
let events;
const call=Parser.prototype.callFunction;
Parser.prototype.callFunction=function(name,...args){events.add('function:'+name);return call.call(this,name,...args);};
const expansion=Expander.prototype._getExpansion;
Expander.prototype._getExpansion=function(name){const actual=this.macros.get(name);if(actual!=null)events.add('macro:'+name);return expansion.call(this,name);};
const envSeen=new Set();
for(const spec of Object.values(source.environments._environments))if(!envSeen.has(spec)){envSeen.add(spec);const handler=spec.handler;spec.handler=function(context,...args){events.add('environment:'+context.envName);return handler.call(this,context,...args);};}
for(const [kind,table]of [['html',source.functions._htmlGroupBuilders],['mathml',source.functions._mathmlGroupBuilders]])for(const [name,handler]of Object.entries(table))table[name]=function(...args){events.add('builder:'+name);return handler.apply(this,args);};
const all=new Map(),runs=[];
const oldLog=console.log,oldWarn=console.warn,oldError=console.error;
try{
 console.log=console.warn=console.error=()=>{};
 for(const fixture of cases){
  events=new Set();let success=true,errorName;
  try{source.katex.renderToString(fixture.latex,{displayMode:!!fixture.displayMode,strict:'ignore',trust:false,throwOnError:true,macros:{...fixture.macros},...fixture.options,...fixture.referenceOptions});}
  catch(error){success=false;errorName=error.name;}
  for(const event of events){if(!all.has(event))all.set(event,[]);all.get(event).push(fixture.id);}
  runs.push({fixtureId:fixture.id,corpus:fixture.corpus,source:fixture.latex,success,...(errorName?{errorName}:{}),events:[...events].sort()});
 }
}finally{console.log=oldLog;console.warn=oldWarn;console.error=oldError;}
const identities=[];
for(const entry of inventory.entries.filter(e=>e.scope==='included'&&['function','macro','environment','builder'].includes(e.kind))){
 const fixtureIds=[...new Set(all.get(entry.kind+':'+entry.name)??[])];
 identities.push({id:entry.id,kind:entry.kind,name:entry.name,status:fixtureIds.length?'actual-source-call':'unexercised',fixtureIds,...(!fixtureIds.length?{reason:'No actual call event reached this exact registered name. Alias reuse alone is insufficient.'}:{})});
}
const unexercised=identities.filter(x=>x.fixtureIds.length===0);
const report={schemaVersion:1,reference:lock.source,policy:'Exact source methods and builders are observed during actual rendering. Function/environment identity comes from call context, preserving aliases. Macro identity comes from successful registered-name definition lookup before its actual expansion. Failed inputs retain reached call events and their concrete error type. This is input execution evidence, not native compatibility.',counts:{fixtures:runs.length,identities:identities.length,exercised:identities.length-unexercised.length,unexercised:unexercised.length},identities,unexercised,runs,evidence:await Promise.all(files.map(async file=>({path:file,sha256:sha256(await fs.readFile(path.join(root,file)))})))};
const target=path.join(root,'tests/fixtures/p4-executed-identities.json');
if(process.argv.includes('--check')){if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(report))throw new Error('Executed registration identity audit drift');}else await writeJson(target,report);
console.log(JSON.stringify({...report.counts,unexercised:unexercised.map(x=>({kind:x.kind,name:x.name}))}));
