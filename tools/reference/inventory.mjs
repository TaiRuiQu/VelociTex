import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,writeJson,sha256} from './acquire.mjs';
import {loadSource} from './load-source.mjs';

const loaded=loadSource();
const {katex,functions,environments,macros,symbols,implicitCommands,registrations,propertySources}=loaded;
const sources=new Map();
for(const registration of registrations) {
  const {kind,args,source}=registration;
  let names=[];
  if(['function','environment'].includes(kind)) names=args[0].names;
  else if(kind==='macro') names=[args[0]];
  else if(kind==='symbol') names=args[5]&&args[3]?[args[4],args[3]]:[args[4]];
  else if(kind==='builder') names=[args[0].type];
  for(const name of names) sources.set(`${kind}:${kind==='symbol'?args[0]+':':''}${name}`,source);
}
const browserCommands=new Map([
  ['\\href','External hyperlinks are outside single-formula native math scope.'],
  ['\\url','URL and hyperlink behavior is excluded.'],
  ['\\includegraphics','External image and resource loading is excluded.'],
  ['\\htmlClass','Browser DOM class injection is excluded.'],
  ['\\htmlId','Browser DOM identifier injection is excluded.'],
  ['\\htmlStyle','Browser CSS injection is excluded.'],
  ['\\htmlData','Browser DOM attribute injection is excluded.'],
]);
const excludedOptions=new Map([
  ['trust','Browser/resource trust and JavaScript callback policies are excluded.'],
]);
const entries=[];
function add(kind,name,data,mode) {
  const source=kind==='option'?propertySources.get(`SETTINGS_SCHEMA:${name}`):kind==='implicit'?propertySources.get(`implicitCommands:${name}`):sources.get(`${kind}:${mode?mode+':':''}${name}`);
  if(!source) throw new Error(`No AST-observed registration source for ${kind}:${mode??''}:${name}`);
  const reason=kind==='option'?excludedOptions.get(name):browserCommands.get(name);
  const internal=name.includes('@')||name.startsWith('\\@');
  const id=`${kind}${mode?'-'+mode:''}-${sha256(Buffer.from(name)).slice(0,16)}`;
  const status=reason?'excluded':'not-implemented';
  const item={id,kind,name,...(mode?{mode}:{}),scope:reason?'excluded':'included',scopeReason:reason??(internal?'Required core helper semantics; HTML-named helpers remain included.':'Locked 0.19.0 core math registration.'),classification:reason?'browser-or-resource':internal?'core-helper':'core-math',source:{...source,commit:lock.source.commit},fixtureId:`coverage-${id}`,fixtureStatus:'planned',status:{semantic:status,geometry:status,mathml:status,native:status,png:status,svg:status},upstream:{}};
  if(kind==='function'||kind==='environment') item.upstream={nodeType:data.type,numArgs:data.numArgs??0,numOptionalArgs:data.numOptionalArgs??0,argTypes:data.argTypes??[],allowedInText:data.allowedInText??false,allowedInMath:data.allowedInMath??true,...(kind==='function'?{aliases:data.names.filter(x=>x!==name)}:{})};
  else if(kind==='symbol') item.upstream={...data};
  else if(kind==='macro') item.upstream={definitionType:typeof data,definition:typeof data==='string'?data:null,requiredBuiltinCallback:typeof data==='function'};
  else if(kind==='option') {
    item.upstream={description:data.description??null,type:data.type,default:Object.hasOwn(data,'default')?(Number.isFinite(data.default)||typeof data.default!=='number'?data.default:String(data.default)):null};
    if(name==='output') {
      item.scopeReason='MathML export semantics are required. HTML serialization values are excluded; native visual and export APIs are designed separately.';
      item.optionValueScope={mathml:'included',html:'excluded-browser-serialization',htmlAndMathml:'mathml-semantics-included; HTML-serialization-excluded'};
    }
    if(name==='macros') item.optionValueScope={stringDefinitions:'included',tokenDefinitions:'included-semantics',javascriptCallbacks:'excluded-user-extension'};
    if(name==='strict') item.optionValueScope={boolean:'included',enum:'included',javascriptCallbacks:'excluded-user-extension'};
  }
  entries.push(item);
}
for(const [name,data] of Object.entries(functions._functions)) add('function',name,data);
for(const [name,data] of Object.entries(environments._environments)) add('environment',name,data);
for(const [name,data] of Object.entries(macros)) add('macro',name,data);
for(const [mode,map] of Object.entries(symbols)) for(const [name,data] of Object.entries(map)) add('symbol',name,data,mode);
for(const [name,data] of Object.entries(katex.SETTINGS_SCHEMA)) add('option',name,data);
for(const [name,data] of Object.entries(implicitCommands)) add('implicit',name,data);
for(const [name,source] of sources) if(name.startsWith('builder:')) {
  const node=name.slice('builder:'.length);
  const item={id:`builder-${node}`,kind:'builder',name:node,scope:'included',scopeReason:'Core parse node builders are transitive layout dependencies.',classification:'core-helper',source:{...source,commit:lock.source.commit},fixtureId:`coverage-builder-${node}`,fixtureStatus:'planned',status:Object.fromEntries(['semantic','geometry','mathml','native','png','svg'].map(x=>[x,'not-implemented'])),upstream:{html:Boolean(functions._htmlGroupBuilders[node]),mathml:Boolean(functions._mathmlGroupBuilders[node])}};
  entries.push(item);
}
entries.sort((a,b)=>a.id<b.id?-1:a.id>b.id?1:0);
const byKind={};
for(const item of entries) byKind[item.kind]=(byKind[item.kind]??0)+1;
const result={schemaVersion:1,reference:{version:lock.npm.version,commit:lock.source.commit,npmSha256:lock.npm.sha256},method:'TypeScript AST call instrumentation plus controlled execution of the exact source transitive registry graph; final registries determine the denominator.',manualReview:{date:'2026-10-19',policy:'Only explicitly named external resources, DOM/CSS injection and the browser trust option are wholly excluded. Output remains included for MathML semantics, with browser-only values separately annotated. CD, all core callback macros, internal HTML/MathML choice helpers and all other registrations remain included. Callback-style user extensions remain out of scope.',unclassified:0},summary:{total:entries.length,included:entries.filter(x=>x.scope==='included').length,excluded:entries.filter(x=>x.scope==='excluded').length,byKind},transitiveSourceModules:loaded.moduleSources.sort(),entries};
await writeJson(path.join(root,'compatibility/core-0.19.0.json'),result);
await writeJson(path.join(root,'artifacts/reference/inventory-summary.json'),{...result.summary,registrationEvents:registrations.length,transitiveModules:loaded.moduleSources.length,sha256:sha256(Buffer.from(JSON.stringify(result,null,2)+'\n'))});
console.log(JSON.stringify(result.summary));
