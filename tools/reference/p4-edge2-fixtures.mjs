import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const {katex}=loadSource();
const examples=[
 [String.raw`\frac\sqrt12`,'invalid'],
 [String.raw`\frac\frac12ab`,'invalid'],
 [String.raw`\hat\frac12`,'invalid'],
 [String.raw`\sqrt\frac12`],
 [String.raw`\sqrt[3]\frac12`,'invalid'],
 [String.raw`x^\frac12`],
 [String.raw`x^{\sum_i^n i}`],
 [String.raw`\mathop\frac12`],
 [String.raw`\mathord\frac12`],
 [String.raw`\text\sqrt12`,'invalid'],
 [String.raw`\text{\]}`,'invalid',false,['function-c9d6fcd1d11b3424']],
 [String.raw`\text{क्‍ष}`],
 [String.raw`\text{क्‌ष}`],
 [String.raw`\text{Aनमस्तेB}`],
 [String.raw`\text{กำ}`],
 [String.raw`\text{한}`],
 [String.raw`\text{العربية}`],
 [String.raw`\text{AالعربيةB}`],
 [String.raw`\hline`,'invalid',false,['function-4c5b1d0f000ac0ff']],
 [String.raw`\hdashline`,'invalid',false,['function-a4c9ed8cf65e45b6']],
 [String.raw`\verb|unterminated`,'invalid',false,['function-547d2840d5717c02']],
 [String.raw`\text{\)}`,'invalid',false,['function-c07f428d83fad218']],
];
const cases=[];
for(const [index,[latex,expectation='valid',displayMode=false,registrationIds]] of examples.entries()){
 let success=true,upstreamErrorName;try{katex.renderToString(latex,{displayMode,throwOnError:true,strict:'ignore',trust:false});}catch(error){success=false;upstreamErrorName=error.name;}
 if(success!==(expectation==='valid'))throw new Error('Edge input expectation differs from locked upstream: '+latex);
 cases.push({id:'p4-edge2-'+String(index+1).padStart(3,'0'),category:'argument-and-contextual-run-edges',latex,expectation,displayMode,referenceOptions:{strict:'ignore'},...(registrationIds?{registrationIds}:{}),...(upstreamErrorName?{upstreamErrorName}:{}),provenance:'Extra source-verified cases frozen separately after the main and first edge corpus; no native-result selection.'});
}
const result={schemaVersion:1,phase:'P4',reference:lock.source,counts:{valid:cases.filter(x=>x.expectation==='valid').length,invalid:cases.filter(x=>x.expectation==='invalid').length},cases};
const target=path.join(root,'tests/fixtures/p4-edge2.json');
if(process.argv.includes('--check')){if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(result))throw new Error('P4 edge2 drift');}else await writeJson(target,result);
console.log(JSON.stringify({...result.counts,sha256:sha256(await fs.readFile(target))}));
