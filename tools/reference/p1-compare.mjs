// Independent structural / geometric assertions. Never derives expectations
// from C# layout tables. Raster checks live in p1-images.mjs.
import fs from 'node:fs/promises';
import path from 'node:path';
import {chromium} from 'playwright';
import {root,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
import {commonSemantics} from './p2-semantics.mjs';
import {commonSemantics as completeSemantics} from './p4-semantics.mjs';
const inputs=process.argv.slice(2).filter(x=>!x.startsWith('--'));
const phase=process.argv.find(x=>x.startsWith('--phase='))?.slice(8)??'p1';
if(!['p1','p2','p4'].includes(phase))throw new Error('Unknown phase');
const folders=inputs.length?inputs:[`artifacts/${phase}/jit`,`artifacts/${phase}/aot`];
const referenceSubset=process.argv.includes('--reference-subset');
const reference=JSON.parse(await fs.readFile(path.join(root,`artifacts/${phase}/${referenceSubset?'reference-subset':'reference'}/reference.json`),'utf8'));
const includedIds=new Set(reference.semantics.map(item=>item.id));
const corpusBytes=await fs.readFile(path.join(root,`tests/fixtures/${phase}.json`));
if(reference.corpusSha256!==sha256(corpusBytes))throw new Error('Reference corpus changed');
const source=loadSource();
const common=phase==='p4'?completeSemantics(source):commonSemantics(source);
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
    for(const actual of report.results.filter(item=>!referenceSubset||includedIds.has(item.id))) {
      const sem=reference.semantics.find(x=>x.id===actual.id);
      const fail=(check,detail)=>{failures.push({id:actual.id,scenario:actual.scenario,check,detail});};
      if(!actual.success) {if(sem.result!=='invalid')fail('expectation','unexpected error');continue;}
      const expected=reference.results.find(x=>x.id===actual.id&&x.scenario===actual.scenario);
      if(!expected)throw new Error(`Missing browser expectation ${actual.id}/${actual.scenario}`);
      const geometry=expected.geometry;
      let semanticPass=process.argv.includes('--geometry-diagnostic')?null:true;
      if(actual.scenario==='24dip-100pct-light'&&!process.argv.includes('--geometry-diagnostic')) {
        const a=phase==='p1'?native(actual):common.native(actual),e=phase==='p1'?upstream(sem.tree):common.upstream(sem.tree);
        if(JSON.stringify(a)!==JSON.stringify(e)){semanticPass=false;fail('semantic',{expected:e,actual:a});}
      }
      const size=actual.fontSize,totalTolerance=Math.max(.04*size,.75),positionTolerance=Math.max(.02*size,.35),ruleTolerance=Math.max(.015*size,.25);
      const deltas={advance:Math.abs(actual.width-geometry.advanceDip),ascent:Math.abs(actual.baseline-geometry.ascentDip),depth:Math.abs(actual.height-actual.baseline-geometry.depthDip)};
      for(const [key,delta]of Object.entries(deltas))if(delta>totalTolerance)fail('geometry.'+key,{delta,tolerance:totalTolerance,actual:{width:actual.width,height:actual.height,baseline:actual.baseline},expected:geometry});
      const used=new Set(),glyphDifferences=[];
      // DOM lists vlist children bottom-up. Pair actual painted glyphs by scalar,
      // font and closest declared origin; no translation/scale fitting is used.
      const contextualGlyphs=new Set();
      for(const run of actual.textRuns??[]){
        if(!(actual.paintCommands??[]).some(command=>command.kind==='Path'&&command.index>=run.firstPath&&command.index<run.firstPath+run.pathCount))continue;
        const sourceGlyphs=geometry.glyphs.map((glyph,index)=>({glyph,index})).filter(item=>item.glyph.visible!==false&&!item.glyph.cssShadow&&!item.glyph.generatedCounter&&item.glyph.text!=='\u200b');
        const matches=[];
        for(let start=0;start<sourceGlyphs.length;start++){
          let literal='',selected=[];
          for(let position=start;position<sourceGlyphs.length;position++){
            const item=sourceGlyphs[position];literal+=item.glyph.text;selected.push(item);
            if(!run.text.startsWith(literal))break;
            if(literal!==run.text)continue;
            if(selected.some(item=>contextualGlyphs.has(item.index)||Math.abs(item.glyph.fontSizeDip-run.fontSize)>=.02))break;
            const left=Math.min(...selected.map(item=>item.glyph.x)),right=Math.max(...selected.map(item=>item.glyph.x+(item.glyph.rangeWidthDip??0)));
            const text={text:literal,x:left,y:selected[0].glyph.y,advanceDip:right-left,fontSizeDip:run.fontSize,rangeGlyphIndices:selected.map(item=>item.index),measurement:'Union of independently captured literal DOM Range rectangles; overlapping grapheme ranges are not summed.'};
            matches.push({text,distance:Math.hypot(text.x-run.x,text.y-(run.y-actual.baseline))});break;
          }
        }
        matches.sort((a,b)=>a.distance-b.distance);
        if(!matches.length){fail('contextual-text-source',{run,expectedTextRuns:geometry.textRuns});continue;}
        const match=matches[0];for(const index of match.text.rangeGlyphIndices)contextualGlyphs.add(index);
        const dx=Math.abs(match.text.x-run.x),dy=Math.abs(match.text.y-(run.y-actual.baseline));
        if(dx>positionTolerance||dy>positionTolerance)fail('contextual-text-origin',{run,expected:match.text,dx,dy,tolerance:positionTolerance});
        if(!Number.isFinite(run.advance)||run.advance<=0)fail('contextual-text-advance-provenance',{run,reason:'The full compiler must report actual shaped advance; outline ink does not determine advance.'});
        else if(Math.abs(run.advance-match.text.advanceDip)>totalTolerance)fail('contextual-text-advance',{run,expected:match.text,tolerance:totalTolerance});
      }
      // A contextual run may have fewer outlines than Unicode scalars because
      // ligatures and joining are real shaping results. Its original text,
      // natural DOM Range advance and baseline are checked above; all positioned
      // outline paths remain subject to the independent raster/export checks.
      const expectedGlyphs=geometry.glyphs.filter((g,index)=>g.visible!==false&&g.text!=='\u200b'&&!/^\s$/u.test(g.text)&&!contextualGlyphs.has(index));
      const color=value=>{
        if(value==null)return null;
        if(value.startsWith('#')){const h=value.slice(1);return[parseInt(h.slice(0,2),16),parseInt(h.slice(2,4),16),parseInt(h.slice(4,6),16),h.length===8?parseInt(h.slice(6,8),16)/255:1];}
        const n=value.match(/[\d.]+/g)?.map(Number);return n?.length>=3?[n[0],n[1],n[2],n[3]??1]:null;
      };
      const colorsMatch=(a,b)=>a===null||b===null||a.every((x,i)=>Math.abs(x-b[i])<=(i===3?1/255:1));
      const defaultColor=color(expected.theme==='dark'?'rgb(238,238,238)':'rgb(17,17,17)');
      const ordered=(actual.paintStates?.length??0)>0;
      const paintedGlyphs=ordered?actual.paintCommands.filter(c=>c.kind==='Glyph').map(c=>{
        const g=actual.glyphs[c.index],state=actual.paintStates[c.state],t=state.transform;
        const x=g.x+(t[0]<0?(g.advance??0):0);
        return{...g,x:x*t[0]+g.y*t[2]+t[4],y:x*t[1]+g.y*t[3]+t[5],paintIndex:c.index,paintColor:color(state.color)??defaultColor};
      }):actual.glyphs;
      // Computed CSS text shadows are independently expanded by the collector;
      // both original and offset native paints must match their real origins.
      const fallbackGlyphs=(actual.textGlyphs??[]).flatMap(g=>{
        if(!ordered)return[{...g,fallback:true}];
        return actual.paintCommands.filter(c=>c.kind==='Path'&&actual.paths[c.index].fallback&&Math.abs(actual.paths[c.index].x-g.x)<1e-8&&Math.abs(actual.paths[c.index].y-g.y)<1e-8&&Math.abs(actual.paths[c.index].scaleX-g.fontSize)<1e-8).map(c=>{
          const state=actual.paintStates[c.state],t=state.transform;return{...g,x:g.x*t[0]+g.y*t[2]+t[4],y:g.x*t[1]+g.y*t[3]+t[5],fallback:true,paintColor:color(state.color)??defaultColor};
        });
      });
      const actualGlyphs=[...paintedGlyphs,...fallbackGlyphs].filter(g=>!/^\s$/u.test(g.text)&&g.paintColor?.[3]!==0);
      if(expectedGlyphs.length!==actualGlyphs.length)fail('glyph-count',{expected:expectedGlyphs.length,actual:actualGlyphs.length});
      for(const glyph of actualGlyphs) {
        const suffix=glyph.font.endsWith('BoldItalic')?'bold-italic':glyph.font.endsWith('Italic')?'italic':glyph.font.endsWith('Bold')?'bold':'normal';
        const family=glyph.font.split('-')[0];
        const candidates=expectedGlyphs.map((g,i)=>({g,i,distance:Math.hypot(g.x-glyph.x,g.y-(glyph.y-actual.baseline))})).filter(x=>!used.has(x.i)&&x.g.text===glyph.text&&Math.abs(x.g.fontSizeDip-glyph.fontSize)<.02&&(glyph.fallback||x.g.fontFamily.includes('KaTeX_'+family)&&((suffix.includes('italic'))===(x.g.fontStyle==='italic'))&&((suffix.includes('bold'))===(Number(x.g.fontWeight)>=600)))).sort((a,b)=>a.distance-b.distance);
        if(!candidates.length){fail('glyph-font-or-scalar',glyph);continue;}
        const match=candidates[0];used.add(match.i);
        if(glyph.fallback&&phase==='p4'&&match.g.platformFontFamilies?.length&&!match.g.platformFontFamilies.some(family=>family.replaceAll(' ','').toLowerCase()===glyph.font.replaceAll(' ','').toLowerCase()))fail('fallback-platform-font',{glyph,expectedPlatformFonts:match.g.platformFontFamilies});
        if(!colorsMatch(glyph.paintColor??null,color(match.g.color)))fail('glyph-color',{glyph,expected:match.g});
        const dx=Math.abs(match.g.x-glyph.x),dy=Math.abs(match.g.y-(glyph.y-actual.baseline));
        glyphDifferences.push({text:glyph.text,dx,dy});
        if(dx>positionTolerance||dy>positionTolerance)fail('glyph-origin',{glyph,expected:match.g,dx,dy,tolerance:positionTolerance});
      }
      const point=(x,y,t)=>({x:x*t[0]+y*t[2]+t[4],y:x*t[1]+y*t[3]+t[5]});
      const actualRules=ordered?actual.paintCommands.filter(c=>c.kind==='Rule').map(c=>{
        const r=actual.rules[c.index],state=actual.paintStates[c.state],t=state.transform;
        const corners=[point(r.x,r.y,t),point(r.x+r.width,r.y,t),point(r.x,r.y+r.height,t),point(r.x+r.width,r.y+r.height,t)];
        const x=Math.min(...corners.map(p=>p.x)),y=Math.min(...corners.map(p=>p.y));
        return{x,y,width:Math.max(...corners.map(p=>p.x))-x,height:Math.max(...corners.map(p=>p.y))-y,paintColor:color(state.color)??defaultColor};
      }).filter(r=>r.paintColor?.[3]!==0):actual.rules.map(r=>({...r}));
      for(const rule of geometry.rules.filter(r=>r.dashed)) {
        const vertical=rule.height>rule.width;
        const pieces=actualRules.filter(r=>vertical
          ?Math.abs(r.x-rule.x)<ruleTolerance&&Math.abs(r.width-rule.width)<ruleTolerance&&r.y-actual.baseline>=rule.y-ruleTolerance&&r.y+r.height-actual.baseline<=rule.y+rule.height+ruleTolerance
          :Math.abs(r.y-actual.baseline-rule.y)<ruleTolerance&&Math.abs(r.height-rule.height)<ruleTolerance&&r.x>=rule.x-ruleTolerance&&r.x+r.width<=rule.x+rule.width+ruleTolerance);
        if(!pieces.length)continue;
        for(const piece of pieces)actualRules.splice(actualRules.indexOf(piece),1);
        // A CSS dashed border is one logical rule. Native output represents its
        // painted dashes as several rectangles; compare their common geometric
        // envelope, retaining the original pixel comparison for dash painting.
        const left=Math.min(...pieces.map(r=>r.x)),right=Math.max(...pieces.map(r=>r.x+r.width)),top=Math.min(...pieces.map(r=>r.y)),bottom=Math.max(...pieces.map(r=>r.y+r.height));
        actualRules.push({x:left,y:top,width:right-left,height:bottom-top,paintColor:pieces[0].paintColor});
      }
      if(actualRules.length!==geometry.rules.length)fail('rule-count',{expected:geometry.rules.length,actual:actualRules.length});
      const unusedRules=new Set(geometry.rules.map((_,i)=>i));
      for(const rule of actualRules) {
        const matches=[...unusedRules].map(i=>({i,d:Math.hypot(rule.x-geometry.rules[i].x,rule.y-actual.baseline-geometry.rules[i].y,rule.width-geometry.rules[i].width,rule.height-geometry.rules[i].height)})).sort((a,b)=>a.d-b.d);
        if(!matches.length)continue;const i=matches[0].i;unusedRules.delete(i);const e=geometry.rules[i];
        if(!colorsMatch(rule.paintColor??null,color(e.color)))fail('rule-color',{rule,expected:e});
        for(const [key,delta]of Object.entries({x:Math.abs(rule.x-e.x),y:Math.abs(rule.y-actual.baseline-e.y),width:Math.abs(rule.width-e.width),height:Math.abs(rule.height-e.height)}))if(delta>ruleTolerance)fail('rule.'+key,{rule,expected:e,delta,tolerance:ruleTolerance});
      }
      const constructionPaths=ordered?actual.paintCommands.filter(c=>c.kind==='Path').map(c=>{
        const p=actual.paths[c.index],state=actual.paintStates[c.state],t=state.transform,origin=point(p.x,p.y,t);
        return{...p,x:origin.x,y:origin.y,scaleX:p.scaleX*t[0],scaleY:p.scaleY*t[3],paintColor:color(state.color)??defaultColor};
      }).filter(p=>!p.fallback&&p.paintColor?.[3]!==0):actual.paths.filter(p=>!p.fallback);
      if(constructionPaths.length!==geometry.paths.length)fail('path-count',{expected:geometry.paths.length,actual:constructionPaths.length});
      const unusedPaths=new Set(geometry.paths.map((_,i)=>i));
      for(const painted of constructionPaths) {
        const candidates=[...unusedPaths].map(i=>({i,d:Math.hypot(painted.x-geometry.paths[i].transform.x,painted.y-actual.baseline-geometry.paths[i].transform.y)})).sort((a,b)=>a.d-b.d);
        if(!candidates.length)continue;const i=candidates[0].i;unusedPaths.delete(i);const e=geometry.paths[i].transform;
        const refPath=geometry.paths[i],vb=refPath.viewBox?.split(/\s+/).map(Number)??[0,0,0,0];
        if(!colorsMatch(painted.paintColor??null,color(refPath.color)))fail('path-color',{painted,expected:refPath});
        const anchorX=refPath.preserveAspectRatio?.includes('xMax')?vb[0]+vb[2]:refPath.preserveAspectRatio?.includes('xMid')?vb[0]+vb[2]/2:vb[0];
        // Long 400em paths deliberately paint only one end of a clipped SVG.
        // Compare the declared alignment anchor, avoiding amplification of
        // 1/64px CSS rounding at an invisible origin thousands of DIP away.
        const dx=Math.abs(painted.x+anchorX*painted.scaleX-(e.x+anchorX*e.scaleX)),dy=Math.abs(painted.y-actual.baseline-e.y);
        // CTM scale includes the actual CSS-constrained SVG viewport and slice
        // behavior. Relative tolerance only covers 1/64 CSS-pixel rounding of
        // the measured viewport, not a different transform construction.
        const relativeScaleX=Math.abs(painted.scaleX-e.scaleX)/Math.abs(e.scaleX),relativeScaleY=Math.abs(painted.scaleY-e.scaleY)/Math.abs(e.scaleY);
        // Test the change over the actual clipped viewport in DIP. A tiny
        // extender's 1/64px CSS rounding can have a large relative percentage
        // while changing visible ink by far less than the frozen DIP budget.
        // Conversely a small relative change over a wide path must still pass
        // this absolute geometric guard. Raw relative deltas remain in failures.
        const inkWidth=Math.min(refPath.width,(painted.normalizedInkWidth??Infinity)*Math.abs(e.scaleX)),inkHeight=Math.min(refPath.height,(painted.normalizedInkHeight??Infinity)*Math.abs(e.scaleY));
        const visibleScaleEffectX=relativeScaleX*inkWidth,visibleScaleEffectY=relativeScaleY*inkHeight;
        if(dx>positionTolerance||dy>positionTolerance||visibleScaleEffectX>positionTolerance||visibleScaleEffectY>positionTolerance)fail('path-transform',{painted,expected:e,dx,dy,relativeScaleX,relativeScaleY,visibleScaleEffectX,visibleScaleEffectY,tolerance:positionTolerance});
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
          let children=[];
          for(const child of n.children.filter(n=>n.name!=='annotation').map(normalize)){
            // Adjacent DOM text-node boundaries are XML serialization syntax.
            // Preserve every character while coalescing only neighboring text.
            if(child.type==='text'&&children.at(-1)?.type==='text')children.at(-1).text+=child.text;
            else children.push(child);
          }
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
    const presentationChecks=[];
    for(const expected of reference.presentations??[]){
      const directory=path.join(root,folder,'presentation',expected.id),result=JSON.parse(await fs.readFile(path.join(directory,'result.json'),'utf8'));
      const mathml=await fs.readFile(path.join(directory,'error.mathml'),'utf8');
      const comparison=await page.evaluate(({mathml,expected,result})=>{
        const canonicalColor=value=>{
          if(value.startsWith('#'))return value.toLowerCase().replace(/^#([0-9a-f]{6})ff$/,'#$1');
          const n=value.match(/[\d.]+/g)?.map(Number);return n?.length>=3?'#'+n.slice(0,3).map(v=>Math.round(v).toString(16).padStart(2,'0')).join(''):value;
        };
        const parse=text=>new DOMParser().parseFromString(text,'application/xml');
        const a=parse(mathml),e=parse(expected.mathmlText);
        if(a.querySelector('parsererror')||e.querySelector('parsererror'))return{pass:false,error:'Invalid actual or captured browser MathML'};
        if(a.querySelector('annotation')?.textContent!==expected.sourceText)return{pass:false,error:'Tolerant presentation original source annotation differs'};
        const normalize=node=>{
          if(node.nodeType===3)return{type:'text',text:node.data};
          const attributes=Object.fromEntries([...node.attributes].filter(a=>a.name!=='xmlns'&&!(a.name==='display'&&a.value==='inline')).map(a=>[a.name,a.name==='mathcolor'?canonicalColor(a.value):a.value]).sort(([a],[b])=>a.localeCompare(b)));
          const children=[];
          for(const child of [...node.childNodes].filter(n=>n.nodeType===1||n.nodeType===3).filter(n=>n.localName!=='annotation').map(normalize)){
            if(child.type==='text'&&children.at(-1)?.type==='text')children.at(-1).text+=child.text;
            else children.push(child);
          }
          if(node.localName==='semantics')return children[0];if(node.localName==='mrow'&&children.length===1&&!Object.keys(attributes).length)return children[0];
          return{namespace:node.namespaceURI,name:node.localName,attributes,children};
        };
        const actual=normalize(a.documentElement),reference=normalize(e.documentElement);
        return{pass:result.isErrorFallback&&result.source===expected.sourceText&&canonicalColor(result.errorColor)===canonicalColor(expected.errorColor)&&JSON.stringify(actual)===JSON.stringify(reference),actual,expected:reference,actualColor:result.errorColor,expectedColor:expected.errorColor};
      },{mathml,expected:expected.geometry,result});
      presentationChecks.push({id:expected.id,pass:comparison.pass});
      if(!comparison.pass)failures.push({id:expected.id,scenario:'presentation',check:'tolerant-presentation-source-color-mathml',detail:comparison});
    }
    await writeJson(path.join(root,folder,referenceSubset?'differential.subset.json':'differential.json'),{schemaVersion:1,folder,referenceSubset,semanticComparisonExecuted:!process.argv.includes('--geometry-diagnostic'),validFixtures:report.validFixtures,invalidFixtures:report.invalidFixtures,measurements:checks.length,presentationChecks,failures,checks});
    all.push({folder,referenceSubset,validFixtures:report.validFixtures,invalidFixtures:report.invalidFixtures,measurements:checks.length,failures:failures.length,maxAdvanceDelta:Math.max(...checks.map(x=>x.deltas.advance)),maxAscentDelta:Math.max(...checks.map(x=>x.deltas.ascent)),maxDepthDelta:Math.max(...checks.map(x=>x.deltas.depth)),maxGlyphXDelta:Math.max(...checks.map(x=>x.maxGlyphXDelta)),maxGlyphBaselineDelta:Math.max(...checks.map(x=>x.maxGlyphBaselineDelta))});
    console.log(JSON.stringify(all.at(-1)));
    if(failures.length)console.log('Failures: '+JSON.stringify(failures.slice(0,8)));
  }
  await writeJson(path.join(root,`artifacts/${phase}/${referenceSubset?'differential-subset-summary':'differential-summary'}.json`),all);
  if(all.some(x=>x.failures))process.exitCode=1;
}finally{await browser.close();}
