import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import os from 'node:os';
import {execFileSync} from 'node:child_process';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';
import {createRequire} from 'node:module';
import {root,lock,packageRoot,writeJson,sha256} from './acquire.mjs';
import {deriveFormulaGeometry} from './geometry.mjs';

const require=createRequire(import.meta.url);
const katex=require(path.join(packageRoot,'dist/katex.js'));
if(katex.version!==lock.npm.version) throw new Error('Wrong KaTeX runtime');
const corpus=JSON.parse(await fs.readFile(path.join(root,'docs/examples/formulas.json'),'utf8'));
const output=path.join(root,'artifacts/reference');
await fs.mkdir(output,{recursive:true});
await fs.copyFile(new URL(import.meta.url),path.join(output,'reference.capture-source.mjs'));
const executable=process.env.VELOCITEX_BROWSER??'C:/Program Files/Google/Chrome/Application/chrome.exe';
const browser=await chromium.launch({executablePath:executable,headless:true,args:['--force-color-profile=srgb','--disable-lcd-text']});
const toolchain={node:process.versions.node,npm:execFileSync(process.platform==='win32'?'npm.cmd':'npm',['--version'],{shell:process.platform==='win32',encoding:'utf8'}).trim(),playwright:require('playwright/package.json').version,typescript:require('typescript/package.json').version,pngjs:require('pngjs/package.json').version,browserVersion:browser.version(),browserExecutable:path.resolve(executable),browserSha256:sha256(await fs.readFile(executable)),browserAcquisition:'Locally installed Chrome, pinned by version and executable SHA-256. Official Playwright Chromium v1194 download was attempted and canceled after no progress; automatic browser acquisition is not verified.'};
const toolLockPath=path.join(root,'docs/reference/development-toolchain.lock.json');
try {
  const previous=JSON.parse(await fs.readFile(toolLockPath,'utf8'));
  for(const key of ['node','npm','playwright','typescript','pngjs','browserVersion','browserSha256']) if(previous[key]!==toolchain[key]) throw new Error(`Toolchain drift ${key}: ${previous[key]} != ${toolchain[key]}`);
} catch(error) { if(error.code!=='ENOENT') throw error; }
await writeJson(toolLockPath,toolchain);

const server=http.createServer(async(req,res)=>{
  try {
    const url=new URL(req.url,'http://localhost');
    if(url.pathname==='/') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      res.end('<!doctype html><html><head><meta charset="utf-8"><link rel="stylesheet" href="/vendor/dist/katex.min.css"><style>html,body{margin:0;padding:0}body{padding:32px;font-family:Arial,sans-serif}*{animation:none!important;transition:none!important}#host{padding:16px;display:inline-block}.katex-display{margin:0}</style></head><body><div id="host"></div><script src="/vendor/dist/katex.js"></script></body></html>');
      return;
    }
    const base=url.pathname.startsWith('/vendor/')?packageRoot:root;
    const relative=url.pathname.startsWith('/vendor/')?url.pathname.slice('/vendor/'.length):url.pathname.slice(1);
    const file=path.resolve(base,decodeURIComponent(relative));
    if(!file.startsWith(base+path.sep)) throw new Error('Path traversal');
    const type={'.css':'text/css','.js':'application/javascript','.woff2':'font/woff2','.ttf':'font/ttf','.html':'text/html; charset=utf-8'}[path.extname(file)]??'application/octet-stream';
    res.setHeader('Content-Type',type);res.end(await fs.readFile(file));
  } catch {res.statusCode=404;res.end('not found');}
});
await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));
const origin=`http://127.0.0.1:${server.address().port}`;
const filesManifest=JSON.parse(await fs.readFile(path.join(root,'docs/reference/katex-0.19.0.files.json'),'utf8'));
const normalize=value=>{
  if(Array.isArray(value))return value.map(normalize);
  if(value&&typeof value==='object')return Object.fromEntries(Object.keys(value).filter(key=>key!=='loc').sort().map(key=>[key,normalize(value[key])]));
  return value;
};
const optionsFor=fixture=>({displayMode:fixture.displayMode,throwOnError:true,trust:false,maxExpand:corpus.defaults.maxExpand,macros:{},strict:'warn'});
const semanticSummary=[];
for(const fixture of corpus.cases) {
  const directory=path.join(output,fixture.id);
  await writeJson(path.join(directory,'input.json'),fixture);
  if(fixture.expectation==='excluded') {semanticSummary.push({id:fixture.id,result:'excluded'});continue;}
  try {
    const semantic=normalize(katex.__parse(fixture.latex,optionsFor(fixture)));
    if(fixture.expectation!=='valid')throw new Error(`Unexpected success: ${fixture.id}`);
    await writeJson(path.join(directory,'reference.semantic.json'),semantic);
    semanticSummary.push({id:fixture.id,result:'valid',semanticSha256:sha256(Buffer.from(JSON.stringify(semantic)))});
  } catch(error) {
    if(fixture.expectation!=='invalid'||error.name!=='ParseError')throw error;
    const diagnostic={name:error.name,message:error.message,position:error.position??null,length:error.length??null};
    await writeJson(path.join(directory,'reference.error.json'),diagnostic);
    semanticSummary.push({id:fixture.id,result:'invalid'});
  }
}
const valid=corpus.cases.filter(x=>x.expectation==='valid');
let randomState=0x19_00_2026;
const random=()=>{randomState^=randomState<<13;randomState^=randomState>>>17;randomState^=randomState<<5;return(randomState>>>0)/4294967296;};
const shuffle=items=>{const result=[...items];for(let i=result.length-1;i>0;i--){const j=Math.floor(random()*(i+1));[result[i],result[j]]=[result[j],result[i]];}return result;};
const scenarios=process.argv.includes('--full-matrix')?[16,24,32].flatMap(fontSizeDip=>[1,1.25,1.5,2].flatMap(dpiScale=>['light','dark'].map(theme=>({fontSizeDip,dpiScale,theme})))):[{fontSizeDip:24,dpiScale:1,theme:'light'}];
const geometrySummary=[];
let firstPage;
for(const scenario of scenarios) {
  const context=await browser.newContext({viewport:{width:1000,height:1000},deviceScaleFactor:scenario.dpiScale,colorScheme:scenario.theme,locale:'en-US',timezoneId:'Asia/Shanghai'});
  const page=await context.newPage();
  await page.goto(origin);
  await page.evaluate(async()=>{await document.fonts.ready;await Promise.all([...document.fonts].map(f=>f.load()));});
  const scenarioId=`${scenario.fontSizeDip}dip-${scenario.dpiScale*100}pct-${scenario.theme}`;
  for(const fixture of shuffle(valid)) {
    const measured=await page.evaluate(async({fixture,scenario,options})=>{
      const host=document.getElementById('host');
      document.body.style.background=scenario.theme==='dark'?'#161616':'#ffffff';
      document.body.style.color=scenario.theme==='dark'?'#eeeeee':'#111111';
      host.style.fontSize=`${scenario.fontSizeDip/1.21}px`;
      host.style.width=fixture.displayMode?`${fixture.containerWidthDip??880}px`:'max-content';
      katex.render(fixture.latex,host,options);
      await document.fonts.ready;
      const html=host.querySelector('.katex-html');
      const marker=document.createElement('span');marker.id='baseline';marker.style.cssText='display:inline-block;width:0;height:0;padding:0;margin:0;vertical-align:baseline';html.append(marker);
      const rect=element=>{const r=element.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height};};
      const origin=rect(host);
      const relative=r=>({x:r.x-origin.x,y:r.y-origin.y,width:r.width,height:r.height});
      const normalizeXml=node=>{
        if(node.nodeType===Node.TEXT_NODE)return{type:'text',text:node.data};
        return{namespace:node.namespaceURI,name:node.localName,attributes:Object.fromEntries([...node.attributes].map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b))),children:[...node.childNodes].map(normalizeXml)};
      };
      const math=host.querySelector('math');
      const mathml=new XMLSerializer().serializeToString(math);
      const glyphRanges=[];
      const walker=document.createTreeWalker(html,NodeFilter.SHOW_TEXT);
      while(walker.nextNode()) {
        const node=walker.currentNode;
        for(let i=0;i<node.length;) {
          const text=String.fromCodePoint(node.data.codePointAt(i));
          const range=document.createRange();range.setStart(node,i);range.setEnd(node,i+text.length);
          const r=range.getBoundingClientRect();
          const css=getComputedStyle(node.parentElement);
          glyphRanges.push({text,...relative(r),fontFamily:css.fontFamily,fontStyle:css.fontStyle,fontWeight:css.fontWeight,fontSize:css.fontSize});
          i+=text.length;
        }
      }
      const nodes=[...html.querySelectorAll('span,svg,path,line')].filter(x=>x.id!=='baseline').map((element,index)=>({index,tag:element.localName,classes:element.getAttribute('class')??'',...relative(rect(element))}));
      return {host:origin,logical:relative(rect(html)),baselineDip:marker.getBoundingClientRect().y-origin.y,effectiveEmDip:parseFloat(getComputedStyle(host.querySelector('.katex')).fontSize),nodes,glyphRanges,mathml,normalizedMathml:normalizeXml(math),fonts:[...document.fonts].map(f=>({family:f.family,weight:f.weight,style:f.style,status:f.status})),userAgent:navigator.userAgent};
    },{fixture,scenario,options:optionsFor(fixture)});
    if(measured.fonts.some(x=>x.status!=='loaded'))throw new Error(`Unloaded font in ${fixture.id}`);
    if(Math.abs(measured.effectiveEmDip-scenario.fontSizeDip)>0.001)throw new Error('KaTeX scaling applied incorrectly');
    const directory=path.join(output,fixture.id,scenarioId);
    await fs.mkdir(directory,{recursive:true});
    const bytes=await page.locator('#host').screenshot({animations:'disabled'});
    await fs.writeFile(path.join(directory,'reference.png'),bytes);
    const image=PNG.sync.read(bytes);
    const background=scenario.theme==='dark'?[22,22,22]:[255,255,255];
    let minX=image.width,minY=image.height,maxX=-1,maxY=-1;
    for(let y=0;y<image.height;y++)for(let x=0;x<image.width;x++) {
      const offset=(y*image.width+x)*4;
      if(background.some((v,i)=>Math.abs(image.data[offset+i]-v)>3)) {minX=Math.min(minX,x);minY=Math.min(minY,y);maxX=Math.max(maxX,x);maxY=Math.max(maxY,y);}
    }
    measured.inkBoundsDevicePixels=maxX<0?null:{x:minX,y:minY,width:maxX-minX+1,height:maxY-minY+1};
    measured.screenshot={width:image.width,height:image.height,sha256:sha256(bytes)};
    const {mathml,normalizedMathml,...measuredGeometry}=measured;
    const geometry=deriveFormulaGeometry(measuredGeometry);
    await writeJson(path.join(directory,'reference.geometry.raw.json'),measuredGeometry);
    await fs.writeFile(path.join(directory,'reference.mathml'),mathml+'\n');
    await writeJson(path.join(directory,'reference.mathml.normalized.json'),normalizedMathml);
    await writeJson(path.join(directory,'reference.geometry.json'),geometry);
    if(scenarioId==='24dip-100pct-light') {
      await fs.copyFile(path.join(directory,'reference.mathml'),path.join(output,fixture.id,'reference.mathml'));
      await fs.copyFile(path.join(directory,'reference.geometry.json'),path.join(output,fixture.id,'reference.geometry.json'));
      await fs.copyFile(path.join(directory,'reference.png'),path.join(output,fixture.id,'reference.png'));
    }
    geometrySummary.push({id:fixture.id,scenario:scenarioId,baselineDip:geometry.baselineDip,widthDip:geometry.logical.width,heightDip:geometry.logical.height,screenshotSha256:geometry.screenshot.sha256});
  }
  if(!firstPage) firstPage={context,page}; else await context.close();
  console.log(`${scenarioId}: ${valid.length} rendered after font readiness`);
}

// Recreate the same reference five times. Screenshots are outside timed work;
// geometry and pixel hashes measure reference tooling noise directly.
const noise=[];
const page=firstPage.page;
for(const fixture of valid) {
  const repetitions=[];
  for(let repeat=0;repeat<5;repeat++) {
    const geometry=await page.evaluate(async({fixture,options})=>{
      const host=document.getElementById('host');host.style.fontSize=`${24/1.21}px`;host.style.width=fixture.displayMode?`${fixture.containerWidthDip??880}px`:'max-content';document.body.style.background='#fff';document.body.style.color='#111';
      katex.render(fixture.latex,host,options);await document.fonts.ready;
      const html=host.querySelector('.katex-html'),r=html.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height};
    },{fixture,options:optionsFor(fixture)});
    const bytes=await page.locator('#host').screenshot({animations:'disabled'});
    repetitions.push({geometry,pngSha256:sha256(bytes)});
  }
  const geometryMaxDeltaDip=Math.max(...repetitions.flatMap(x=>Object.keys(x.geometry).map(k=>Math.abs(x.geometry[k]-repetitions[0].geometry[k]))));
  noise.push({id:fixture.id,repetitions,geometryMaxDeltaDip,distinctPngHashes:new Set(repetitions.map(x=>x.pngSha256)).size});
}
await writeJson(path.join(output,'reference-noise.json'),{seed:'0x19002026',repetitions:5,fixtures:noise,maxGeometryDeltaDip:Math.max(...noise.map(x=>x.geometryMaxDeltaDip)),nonidenticalScreenshotFixtures:noise.filter(x=>x.distinctPngHashes!==1).length});
const timing=await page.evaluate(({fixtures,maxExpand})=>{
  const host=document.getElementById('host');
  const options=f=>({displayMode:f.displayMode,throwOnError:true,maxExpand,macros:{},strict:'ignore'});
  for(let i=0;i<100;i++){const f=fixtures[i%fixtures.length];katex.render(f.latex,host,options(f));host.getBoundingClientRect();}
  const samples=[];
  for(let i=0;i<1050;i++) {
    const f=fixtures[(i*13)%fixtures.length];const start=performance.now();katex.render(f.latex,host,options(f));const endMarkup=performance.now();host.getBoundingClientRect();const endLayout=performance.now();samples.push({fixtureId:f.id,category:f.category,markupMs:endMarkup-start,domAndLayoutMs:endLayout-start});
  }
  return samples;
},{fixtures:shuffle(valid),maxExpand:corpus.defaults.maxExpand});
await writeJson(path.join(output,'browser-timing.samples.json'),{metric:'Warmed KaTeX render plus forced synchronous DOM layout CPU wall time; excludes actual drawing/presentation, frame waits and screenshot encoding. No end-to-end multiple or old-CPU acceptance claim.',samples:timing});
if(process.argv.includes('--font-calibration')) {
  await page.goto(`${origin}/artifacts/fonts/font-calibration.html`);
  const calibration=await page.evaluate(()=>window.fontCalibrationReady);
  await writeJson(path.join(root,'artifacts/fonts/browser-calibration.json'),calibration);
  await page.screenshot({path:path.join(root,'artifacts/fonts/font-calibration.png'),fullPage:true});
}
await writeJson(path.join(output,'run-manifest.json'),{schemaVersion:1,runDate:new Date().toISOString(),sourceCommit:lock.source.commit,npmSha256:lock.npm.sha256,packageFilesManifestSha256:sha256(await fs.readFile(path.join(root,'docs/reference/katex-0.19.0.files.json'))),corpusSha256:sha256(await fs.readFile(path.join(root,'docs/examples/formulas.json'))),toolchain,os:{platform:os.platform(),release:os.release(),arch:os.arch(),cpu:os.cpus()[0]?.model},seed:'0x19002026',fontScaling:'Host em = FontSizeDip / 1.21; measured .katex effective em asserted against requested DIP.',scenarios,fontFiles:filesManifest.packageFiles.filter(x=>x.path.startsWith('dist/fonts/')),generatorSha256:Object.fromEntries(await Promise.all(['acquire.mjs','inventory.mjs','load-source.mjs','reference.mjs','package-lock.json'].map(async name=>[name,sha256(await fs.readFile(new URL(name,import.meta.url)))])))});
await writeJson(path.join(output,'summary.json'),{semantic:semanticSummary,referenceScreenshots:geometrySummary.length,scenarios:scenarios.length,geometries:geometrySummary,noise:{maxGeometryDeltaDip:Math.max(...noise.map(x=>x.geometryMaxDeltaDip)),nonidenticalScreenshotFixtures:noise.filter(x=>x.distinctPngHashes!==1).length},timingSamples:timing.length,nativeComparison:'not-executed; P0 native probes use hard-coded data and do not count as compatibility'});
await browser.close();
await new Promise(resolve=>server.close(resolve));
console.log(JSON.stringify({fixtures:semanticSummary.length,referenceScreenshots:geometrySummary.length,noiseMaxDip:Math.max(...noise.map(x=>x.geometryMaxDeltaDip))}));
