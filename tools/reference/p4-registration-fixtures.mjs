// Inputs are selected by the locked upstream registrations and accepted by the
// locked upstream itself. No native result participates in fixture selection.
import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const upstream=loadSource();
const inventory=JSON.parse(await fs.readFile(path.join(root,'compatibility/core-0.19.0.json'),'utf8'));
const cases=[], identities=[], failures=[], keys=new Map();
const messages=[];
const originalLog=console.log,originalWarn=console.warn,originalError=console.error;
function candidate(latex,displayMode=false,extra={}) {return {latex,displayMode,expectation:'valid',...extra};}
function accepted(value){
 try {
  console.log=(...args)=>messages.push({kind:'log',message:args.map(String).join(' ')});
  console.warn=(...args)=>messages.push({kind:'warn',message:args.map(String).join(' ')});
  console.error=(...args)=>messages.push({kind:'error',message:args.map(String).join(' ')});
  const opts={displayMode:value.displayMode,throwOnError:true,trust:false,strict:'ignore',maxExpand:1000,...value.referenceOptions,macros:{...value.macros}};
  upstream.katex.renderToString(value.latex,opts);
  return true;
 }catch{return false;}finally{console.log=originalLog;console.warn=originalWarn;console.error=originalError;}
}
function add(entry,value,reason){
 const signature=JSON.stringify([value.latex,value.displayMode,value.macros??null,value.referenceOptions??null,value.expectation]);
 let result=keys.get(signature);
 if(!result){result={id:'p4-registration-'+sha256(Buffer.from(signature)).slice(0,16),category:'registrations-'+entry.kind,...value,provenance:reason,registrationIds:[]};keys.set(signature,result);cases.push(result);}
 if(!result.registrationIds.includes(entry.id))result.registrationIds.push(entry.id);
 return result.id;
}
const special={
 '\\char':['\\char65','\\char\'101','\\char"41','\\char`A'],
 '\\noexpand':['\\def\\a{x}a\\noexpand\\a+b'],
 '\\expandafter':['\\def\\a{x}\\expandafter\\def\\expandafter\\b\\expandafter{\\a}\\b'],
 '\\@firstoftwo':['\\@firstoftwo{a}{b}'], '\\@secondoftwo':['\\@secondoftwo{a}{b}'],
 '\\@ifnextchar':['\\@ifnextchar x{a}{b}x'], '\\TextOrMath':['\\TextOrMath{text}{math}+\\text{\\TextOrMath{text}{math}}'],
 '\\message':['\\message{message}x'], '\\errmessage':['\\errmessage{message}x'], '\\show':['\\show\\frac x'],
 '\\newcommand':['\\newcommand\\registration[1]{#1^2}\\registration{x}'],
 '\\renewcommand':['\\newcommand\\registration{x}\\renewcommand\\registration{y}\\registration'],
 '\\providecommand':['\\providecommand\\registration{x}\\registration'],
 '\\tag@literal':['x=1\\tag@literal{A}'], '\\tag':['x=1\\tag{A}'], '\\tag@paren':['x=1\\tag@paren{A}'],
 '\\hspace':['x\\hspace{1em}y','x\\hspace*{1em}y'], '\\@ifstar':['\\@ifstar{a}{b}*','\\@ifstar{a}{b}x'],
 '\\Set':['\\Set{x|x>0|y}'], '\\set':['\\set{x|x>0|y}'], '\\Braket':['\\Braket{a|b||c}'], '\\operatorname':['\\operatorname{rank}(A)','\\operatorname*{argmax}_x f(x)'],
 '\\bra@ket':['\\Braket{a|b||c}'], '\\bra@set':['\\Set{x|x>0|y}'],
 '\\def':['\\def\\registration#1{#1^2}\\registration{x}'], '\\gdef':['\\gdef\\registration{x}\\registration'],
 '\\edef':['\\def\\a{x}\\edef\\b{\\a}\\b'], '\\xdef':['\\def\\a{x}{\\xdef\\b{\\a}}\\b'],
 '\\let':['\\let\\registration=\\alpha\\registration'], '\\\\globallet':['{\\global\\let\\registration=\\alpha}\\registration'],
 '\\futurelet':['\\def\\a{x}\\futurelet\\b\\a\\alpha'], '\\\\globalfuture':['{\\global\\futurelet\\b\\alpha x}\\b'],
 '\\global':['{\\global\\def\\a{x}}\\a'], '\\long':['\\long\\def\\a#1{#1}\\a{x}'], '\\\\globallong':['\\global\\long\\def\\a#1{#1}\\a{x}'],
 '\\bgroup':['\\bgroup x\\egroup'], '\\egroup':['\\bgroup x\\egroup'],
 '\\relax':['x\\relax+y'], '\\cr':['\\begin{matrix}a\\cr b\\end{matrix}'],
 '\\hline':['\\begin{array}{c}\\hline x\\\\\\hline\\end{array}'], '\\hdashline':['\\begin{array}{c}\\hdashline x\\\\\\hdashline\\end{array}'],
 '\\end':['\\begin{matrix}x\\end{matrix}'], '\\begin':['\\begin{matrix}x\\end{matrix}'],
 '\\right':['\\left(x\\right)'], '\\middle':['\\left(a\\middle|b\\right)'],
 '\\limits':['\\sum\\limits_i^n x_i'], '\\nolimits':['\\sum\\nolimits_i^n x_i'],
 '^':['x^2'], '_':['x_i'], '\\operatorname@':['\\operatorname{rank}(A)'], '\\operatornamewithlimits':['\\operatorname*{argmax}_x f(x)'],
 '\\\\atopfrac':['{a\\atop b}'], '\\\\bracefrac':['{a\\brace b}'], '\\\\brackfrac':['{a\\brack b}'], '\\\\abovefrac':['{a\\above1pt b}'],
 '\\\\cdrightarrow':['\\begin{CD}A@>f>>B\\end{CD}'], '\\\\cdleftarrow':['\\begin{CD}A@<f<<B\\end{CD}'], '\\\\cdlongequal':['\\begin{CD}A@=B\\end{CD}'],
 '\\\\cdleft':['\\begin{CD}A\\\\@VfVgV\\\\B\\end{CD}'], '\\\\cdright':['\\begin{CD}A\\\\@VfVgV\\\\B\\end{CD}'], '\\\\cdparent':['\\begin{CD}A\\\\@VfVgV\\\\B\\end{CD}'],
 '\\verb':['\\verb|a_b^c\\%|','\\verb*|a b|'], '\\@char':['\\char65'],
 '$':['\\text{a $x$ b}'], '\\(':['\\text{a \\(x\\) b}'], '\\)':['\\text{\\(x\\)}'], '\\]':['\\text{x}'],
 '\\above':['{a\\above1pt b}'], '\\over':['{a\\over b}'], '\\atop':['{a\\atop b}'], '\\choose':['{n\\choose k}'], '\\brace':['{a\\brace b}'], '\\brack':['{a\\brack b}'],
 '\\tmspace':['x\\tmspace+{3mu}{.1667em}y'], '\\@hspace':['x\\@hspace{1em}y'], '\\@hspacer':['x\\@hspacer{1em}y'],
 '\\nonumber':['\\begin{align}x&=1\\nonumber\\end{align}'], '\\notag':['\\begin{align}x&=1\\notag\\end{align}'],
};
function functionCandidates(entry){
 const n=entry.name,u=entry.upstream,type=u.nodeType;
 if(special[n])return special[n].flatMap(latex=>[candidate(latex),candidate(latex,true)]);
 if(type==='delimsizing')return[candidate(n+'(')];
 if(type==='leftright')return[candidate('\\left(x\\right)')];
 if(type==='infix')return[candidate('{a'+n+' b}')];
 if(type==='genfrac'&&n==='\\genfrac')return[candidate('\\genfrac{(}{)}{0pt}{}{a}{b}')];
 if(type==='rule')return[candidate(n+'[1pt]{1em}{.4pt}')];
 if(type==='kern')return[candidate('x'+n+(n==='\\mkern'||n==='\\mskip'?'3mu':'3pt')+'y')];
 if(type==='raisebox')return[candidate(n+'{1ex}{$x$}')];
 if(type==='color')return[candidate(n+'{red}{x+y}')];
 if(type==='enclose')return[candidate(n+(n==='\\fcolorbox'?'{red}{yellow}{$x$}':n==='\\colorbox'?'{yellow}{$x$}':n==='\\fbox'||n==='\\angl'?'{abc}':'{x+y}'))];
 if(type==='sizing')return[candidate(n+' x_i')];
 if(type==='op')return[candidate(n+(u.numArgs?'{x}':'_i^n'))];
 const count=u.numArgs??0,types=u.argTypes??[];
 const argument=(argType,index)=>argType==='size'?'{1em}':argType==='color'?'{red}':argType==='url'?'{https://example.invalid}':argType==='hbox'?'{abc}':argType==='raw'?'{x}':index===1&&type==='mclass'?'{=}':'{x}';
 const body=n+Array.from({length:count},(_,i)=>argument(types[i],i)).join('');
 return[candidate(body),candidate('\\text{'+body+'}')];
}
function macroArity(value){let max=0;for(let i=0;i<value.length;i++)if(value[i]==='#'){if(value[i+1]==='#'){i++;continue;}if(/[1-9]/.test(value[i+1]??''))max=Math.max(max,Number(value[++i]));}return max;}
function macroCandidates(entry){
 if(special[entry.name])return special[entry.name].flatMap(latex=>[candidate(latex),candidate(latex,true)]);
 const arity=macroArity(entry.upstream.definition??'');
 return[candidate(entry.name+Array.from({length:arity},()=>'{x}').join('')),candidate('\\text{'+entry.name+Array.from({length:arity},()=>'{x}').join('')+'}'),candidate(entry.name+Array.from({length:arity},()=>'{1em}').join(''))];
}
function envCandidate(entry){
 const n=entry.name;let args='';if(['array','darray'].includes(n))args='{cc}';if(n==='subarray')args='{c}';if(n.startsWith('alignat')||n==='alignedat')args='{2}';
 const body=n==='CD'?'A@>f>>B':n.startsWith('equation')?'x=1':n.startsWith('gather')||n==='subarray'?'a\\\\b':'a&b\\\\c&d';
 return[candidate('\\begin{'+n+'}'+args+body+'\\end{'+n+'}',true)];
}
const optionCandidates={
 displayMode:[candidate('\\sum_i^n x_i',false,{referenceOptions:{displayMode:false}}),candidate('\\sum_i^n x_i',true,{referenceOptions:{displayMode:true}})],
 macros:[candidate('\\registration{x}',false,{macros:{'\\registration':'#1^2'}})],
 maxExpand:[candidate('\\a',false,{macros:{'\\a':'x'},referenceOptions:{maxExpand:1}})],
 maxSize:[candidate('\\rule{5em}{3em}',false,{referenceOptions:{maxSize:1}})],
 minRuleThickness:[candidate('\\frac{x}{y}+\\sqrt{x}',false,{referenceOptions:{minRuleThickness:.1}})],
 colorIsTextColor:[candidate('\\color{red}{x}+y',false,{referenceOptions:{colorIsTextColor:true}})],
 leqno:[candidate('x=1\\tag{A}',true,{referenceOptions:{leqno:true}})],fleqn:[candidate('x=1',true,{referenceOptions:{fleqn:true}})],
 globalGroup:[candidate('\\def\\a{x}\\a',false,{referenceOptions:{globalGroup:true}})],
 strict:[candidate('é',false,{referenceOptions:{strict:'ignore'}}),candidate('é',false,{referenceOptions:{strict:'warn'}})],
 output:[candidate('x+1',false,{referenceOptions:{output:'mathml'}}),candidate('x+1',false,{referenceOptions:{output:'html'}}),candidate('x+1',false,{referenceOptions:{output:'htmlAndMathml'}})],
 throwOnError:[candidate('\\undefinedRegistration',false,{referenceOptions:{throwOnError:false}})],
 errorColor:[candidate('\\undefinedRegistration',false,{referenceOptions:{throwOnError:false,errorColor:'#123456'}})],
};
const builders={atom:'x+y',mathord:'x',ordgroup:'{x+y}',spacing:'x\\,y',supsub:'x_i^2',tag:'x=1\\tag{A}',textord:'\\text{a}'};
for(const entry of inventory.entries.filter(e=>e.scope==='included'&&e.kind!=='symbol')){
 const candidates=entry.kind==='function'?functionCandidates(entry):entry.kind==='macro'?macroCandidates(entry):entry.kind==='environment'?envCandidate(entry):entry.kind==='option'?optionCandidates[entry.name]??[]:entry.kind==='builder'?[candidate(builders[entry.name]??'x',entry.name==='tag')]:entry.kind==='implicit'?(special[entry.name]??[]).map(latex=>candidate(latex)):[];
 const acceptedValues=candidates.filter(accepted);
 const fixtureIds=[];
 for(const value of acceptedValues)fixtureIds.push(add(entry,value,'Actual locked '+entry.kind+' registration '+entry.name+'; arguments derived from registry and exact source.'));
 const identity={id:entry.id,kind:entry.kind,name:entry.name,fixtureIds:[...new Set(fixtureIds)],status:fixtureIds.length?'upstream-accepted-input':'unexercised',note:fixtureIds.length?'Input validity only; native compatibility remains unverified.':'No accepted candidate was found; identity remains included and unverified.'};
 identities.push(identity);if(!fixtureIds.length)failures.push({...identity,candidates});
}
const result={schemaVersion:1,phase:'P4',reference:lock.source,inventoryDenominator:inventory.summary,policy:'Extra immutable registration inputs, independently accepted by locked upstream. Native acceptance never filters membership. Unexercised identities remain included; input validity earns no compatibility coverage.',counts:{fixtures:cases.length,identities:identities.length,upstreamAccepted:identities.length-failures.length,unexercised:failures.length},identities,cases,unexercised:failures};
const target=path.join(root,'tests/fixtures/p4-registrations.json');
if(process.argv.includes('--check')) {if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(result))throw new Error('Registration fixture drift');} else await writeJson(target,result);
console.log(JSON.stringify({...result.counts,sha256:sha256(await fs.readFile(target)),unexercised:failures.map(x=>x.name)}));
