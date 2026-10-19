import fs from 'node:fs/promises';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {chromium} from 'playwright';
import {root,writeJson,sha256} from './acquire.mjs';

const lock=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
const executable=process.env.VELOCITEX_BROWSER??lock.browserExecutable;
if(sha256(await fs.readFile(executable))!==lock.browserSha256)throw new Error('Browser binary drift');
const browser=await chromium.launch({executablePath:executable,headless:true,args:['--force-color-profile=srgb','--disable-lcd-text']});
if(browser.version()!==lock.browserVersion)throw new Error('Browser version drift');
const page=await browser.newPage({viewport:{width:1200,height:1000},deviceScaleFactor:1});
await page.goto(pathToFileURL(path.join(root,'artifacts/fonts/font-calibration.html')).href);
const data=await page.evaluate(()=>window.fontCalibrationReady);
if(!data||data.glyphs.length!==20||data.glyphs.some(x=>!x.fontLoaded))throw new Error('Private calibration fonts not loaded');
await writeJson(path.join(root,'artifacts/fonts/browser-calibration.json'),{...data,browser:lock});
await page.screenshot({path:path.join(root,'artifacts/fonts/font-calibration.png'),fullPage:true});
console.log(JSON.stringify({privateFonts:data.glyphs.length,css:data.css,formulas:data.formulas.length}));
await browser.close();
