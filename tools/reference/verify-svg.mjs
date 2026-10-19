import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';
import {root,sha256,writeJson} from './acquire.mjs';

const lock=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
const executable=process.env.VELOCITEX_BROWSER??lock.browserExecutable;
if(sha256(await fs.readFile(executable))!==lock.browserSha256)throw new Error('Browser binary drift');
const browser=await chromium.launch({executablePath:executable,headless:true});
const page=await browser.newPage({viewport:{width:640,height:180}});
const results=[];
for(const mode of ['jit','aot'])for(const theme of ['light','dark']) {
  const file=path.join(root,`artifacts/native/${mode}/probe-${theme}.svg`);
  const requests=[];
  const onRequest=request=>requests.push({url:request.url(),type:request.resourceType()});
  page.on('request',onRequest);
  await page.goto(pathToFileURL(file).href);
  const result=await page.evaluate(()=>({svg:document.documentElement.localName,fontFaces:document.fonts.size,paths:document.querySelectorAll('path').length,textOrExternalNodes:document.querySelectorAll('text,image,foreignObject,style,link,script').length,nonlocalReferences:[...document.querySelectorAll('[href]')].map(x=>x.getAttribute('href')).filter(x=>!x.startsWith('#')),width:document.documentElement.getBoundingClientRect().width,height:document.documentElement.getBoundingClientRect().height}));
  page.off('request',onRequest);
  if(result.svg!=='svg'||result.fontFaces!==0||result.paths===0||result.textOrExternalNodes!==0||result.nonlocalReferences.length!==0||requests.some(x=>x.type!=='document'))throw new Error(`SVG has external/text dependencies: ${file}`);
  await page.evaluate(theme=>{document.documentElement.style.background=theme==='dark'?'#161616':'#ffffff';},theme);
  const bytes=await page.screenshot();
  const screenshot=`artifacts/reference/native-svg-preview-${mode}-${theme}.png`;
  await fs.writeFile(path.join(root,screenshot),bytes);
  results.push({source:path.relative(root,file).replaceAll('\\','/'),sourceSha256:sha256(await fs.readFile(file)),...result,requests,screenshot,screenshotSha256:sha256(bytes)});
}
await writeJson(path.join(root,'artifacts/reference/native-svg-check.json'),{browserVersion:browser.version(),results});
console.log(JSON.stringify(results.map(x=>({source:x.source,paths:x.paths,fontFaces:x.fontFaces,externalNodes:x.textOrExternalNodes}))));
await browser.close();
