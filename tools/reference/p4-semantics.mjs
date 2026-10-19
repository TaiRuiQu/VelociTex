import fs from 'node:fs/promises';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {root,sourceRoot,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';

// Full-core semantic projection only. No geometry or C# rule table is used for the
// upstream side. Redundant implementation wrappers are normalized explicitly;
// grouping, script attachment, table cells and construction arguments survive.
export function commonSemantics(source,extended=true) {
  const fonts={mathrm:'MathRoman',mathbf:'MathBold',mathit:'MathItalic',mathnormal:'MathNormal',boldsymbol:'MathBoldItalic',mathsf:'MathSans',mathsfit:'MathSansItalic',mathtt:'MathTypewriter',mathcal:'Caligraphic',mathfrak:'Fraktur',mathscr:'Script',mathbb:'AMS'};
  const round=x=>Math.round(x*1e9)/1e9;
  const dim=d=>!d?0:round(d.number*({mu:1/18,em:1,ex:.431,pt:.1,bp:.100375,px:.100375,pc:1.2,in:7.227,cm:7.227/2.54,mm:7.227/25.4,dd:.1*1238/1157,cc:.1*14856/1157,nd:.1*685/642,nc:.1*1370/107,sp:.1/65536}[d.unit]??NaN));
  const delim=x=>!x||x==='.'?'':x==='|'||x==='\\vert'||x==='\\lvert'||x==='\\rvert'?'|':x==='\\|'||x==='\\Vert'||x==='\\lVert'||x==='\\rVert'?'‖':source.symbols.math[x]?.replace??({'\\{':'{','\\}':'}'}[x]??x);
  const compact=x=>{
    if(Array.isArray(x))return x.flatMap(compact);
    if(!x||typeof x!=='object')return x;
    let n=Object.fromEntries(Object.entries(x).map(([k,v])=>[k,compact(v)]));
    // Parser-generated style wrappers and argument groups do not create extra
    // math atoms. Keep ordinary source groups, including empty groups.
    if(n.kind==='argument')return n.children;
    return n;
  };
  function upstream(tree) {
    const list=n=>n==null?null:n.type==='ordgroup'?n.body.map(node):Array.isArray(n)?n.map(node):[node(n)];
    const arg=n=>compact(list(n));
    function node(n) {
      switch(n.type) {
        case 'mathord':case 'textord':case 'atom':return{kind:'symbol',text:extended&&n.text.codePointAt(0)>0xffff?n.text:source.symbols[n.mode][n.text]?.replace??n.text,atom:n.family??'ord',textMode:n.mode==='text'};
        case 'spacing':return n.text==='\\nobreak'||n.text==='\\allowbreak'?{kind:'space',value:0}:{kind:'symbol',text:' ',atom:'ord',textMode:true};
        case 'ordgroup':return{kind:'group',children:n.body.map(node),...(n.semisimple?{semisimple:true}:{})};
        case 'supsub':return{kind:'script',base:n.base?node(n.base):{kind:'group',children:[]},sup:arg(n.sup),sub:arg(n.sub)};
    case 'genfrac':return{kind:'fraction',numer:arg(n.numer),denom:arg(n.denom),hasBar:n.hasBarLine,left:delim(n.leftDelim),right:delim(n.rightDelim),bar:dim(n.barSize),continued:!!n.continued};
        case 'sqrt':return{kind:'radical',body:arg(n.body),index:arg(n.index)};
        case 'font':return{kind:'font',font:fonts[n.font]??n.font,body:arg(n.body)};
        case 'text':return{kind:'text',command:n.font??'\\text',body:arg(n.body)};
        case 'styling':return{kind:'style',style:n.style.toLowerCase(),body:arg(n.body)};
        case 'kern':return{kind:'space',value:dim(n.dimension)};
        case 'cr':return{kind:'linebreak',enabled:!!n.newLine,gap:dim(n.size)};
        case 'op':return{kind:'operator',name:n.name??'\\mathop',body:n.name?null:arg(n.body),limits:!!n.limits,always:!!n.alwaysHandleSupSub,never:n.limits===false&&!!n.alwaysHandleSupSub};
        case 'operatorname':return{kind:'operator',name:'\\operatorname',body:arg(n.body),limits:!!n.limits||!!n.alwaysHandleSupSub,always:false,never:false};
        case 'delimsizing':return{kind:'delimiter',delimiter:delim(n.delim),size:n.size,atom:n.mclass.slice(1)};
        case 'middle':return{kind:'delimiter',delimiter:delim(n.delim),size:0,atom:'rel'};
        case 'leftright':return{kind:'delimited',left:delim(n.left),right:delim(n.right),body:arg(n.body)};
        case 'accent':case 'accentUnder':case 'horizBrace':return{kind:'accent',name:n.label,body:arg(n.base),below:null};
        case 'overline':case 'underline':return{kind:'accent',name:'\\'+n.type,body:arg(n.body),below:null};
        case 'xArrow':return{kind:'accent',name:n.label,body:arg(n.body),below:arg(n.below)};
        case 'mclass':{
          // A transparent mord wrapper used by boldsymbol is normalized; other
          // explicit atom-class wrappers must be compared when P4 adds them.
          if(!extended&&n.mclass==='mord'&&n.body.length===1&&n.body[0].type==='font')return node(n.body[0]);
          return{kind:'class',atom:n.mclass.slice(1),body:arg(n.body)};
        }
        case 'color':return{kind:'color',color:n.color,body:arg(n.body)};
        case 'sizing':return{kind:'sizing',size:n.size,body:arg(n.body)};
        case 'mathchoice':return{kind:'choice',display:arg(n.display),text:arg(n.text),script:arg(n.script),scriptscript:arg(n.scriptscript)};
        case 'htmlmathml':return{kind:'mathml-choice',html:arg(n.html),mathml:arg(n.mathml)};
        case 'phantom':case 'vphantom':case 'hbox':case 'pmb':case 'reflectbox':case 'vcenter':return{kind:'box',name:'\\'+n.type,body:arg(n.body)};
        case 'smash':return{kind:'box',name:'\\smash',body:arg(n.body),selection:(n.smashHeight?'t':'')+(n.smashDepth?'b':'')};
        case 'lap':return{kind:'box',name:'\\math'+n.alignment,body:arg(n.body)};
        case 'raisebox':return{kind:'box',name:'\\raisebox',body:arg(n.body),shift:dim(n.dy)};
        case 'enclose':return{kind:'enclose',name:n.label,body:arg(n.body),background:n.backgroundColor??null,border:n.borderColor??null};
        case 'rule':return{kind:'rule',width:dim(n.width),height:dim(n.height),shift:dim(n.shift)};
        case 'verb':return{kind:'verb',text:n.body,star:n.star};
        case 'tag':return{kind:'tag',body:arg(n.body),tag:arg(n.tag)};
        case 'cdlabelparent':return{kind:'box',name:'cdparent',body:arg(n.fragment)};
        case 'cdlabel':return{kind:'box',name:n.side==='left'?'cdleft':'cdright',body:arg(n.label)};
        case 'array':return{kind:'matrix',columns:n.cols?.map(c=>c.type==='separator'?c.separator:c.align).join('')??'c',stretch:n.arraystretch??1,rows:n.body.map((r,i)=>({cells:r.map(cell=>{
          // Each upstream array cell has a reset-font style and an argument
          // ordgroup. That style is retained, with the argument wrapper removed.
          if(cell.type==='styling')return[{kind:'style',style:cell.style,body:arg(cell.body.length===1?cell.body[0]:cell.body)}];return arg(cell);
        }),ruleAbove:(n.hLinesBeforeRow?.[i]??[]).reduce((v,d)=>v+(d?-1:1),0),gap:dim(n.rowGaps?.[i]),...(n.tags?{tag:Array.isArray(n.tags[i])?arg(n.tags[i]):!!n.tags[i]}:{})})),ruleBelow:(n.hLinesBeforeRow?.[n.body.length]??[]).reduce((v,d)=>v+(d?-1:1),0)};
        default:throw new Error('Unprojected upstream common node '+n.type);
      }
    }
    return compact(tree.map(node));
  }
  function native(doc) {
    const ns=doc.nodes;
    const chain=id=>{const a=[];for(let i=id;i>=0;i=ns[i].next)a.push(node(i));return a;};
    const list=id=>id<0?null:ns[id].kind==='Row'?chain(ns[id].first):[node(id)];
    const stripCell=id=>{const style=ns[ns[id].first];return[{kind:'style',style:style.style.toLowerCase(),body:list(style.first)}];};
    function node(id) {
      const n=ns[id];
      switch(n.kind) {
        case 'Row':return{kind:'group',children:chain(n.first),...(n.value===2?{semisimple:true}:{})};
        case 'Symbol':return{kind:'symbol',text:n.codePoint===160?' ':String.fromCodePoint(extended&&n.originalCodePoint>0?n.originalCodePoint:n.codePoint),atom:n.atom.toLowerCase(),textMode:n.codePoint===160?true:n.textMode};
        case 'Script':return{kind:'script',base:node(n.first),sup:list(n.second),sub:list(n.third)};
        case 'Fraction':return{kind:'fraction',numer:list(n.first),denom:list(n.second),hasBar:n.hasBar,left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',bar:n.text==='\\genfrac'||n.text==='\\above'?dim({number:n.length1.number,unit:n.length1.unit.toLowerCase()}):round(n.value),continued:n.text==='\\cfrac'};
        case 'Radical':return{kind:'radical',body:list(n.first),index:list(n.second)};
        case 'Font':return{kind:'font',font:n.font,body:list(n.first)};
        case 'Text':return{kind:'text',command:n.text??'\\text',body:list(n.first)};
        case 'Style':return{kind:'style',style:n.style.toLowerCase(),body:n.textMode&&ns[n.first]?.kind==='Row'&&ns[n.first].value===1?[node(n.first)]:list(n.first)};
        case 'Space':return n.text==='cdnull'?{kind:'symbol',text:' ',atom:'ord',textMode:false}:n.text==='\\\\'?{kind:'linebreak',enabled:!!n.value,gap:dim({number:n.length1.number,unit:n.length1.unit.toLowerCase()})}:{kind:'space',value:n.text&&['\\kern','\\mkern','\\hskip','\\mskip'].includes(n.text)?dim({number:n.length1.number,unit:n.length1.unit.toLowerCase()}):n.textMode&&Math.abs(n.value)<1?round(Math.round(n.value*1e4)/1e4):round(n.value)};
        case 'Color':return{kind:'color',color:n.text2,body:ns[n.first]?.kind==='Row'?list(n.first):chain(n.first)};
        case 'Sizing':return{kind:'sizing',size:n.value,body:list(n.first)};
        case 'Choice':return{kind:'choice',display:list(n.first),text:list(n.second),script:list(n.third),scriptscript:list(n.fourth)};
        case 'MathMlChoice':return{kind:'mathml-choice',html:list(n.first),mathml:list(n.second)};
        case 'Class':return{kind:'class',atom:n.atom.toLowerCase(),body:n.text==='\\boldsymbol'||n.text==='\\bm'?[{kind:'font',font:'MathBoldItalic',body:list(n.first)}]:list(n.first)};
        case 'Box':{
          const result={kind:'box',name:n.text==='\\mathreflectbox'?'\\reflectbox':n.text,body:n.text==='\\phantom'||n.text==='\\hbox'||n.text==='\\pmb'?ns[n.first]?.kind==='Row'?list(n.first):chain(n.first):list(n.first)};
          if(n.text==='\\smash')result.selection=n.alignment??'tb';
          if(n.text==='\\raisebox')result.shift=dim(n.length1?{number:n.length1.number,unit:n.length1.unit.toLowerCase()}:null);
          return result;
        }
        case 'Enclose':return{kind:'enclose',name:n.text,body:list(n.first),background:n.text2??null,border:n.text3??null};
        case 'Rule':return{kind:'rule',width:dim({number:n.length1.number,unit:n.length1.unit.toLowerCase()}),height:dim({number:n.length2.number,unit:n.length2.unit.toLowerCase()}),shift:dim({number:n.length3.number,unit:n.length3.unit.toLowerCase()})};
        case 'Verb':return{kind:'verb',text:n.text2??n.text,star:n.value!==0};
        case 'Tag':return{kind:'tag',body:list(n.first),tag:list(n.second)};
        case 'Operator':{
          const expandedName=n.text==='\\limsup'||n.text==='\\liminf';
          return{kind:'operator',name:expandedName?'\\operatorname':n.text,body:n.text==='\\mathop'?ns[n.first]?.kind==='Row'?list(n.first):chain(n.first):n.text==='\\operatorname'||expandedName?list(n.first):null,limits:n.text==='\\operatorname'?n.text2==='star':n.neverLimits?false:n.alwaysLimits||n.limits,always:n.text==='\\operatorname'||expandedName?false:n.alwaysLimits||n.neverLimits,never:n.text==='\\operatorname'?false:n.neverLimits};
        }
        case 'Delimiter':return{kind:'delimiter',delimiter:n.codePoint?String.fromCodePoint(n.codePoint):'',size:n.value,atom:n.atom.toLowerCase()};
        case 'Delimited':return{kind:'delimited',left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',body:list(n.first)};
        case 'Accent':return{kind:'accent',name:n.text,body:list(n.first),below:list(n.second)};
        case 'Matrix':{
          const rows=[];for(let r=n.first;r>=0;r=ns[r].next){const cells=[];for(let c=ns[r].first;c>=0;c=ns[c].next)cells.push(stripCell(c));const tag=ns[r].alwaysLimits?true:ns[r].second>=0?list(ns[r].second):false;rows.push({cells,ruleAbove:ns[r].ruleAbove,gap:round(ns[r].value),...(['equation','equation*','align','align*','alignat','alignat*','gather','gather*'].includes(n.text)?{tag}:{})});}
          let columns=n.alignment;
          if(['aligned','alignedat','align','align*','alignat','alignat*','split'].includes(n.text))columns='rl'.repeat(['alignedat','alignat','alignat*'].includes(n.text)?n.value:Math.ceil(n.columnCount/2));
          if(n.text==='CD')columns='c'.repeat(n.columnCount);
          if(n.text==='cases'||n.text==='dcases'||n.text==='rcases'||n.text==='drcases')columns='ll';
          if(['matrix','pmatrix','bmatrix','Bmatrix','vmatrix','Vmatrix'].includes(n.text.replace(/\*$/,'')))columns=columns.repeat(n.columnCount);
          const fixedStretch=['cases','dcases','rcases','drcases'].includes(n.text)?1.2:['smallmatrix','subarray'].includes(n.text)?.5:1;
          const table={kind:'matrix',columns,stretch:fixedStretch!==1||n.text==='CD'?fixedStretch:n.value2>0?n.value2:1,rows,ruleBelow:n.ruleBelow};
          return n.leftDelimiter||n.rightDelimiter?{kind:'delimited',left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',body:[table]}:table;
        }
        default:throw new Error('Unprojected common node '+n.kind);
      }
    }
    return compact(ns[doc.root].kind==='Row'?chain(ns[doc.root].first):[node(doc.root)]);
  }
  return{upstream,native};
}

if(process.argv[1]&&path.resolve(process.argv[1])===fileURLToPath(import.meta.url)) {
  const folder=process.argv.find(x=>x.startsWith('--folder='))?.slice(9)??'artifacts/p4/diagnostic';
  const source=loadSource();
  const manifest=JSON.parse(await fs.readFile(path.join(root,'docs/reference/katex-0.19.0.files.json'),'utf8'));
  for(const module of source.moduleSources){const entry=manifest.sourceFiles.find(x=>x.path===module);if(!entry||sha256(await fs.readFile(path.join(sourceRoot,module)))!==entry.sha256)throw new Error('Locked semantic reference source drift: '+module);}
  const projection=commonSemantics(source);
  const referencePath=path.join(root,'artifacts/p4/reference/semantics.json'),reportPath=path.join(root,folder,'report.json');
  const reference=JSON.parse(await fs.readFile(referencePath,'utf8')),report=JSON.parse(await fs.readFile(reportPath,'utf8'));
  const failures=[],checks=[];
  for(const expected of reference.filter(x=>x.result==='valid')) {
    const actual=report.results.find(x=>x.id===expected.id&&x.success);
    if(!actual){failures.push({id:expected.id,check:'missing-native'});continue;}
    try {
      const normalizedActual=projection.native(actual),normalizedExpected=projection.upstream(expected.tree);
      const pass=JSON.stringify(normalizedActual)===JSON.stringify(normalizedExpected);
      checks.push({id:actual.id,scenario:actual.scenario,pass});
      if(!pass)failures.push({id:actual.id,source:actual.source,expected:normalizedExpected,actual:normalizedActual});
    }catch(error){failures.push({id:actual.id,source:actual.source,error:error.stack});}
  }
  const result={schemaVersion:1,semanticComparisonExecuted:true,phase:'P4',folder,corpusSha256:report.corpusSha256,sourceCommit:manifest.reference?.commit??'6ea2dc97188f0cc282e05a9690418ae7fe5457ba',nativeProcessId:report.processId,nativeReportSha256:sha256(await fs.readFile(reportPath)),upstreamSemanticsSha256:sha256(await fs.readFile(referencePath)),projectionSha256:sha256(await fs.readFile(fileURLToPath(import.meta.url))),validFixtures:reference.filter(x=>x.result==='valid').length,compared:checks.length,failures,checks};
  await writeJson(path.join(root,'artifacts/p4/semantic-diagnostic.json'),result);
  console.log(JSON.stringify({compared:checks.length,failures:failures.length,folder}));
  if(failures.length)process.exitCode=1;
}
