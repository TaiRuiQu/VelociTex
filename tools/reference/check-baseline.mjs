import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';
import {root,packageRoot,writeJson,sha256} from './acquire.mjs';

const lock=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
const executable=process.env.VELOCITEX_BROWSER??lock.browserExecutable;
if(sha256(await fs.readFile(executable))!==lock.browserSha256)throw new Error('Browser binary drift');
const browser=await chromium.launch({executablePath:executable,headless:true});
const page=await browser.newPage({viewport:{width:1000,height:1000}});
await page.goto(pathToFileURL(path.join(packageRoot,'dist/katex.js')).href);
await page.setContent('<!doctype html><html><head></head><body><div id="host" style="font-size:19.83471074380165px"></div></body></html>');
await page.addStyleTag({path:path.join(packageRoot,'dist/katex.css')});
await page.addStyleTag({content:'.katex-display{margin:0}'});
await page.addScriptTag({path:path.join(packageRoot,'dist/katex.js')});
const cases=JSON.parse(await fs.readFile(path.join(root,'docs/examples/formulas.json'),'utf8')).cases.filter(x=>x.expectation==='valid');
const result=[];
for(const fixture of cases) {
  const measured=await page.evaluate(async fixture=>{
    const host=document.getElementById('host');host.style.width=fixture.displayMode?`${fixture.containerWidthDip??880}px`:'max-content';
    katex.render(fixture.latex,host,{displayMode:fixture.displayMode,maxExpand:1000,macros:{}});
    await document.fonts.ready;
    const html=host.querySelector('.katex-html');
    const rect=()=>{const r=html.getBoundingClientRect();return{x:r.x,y:r.y,width:r.width,height:r.height};};
    const before=rect();
    const strut=html.querySelector(':scope > .katex-base > .katex-strut');
    if(!strut)throw new Error('No independent strut');
    const strutBaseline=strut.getBoundingClientRect().bottom+(parseFloat(getComputedStyle(strut).verticalAlign)||0);
    const marker=document.createElement('span');marker.style.cssText='display:inline-block;width:0;height:0;padding:0;margin:0;vertical-align:baseline';html.append(marker);
    const after=rect();
    return{before,after,markerBaseline:marker.getBoundingClientRect().y,strutBaseline,effectiveEmDip:parseFloat(getComputedStyle(host.querySelector('.katex')).fontSize)};
  },fixture);
  const geometryDrift=Math.max(...Object.keys(measured.before).map(key=>Math.abs(measured.before[key]-measured.after[key])));
  const baselineDifference=Math.abs(measured.markerBaseline-measured.strutBaseline);
  if(geometryDrift!==0||baselineDifference>0.02||Math.abs(measured.effectiveEmDip-24)>0.001)throw new Error(`Baseline instrumentation changes formula ${fixture.id}: ${JSON.stringify(measured)}`);
  result.push({fixtureId:fixture.id,...measured,geometryDriftDip:geometryDrift,baselineDifferenceDip:baselineDifference});
}
await writeJson(path.join(root,'artifacts/reference/baseline-check.json'),{method:'Independent baseline = direct base strut bottom + computed vertical-align; compare to appended zero-size inline marker. Verify marker introduces no bounding-box change.',fixtures:result,maxGeometryDriftDip:Math.max(...result.map(x=>x.geometryDriftDip)),maxBaselineDifferenceDip:Math.max(...result.map(x=>x.baselineDifferenceDip))});
console.log(JSON.stringify({fixtures:result.length,maxGeometryDriftDip:Math.max(...result.map(x=>x.geometryDriftDip)),maxBaselineDifferenceDip:Math.max(...result.map(x=>x.baselineDifferenceDip))}));
await browser.close();
