// Development-only browser/semantic oracle. All measurements use the locked
// KaTeX build; the C# engine is never consulted when generating expectations.
import fs from 'node:fs/promises';
import path from 'node:path';
import http from 'node:http';
import {createRequire} from 'node:module';
import {fileURLToPath} from 'node:url';
import {chromium} from 'playwright';
import {root,lock,packageRoot,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const require=createRequire(import.meta.url);
const katex=require(path.join(packageRoot,'dist/katex.js'));
const source=loadSource();
const oracleCodeSha256=sha256(await fs.readFile(fileURLToPath(import.meta.url)));
const phase=process.argv.find(x=>x.startsWith('--phase='))?.slice(8)??'p1';
if(!['p1','p2','p4'].includes(phase))throw new Error('Unknown verification phase');
const corpusFile=path.join(root,`tests/fixtures/${phase}.json`);
const corpusBytes=await fs.readFile(corpusFile),corpus=JSON.parse(corpusBytes);
const only=process.argv.find(x=>x.startsWith('--only='))?.slice(7).split(',');
const cases=only?corpus.cases.filter(f=>only.includes(f.id)):corpus.cases;
if(only&&cases.length!==new Set(only).size)throw new Error('Unknown requested subset fixture');
const out=path.join(root,`artifacts/${phase}/${only?'reference-subset':'reference'}`);
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
const options=f=>({displayMode:!!f.displayMode,throwOnError:true,trust:false,strict:phase==='p4'?'ignore':'error',maxExpand:1000,macros:{...f.macros},...f.options});
const semantic=[];
for(const f of cases) {
  let distTree,sourceTree,distError,sourceError;
  try{distTree=normalize(katex.__parse(f.latex,options(f)));}catch(e){distError=e;}
  try{sourceTree=normalize(source.katex.__parse(f.latex,options(f)));}catch(e){sourceError=e;}
  if(f.expectation==='valid') {
    if(distError||sourceError)throw new Error(`${f.id}: ${distError??sourceError}`);
    if(JSON.stringify(distTree)!==JSON.stringify(sourceTree))throw new Error(`Source/distribution semantics differ ${f.id}`);
    semantic.push({id:f.id,category:f.category,result:'valid',tree:distTree});
  }else if(f.presentationFallback){
    if(distError||sourceError||JSON.stringify(distTree)!==JSON.stringify(sourceTree))throw new Error(`Tolerant presentation source/distribution differs ${f.id}`);
    let strictDist,strictSource;
    try{katex.__parse(f.latex,{...options(f),throwOnError:true});}catch(error){strictDist=error;}
    try{source.katex.__parse(f.latex,{...options(f),throwOnError:true});}catch(error){strictSource=error;}
    if(strictDist?.name!=='ParseError'||strictSource?.name!=='ParseError')throw new Error(`Presentation fixture has no strict parse diagnostic ${f.id}`);
    semantic.push({id:f.id,category:f.category,result:'invalid',presentationFallback:true,presentationTree:distTree,error:{name:strictDist.name,position:strictDist.position??null,length:strictDist.length??null,message:strictDist.message}});
  }else {
    const errorName=f.upstreamErrorName??'ParseError';
    if(distError?.name!==errorName||sourceError?.name!==errorName)throw new Error(`Negative fixture did not produce the frozen upstream error ${f.id}: ${distError?.name}/${sourceError?.name}, expected ${errorName}`);
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
  const selected=new Set(phase==='p1'?['single-x','both-scripts','frac-display','sqrt-frac','roman','macro-arg']:['p2-integral-display','p2-sum-display','p2-binom','p2-delimiter-tall','p2-widehat','p2-matrix-fraction','p2-aligned','p2-text-fonts']);
  const scenarios=[{fontSizeDip:24,dpiScale:1,theme:'light'}];
  if(process.argv.includes('--full-matrix'))for(const fontSizeDip of [16,24,32])for(const dpiScale of [1,1.25,1.5,2])for(const theme of ['light','dark'])if(fontSizeDip!==24||dpiScale!==1||theme!=='light')scenarios.push({fontSizeDip,dpiScale,theme});
  const results=[],presentations=[],assets=[];
  for(const scenario of scenarios) {
    const context=await browser.newContext({viewport:{width:1600,height:900},deviceScaleFactor:scenario.dpiScale,colorScheme:scenario.theme});
    const page=await context.newPage();page.setDefaultTimeout(30_000);await page.goto(`http://127.0.0.1:${server.address().port}`);
    const cdp=await context.newCDPSession(page);
    const scenarioId=`${scenario.fontSizeDip}dip-${scenario.dpiScale*100}pct-${scenario.theme}`;
    let fixtureIndex=0;
    for(const f of cases.filter(f=>(f.expectation==='valid'||f.presentationFallback)&&(scenarioId==='24dip-100pct-light'||selected.has(f.id)))) {
      if(++fixtureIndex%100===0)console.log(`${scenarioId}: ${fixtureIndex} fixtures, ${f.id}`);
      let requestedOutput=null,requestedImage=null;
      const renderOptions=options(f);
      if(renderOptions.output&&renderOptions.output!=='htmlAndMathml'){
        requestedOutput=await page.evaluate(async({f,scenario,options})=>{
          const host=document.getElementById('host');host.style.fontSize=`${scenario.fontSizeDip/1.21}px`;
          document.body.style.background=scenario.theme==='dark'?'#161616':'#ffffff';document.body.style.color=scenario.theme==='dark'?'#eeeeee':'#111111';
          katex.render(f.latex,host,options);await Promise.race([document.fonts.ready,new Promise((_,reject)=>setTimeout(()=>reject(new Error('Requested output fonts timed out')),30_000))]);
          return{mode:options.output,html:host.innerHTML,text:host.textContent,mathmlText:host.querySelector('math')?new XMLSerializer().serializeToString(host.querySelector('math')):null};
        },{f,scenario,options:renderOptions});
        requestedImage=await page.locator('#host').screenshot({animations:'disabled'});
      }
      const geometry=await page.evaluate(async({f,scenario,options})=>{
        const host=document.getElementById('host');host.style.fontSize=`${scenario.fontSizeDip/1.21}px`;
        document.body.style.background=scenario.theme==='dark'?'#161616':'#ffffff';document.body.style.color=scenario.theme==='dark'?'#eeeeee':'#111111';
        katex.render(f.latex,host,options);
        await Promise.race([document.fonts.ready,new Promise((_,reject)=>setTimeout(()=>reject(new Error('Reference fonts timed out')),30_000))]);
        const html=host.querySelector('.katex-html'),outer=host.getBoundingClientRect();
        if(f.presentationFallback){
          const error=host.querySelector('.katex-error')??html?.querySelector('.mord.text')??html;if(!error)throw new Error('The tolerant facade did not produce its actual colored source element');
          const css=getComputedStyle(error),rect=error.getBoundingClientRect();
          return{presentationFallback:true,sourceText:error.textContent,errorColor:css.color,title:error.title,html:host.innerHTML,mathmlText:host.querySelector('math')?.outerHTML??null,rect:{x:rect.x-outer.x,y:rect.y-outer.y,width:rect.width,height:rect.height},generatedTags:[],glyphs:[],effectiveEmDip:null};
        }
        const marker=document.createElement('span');marker.style.cssText='display:inline-block;width:0;height:0;vertical-align:baseline;margin:0;padding:0';html.append(marker);
        const baseline=marker.getBoundingClientRect().y;
        const bases=[...html.querySelectorAll('.katex-base')],rects=bases.map(e=>e.getBoundingClientRect());
        // The base's inline line box can exceed the TeX box for short glyphs.
        // The direct strut is the independently measured natural math extent.
        const struts=bases.map(e=>e.querySelector(':scope > .katex-strut').getBoundingClientRect());
        const left=rects.length?Math.min(...rects.map(r=>r.left)):marker.getBoundingClientRect().x;
        const top=struts.length?Math.min(...struts.map(r=>r.top)):baseline;
        const bottom=struts.length?Math.max(...struts.map(r=>r.bottom)):baseline;
        const alpha=color=>{const values=color.match(/[\d.]+/g)?.map(Number);return color==='transparent'?0:values?.length===4?values[3]:1;};
        const visible=(element,color)=>{
          for(let e=element;e&&e!==host.parentElement;e=e.parentElement){const s=getComputedStyle(e);if(s.visibility==='hidden'||s.visibility==='collapse'||s.display==='none'||Number(s.opacity)===0)return false;}
          return alpha(color)>0;
        };
        const glyphs=[],textRuns=[],walker=document.createTreeWalker(html,NodeFilter.SHOW_TEXT),texts=[];
        while(walker.nextNode())texts.push(walker.currentNode);
        for(const text of texts) {
          const parent=text.parentElement,css=getComputedStyle(parent),parentIndex=glyphs.length;
          parent.setAttribute('data-reference-glyph-parent',String(parentIndex));
          const m=document.createElement('span');m.style.cssText='display:inline-block;width:0;height:0;vertical-align:baseline;margin:0;padding:0';parent.append(m);
          const glyphBaseline=m.getBoundingClientRect().y;
          const fullRange=document.createRange();fullRange.selectNodeContents(text);const fullRect=fullRange.getBoundingClientRect();
          textRuns.push({text:text.data,x:fullRect.x-left,y:glyphBaseline-baseline,advanceDip:fullRect.width,fontSizeDip:parseFloat(css.fontSize),fontFamily:css.fontFamily,fontStyle:css.fontStyle,fontWeight:css.fontWeight,visible:visible(parent,css.color),color:css.color,domParent:parentIndex});
          for(let i=0;i<text.length;) {
            const char=String.fromCodePoint(text.data.codePointAt(i));const range=document.createRange();range.setStart(text,i);range.setEnd(text,i+char.length);const r=range.getBoundingClientRect();
            const glyph={text:char,x:r.x-left,y:glyphBaseline-baseline,rangeWidthDip:r.width,fontSizeDip:parseFloat(css.fontSize),fontFamily:css.fontFamily,fontStyle:css.fontStyle,fontWeight:css.fontWeight,visible:visible(parent,css.color),color:css.color,domParent:parentIndex};
            glyphs.push(glyph);
            // Text-shadow is inherited CSS paint, not an additional semantic
            // glyph. Project each actual computed zero/low-blur shadow into
            // its declared paint origin, with no source-name special cases.
            for(const shadow of css.textShadow==='none'?[]:css.textShadow.split(/,(?![^()]*\))/)){
              const color=shadow.match(/rgba?\([^)]*\)/)?.[0]??css.color;
              const lengths=shadow.replace(/rgba?\([^)]*\)/g,'').match(/-?[\d.]+px/g)?.map(parseFloat)??[];
              if(lengths.length>=2)glyphs.push({...glyph,x:glyph.x+lengths[0],y:glyph.y+lengths[1],color,visible:visible(parent,color),shadowBlurDip:lengths[2]??0,cssShadow:true});
            }
            i+=char.length;
          }m.remove();
        }
        const ruleElements=[...html.querySelectorAll('span')];
        const rules=ruleElements.flatMap(e=>{
          const r=e.getBoundingClientRect(),css=getComputedStyle(e),result=[];
          // A transparent foreground does not make an opaque background or
          // explicitly colored border disappear; test each paint's own alpha.
          if(!visible(e,'rgb(0, 0, 0)'))return result;
          const rect=(x,y,width,height,kind,color,dashed=false)=>{if(width>0&&height>0&&alpha(color)>0)result.push({x:x-left,y:y-baseline,width,height,layoutBoxHeight:r.height,kind:e.className+':'+kind,color,dashed});};
          if(e.classList.contains('katex-rule')){rect(r.x,r.y,r.width,r.height,'rule',css.borderRightColor);return result;}
          if(css.backgroundColor!=='rgba(0, 0, 0, 0)'&&css.backgroundColor!=='transparent')rect(r.x,r.y,r.width,r.height,'background',css.backgroundColor);
          for(const edge of ['Top','Right','Bottom','Left']){
            const thickness=parseFloat(css['border'+edge+'Width']);if(!thickness||['none','hidden'].includes(css['border'+edge+'Style']))continue;
            const vertical=edge==='Right'||edge==='Left';
            const x=edge==='Right'?r.right-thickness:r.x,y=edge==='Bottom'?r.bottom-thickness:r.y;
            rect(x,y,vertical?thickness:r.width,vertical?r.height:thickness,edge,css['border'+edge+'Color'],css['border'+edge+'Style']==='dashed');
          }
          return result;
        });
        const paths=[...html.querySelectorAll('svg')].flatMap(e=>{const r=e.getBoundingClientRect(),children=[...e.querySelectorAll('path,line,rect,polyline,polygon')];return(children.length?children:[e]).flatMap(child=>{const css=getComputedStyle(child),color=css.fill==='none'?css.stroke:css.fill;if(!visible(child,color))return[];const m=child.getScreenCTM();return[{x:r.x-left,y:r.y-baseline,width:r.width,height:r.height,viewBox:e.getAttribute('viewBox'),preserveAspectRatio:e.getAttribute('preserveAspectRatio'),shape:child.localName,color,transform:{x:m.e-left,y:m.f-baseline,scaleX:m.a,scaleY:m.d}}];});});
        const normalizeXml=node=>node.nodeType===Node.TEXT_NODE?{type:'text',text:node.data}:{namespace:node.namespaceURI,name:node.localName,attributes:Object.fromEntries([...node.attributes].map(a=>[a.name,a.value]).sort(([a],[b])=>a.localeCompare(b))),children:[...node.childNodes].map(normalizeXml)};
        // Auto equation labels are CSS generated content. Record their computed
        // counter text and real marker origin separately; they are not DOM text.
        const generatedTags=[...html.querySelectorAll('.eqn-num')].map((e,index)=>{
          const r=e.getBoundingClientRect(),m=document.createElement('span');m.style.cssText='display:inline-block;width:0;height:0;vertical-align:baseline;margin:0;padding:0';e.append(m);const glyphBaseline=m.getBoundingClientRect().y;m.remove();
          e.setAttribute('data-reference-generated-tag',String(index));
          return{index,content:getComputedStyle(e,'::before').content,x:r.x-left,y:glyphBaseline-baseline,width:r.width,height:r.height};
        });
        return{advanceDip:rects.reduce((v,r)=>v+r.width,0),ascentDip:baseline-top,depthDip:bottom-baseline,origin:{x:left-outer.x,y:baseline-outer.y},pageOrigin:{x:left,y:baseline},glyphs,textRuns,rules,paths,generatedTags,mathml:normalizeXml(host.querySelector('math')),mathmlText:new XMLSerializer().serializeToString(host.querySelector('math')),effectiveEmDip:parseFloat(getComputedStyle(host.querySelector('.katex')).fontSize)};
      },{f,scenario,options:requestedOutput?{...renderOptions,output:'htmlAndMathml'}:renderOptions});
      if(requestedOutput){geometry.requestedOutput=requestedOutput;geometry.geometryOutputOracle='Separate actual locked KaTeX htmlAndMathml render; requested serialization markup and screenshot are retained, never inferred from C#.';}
      if(geometry.generatedTags.length){
        // DOMSnapshot exposes the browser's resolved CSS counter text. Computed
        // ::before content only returns the unresolved counter() expression.
        const snapshot=await cdp.send('DOMSnapshot.captureSnapshot',{computedStyles:['font-family','font-size','font-weight','font-style','color','visibility','opacity'],includeDOMRects:true});
        const strings=snapshot.strings,document=snapshot.documents[0],nodes=document.nodes,layout=document.layout;
        for(let i=0;i<layout.nodeIndex.length;i++){
          const node=layout.nodeIndex[i];if(strings[nodes.nodeName[node]]!=='::before')continue;
          const owner=nodes.parentIndex[node],attributes=nodes.attributes[owner]??[];let tagIndex;
          for(let j=0;j<attributes.length;j+=2)if(strings[attributes[j]]==='data-reference-generated-tag')tagIndex=Number(strings[attributes[j+1]]);
          if(tagIndex===undefined)continue;
          const tag=geometry.generatedTags[tagIndex],text=strings[layout.text[i]],bounds=layout.bounds[i],css=layout.styles[i].map(id=>strings[id]);
          if(!text)continue;
          const characters=[...text];
          if(characters.length!==1)throw new Error(`${f.id}: CSS counter fragment did not expose individual browser glyph bounds`);
          geometry.glyphs.push({text,x:bounds[0]-geometry.pageOrigin.x,y:tag.y,fontFamily:css[0],fontSizeDip:parseFloat(css[1]),fontWeight:css[2],fontStyle:css[3],color:css[4],visible:css[5]!=='hidden'&&Number(css[6])!==0,generatedCounter:true});
        }
        for(const tag of geometry.generatedTags)if(!geometry.glyphs.some(g=>g.generatedCounter&&Math.abs(g.y-tag.y)<.01))throw new Error(`${f.id}: No resolved CSS equation counter paint found`);
      }
      // CSS family lists do not identify fallback faces. Capture the browser's
      // actual platform-font result independently for all non-ASCII text parents.
      if(geometry.glyphs.some(g=>[...g.text].some(char=>char.codePointAt(0)>127)&&g.text!=='\u200b')){
        await cdp.send('DOM.enable');await cdp.send('CSS.enable');
        const {root:documentRoot}=await cdp.send('DOM.getDocument');
        const {nodeIds}=await cdp.send('DOM.querySelectorAll',{nodeId:documentRoot.nodeId,selector:'.katex-html [data-reference-glyph-parent]'});
        const fonts=[];
        for(const nodeId of nodeIds){
          const {node}=await cdp.send('DOM.describeNode',{nodeId});
          const attributes=node.attributes??[];let index;
          for(let j=0;j<attributes.length;j+=2)if(attributes[j]==='data-reference-glyph-parent')index=Number(attributes[j+1]);
          if(!geometry.glyphs.some(g=>g.domParent===index&&[...g.text].some(char=>char.codePointAt(0)>127)&&g.text!=='\u200b'))continue;
          const result=await cdp.send('CSS.getPlatformFontsForNode',{nodeId});fonts.push({firstGlyphIndex:index,fonts:result.fonts});
          for(const glyph of geometry.glyphs)if(glyph.domParent===index)glyph.platformFontFamilies=result.fonts.map(font=>font.familyName);
        }
        geometry.platformFonts=fonts;
      }
      if(!geometry.presentationFallback&&Math.abs(geometry.effectiveEmDip-scenario.fontSizeDip)>1e-5)throw new Error('Em scale changed');
      const dir=geometry.presentationFallback?path.join(out,'presentation',f.id,scenarioId):path.join(out,f.id,scenarioId);await fs.mkdir(dir,{recursive:true});
      if(requestedImage){await fs.writeFile(path.join(dir,'requested-output.png'),requestedImage);assets.push({path:path.relative(root,path.join(dir,'requested-output.png')).replaceAll('\\','/'),sha256:sha256(requestedImage)});}
      const image=await page.locator('#host').screenshot({animations:'disabled'});await fs.writeFile(path.join(dir,'reference.png'),image);
      // A second capture retains identical layout but exposes real alpha. RGB
      // contrast in an opaque screenshot cannot recover font coverage because
      // the browser applies theme-dependent font contrast/gamma adjustments.
      await page.evaluate(()=>{document.body.style.background='transparent';});
      const transparent=await page.locator('#host').screenshot({animations:'disabled',omitBackground:true});
      await fs.writeFile(path.join(dir,'reference.transparent.png'),transparent);
      await writeJson(path.join(dir,'reference.json'),geometry);
      (geometry.presentationFallback?presentations:results).push({id:f.id,category:f.category,scenario:scenarioId,...scenario,geometry});
      assets.push({path:path.relative(root,path.join(dir,'reference.png')).replaceAll('\\','/'),sha256:sha256(image)});
      assets.push({path:path.relative(root,path.join(dir,'reference.transparent.png')).replaceAll('\\','/'),sha256:sha256(transparent)});
    }
    await context.close();console.log(`${scenarioId}: reference complete`);
  }
  const fixtureSignatures=Object.fromEntries(cases.map(f=>[f.id,sha256(Buffer.from(JSON.stringify({source:f.latex,expectation:f.expectation,options:options(f),presentationFallback:!!f.presentationFallback,upstreamErrorName:f.upstreamErrorName??'ParseError'})))]));
  await writeJson(path.join(out,'reference.json'),{schemaVersion:1,sourceCommit:lock.source.commit,corpusSha256:sha256(corpusBytes),oracleCodeSha256,fixtureSignatures,semantics:semantic,results,presentations});
  await writeJson(only?path.join(out,'subset.evidence.json'):path.join(root,`docs/reference/${phase}-reference.evidence.json`),{schemaVersion:2,sourceCommit:lock.source.commit,npmSha256:lock.npm.sha256,corpusSha256:sha256(corpusBytes),oracleCodeSha256,fixtureSignatures,browserVersion:browser.version(),browserSha256:toolchain.browserSha256,valid:semantic.filter(x=>x.result==='valid').length,invalid:semantic.filter(x=>x.result==='invalid').length,scenarios:scenarios.length,measurements:results.length,presentationFallbacks:presentations.length,sourceAndDistributionSemanticsAgree:true,subset:only??null,measurement:'Advance uses real .katex-base width; natural ascent/depth uses direct .katex-strut rectangles. Text origins use zero-size baseline markers; actual computed CSS visibility/color and shadows, span backgrounds/four border edges, and SVG shape-child CTMs are retained. CSS equation-counter characters and bounds come from Chrome DOMSnapshot resolved pseudo-layout, never counter guesses. Contextual text runs retain original text, actual DOM Range advance and baseline. Invalid facade cases separately retain actual colored-source HTML/PNG; no fabricated MathML when absent. No C# data used.',assets});
  console.log(JSON.stringify({valid:semantic.filter(x=>x.result==='valid').length,invalid:semantic.filter(x=>x.result==='invalid').length,measurements:results.length}));
}finally{await browser.close();await new Promise(r=>server.close(r));}
