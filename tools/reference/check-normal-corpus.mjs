import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {root,packageRoot,lock,sha256,writeJson} from './acquire.mjs';
const require=createRequire(import.meta.url);
const katex=require(path.join(packageRoot,'dist/katex.js'));
if(katex.version!==lock.npm.version)throw new Error('Normal workload uses wrong KaTeX version');
const file=path.join(root,'benchmarks/corpus.normal.json');
const bytes=await fs.readFile(file);
const corpus=JSON.parse(bytes);
if(corpus.fixtures.length!==60||corpus.categories.length!==6)throw new Error('Normal workload denominator changed');
if(Math.abs(corpus.categories.reduce((sum,x)=>sum+x.weight,0)-1)>1e-12)throw new Error('Category weights do not sum to one');
const ids=new Set(),results=[];
for(const category of corpus.categories)if(corpus.fixtures.filter(x=>x.category===category.id).length!==10)throw new Error(`Expected ten cases for ${category.id}`);
for(const fixture of corpus.fixtures) {
  if(ids.has(fixture.id))throw new Error('Duplicate corpus fixture ID');ids.add(fixture.id);
  if(fixture.latex.length>corpus.normalInputEnvelope.maxInputUtf16)throw new Error('Normal formula exceeds frozen input envelope');
  const options={...corpus.referenceOptions,displayMode:fixture.displayMode,macros:{}};
  const parse=katex.__parse(fixture.latex,options);
  const mathml=katex.renderToString(fixture.latex,{...options,macros:{},output:'mathml'});
  if(!mathml.includes('<math')||!mathml.includes('http://www.w3.org/1998/Math/MathML'))throw new Error(`Missing MathML ${fixture.id}`);
  const normalize=value=>Array.isArray(value)?value.map(normalize):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).filter(x=>x!=='loc').sort().map(x=>[x,normalize(value[x])])):value;
  const semantic=normalize(parse);
  if(fixture.dimensions) {
    const arrays=[];
    const walk=value=>{if(!value||typeof value!=='object')return;if(value.type==='array')arrays.push(value);for(const item of Object.values(value))walk(item);};
    walk(semantic);
    if(arrays.length!==1||arrays[0].body.length!==fixture.dimensions.rows||arrays[0].body.some(row=>row.length!==fixture.dimensions.columns))throw new Error(`Wrong matrix dimensions ${fixture.id}`);
  }
  results.push({id:fixture.id,category:fixture.category,inputUtf16:fixture.latex.length,semanticSha256:sha256(Buffer.from(JSON.stringify(semantic))),mathmlSha256:sha256(Buffer.from(mathml))});
}
const result={schemaVersion:1,referenceVersion:katex.version,sourceCommit:lock.source.commit,npmSha256:lock.npm.sha256,corpusSha256:sha256(bytes),status:'60 frozen normal inputs parsed and produced MathML in exact KaTeX 0.19.0; no C# or browser timing was performed by this check.',categories:corpus.categories,maximumInputUtf16:Math.max(...results.map(x=>x.inputUtf16)),fixtures:results};
await writeJson(path.join(root,'docs/reference/normal-corpus-0.19.0.check.json'),result);
console.log(JSON.stringify({fixtures:results.length,categories:corpus.categories.length,maximumInputUtf16:result.maximumInputUtf16,corpusSha256:result.corpusSha256}));
