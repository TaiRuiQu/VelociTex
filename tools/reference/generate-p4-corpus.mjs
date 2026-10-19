// Broad, immutable input corpus drawn from the locked registries and upstream
// tests. Native acceptance never influences which valid inputs remain in scope.
import fs from 'node:fs/promises';
import path from 'node:path';
import ts from 'typescript';
import {root,sourceRoot,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const upstream=loadSource(),cases=[],seen=new Set();
function add(id,category,latex,displayMode,provenance,macros,expectation='valid') {
  const key=JSON.stringify([latex,!!displayMode,macros??null]);if(seen.has(key))return;seen.add(key);
  cases.push({id,category,latex,displayMode:!!displayMode,expectation,provenance,...(macros?{macros}:{})});
}
const p2=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p2.json'),'utf8'));
for(const f of p2.cases)add(f.id,f.category,f.latex,f.displayMode,'P2 frozen regression',f.macros,f.expectation);
const outside=new Set(['\\href','\\url','\\includegraphics','\\htmlClass','\\htmlId','\\htmlStyle','\\htmlData']);
function accepts(latex,displayMode=false,macros){try{upstream.katex.renderToString(latex,{displayMode,macros:{...macros},throwOnError:true,trust:false,strict:'ignore',maxExpand:1000});return true;}catch{return false;}}
// Each registered symbol gets an actual use case, separated by original mode,
// atom/font group. Aliases retain identities even if they share a glyph.
for(const [mode,symbols]of Object.entries(upstream.symbols))for(const [name,definition]of Object.entries(symbols)) {
  if(outside.has(name)||name==='$'||name==='\\')continue;
  let expression=name;
  if(definition.group==='accent-token')expression=name+'{x}';
  if(mode==='text')expression='\\text{'+expression+'}';
  if(accepts(expression))add(`p4-symbol-${mode}-${sha256(Buffer.from(name)).slice(0,16)}`,`symbols-${mode}-${definition.font}-${definition.group}`,expression,false,`Locked src/symbols.ts registration ${mode}:${name}`);
}
// AST strings passed to expect() in upstream tests supply nested interactions,
// Unicode branches, styles, boxes and macro syntax. Reject invalid strings with
// the upstream itself; do not execute the test suite or infer native support.
for(const file of ['test/katex-spec.ts','test/mathml-spec.ts','test/unicode-spec.ts']) {
  const content=await fs.readFile(path.join(sourceRoot,file),'utf8'),tree=ts.createSourceFile(file,content,ts.ScriptTarget.Latest,true);
  const visit=node=>{
    if(ts.isCallExpression(node)&&ts.isIdentifier(node.expression)&&node.expression.text==='expect'&&node.arguments.length){
      const value=node.arguments[0];if(ts.isStringLiteralLike(value)){
        const latex=value.text;
        if(latex.length<=4096&&/[\\_^{}]|[^\x00-\x7f]/u.test(latex)&&![...outside].some(command=>latex.includes(command))) {
          const displayMode=!accepts(latex,false)&&accepts(latex,true);
          if(accepts(latex,displayMode))add(`p4-upstream-${sha256(Buffer.from(file+':'+latex)).slice(0,16)}`,'upstream-interactions',latex,displayMode,`${file}:${tree.getLineAndCharacterOfPosition(value.pos).line+1}`);
        }
      }
    }
    ts.forEachChild(node,visit);
  };visit(tree);
}
const constructions=[
 String.raw`\color{red}{a+b}+c`,String.raw`\textcolor{#369}{x^2}+y`,String.raw`\colorbox{yellow}{$x+1$}`,
 String.raw`\fcolorbox{red}{#eeeeee}{$\frac ab$}`,String.raw`\boxed{\frac ab}`,String.raw`\cancel{x+y}`,String.raw`\bcancel{x+y}`,String.raw`\xcancel{x+y}`,String.raw`\sout{abc}`,
 String.raw`\phantom{xyz}+a`,String.raw`\hphantom{xyz}+a`,String.raw`\vphantom{\frac ab}x`,String.raw`\smash{\frac ab}x`,String.raw`\smash[t]{x^2}y`,String.raw`\smash[b]{x_i}y`,
 String.raw`a\llap{x}b`,String.raw`a\rlap{x}b`,String.raw`a\clap{x}b`,String.raw`\mathllap{x^2}+y`,
 String.raw`\raisebox{1ex}{$x$}+y`,String.raw`\rule[1pt]{2em}{.4pt}`,String.raw`\vcenter{\hbox{abc}}`,String.raw`\pmb{x+y}`,String.raw`\reflectbox{abc}`,
 String.raw`\mathchoice{D}{T}{S}{s}`,String.raw`x^{\mathchoice{D}{T}{S}{s}}`,String.raw`\html@mathml{a}{b}`,
 String.raw`\overset{a}{x}+\underset{b}{y}`,String.raw`\stackrel{!}{=}`,String.raw`a\mathbin{*}b`,String.raw`\mathrel{a}=b`,
 String.raw`\Huge x_i+{\tiny y^2}`,String.raw`\large\frac{x}{y}`,String.raw`x\kern 3pt y\mkern-2mu z`,
 String.raw`\verb|a_b^c\%|`,"\\text{\\'e \\`a \\\"o \\c c}",String.raw`f'(x)+g''(x)`,
 String.raw`\begin{equation}x=1\end{equation}`,String.raw`\begin{align}x&=1\\y&=2\end{align}`,
 String.raw`x=1\tag{A}`,String.raw`x=1\tag*{custom}`,String.raw`\begin{CD}A@>f>>B\\@VgVV@VVhV\\C@>>k>D\end{CD}`,
 String.raw`\def\f#1,#2;{\frac{#1}{#2}}\f a,b;`,String.raw`\def\a{x}\edef\b{\a}\def\a{y}\b`,String.raw`\def\a{x}\let\b=\a\def\a{y}\b`,
 String.raw`\@firstoftwo{a}{b}+\@secondoftwo{c}{d}`,String.raw`\TextOrMath{text}{math}+\text{\TextOrMath{text}{math}}`,
];
for(const [i,latex]of constructions.entries()){
  const displayMode=!accepts(latex,false)&&accepts(latex,true);
  if(!accepts(latex,displayMode))throw new Error(`Authored construction ${i} invalid upstream: ${latex}`);
  add(`p4-construction-${String(i+1).padStart(3,'0')}`,'remaining-constructions',latex,displayMode,'Authored core construction checked against exact upstream');
}
const valid=cases.filter(f=>f.expectation==='valid');
if(valid.length<1000)throw new Error('Fewer than1000 independent valid fixtures');
const result={schemaVersion:1,phase:'P4',reference:lock.source,policy:'Original core scope and denominator preserved. Registered symbols plus AST-literal upstream interactions; validity independently checked with exact upstream. Native failures must remain visible and do not alter corpus membership.',counts:{valid:valid.length,invalid:cases.length-valid.length},cases};
if(process.argv.includes('--registrations')){
  const registrations=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p4-registrations.json'),'utf8'));
  for(const f of registrations.cases){
    if(cases.some(x=>x.id===f.id))throw new Error('Duplicate registration fixture ID');
    cases.push({...f,options:f.referenceOptions??{},expectation:f.referenceOptions?.throwOnError===false?'invalid':f.expectation,...(f.referenceOptions?.throwOnError===false?{presentationFallback:true}:{})});
  }
  result.registrationIdentities=registrations.identities;
  result.counts={valid:cases.filter(x=>x.expectation==='valid').length,invalid:cases.filter(x=>x.expectation==='invalid').length};
}
if(process.argv.includes('--edges')){
  const edges=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p4-edge.json'),'utf8'));
  for(const f of edges.cases){if(cases.some(x=>x.id===f.id))throw new Error('Duplicate edge ID');cases.push({...f,options:f.referenceOptions??{}});}
  result.counts={valid:cases.filter(x=>x.expectation==='valid').length,invalid:cases.filter(x=>x.expectation==='invalid').length};
}
if(process.argv.includes('--edges2')){
  const edges=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p4-edge2.json'),'utf8'));
  for(const f of edges.cases){if(cases.some(x=>x.id===f.id))throw new Error('Duplicate edge2 ID');cases.push({...f,options:f.referenceOptions??{}});}
  result.counts={valid:cases.filter(x=>x.expectation==='valid').length,invalid:cases.filter(x=>x.expectation==='invalid').length};
}
const target=path.join(root,'tests/fixtures/p4.json');
if(process.argv.includes('--check')){if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(result))throw new Error('P4 corpus changed');}else await writeJson(target,result);
console.log(JSON.stringify({...result.counts,sha256:sha256(await fs.readFile(target))}));
