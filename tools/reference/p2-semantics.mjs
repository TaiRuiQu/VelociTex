// Common semantic projection only. No geometry or C# rule table is used for the
// upstream side. Redundant implementation wrappers are normalized explicitly;
// grouping, script attachment, table cells and construction arguments survive.
export function commonSemantics(source) {
  const fonts={mathrm:'MathRoman',mathbf:'MathBold',mathit:'MathItalic',mathnormal:'MathNormal',boldsymbol:'MathBoldItalic',mathsf:'MathSans',mathsfit:'MathSansItalic',mathtt:'MathTypewriter',mathcal:'Caligraphic',mathfrak:'Fraktur',mathscr:'Script',mathbb:'AMS'};
  const round=x=>Math.round(x*1e9)/1e9;
  const dim=d=>!d?0:round(d.number*({mu:1/18,em:1,ex:.431,pt:.1,bp:.100375,pc:1.2,in:7.227,cm:7.227/2.54,mm:7.227/25.4,sp:.1/65536}[d.unit]??NaN));
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
        case 'mathord':case 'textord':case 'atom':return{kind:'symbol',text:source.symbols[n.mode][n.text]?.replace??n.text,atom:n.family??'ord',textMode:n.mode==='text'};
        case 'spacing':return{kind:'symbol',text:' ',atom:'ord',textMode:true};
        case 'ordgroup':return{kind:'group',children:n.body.map(node)};
        case 'supsub':return{kind:'script',base:node(n.base),sup:arg(n.sup),sub:arg(n.sub)};
        case 'genfrac':return{kind:'fraction',numer:arg(n.numer),denom:arg(n.denom),hasBar:n.hasBarLine,left:delim(n.leftDelim),right:delim(n.rightDelim),bar:dim(n.barSize),continued:!!n.continued};
        case 'sqrt':return{kind:'radical',body:arg(n.body),index:arg(n.index)};
        case 'font':return{kind:'font',font:fonts[n.font]??n.font,body:arg(n.body)};
        case 'text':return{kind:'text',body:arg(n.body)};
        case 'styling':return{kind:'style',style:n.style.toLowerCase(),body:arg(n.body)};
        case 'kern':return{kind:'space',value:dim(n.dimension)};
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
          if(n.mclass==='mord'&&n.body.length===1&&n.body[0].type==='font')return node(n.body[0]);
          throw new Error('Unprojected atom class '+n.mclass);
        }
        case 'array':return{kind:'matrix',columns:n.cols?.map(c=>c.type==='separator'?c.separator:c.align).join('')??'c',rows:n.body.map((r,i)=>({cells:r.map(cell=>{
          // Each upstream array cell has a reset-font style and an argument
          // ordgroup. That style is retained, with the argument wrapper removed.
          if(cell.type==='styling')return[{kind:'style',style:cell.style,body:arg(cell.body.length===1?cell.body[0]:cell.body)}];return arg(cell);
        }),ruleAbove:(n.hLinesBeforeRow?.[i]??[]).reduce((v,d)=>v+(d?-1:1),0),gap:dim(n.rowGaps?.[i])})),ruleBelow:(n.hLinesBeforeRow?.[n.body.length]??[]).reduce((v,d)=>v+(d?-1:1),0)};
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
        case 'Row':return{kind:'group',children:chain(n.first)};
        case 'Symbol':return{kind:'symbol',text:String.fromCodePoint(n.codePoint),atom:n.atom.toLowerCase(),textMode:n.textMode};
        case 'Script':return{kind:'script',base:node(n.first),sup:list(n.second),sub:list(n.third)};
        case 'Fraction':return{kind:'fraction',numer:list(n.first),denom:list(n.second),hasBar:n.hasBar,left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',bar:round(n.value),continued:n.text==='\\cfrac'};
        case 'Radical':return{kind:'radical',body:list(n.first),index:list(n.second)};
        case 'Font':return{kind:'font',font:n.font,body:list(n.first)};
        case 'Text':return{kind:'text',body:list(n.first)};
        case 'Style':return{kind:'style',style:n.style.toLowerCase(),body:list(n.first)};
        case 'Space':return{kind:'space',value:round(n.value)};
        case 'Operator':{
          const expandedName=n.text==='\\limsup'||n.text==='\\liminf';
          return{kind:'operator',name:expandedName?'\\operatorname':n.text,body:n.text==='\\mathop'||n.text==='\\operatorname'||expandedName?list(n.first):null,limits:n.neverLimits?false:n.alwaysLimits||n.limits,always:expandedName?false:n.alwaysLimits||n.neverLimits,never:n.neverLimits};
        }
        case 'Delimiter':return{kind:'delimiter',delimiter:n.codePoint?String.fromCodePoint(n.codePoint):'',size:n.value,atom:n.atom.toLowerCase()};
        case 'Delimited':return{kind:'delimited',left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',body:list(n.first)};
        case 'Accent':return{kind:'accent',name:n.text,body:list(n.first),below:list(n.second)};
        case 'Matrix':{
          const rows=[];for(let r=n.first;r>=0;r=ns[r].next){const cells=[];for(let c=ns[r].first;c>=0;c=ns[c].next)cells.push(stripCell(c));rows.push({cells,ruleAbove:ns[r].ruleAbove,gap:round(ns[r].value)});}
          let columns=n.alignment;
          if(n.text==='aligned')columns='rl'.repeat(Math.ceil(n.columnCount/2));
          if(n.text==='alignedat')columns='rl'.repeat(Math.ceil(n.columnCount/2));
          if(n.text==='cases'||n.text==='dcases'||n.text==='rcases')columns='ll';
          if(['matrix','pmatrix','bmatrix','Bmatrix','vmatrix','Vmatrix'].includes(n.text))columns=columns.repeat(n.columnCount);
          const table={kind:'matrix',columns,rows,ruleBelow:n.ruleBelow};
          return n.leftDelimiter||n.rightDelimiter?{kind:'delimited',left:n.leftDelimiter?String.fromCodePoint(n.leftDelimiter):'',right:n.rightDelimiter?String.fromCodePoint(n.rightDelimiter):'',body:[table]}:table;
        }
        default:throw new Error('Unprojected common node '+n.kind);
      }
    }
    return compact(chain(ns[doc.root].first));
  }
  return{upstream,native};
}
