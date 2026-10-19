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
await page.addScriptTag({path:path.join(packageRoot,'dist/katex.js')});
const session=await page.context().newCDPSession(page);
await session.send('DOM.enable');await session.send('CSS.enable');
const corpus=JSON.parse(await fs.readFile(path.join(root,'docs/examples/formulas.json'),'utf8'));
const result=[];
for(const fixture of corpus.cases.filter(x=>x.expectation==='valid')) {
  await page.evaluate(async fixture=>{katex.render(fixture.latex,document.getElementById('host'),{displayMode:fixture.displayMode,macros:{},maxExpand:1000});await document.fonts.ready;},fixture);
  const document=await session.send('DOM.getDocument');
  const nodes=await session.send('DOM.querySelectorAll',{nodeId:document.root.nodeId,selector:'.katex-html span'});
  const fonts=new Map();
  for(const nodeId of nodes.nodeIds) {
    const {fonts:used}=await session.send('CSS.getPlatformFontsForNode',{nodeId});
    for(const font of used) fonts.set(`${font.familyName}:${font.postScriptName}`,{familyName:font.familyName,postScriptName:font.postScriptName,isCustomFont:font.isCustomFont});
  }
  result.push({fixtureId:fixture.id,platformFonts:[...fonts.values()]});
}
await writeJson(path.join(root,'artifacts/reference/platform-fonts.json'),{browser:lock.browserVersion,fonts:result});
console.log(JSON.stringify(result.find(x=>x.fixtureId==='chinese-math-text')));
await browser.close();
