// Raster checks align only by recorded logical origins / baselines. No fitted
// translation, scaling, deformation, SSIM background scores or hidden cases.
import fs from 'node:fs/promises';
import path from 'node:path';
import {chromium} from 'playwright';
import {PNG} from 'pngjs';
import {root,sha256,writeJson} from './acquire.mjs';
const requested=process.argv.slice(2).filter(x=>!x.startsWith('--'));
const folders=requested.length?requested:['artifacts/p1/jit','artifacts/p1/aot'];
const useReviewed=process.argv.includes('--reviewed');
const reviewed=useReviewed?JSON.parse(await fs.readFile(path.join(root,'compatibility/p1-raster-review.json'),'utf8')):null;
const reference=JSON.parse(await fs.readFile(path.join(root,'artifacts/p1/reference/reference.json'),'utf8'));
const toolchain=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
if(sha256(await fs.readFile(toolchain.browserExecutable))!==toolchain.browserSha256)throw new Error('Browser drift');
function edgePoints(image,origin,scale,background) {
  const mask=new Uint8Array(image.width*image.height);
  for(let i=0;i<mask.length;i++) {
    const p=i*4;
    const foreground=background?.[0]===22?238:17;
    mask[i]=background?Number(image.data[p+3]>32&&background.some((v,c)=>Math.abs(image.data[p+c]-v)/Math.abs(foreground-v)>32/255)):Number(image.data[p+3]>32);
  }
  const points=[];
  for(let y=0;y<image.height;y++)for(let x=0;x<image.width;x++) {
    const i=y*image.width+x;
    if(mask[i]&&(x===0||y===0||x===image.width-1||y===image.height-1||!mask[i-1]||!mask[i+1]||!mask[i-image.width]||!mask[i+image.width]))points.push([x+.5-origin.x*scale,y+.5-origin.y*scale]);
  }
  return points;
}
function coverage(a,b,tolerance) {
  if(!a.length)return b.length?0:1;
  const grid=new Map();for(const p of b){const key=`${Math.floor(p[0])},${Math.floor(p[1])}`;if(!grid.has(key))grid.set(key,[]);grid.get(key).push(p);}
  let covered=0;const radius=Math.ceil(tolerance)+1,limit=tolerance*tolerance+1e-10;
  for(const p of a) {
    let found=false;const x=Math.floor(p[0]),y=Math.floor(p[1]);
    for(let dy=-radius;dy<=radius&&!found;dy++)for(let dx=-radius;dx<=radius&&!found;dx++)for(const q of grid.get(`${x+dx},${y+dy}`)??[])if((p[0]-q[0])**2+(p[1]-q[1])**2<=limit){found=true;break;}
    if(found)covered++;
  }
  return covered/a.length;
}
const browser=await chromium.launch({executablePath:toolchain.browserExecutable,headless:true,args:['--force-color-profile=srgb','--disable-lcd-text']});
const all=[];
try {
  for(const folder of folders) {
    const report=JSON.parse(await fs.readFile(path.join(root,folder,'report.json'),'utf8'));
    const contexts=new Map(),checks=[],failures=[];
    for(const actual of report.results.filter(x=>x.success)) {
      const ref=reference.results.find(x=>x.id===actual.id&&x.scenario===actual.scenario),scale=ref.dpiScale;
      const directory=path.join(root,folder,actual.id,actual.scenario);
      const pngBytes=await fs.readFile(path.join(directory,'formula.png')),png=PNG.sync.read(pngBytes);
      const svg=await fs.readFile(path.join(directory,'formula.svg'),'utf8');
      if(!contexts.has(scale)) {
        const context=await browser.newContext({viewport:{width:1600,height:900},deviceScaleFactor:scale});
        const page=await context.newPage();
        await page.route('**/*',route=>route.abort());
        contexts.set(scale,{context,page});
      }
      const {page}=contexts.get(scale);
      const details=await page.evaluate(async svg=>{
        const d=new DOMParser().parseFromString(svg,'application/xml'),root=d.documentElement;
        if(d.querySelector('parsererror'))throw new Error('SVG parse error');
        if(d.querySelector('script,text,foreignObject,image,style')||[...d.querySelectorAll('[href]')].some(x=>!x.getAttribute('href').startsWith('#')))throw new Error('Non-standalone SVG');
        const viewBox=root.getAttribute('viewBox').split(/\s+/).map(Number),width=Number(root.getAttribute('width')),height=Number(root.getAttribute('height'));
        document.body.style.cssText='margin:0;background:transparent';
        document.documentElement.style.background='transparent';
        let image=document.querySelector('img');if(!image){image=document.createElement('img');document.body.append(image);}
        image.style.cssText=`position:absolute;left:0;top:0;width:${width}px;height:${height}px`;
        image.src='data:image/svg+xml;base64,'+btoa(unescape(encodeURIComponent(svg)));await image.decode();
        return{viewBox,width,height,glyphUses:d.querySelectorAll('use').length,paths:d.querySelectorAll('path').length,rectangles:d.querySelectorAll('rect').length};
      },svg);
      const svgBytes=await page.screenshot({omitBackground:true,clip:{x:0,y:0,width:png.width/scale,height:png.height/scale}});
      await fs.writeFile(path.join(directory,'svg-view.png'),svgBytes);
      const svgImage=PNG.sync.read(svgBytes);
      const refImage=PNG.sync.read(await fs.readFile(path.join(root,'artifacts/p1/reference',actual.id,actual.scenario,'reference.png')));
      const transparentRefImage=PNG.sync.read(await fs.readFile(path.join(root,'artifacts/p1/reference',actual.id,actual.scenario,'reference.transparent.png')));
      const origin={x:-details.viewBox[0],y:actual.baseline-details.viewBox[1]};
      const nativeEdges=edgePoints(png,origin,scale),svgEdges=edgePoints(svgImage,origin,scale),refEdges=edgePoints(refImage,ref.geometry.origin,scale,ref.theme==='dark'?[22,22,22]:[255,255,255]);
      const transparentReferenceEdges=edgePoints(transparentRefImage,ref.geometry.origin,scale);
      const tolerance=Math.max(1,.5*scale);
      const nativeToReference=coverage(nativeEdges,refEdges,tolerance),referenceToNative=coverage(refEdges,nativeEdges,tolerance),nativeToSvg=coverage(nativeEdges,svgEdges,tolerance),svgToNative=coverage(svgEdges,nativeEdges,tolerance);
      const nativeToTransparentReference=coverage(nativeEdges,transparentReferenceEdges,tolerance),transparentReferenceToNative=coverage(transparentReferenceEdges,nativeEdges,tolerance);
      let edgeClipping=false;
      for(let x=0;x<png.width;x++)if(png.data[x*4+3]>0||png.data[((png.height-1)*png.width+x)*4+3]>0)edgeClipping=true;
      for(let y=0;y<png.height;y++)if(png.data[(y*png.width)*4+3]>0||png.data[(y*png.width+png.width-1)*4+3]>0)edgeClipping=true;
      const check={id:actual.id,category:actual.category,scenario:actual.scenario,pngSha256:sha256(pngBytes),svgSha256:sha256(Buffer.from(svg)),width:png.width,height:png.height,nativeEdgePoints:nativeEdges.length,referenceEdgePoints:refEdges.length,toleranceDevicePixels:tolerance,nativeToReference,referenceToNative,nativeToSvg,svgToNative,nativeToTransparentReference,transparentReferenceToNative,edgeClipping,details};
      checks.push(check);
      if(edgeClipping||[nativeToReference,referenceToNative,nativeToSvg,svgToNative].some(x=>x<.95))failures.push(check);
    }
    for(const {context} of contexts.values())await context.close();
    const categories=[...new Set(checks.map(x=>x.category))].map(category=>{const c=checks.filter(x=>x.category===category);return{category,measurements:c.length,minimumNativeToReference:Math.min(...c.map(x=>x.nativeToReference)),minimumReferenceToNative:Math.min(...c.map(x=>x.referenceToNative)),minimumNativeToSvg:Math.min(...c.map(x=>x.nativeToSvg)),minimumSvgToNative:Math.min(...c.map(x=>x.svgToNative))};});
    const unreviewedFailures=[];
    for(const failure of failures) {
      const entry=reviewed?.entries.find(e=>e.id===failure.id&&e.scenario===failure.scenario);
      const referencePngSha256=sha256(await fs.readFile(path.join(root,'artifacts/p1/reference',failure.id,failure.scenario,'reference.png')));
      if(!entry||entry.nativePngSha256!==failure.pngSha256||entry.svgSha256!==failure.svgSha256||entry.referencePngSha256!==referencePngSha256||failure.edgeClipping||failure.nativeToSvg<.95||failure.svgToNative<.95)unreviewedFailures.push(failure);
    }
    await writeJson(path.join(root,folder,'image-check.json'),{schemaVersion:2,method:'Foreground boundary points, alpha>32 (native/SVG), opaque reference contrast-derived coverage>32/255; extra transparent-reference true-alpha metrics retained separately. Bidirectional nearest distance; only recorded origin/baseline alignment. 95% each direction within max(1 device px,0.5 DIP). No fitted transformations. All exported SVGs decoded independently in locked Chrome, network blocked, all PNGs decoded independently. Raw automatic failures remain failures even when an exact-hash rendering review is applied.',browserVersion:browser.version(),measurements:checks.length,failures,reviewedRenderingDifferences:failures.length-unreviewedFailures.length,unreviewedFailures,categories,checks});
    all.push({folder,measurements:checks.length,automaticFailures:failures.length,reviewedRenderingDifferences:failures.length-unreviewedFailures.length,unreviewedFailures:unreviewedFailures.length,categories});console.log(JSON.stringify(all.at(-1)));
    if(unreviewedFailures.length)console.log('Unreviewed raster failures: '+JSON.stringify(unreviewedFailures.slice(0,8).map(x=>({id:x.id,scenario:x.scenario,nativeToReference:x.nativeToReference,referenceToNative:x.referenceToNative,nativeToSvg:x.nativeToSvg,svgToNative:x.svgToNative,edgeClipping:x.edgeClipping}))));
  }
  await writeJson(path.join(root,'artifacts/p1/image-summary.json'),all);
  if(all.some(x=>x.unreviewedFailures))process.exitCode=1;
}finally{await browser.close();}
