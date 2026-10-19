import fs from 'node:fs/promises';
import path from 'node:path';
import {root,lock,sha256,writeJson} from './acquire.mjs';
import {loadSource} from './load-source.mjs';
const {katex}=loadSource();
const examples=[
 [String.raw`\kern + 1 em x`],
 [String.raw`\genfrac{x}{y}{0pt}{}{a}{b}`],
 [String.raw`\genfrac{||}{||}{0pt}{}{a}{b}`],
 [String.raw`\genfrac{\{}{}{0pt}{}{a}{b}`],
 [String.raw`\genfrac{}{}{}{2x}{a}{b}`],
 [String.raw`\genfrac{}{}{}{x}{a}{b}`],
 [String.raw`\genfrac{\frac}{x}{0pt}{}{a}{b}`,'invalid'],
 [String.raw`\begin{array}{c:r}a&b\\c&d\end{array}`],
 [String.raw`\def\arraystretch{2}\begin{matrix}x\\y\end{matrix}`],
 [String.raw`\def\arraystretch{2}\begin{cases}x&x>0\\y&x<0\end{cases}`],
 [String.raw`\def\arraystretch{bad}\begin{cases}x&x>0\end{cases}`],
 [String.raw`\def\arraystretch{0}\begin{matrix}x\\y\end{matrix}`,'invalid'],
 [String.raw`\noexpand\frac x+y`],
 [String.raw`\def\a{x}\edef\b{\noexpand\a}\def\a{y}\b`],
 [String.raw`\def\a{x}\expandafter\def\expandafter\b\expandafter{\a}\b`],
 [String.raw`\def a{x}a`],
 [String.raw`\def\n{2}\newcommand\f[\n]{#1+#2}\f xy`],
 [String.raw`\def\unused{#2}`],
 [String.raw`\def\unused#1{#2}`],
 [String.raw`\newcommand\unused[1]{#2}`],
 [String.raw`\def\unused#1{#2}\unused x`,'invalid'],
 [String.raw`\def\arg{\TextOrMath{T}{M}}\text\arg`],
 [String.raw`\text{a $\TextOrMath{T}{M}$ b}`],
 [String.raw`\text{a \(\TextOrMath{T}{M}\) b}`],
 [String.raw`\fbox{\TextOrMath{T}{M}}`],
 [String.raw`\colorbox{yellow}{\TextOrMath{T}{M}}`],
 [String.raw`\operatorname{rank}\limits_x`,'valid',true],
 [String.raw`\operatorname*{argmax}\nolimits_x`,'valid',true],
 [String.raw`\operatorname*{argmax}\limits_x`],
 [String.raw`\overset{!}{x+y}`],
 [String.raw`\mathop{x+y}_i`],
 [String.raw`\mathop{x}`],
 [String.raw`x'^2+f''(x)`],
 [String.raw`x^\relax2`],
 [String.raw`x^\sum`,'invalid'],
 [String.raw`\text\frac12`,'invalid'],
 [String.raw`\textsf{\textbf{A}}+\textbf{\textit{B}}+\textit{\emph{C}}`],
 [String.raw`\text{नमस्ते}`],
 [String.raw`\text{ગુજરાતી}`],
 [String.raw`\text{বাংলা}`],
 [String.raw`\text{ภาษาไทย}`],
 [String.raw`\text{한글}`],
 [String.raw`\text{한글}`],
 [String.raw`\text{Привет й}`],
 [String.raw`\text{Հայերեն}`],
 [String.raw`\text{გთხოვთ}`],
 [String.raw`a\dots+b\dots=\sum_i c_i`],
 [String.raw`(a\dotso)+a\dotsc,b\dotsc;c\cdots)`],
 [String.raw`\Braket{a|b||c}+\Set{x|x>0|y}`],
 [String.raw`\phantom{x}`],
 [String.raw`\phantom{xyz}`],
 [String.raw`\hphantom{x}`],
 [String.raw`\vphantom{x}`],
 [String.raw`\color{transparent}x`],
 [String.raw`\textcolor{red}{\phantom{xyz}}`],
 ['\\text{hello% comment\nworld}'],
 ['é','valid',false,{strict:'warn'}],
 ['é','invalid',false,{strict:'error'}],
 ['x% comment','valid',false,{strict:'warn'}],
 ['x% comment','invalid',false,{strict:'error'}],
 [String.raw`\color{red}{x}+y`,'valid',false,{colorIsTextColor:true,globalGroup:true}],
];
const cases=[];
for(const [index,[latex,expectation='valid',displayMode=false,referenceOptions={}]] of examples.entries()){
 let success=true,upstreamErrorName;try{katex.renderToString(latex,{displayMode,throwOnError:true,strict:'ignore',trust:false,...referenceOptions});}catch(error){success=false;upstreamErrorName=error.name;}
 if(success!==(expectation==='valid'))throw new Error('Edge input expectation differs from locked upstream: '+latex);
 cases.push({id:'p4-edge-'+String(index+1).padStart(3,'0'),category:'source-verified-core-edges',latex,expectation,displayMode,referenceOptions,...(upstreamErrorName?{upstreamErrorName}:{}),provenance:'Authored edge independently checked using locked KaTeX 0.19.0; no native-result filtering.'});
}
const result={schemaVersion:1,phase:'P4',reference:lock.source,counts:{valid:cases.filter(x=>x.expectation==='valid').length,invalid:cases.filter(x=>x.expectation==='invalid').length},cases};
const target=path.join(root,'tests/fixtures/p4-edge.json');
if(process.argv.includes('--check')){if(JSON.stringify(JSON.parse(await fs.readFile(target,'utf8')))!==JSON.stringify(result))throw new Error('P4 edge input drift');}else await writeJson(target,result);
console.log(JSON.stringify({...result.counts,sha256:sha256(await fs.readFile(target))}));
