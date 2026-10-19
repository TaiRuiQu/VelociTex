// Development-only browser/semantic oracle. All measurements use the locked
// KaTeX build; the C# engine is never consulted when generating expectations.
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import {createRequire} from 'node:module';
import {chromium} from 'playwright';
import {root,lock,packageRoot,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const require=createRequire(import.meta.url);
const katex=require(path.join(packageRoot,'dist/katex.js'));
const source=loadSource();
const corpusFile=path.join(root,'tests/fixtures/p1.json');
const corpusBytes=await fs.readFile(corpusFile),corpus=JSON.parse(corpusBytes);
const out=path.join(root,'artifacts/p1/reference');
const toolchain=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
const files=JSON.parse(await fs.readFile(path.join(root,'docs/reference/katex-0.19.0.files.json'),'utf8'));
for(const file of files.packageFiles) {
  if(file.path==='dist/katex.js'||file.path==='dist/katex.min.css'||file.path.startsWith('dist/fonts/')) {
    if(sha256(await fs.readFile(path.join(packageRoot,file.path)))!==file.sha256)throw new Error(`Upstream drift ${file.path}`);
  }
}
for(const file of files.sourceFiles)if(sha256(await fs.readFile(path.join(root,'artifacts/upstream/source',file.path)))!==file.sha256)throw new Error(`Source drift ${file.path}`);
if(sha256(await fs.readFile(toolchain.browserExecutable))!==toolchain.browserSha256)throw new Error('Browser bytes changed');
const normalize=value=>Array.isArray(value)?value.map(normalize):value&&typeof value==='object'?Object.fromEntries(Object.keys(value).filter(k=>k!=='loc').sort().map(k=>[k,normalize(value[k])])):value;
const options=f=>({displayMode:!!f.displayMode,throwOnError:true,trust:false,strict:'error',maxExpand:1000,macros:{...f.macros}});
const semantic=[];
for(const f of corpus.cases) {
  let distTree,sourceTree,distError,sourceError;
  try{distTree=normalize(katex.__parse(f.latex,options(f)));}catch(e){distError=e;}
  try{sourceTree=normalize(source.katex.__parse(f.latex,options(f)));}catch(e){sourceError=e;}
  if(f.expectation==='valid') {
    if(distError||sourceError)throw new Error(`${f.id}: ${distError??sourceError}`);
    if(JSON.stringify(distTree)!==JSON.stringify(sourceTree))throw new Error(`Source/distribution semantics differ ${f.id}`);
    semantic.push({id:f.id,category:f.category,result:'valid',tree:distTree});
  }else {
    if(distError?.name!=='ParseError'||sourceError?.name!=='ParseError')throw new Error(`Negative fixture unexpectedly accepted ${f.id}`);
    semantic.push({id:f.id,category:f.category,result:'invalid',error:{name:distError.name,position:distError.position??null,length:distError.length??null,message:distError.message}});
  }
}
await writeJson(path.join(out,'semantics.json'),semantic);
const server=http.createServer(async(req,res)=>{
  try {
    const url=new URL(req.url,'http://localhost');
    if(url.pathname==='/') {
      res.setHeader('Content-Type','text/html; charset=utf-8');
      return res.end('<!doctype html><meta charset="utf-8"><link rel="stylesheet" href="/dist/katex.min.css"><style>html,body{margin:0;padding:0}body{padding:16px;font-family:Arial,sans-serif}#host{display:inline-block;padding:8px}.katex-display{margin:0}*{animation:none!important;transition:none!important}</style><div id="host"></div><script src="/dist/katex.js"></script>');
    }
    const file=path.resolve(packageRoot,'.'+decodeURIComponent(url.pathname));
    if(!file.startsWith(packageRoot+path.sep))throw new Error('Path escape');
    res.setHeader('Content-Type',({'.js':'application/javascript','.css':'text/css','.woff2':'font/woff2','.ttf':'font/ttf'})[path.extname(file)]??'application/octet-stream');
    res.end(await fs.readFile(file));
  }catch{res.statusCode=404;res.end();}
});
await new Promise(r=>server.listen(0,'127.0.0.1',r));
const browser=await chromium.launch({executablePath:toolchain.browserExecutable,headless:true,args:['--force-color-profile=srgb','--disable-lcd-text']});
try {
  if(browser.version()!==toolchain.browserVersion||katex.version!==lock.npm.version)throw new Error('Reference version changed');
  const selected=new Set(['single-x','both-scripts','frac-display','sqrt-frac','roman','macro-arg']);
  const scenarios=[{fontSizeDip:24,dpiScale:1,theme:'light'}];
  if(process.argv.includes('--full-matrix'))for(const fontSizeDip of [16,24,32])for(const dpiScale of [1,1.25,1.5,2])for(const theme of ['light','dark'])if(fontSizeDip!==24||dpiScale!==1||theme!=='light')scenarios.push({fontSizeDip,dpiScale,theme});
  const results=[],assets=[];
  for(const scenario of scenarios) {
    const context=await browser.newContext({viewport:{width:1600,height:900},deviceScaleFactor:scenario.dpiScale,colorScheme:scenario.theme});
    const page=await context.newPage();await page.goto(`http://127.0.0.1:${server.address().port}`);
    const scenarioId=`${scenario.fontSizeDip}dip-${scenario.dpiScale*100}pct-${scenario.theme}`;
    for(const f of corpus.cases.filter(f=>f.expectation==='valid'&&(scenarioId==='24dip-100pct-light'||selected.has(f.id)))) {
      const geometry=await page.evaluate(async({f,scenario,options})=>{
        const host=document.getElementById('host');host.style.fontSize=`${scenario.fontSizeDip/1.21}px`;
        document.body.style.background=scenario.theme==='dark'?'#161616':'#ffffff';document.body.style.color=scenario.theme==='dark'?'#eeeeee':'#111111';
        katex.render(f.latex,host,options);await document.fonts.ready;
        const html=host.querySelector('.katex-html'),outer=host.getBoundingClientRect();
        const marker=document.createElement('span');marker.style.cssText='display:inline-block;width:0;height:0;vertical-align:baseline;margin:0;padding:0';html.append(marker);
        const baseline=marker.getBoundingClientRect().y;
        const bases=[...html.querySelectorAll('.katex-base')],rects=bases.map(e=>e.getBoundingClientRect());
        // The base's inline line box can exceed the TeX box for short glyphs.
        // The direct strut is the independently measured natural math extent.
        const struts=bases.map(e=>e.querySelector(':scope > .katex-strut').getBoundingClientRect());
        const left=rects.length?Math.min(...rects.map(r=>r.left)):marker.getBoundingClientRect().x;
        const top=struts.length?Math.min(...struts.map(r=>r.top)):baseline;
        const bottom=struts.length?Math.max(...struts.map(r=>r.bottom)):baseline;
        const glyphs=[],walker=document.createTreeWalker(html,NodeFilter.SHOW_TEXT),texts=[];
        while(walker.nextNode())texts.push(walker.currentNode);
        for(const text of texts) {
          const parent=text.parentElement,css=getComputedStyle(parent);
          const m=document.createElement('span');m.style.cssText='display:inline-block;width:0;height:0;vertical-align:baseline;margin:0;padding:0';parent.append(m);
          const glyphBaseline=m.getBoundingClientRect().y;
          for(let i=0;i<text.length;) {
            const char=String.fromCodePoint(text.data.codePointAt(i));const range=document.createRange();range.setStart(text,i);range.setEnd(text,i+char.length);const r=range.getBoundingClientRect();
            glyphs.push({text:char,x:r.x-left,y:glyphBaseline-baseline,fontSizeDip:parseFloat(css.fontSize),fontFamily:css.fontFamily,fontStyle:css.fontStyle,fontWeight:css.fontWeight});i+=char.length;
          }m.remove();
        }
        const rules=[...html.querySelectorAll('.frac-line')].map(e=>{const r=e.getBoundingClientRect(),border=parseFloat(getComputedStyle(e).borderBottomWidth);return{x:r.x-left,y:r.bottom-border-baseline,width:r.width,height:border,layoutBoxHeight:r.height};});
        const paths=[...html.querySelectorAll('svg')].map(e=>{const r=e.getBoundingClientRect(),m=e.querySelector('path').getScreenCTM();return{x:r.x-left,y:r.y-baseline,width:r.width,height:r.height,viewBox:e.getAttribute('viewBox'),preserveAspectRatio:e.getAttribute('preserveAspectRatio'),transform:{x:m.e-left,y:m.f-baseline,scaleX:m.a,scaleY:m.d}};});
        const normalizeXml=node=>node.nodeType===Node.TEXT_NODE?{type:'text',text:node.data}:{namespace:node.namespaceURI,name:node.localName,attributes:Object.fromEntries([...node.attributes].map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b))),children:[...node.childNodes].map(normalizeXml)};
        return{advanceDip:rects.reduce((v,r)=>v+r.width,0),ascentDip:baseline-top,depthDip:bottom-baseline,origin:{x:left-outer.x,y:baseline-outer.y},glyphs,rules,paths,mathml:normalizeXml(host.querySelector('math')),mathmlText:new XMLSerializer().serializeToString(host.querySelector('math')),effectiveEmDip:parseFloat(getComputedStyle(host.querySelector('.katex')).fontSize)};
      },{f,scenario,options:options(f)});
      if(Math.abs(geometry.effectiveEmDip-scenario.fontSizeDip)>1e-5)throw new Error('Em scale changed');
      const dir=path.join(out,f.id,scenarioId);await fs.mkdir(dir,{recursive:true});
      const image=await page.locator('#host').screenshot({animations:'disabled'});await fs.writeFile(path.join(dir,'reference.png'),image);
      // A second capture retains identical layout but exposes real alpha. RGB
      // contrast in an opaque screenshot cannot recover font coverage because
      // the browser applies theme-dependent font contrast/gamma adjustments.
      await page.evaluate(()=>{document.body.style.background='transparent';});
      const transparent=await page.locator('#host').screenshot({animations:'disabled',omitBackground:true});
      await fs.writeFile(path.join(dir,'reference.transparent.png'),transparent);
      await writeJson(path.join(dir,'reference.json'),geometry);
      results.push({id:f.id,category:f.category,scenario:scenarioId,...scenario,geometry});
      assets.push({path:path.relative(root,path.join(dir,'reference.png')).replaceAll('\\','/'),sha256:sha256(image)});
      assets.push({path:path.relative(root,path.join(dir,'reference.transparent.png')).replaceAll('\\','/'),sha256:sha256(transparent)});
    }
    await context.close();console.log(`${scenarioId}: reference complete`);
  }
  await writeJson(path.join(out,'reference.json'),{schemaVersion:1,sourceCommit:lock.source.commit,corpusSha256:sha256(corpusBytes),semantics:semantic,results});
  await writeJson(path.join(root,'docs/reference/p1-reference.evidence.json'),{schemaVersion:2,sourceCommit:lock.source.commit,npmSha256:lock.npm.sha256,corpusSha256:sha256(corpusBytes),browserVersion:browser.version(),browserSha256:toolchain.browserSha256,valid:semantic.filter(x=>x.result==='valid').length,invalid:semantic.filter(x=>x.result==='invalid').length,scenarios:scenarios.length,measurements:results.length,sourceAndDistributionSemanticsAgree:true,measurement:'Advance uses real .katex-base width; natural ascent/depth uses direct .katex-strut rectangles, avoiding CSS inline line boxes. Formula and individual text baselines measured with zero-size inline markers, positions relative to natural formula left / baseline. Fraction rules measure painted border-bottom, retaining its separate CSS layout height. No C# data used.',assets});
  console.log(JSON.stringify({valid:semantic.filter(x=>x.result==='valid').length,invalid:semantic.filter(x=>x.result==='invalid').length,measurements:results.length}));
}finally{await browser.close();await new Promise(r=>server.close(r));}
