import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {root,packageRoot,writeJson,sha256} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const require=createRequire(import.meta.url);
const source=loadSource().katex;
const dist=require(path.join(packageRoot,'dist/katex.js'));
const cases=JSON.parse(await fs.readFile(path.join(root,'docs/examples/formulas.json'),'utf8')).cases;
const normalize=value=>{
  if(Array.isArray(value))return value.map(normalize);
  if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).filter(x=>x!=='loc').sort().map(x=>[x,normalize(value[x])]));
  return value;
};
const results=[];
for(const fixture of cases.filter(x=>x.expectation!=='excluded')) {
  const render=runtime=>{
    const options={displayMode:fixture.displayMode,macros:{},maxExpand:1000,strict:'ignore',output:'mathml'};
    try{return{ok:true,semantic:normalize(runtime.__parse(fixture.latex,options)),mathml:runtime.renderToString(fixture.latex,{...options,macros:{}})}}
    catch(error){return{ok:false,name:error.name,message:error.message,position:error.position??null};}
  };
  const a=JSON.stringify(render(source)),b=JSON.stringify(render(dist));
  if(a!==b)throw new Error(`Locked source/distribution differ: ${fixture.id}`);
  results.push({fixtureId:fixture.id,match:true,sha256:sha256(Buffer.from(a))});
}
await writeJson(path.join(root,'artifacts/reference/source-dist-check.json'),{fixtures:results.length,results});
console.log(`${results.length} source/distribution semantics, MathML and error results match`);
