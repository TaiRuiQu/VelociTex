// Frozen P2 cases exercise different constructions and interactions. Expected
// validity is checked exclusively with the locked upstream, never with C#.
import fs from 'node:fs/promises';
import path from 'node:path';
import {createRequire} from 'node:module';
import {root,packageRoot,lock,sha256,writeJson} from './acquire.mjs';
const require=createRequire(import.meta.url),katex=require(path.join(packageRoot,'dist/katex.js'));
if(katex.version!==lock.npm.version)throw new Error('Reference version drift');
const baseline=JSON.parse(await fs.readFile(path.join(root,'tests/fixtures/p1.json'),'utf8'));
const normal=JSON.parse(await fs.readFile(path.join(root,'benchmarks/corpus.normal.json'),'utf8'));
const cases=baseline.cases.map(f=>({...f,provenance:'P1 frozen regression'}));
for(const f of normal.fixtures)cases.push({...f,expectation:'valid',provenance:'P0 frozen representative normal workload'});
const add=(category,id,latex,displayMode=false)=>cases.push({id:'p2-'+id,category,latex,displayMode,expectation:'valid',provenance:'Construction/interaction case authored against locked KaTeX 0.19.0'});
const group=(category,items)=>{for(const [id,latex,display]of items)add(category,id,latex,!!display);};
group('operators',[
 ['integral-display',String.raw`\int_0^1 x^2\,dx`,true],['integral-inline',String.raw`\int_a^b f(x)\,dx`],
 ['sum-display',String.raw`\sum_{k=1}^{n}\frac1{k^2}`,true],['sum-inline',String.raw`\sum_{j=0}^m a_j`],
 ['prod',String.raw`\prod_{k=1}^n(1+x_k)`,true],['coprod',String.raw`\coprod_{i\in I}X_i`,true],
 ['bigcup',String.raw`\bigcup_{i=1}^n A_i`,true],['bigcap',String.raw`\bigcap_{i\in I} B_i`,true],
 ['bigvee',String.raw`\bigvee_{i=0}^{n}p_i`,true],['bigwedge',String.raw`\bigwedge_{i=0}^{n}q_i`,true],
 ['bigoplus',String.raw`\bigoplus_{i=1}^nV_i`,true],['bigotimes',String.raw`\bigotimes_{k=1}^n W_k`,true],
 ['biguplus',String.raw`\biguplus_{i=1}^n S_i`,true],['bigsqcup',String.raw`\bigsqcup_{i\in I} X_i`,true],
 ['iint',String.raw`\iint_D xy\,dx\,dy`,true],['iiint',String.raw`\iiint_V f\,dV`,true],
 ['oint',String.raw`\oint_C z\,dz`,true],['oiint',String.raw`\oiint_S F\,dS`,true],
 ['limits',String.raw`\int\limits_0^1 f(x)\,dx`,true],['nolimits',String.raw`\sum\nolimits_{i=1}^n x_i`,true],
 ['lim',String.raw`\lim_{x\to0}\frac{\sin x}{x}=1`,true],['limsup',String.raw`\limsup_{n\to\infty}a_n`,true],
 ['liminf',String.raw`\liminf_{n\to\infty}b_n`,true],['sup',String.raw`\sup_{x\in S}f(x)`,true],
 ['inf',String.raw`\inf_{x>0}x^2`,true],['max',String.raw`\max_{1\le i\le n}a_i`,true],
 ['min',String.raw`\min_{x\in X}f(x)`,true],['det',String.raw`\det A=ad-bc`],
 ['trig',String.raw`\sin^2x+\cos^2x=1`],['log',String.raw`\log_a x=\frac{\ln x}{\ln a}`],
 ['exp',String.raw`\exp(x+y)=\exp x\exp y`],['arctan',String.raw`\arctan x+\arctan y`],
 ['hyperbolic',String.raw`\sinh x+\cosh x=e^x`],['operatorname',String.raw`\operatorname{rank}(A)`],
 ['operatorname-limits',String.raw`\operatorname*{argmax}_{x\in X}f(x)`,true],
 ['nested-sum',String.raw`\sum_{i=1}^n\sum_{j=1}^m a_{ij}`,true],
 ['script-operator',String.raw`x^{\sum_{i=1}^n i}`],['sum-frac',String.raw`\frac{\sum_{i=1}^n a_i}{\prod_{i=1}^n b_i}`,true],
 ['operator-empty-sup',String.raw`\sum_{}^n x`,true],['operator-group',String.raw`{\sum}_{i=1}^n x_i`,true],
]);
group('generalized-fractions',[
 ['binom',String.raw`\binom{n}{k}`],['dbinom',String.raw`\dbinom{n}{k}`,true],['tbinom',String.raw`\tbinom{n+1}{k-1}`],
 ['binom-script',String.raw`x^{\binom{n}{2}}`],['binom-nested',String.raw`\binom{\binom n2}{k}`,true],
 ['atop',String.raw`{a\atop b}`],['choose',String.raw`{n\choose k}`],['brace-frac',String.raw`{a\brace b}`],['brack-frac',String.raw`{a\brack b}`],
 ['genfrac-no-rule',String.raw`\genfrac{(}{)}{0pt}{}{a}{b}`],['genfrac-rule',String.raw`\genfrac{[}{]}{1pt}{0}{a+b}{c}`,true],
 ['genfrac-script',String.raw`\genfrac{}{}{0pt}{2}{a}{b}`],['genfrac-scriptscript',String.raw`\genfrac{}{}{}{3}{a}{b}`],
 ['cfrac',String.raw`\cfrac{1}{1+\cfrac1{2+x}}`,true],['over',String.raw`{a+b\over c+d}`],
 ['fraction-limits',String.raw`\frac{\lim_{x\to0}f(x)}{1+x}`,true],['root-index',String.raw`\sqrt[3]{x+y}`],
 ['root-index-fraction',String.raw`\sqrt[\frac12]{a+b}`],['root-empty-index',String.raw`\sqrt[]{x}`],
 ['root-nested-index',String.raw`\sqrt[3]{\sqrt[4]{x}}`],['root-script-index',String.raw`x_{\sqrt[n]{a}}`],
 ['fraction-empty-numer',String.raw`\frac{}{x}`],['fraction-empty-denom',String.raw`\frac{x}{}`],
 ['binom-empty',String.raw`\binom{}{}`],['fraction-display-in-script',String.raw`x^{\dfrac{a}{b}}`],
]);
group('delimiters',[
 ['delimiter-tall',String.raw`\left(\frac{a}{b}\right)`],['delimiter-bracket',String.raw`\left[\frac{1+x}{1-x}\right]`],
 ['delimiter-brace',String.raw`\left\{x^2\right\}`],['delimiter-angle',String.raw`\left\langle\frac{a}{b}\right\rangle`],
 ['delimiter-floor',String.raw`\left\lfloor\frac{x}{2}\right\rfloor`],['delimiter-ceil',String.raw`\left\lceil\frac{x}{2}\right\rceil`],
 ['delimiter-bars',String.raw`\left|\frac{x}{y}\right|`],['delimiter-norm',String.raw`\left\|\frac{x}{y}\right\|`],
 ['delimiter-invisible-left',String.raw`\left.\frac{d}{dx}f(x)\right|_{x=0}`],['delimiter-invisible-right',String.raw`\left\{x+y\right.`],
 ['delimiter-middle',String.raw`\left\{x\middle|x>0\right\}`],['delimiter-two-middle',String.raw`\left(a\middle|b\middle|c\right)`],
 ['delimiter-nested',String.raw`\left[1+\left(\frac ab\right)^2\right]`],['delimiter-scripts',String.raw`\left(\frac ab\right)_i^2`],
 ['delimiter-tall-root',String.raw`\left(\sqrt{\frac{1}{1+x^2}}\right)`,true],
 ['delimiter-tall-frac',String.raw`\left\{\frac{\frac ab}{\frac cd}\right\}`,true],
 ['big',String.raw`\bigl(x\bigr)`],['big-size-2',String.raw`\Bigl[x\Bigr]`],['bigg',String.raw`\biggl\{x\biggr\}`],
 ['big-size-4',String.raw`\Biggl\langle x\Biggr\rangle`],['big-middle',String.raw`\bigl(a\bigm|b\bigr)`],
 ['big-unclassified',String.raw`\big( x \big)`],['delim-script-style',String.raw`x_{\left(\frac ab\right)}`],
 ['delim-display-style',String.raw`\left(\dfrac{a+b}{c+d}\right)`,true],['delim-empty',String.raw`\left(\right)`],
]);
group('accents-arrows',[
 ['hat',String.raw`\hat x`],['bar',String.raw`\bar x`],['tilde',String.raw`\tilde a`],['vec',String.raw`\vec v`],
 ['dot',String.raw`\dot x`],['ddot',String.raw`\ddot y`],['acute',String.raw`\acute a`],['grave',String.raw`\grave a`],
 ['check',String.raw`\check x`],['breve',String.raw`\breve x`],['ring',String.raw`\mathring A`],
 ['widehat',String.raw`\widehat{ABC}`],['widetilde',String.raw`\widetilde{a+b+c}`],['widecheck',String.raw`\widecheck{xyz}`],
 ['overline',String.raw`\overline{a+b}`],['underline',String.raw`\underline{x+y}`],
 ['overrightarrow',String.raw`\overrightarrow{AB}`],['overleftarrow',String.raw`\overleftarrow{AB}`],
 ['overleftrightarrow',String.raw`\overleftrightarrow{AB}`],['underrightarrow',String.raw`\underrightarrow{AB}`],
 ['underleftarrow',String.raw`\underleftarrow{AB}`],['underleftrightarrow',String.raw`\underleftrightarrow{AB}`],
 ['overbrace',String.raw`\overbrace{a+b+c}^{n}`],['underbrace',String.raw`\underbrace{x+y+z}_{m}`],
 ['xrightarrow',String.raw`A\xrightarrow{f}B`],['xleftarrow',String.raw`A\xleftarrow[g]{f}B`],
 ['xleftrightarrow',String.raw`A\xleftrightarrow{f}B`],['x-double-rightarrow',String.raw`A\xRightarrow{f}B`],
 ['x-double-leftarrow',String.raw`A\xLeftarrow{f}B`],['x-double-leftrightarrow',String.raw`A\xLeftrightarrow{f}B`],
 ['accent-scripts',String.raw`\hat x_i^2`],['accent-group-scripts',String.raw`{\hat x}_i^2`],
 ['accent-nested',String.raw`\hat{\hat x}`],['accent-fraction',String.raw`\overline{\frac ab}`],
 ['accent-roman',String.raw`\mathrm{\hat x}`],['accent-textstyle',String.raw`x_{\vec a}`],
 ['arrow-fraction-label',String.raw`A\xrightarrow{\frac ab}B`],['overbrace-fraction',String.raw`\overbrace{\frac ab+c}^{x}`,true],
]);
group('environments',[
 ['matrix',String.raw`\begin{matrix}a&b\\c&d\end{matrix}`],['pmatrix',String.raw`\begin{pmatrix}a&b\\c&d\end{pmatrix}`],
 ['bmatrix',String.raw`\begin{bmatrix}1&0\\0&1\end{bmatrix}`],['brace-matrix',String.raw`\begin{Bmatrix}x&y\\z&w\end{Bmatrix}`],
 ['vmatrix',String.raw`\begin{vmatrix}a&b\\c&d\end{vmatrix}`],['double-bar-matrix',String.raw`\begin{Vmatrix}a&b\\c&d\end{Vmatrix}`],
 ['smallmatrix',String.raw`x_{\begin{smallmatrix}a&b\\c&d\end{smallmatrix}}`],
 ['matrix-single',String.raw`\begin{matrix}x\end{matrix}`],['matrix-empty-cell',String.raw`\begin{matrix}a&&b\\&c&\end{matrix}`],
 ['matrix-ragged',String.raw`\begin{matrix}a&b\\c\end{matrix}`],['matrix-trailing-row',String.raw`\begin{matrix}a&b\\c&d\\\end{matrix}`],
 ['matrix-fraction',String.raw`\begin{pmatrix}\frac ab&x^2\\\sqrt y&\frac cd\end{pmatrix}`,true],
 ['matrix-nested',String.raw`\begin{matrix}\begin{pmatrix}a&b\end{pmatrix}&c\\d&e\end{matrix}`],
 ['matrix-script',String.raw`A^{\begin{matrix}i&j\end{matrix}}`],['matrix-tall-delim',String.raw`\left\langle\begin{matrix}a\\b\\c\\d\\e\end{matrix}\right\rangle`,true],
 ['array-lcr',String.raw`\begin{array}{lcr}a&b&c\\xx&y&z\end{array}`],
 ['array-rules',String.raw`\begin{array}{|c|r|}\hline a&b\\\hline c&d\\\hline\end{array}`],
 ['array-double-rules',String.raw`\begin{array}{||c||c||}a&b\\c&d\end{array}`],
 ['array-row-gap',String.raw`\begin{array}{cc}a&b\\[4pt]c&d\end{array}`],
 ['array-script',String.raw`x_{\begin{array}{cc}a&b\\c&d\end{array}}`],
 ['cases',String.raw`f(x)=\begin{cases}x^2&x\ge0\\-x&x<0\end{cases}`,true],
 ['dcases',String.raw`\begin{dcases}\frac ab&x>0\\\frac cd&x<0\end{dcases}`,true],
 ['rcases',String.raw`\begin{rcases}x+y=1\\x-y=0\end{rcases}`],
 ['aligned',String.raw`\begin{aligned}a&=b+c\\d&=e-f\end{aligned}`,true],
 ['aligned-pairs',String.raw`\begin{aligned}a&=b&c&=d\\x&=y&z&=w\end{aligned}`,true],
 ['aligned-frac',String.raw`\begin{aligned}x&=\frac ab\\y&=\sqrt x\end{aligned}`,true],
 ['gathered',String.raw`\begin{gathered}a+b=c\\x+y=z\end{gathered}`,true],
 ['alignedat',String.raw`\begin{alignedat}{2}a&=b&c&=d\\x&=y&z&=w\end{alignedat}`,true],
 ['substack',String.raw`\sum_{\substack{0<i<n\\j>0}}a_{ij}`,true],
 ['array-hdashline',String.raw`\begin{array}{cc}a&b\\\hdashline c&d\end{array}`],
]);
group('fonts-text-macros',[
 ['text-fonts',String.raw`\text{speed }v=\textbf{distance}/\textit{time}`],
 ['caligraphic',String.raw`\mathcal{ABCXYZ}`],['fraktur',String.raw`\mathfrak{Abc123}`],['script',String.raw`\mathscr{ABCXYZ}`],
 ['blackboard',String.raw`\mathbb{RNCZQ}`],['boldsymbol',String.raw`\boldsymbol{\alpha+\beta}`],
 ['bold-italic',String.raw`\boldsymbol{Abx}`],['sans-bold',String.raw`\mathsf{\mathbf{Abx}}`],['sans-italic',String.raw`\mathsfit{Abx}`],
 ['text-nested',String.raw`\text{some \textbf{bold} text}`],['text-punctuation',String.raw`\text{if }x>0\text{, then }y=1`],
 ['text-spaces',String.raw`\text{ a  b }`],['font-scripts',String.raw`\mathbf{x_i^2}+\mathsf{A_j}`],
 ['font-frac',String.raw`\mathit{\frac{ABC}{xyz}}`],['macro-pair',String.raw`\def\pair#1#2{\left(#1,#2\right)}\pair{x}{y}`],
 ['macro-matrix',String.raw`\def\row#1#2{#1&#2}\begin{matrix}\row ab\\\row cd\end{matrix}`],
 ['macro-local-nested',String.raw`\def\a{x}{\def\a{y}{\def\a{z}\a}\a}\a`],
 ['macro-global',String.raw`\def\a{x}{\gdef\a{y}}\a`],
 ['newcommand-args',String.raw`\newcommand{\pair}[2]{(#1,#2)}\pair ab`],
 ['renewcommand',String.raw`\newcommand{\foo}{x}\renewcommand{\foo}{y}\foo`],
 ['providecommand',String.raw`\providecommand{\foo}{x}\providecommand{\foo}{y}\foo`],
 ['macro-space',String.raw`\def\sq#1{#1^2}\sq{x+y}`],['macro-binom',String.raw`\def\C#1#2{\binom{#1}{#2}}\C nk`],
 ['style-limits',String.raw`\textstyle\sum_{i=1}^n i+{\displaystyle\sum_{i=1}^n i}`],
 ['style-matrix',String.raw`\scriptstyle\begin{pmatrix}a&b\\c&d\end{pmatrix}`],
]);
// Keep errors in the mathematical scope; never relabel missing commands as extensions.
for(const [id,latex] of [
 ['left-unclosed',String.raw`\left(x`],['right-unmatched',String.raw`x\right)`],['bad-delimiter',String.raw`\left x\right)`],
 ['middle-unmatched',String.raw`x\middle|y`],['matrix-unclosed',String.raw`\begin{matrix}a&b`],['matrix-mismatch',String.raw`\begin{matrix}x\end{array}`],
 ['environment-unknown',String.raw`\begin{unknown}x\end{unknown}`],['array-bad-column',String.raw`\begin{array}{q}x\end{array}`],
 ['array-no-preamble',String.raw`\begin{array}x\end{array}`],['missing-binom',String.raw`\binom{x}`],
 ['missing-accent',String.raw`\hat`],['root-unclosed-index',String.raw`\sqrt[3{x}`],['operatorname-missing',String.raw`\operatorname`],
 ['invalid-limits',String.raw`x\limits_1`],['double-infix',String.raw`{a\over b\over c}`],
 ['missing-genfrac-denom',String.raw`\genfrac{}{}{}{}{a}`],['invalid-genfrac-unit',String.raw`\genfrac{}{}{2zz}{}{a}{b}`],
 ['macro-missing-param',String.raw`\def\f#1{#1}\f`],['matrix-bad-end',String.raw`\begin{pmatrix}x\end{bmatrix}`],
 ])cases.push({id:'p2-error-'+id,category:'errors-p2',latex,displayMode:false,expectation:'invalid',provenance:'Invalid mathematical input, exact upstream rejects'});

const ids=new Set();for(const f of cases){if(ids.has(f.id.toLowerCase()))throw new Error('Case-insensitive duplicate '+f.id);ids.add(f.id.toLowerCase());let error;try{katex.renderToString(f.latex,{displayMode:!!f.displayMode,macros:{...f.macros},throwOnError:true,strict:'ignore',trust:false,maxExpand:1000});}catch(e){error=e;}
 if((f.expectation==='valid')===!!error)throw new Error(`${f.id}: expected ${f.expectation}: ${error??'unexpected acceptance'}`);
}
const valid=cases.filter(f=>f.expectation==='valid');if(valid.length<300)throw new Error(`Only ${valid.length} valid fixtures`);
const corpus={schemaVersion:1,phase:'P2',reference:lock.source,policy:'Frozen inputs checked for validity by exact upstream; this file is not evidence of native compatibility. P1 regression, the unchanged P0 normal corpus and common-rule interaction cases are reported separately.',counts:{valid:valid.length,invalid:cases.length-valid.length,byCategory:Object.fromEntries([...new Set(valid.map(f=>f.category))].sort().map(c=>[c,valid.filter(f=>f.category===c).length]))},cases};
const target=path.join(root,'tests/fixtures/p2.json');
if(process.argv.includes('--check')){const actual=JSON.parse(await fs.readFile(target,'utf8'));if(JSON.stringify(actual)!==JSON.stringify(corpus))throw new Error('P2 corpus drift');}else await writeJson(target,corpus);
console.log(JSON.stringify({...corpus.counts,sha256:sha256(await fs.readFile(target))}));
