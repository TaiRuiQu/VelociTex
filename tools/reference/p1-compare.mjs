// Independent structural / geometric assertions. Never derives expectations
// from C# layout tables. Raster checks live in p1-images.mjs.
import fs from 'node:fs/promises';
import path from 'node:path';
import {chromium} from 'playwright';
import {root,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const inputs=process.argv.slice(2).filter(x=>!x.startsWith('--'));
const folders=inputs.length?inputs:['artifacts/p1/jit','artifacts/p1/aot'];
const reference=JSON.parse(await fs.readFile(path.join(root,'artifacts/p1/reference/reference.json'),'utf8'));
const corpusBytes=await fs.readFile(path.join(root,'tests/fixtures/p1.json'));
if(reference.corpusSha256!==sha256(corpusBytes))throw new Error('Reference corpus changed');
const source=loadSource();
const row=children=>({kind:'row',children});
const fontNames={mathrm:'MathRoman',mathbf:'MathBold',mathit:'MathItalic',boldsymbol:'MathBoldItalic',mathsf:'MathSans',mathtt:'MathTypewriter',mathcal:'Caligraphic',mathfrak:'Fraktur',mathscr:'Script'};
function upstream(n) {
  if(Array.isArray(n))return row(n.map(upstream));
  const list=arg=>arg?.type==='ordgroup'?arg.body.map(upstream):Array.isArray(arg)?arg.map(upstream):arg?[upstream(arg)]:null;
  switch(n.type) {
    case 'mathord':case 'textord':case 'atom': {
      const text=source.symbols[n.mode][n.text]?.replace??n.text;
      return{kind:'symbol',text,atom:n.family??'ord',mathOrd:n.type==='mathord',textMode:n.mode==='text'};
    }
    case 'spacing':return{kind:'symbol',text:' ',atom:'ord',mathOrd:false,textMode:true};
    case 'ordgroup':return{kind:'group',children:list(n)};
    case 'supsub':return{kind:'script',base:upstream(n.base),sup:list(n.sup),sub:list(n.sub)};
    case 'genfrac':return{kind:'fraction',numer:list(n.numer),denom:list(n.denom)};
    case 'sqrt':return{kind:'radical',body:list(n.body)};
    case 'font':return{kind:'font',font:fontNames[n.font]??n.font,body:list(n.body)};
    case 'text':return{kind:'text',body:list(n.body)};
    case 'styling':return{kind:'style',style:n.style.toLowerCase(),body:list(n.body)};
    case 'kern':return{kind:'space',value:Math.round(n.dimension.number*(n.dimension.unit==='mu'?1/18:1)*1e10)/1e10};
    default:throw new Error(`Unprojected upstream semantic node ${n.type}`);
  }
}
function native(doc) {
  const nodes=doc.nodes;
  const chain=id=>{const values=[];for(let c=id;c>=0;c=nodes[c].next)values.push(node(c));return values;};
  const list=id=>id<0?null:nodes[id].kind==='Row'?chain(nodes[id].first):[node(id)];
  function node(id) {
    const n=nodes[id];
    switch(n.kind) {
      case 'Row':return{kind:'group',children:chain(n.first)};
      case 'Symbol':return{kind:'symbol',text:String.fromCodePoint(n.codePoint),atom:n.atom.toLowerCase(),mathOrd:n.mathOrd,textMode:n.textMode};
      case 'Script':return{kind:'script',base:node(n.first),sup:list(n.second),sub:list(n.third)};
      case 'Fraction':return{kind:'fraction',numer:list(n.first),denom:list(n.second)};
      case 'Radical':return{kind:'radical',body:list(n.first)};
      case 'Font':return{kind:'font',font:n.font,body:list(n.first)};
      case 'Text':return{kind:'text',body:list(n.first)};
      case 'Style':return{kind:'style',style:n.style.toLowerCase(),body:list(n.first)};
      case 'Space':return{kind:'space',value:Math.round(n.value*1e10)/1e10};
      default:throw new Error(`Unprojected Core semantic node ${n.kind}`);
    }
  }
  return row(chain(nodes[doc.root].first));
}
const toolchain=JSON.parse(await fs.readFile(path.join(root,'docs/reference/development-toolchain.lock.json'),'utf8'));
if(sha256(await fs.readFile(toolchain.browserExecutable))!==toolchain.browserSha256)throw new Error('Browser drift');
const browser=await chromium.launch({executablePath:toolchain.browserExecutable,headless:true});
try {
  const page=await browser.newPage(),all=[];
  for(const folder of folders) {
    const report=JSON.parse(await fs.readFile(path.join(root,folder,'report.json'),'utf8'));
    if(report.corpusSha256!==reference.corpusSha256)throw new Error(`Engine corpus changed ${folder}`);
    const checks=[],failures=[];
    for(const actual of report.results) {
      const sem=reference.semantics.find(x=>x.id===actual.id);
      const fail=(check,detail)=>{failures.push({id:actual.id,scenario:actual.scenario,check,detail});};
      if(!actual.success) {if(sem.result!=='invalid')fail('expectation','unexpected error');continue;}
      const expected=reference.results.find(x=>x.id===actual.id&&x.scenario===actual.scenario);
      if(!expected)throw new Error(`Missing browser expectation ${actual.id}/${actual.scenario}`);
      const geometry=expected.geometry;
      let semanticPass=true;
      if(actual.scenario==='24dip-100pct-light') {
        const a=native(actual),e=upstream(sem.tree);
        if(JSON.stringify(a)!==JSON.stringify(e)){semanticPass=false;fail('semantic',{expected:e,actual:a});}
      }
      const size=actual.fontSize,totalTolerance=Math.max(.04*size,.75),positionTolerance=Math.max(.02*size,.35),ruleTolerance=Math.max(.015*size,.25);
      const deltas={advance:Math.abs(actual.width-geometry.advanceDip),ascent:Math.abs(actual.baseline-geometry.ascentDip),depth:Math.abs(actual.height-actual.baseline-geometry.depthDip)};
      for(const [key,delta]of Object.entries(deltas))if(delta>totalTolerance)fail('geometry.'+key,{delta,tolerance:totalTolerance,actual:{width:actual.width,height:actual.height,baseline:actual.baseline},expected:geometry});
      const used=new Set(),glyphDifferences=[];
      // DOM lists vlist children bottom-up. Pair actual painted glyphs by scalar,
      // font and closest declared origin; no translation/scale fitting is used.
      const expectedGlyphs=geometry.glyphs.filter(g=>g.text!=='\u200b'&&!/^\s$/u.test(g.text));
      const actualGlyphs=actual.glyphs.filter(g=>!/^\s$/u.test(g.text));
      if(expectedGlyphs.length!==actualGlyphs.length)fail('glyph-count',{expected:expectedGlyphs.length,actual:actualGlyphs.length});
      for(const glyph of actualGlyphs) {
        const suffix=glyph.font.endsWith('BoldItalic')?'bold-italic':glyph.font.endsWith('Italic')?'italic':glyph.font.endsWith('Bold')?'bold':'normal';
        const family=glyph.font.split('-')[0];
        const candidates=expectedGlyphs.map((g,i)=>({g,i,distance:Math.hypot(g.x-glyph.x,g.y-(glyph.y-actual.baseline))})).filter(x=>!used.has(x.i)&&x.g.text===glyph.text&&Math.abs(x.g.fontSizeDip-glyph.fontSize)<.02&&x.g.fontFamily.includes('KaTeX_'+family)&&((suffix.includes('italic'))===(x.g.fontStyle==='italic'))&&((suffix.includes('bold'))===(Number(x.g.fontWeight)>=600))).sort((a,b)=>a.distance-b.distance);
        if(!candidates.length){fail('glyph-font-or-scalar',glyph);continue;}
        const match=candidates[0];used.add(match.i);
        const dx=Math.abs(match.g.x-glyph.x),dy=Math.abs(match.g.y-(glyph.y-actual.baseline));
        glyphDifferences.push({text:glyph.text,dx,dy});
        if(dx>positionTolerance||dy>positionTolerance)fail('glyph-origin',{glyph,expected:match.g,dx,dy,tolerance:positionTolerance});
      }
      if(actual.rules.length!==geometry.rules.length)fail('rule-count',{expected:geometry.rules.length,actual:actual.rules.length});
      const unusedRules=new Set(geometry.rules.map((_,i)=>i));
      for(const rule of actual.rules) {
        const matches=[...unusedRules].map(i=>({i,d:Math.hypot(rule.x-geometry.rules[i].x,rule.y-actual.baseline-geometry.rules[i].y)})).sort((a,b)=>a.d-b.d);
        if(!matches.length)continue;const i=matches[0].i;unusedRules.delete(i);const e=geometry.rules[i];
        for(const [key,delta]of Object.entries({x:Math.abs(rule.x-e.x),y:Math.abs(rule.y-actual.baseline-e.y),width:Math.abs(rule.width-e.width),height:Math.abs(rule.height-e.height)}))if(delta>ruleTolerance)fail('rule.'+key,{rule,expected:e,delta,tolerance:ruleTolerance});
      }
      if(actual.paths.length!==geometry.paths.length)fail('path-count',{expected:geometry.paths.length,actual:actual.paths.length});
      const unusedPaths=new Set(geometry.paths.map((_,i)=>i));
      for(const painted of actual.paths) {
        const candidates=[...unusedPaths].map(i=>({i,d:Math.hypot(painted.x-geometry.paths[i].transform.x,painted.y-actual.baseline-geometry.paths[i].transform.y)})).sort((a,b)=>a.d-b.d);
        if(!candidates.length)continue;const i=candidates[0].i;unusedPaths.delete(i);const e=geometry.paths[i].transform;
        const dx=Math.abs(painted.x-e.x),dy=Math.abs(painted.y-actual.baseline-e.y);
        // CTM scale includes the actual CSS-constrained SVG viewport and slice
        // behavior. Relative tolerance only covers 1/64 CSS-pixel rounding of
        // the measured viewport, not a different transform construction.
        const relativeScaleX=Math.abs(painted.scaleX-e.scaleX)/e.scaleX,relativeScaleY=Math.abs(painted.scaleY-e.scaleY)/e.scaleY;
        if(dx>positionTolerance||dy>positionTolerance||relativeScaleX>.002||relativeScaleY>.002)fail('path-transform',{painted,expected:e,dx,dy,relativeScaleX,relativeScaleY});
      }
      const mathml=await fs.readFile(path.join(root,folder,actual.id,actual.scenario,'formula.mathml'),'utf8');
      const mathmlComparison=await page.evaluate(({mathml,expected,source})=>{
        const document=new DOMParser().parseFromString(mathml,'application/xml');
        if(document.querySelector('parsererror'))return{pass:false,error:'Invalid XML'};
        const annotation=document.querySelector('annotation');
        if(!annotation||annotation.getAttribute('encoding')!=='application/x-tex'||annotation.textContent!==source)return{pass:false,error:'Original source annotation differs'};
        const read=n=>n.nodeType===3?{type:'text',text:n.data}:{namespace:n.namespaceURI,name:n.localName,attributes:Object.fromEntries([...n.attributes].map(a=>[a.name,a.value])),children:[...n.childNodes].filter(n=>n.nodeType===1||n.nodeType===3).map(read)};
        const normalize=n=>{
          if(n.type==='text')return n;
          // Namespace declarations are XML syntax. A default inline math root
          // and explicit display="inline" have identical MathML semantics.
          const attributes=Object.fromEntries(Object.entries(n.attributes).filter(([k,v])=>k!=='xmlns'&&!(k==='display'&&v==='inline')).sort(([a],[b])=>a.localeCompare(b)));
          let children=n.children.filter(n=>n.name!=='annotation').map(normalize);
          if(n.name==='semantics')return children[0];
          if(n.name==='mrow'&&children.length===1&&Object.keys(attributes).length===0)return children[0];
          return{namespace:n.namespace,name:n.name,attributes,children};
        };
        const a=normalize(read(document.documentElement)),e=normalize(expected);
        return{pass:JSON.stringify(a)===JSON.stringify(e),actual:a,expected:e};
      },{mathml,expected:geometry.mathml,source:actual.source});
      if(!mathmlComparison.pass)fail('mathml',mathmlComparison);
      checks.push({id:actual.id,category:actual.category,scenario:actual.scenario,semanticPass,deltas,maxGlyphXDelta:Math.max(0,...glyphDifferences.map(x=>x.dx)),maxGlyphBaselineDelta:Math.max(0,...glyphDifferences.map(x=>x.dy)),mathmlPass:mathmlComparison.pass});
    }
    await writeJson(path.join(root,folder,'differential.json'),{schemaVersion:1,folder,validFixtures:report.validFixtures,invalidFixtures:report.invalidFixtures,measurements:checks.length,failures,checks});
    all.push({folder,validFixtures:report.validFixtures,invalidFixtures:report.invalidFixtures,measurements:checks.length,failures:failures.length,maxAdvanceDelta:Math.max(...checks.map(x=>x.deltas.advance)),maxAscentDelta:Math.max(...checks.map(x=>x.deltas.ascent)),maxDepthDelta:Math.max(...checks.map(x=>x.deltas.depth)),maxGlyphXDelta:Math.max(...checks.map(x=>x.maxGlyphXDelta)),maxGlyphBaselineDelta:Math.max(...checks.map(x=>x.maxGlyphBaselineDelta))});
    console.log(JSON.stringify(all.at(-1)));
    if(failures.length)console.log('Failures: '+JSON.stringify(failures.slice(0,8)));
  }
  await writeJson(path.join(root,'artifacts/p1/differential-summary.json'),all);
  if(all.some(x=>x.failures))process.exitCode=1;
}finally{await browser.close();}
