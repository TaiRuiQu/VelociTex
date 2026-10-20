"""Emit C++ constant tables from the locked, already verified formula data."""
from pathlib import Path
import argparse, re

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'src/VelociTex.Core/Generated'
OUT=ROOT/'native/core/generated'

def source(name): return (SOURCE/name).read_text(encoding='utf-8-sig')
def split_args(text):
    result=[]; start=0; depth=0; quoted=False; escaped=False
    for i,c in enumerate(text):
        if quoted:
            if escaped: escaped=False
            elif c=='\\': escaped=True
            elif c=='"': quoted=False
        elif c=='"': quoted=True
        elif c in '([{': depth+=1
        elif c in ')]}': depth-=1
        elif c==',' and depth==0: result.append(text[start:i].strip()); start=i+1
    if text[start:].strip(): result.append(text[start:].strip())
    return result

def convert(text):
    text=text.strip()
    if text.startswith('"'): return 'u'+text
    if text.startswith('new('): return '{'+','.join(convert(x) for x in split_args(text[4:-1]))+'}'
    text=re.sub(r'(?<=[0-9.])[dD]\b','',text)
    return text.replace('MathAtom.','MathAtom::').replace('MathFont.','MathFont::')

def array(text,name):
    match=re.search(r'\[\]\s+'+name+r'\s*=\s*\[(.*?)\];',text,re.S)
    if not match: raise ValueError(name)
    body=re.sub(r'//[^\n]*','',match[1])
    return split_args(body)

def generate():
    header=['// Generated from locked KaTeX 0.19.0 data. Do not edit.','#pragma once','#include "../types.hpp"','#include <array>','namespace velocitex {',
      'struct FontDescriptor {std::u16string_view Name,ResourceFileName; int UnitsPerEm,GlyphStart,GlyphCount,MetricStart,MetricCount;};',
      'struct FontGlyph {int CodePoint,GlyphId,AdvanceUnits,LeftSideBearingUnits;};',
      'struct TexGlyphMetric {int CodePoint{}; double Depth{},Height{},ItalicCorrection{},Skew{},Width{};};',
      'struct GlyphInk {int Left,Bottom,Right,Top;};',
      'struct SymbolDefinition {std::u16string_view Name;int CodePoint;MathAtom Atom;MathFont Font;bool IsMathOrd,TextMode;std::u16string_view Group;};',
      'struct MacroString {std::u16string_view Name,Body;};',
      'struct CommandDefinition {std::u16string_view Name;bool AllowedInText,AllowedInMath,AllowedInArgument,Primitive;};',
      'struct FallbackMetric {int Font,CodePoint;bool TextMode;TexGlyphMetric Metric;};']
    cpp=['// Generated from locked KaTeX 0.19.0 data. Do not edit.','#include "data.hpp"','#include <tuple>','namespace velocitex {']
    def table(owner,typename,name,rows,values=None):
        cpp.append(f'static const std::array<{typename},{len(rows)}> {owner}_{name}{{{{')
        for row in rows:
            if values: value=values(row)
            elif row.startswith('new('): value='{'+','.join(convert(x) for x in split_args(row[4:-1]))+'}'
            elif row.startswith('('): value='{'+','.join(convert(x) for x in split_args(row[1:-1]))+'}'
            else: value=convert(row)
            cpp.append(value+',')
        cpp.extend(['}};',f'const std::span<const {typename}> {owner}::{name}{{{owner}_{name}}};'])
    for owner,filename,tables in [
        ('FontData','FontData.g.cs',[('FontDescriptor','Fonts'),('FontGlyph','Glyphs'),('TexGlyphMetric','TexMetrics')]),
        ('GlyphInkData','GlyphInkData.g.cs',[('GlyphInk','Bounds'),('double','FontDescents')]),
        ('SymbolData','SymbolData.g.cs',[('SymbolDefinition','Symbols'),('MacroString','Macros'),('std::u16string_view','KnownCommands')]),
        ('CommandData','CommandData.g.cs',[('CommandDefinition','Definitions')])]:
        header.append(f'struct {owner} {{')
        for typename,name in tables:
            header.append(f'static const std::span<const {typename}> {name};')
            table(owner,typename,name,array(source(filename),name))
        header.append('};')
    fallback=re.findall(r'new\((\d+), (\d+), (true|false), new\(([^)]+)\)\)',source('FallbackMetricData.g.cs'))
    header.append('struct FallbackMetricData {static bool Find(int font,int cp,bool text,TexGlyphMetric& metric);};')
    rows=[f'{{{font},{cp},{text},{{{metric}}}}}' for font,cp,text,metric in fallback]
    cpp.extend([f'static const std::array<FallbackMetric,{len(rows)}> FallbackEntries{{{{',*[r+',' for r in rows],'}};',
    '''bool FallbackMetricData::Find(int font,int cp,bool text,TexGlyphMetric& metric) {
        const auto key=std::tuple(font,cp,text);
        auto it=std::lower_bound(FallbackEntries.begin(),FallbackEntries.end(),key,[](const auto& entry,const auto& value){return std::tuple(entry.Font,entry.CodePoint,entry.TextMode)<value;});
        if(it!=FallbackEntries.end()&&std::tuple(it->Font,it->CodePoint,it->TextMode)==key){metric=it->Metric;return true;} metric={};return false;
    }'''])
    header.append('struct MathConstants {')
    for name,a,b,c in re.findall(r'double (\w+)\(int index\) => index switch \{ 0 => ([^,]+), 1 => ([^,]+), _ => ([^}]+) \};',source('MathConstants.g.cs')):
        header.append(f'static double {name}(int index) {{return index==0?{convert(a)}:index==1?{convert(b)}:{convert(c)};}}')
    header.append('};')
    header.append('struct UnicodeData {static std::optional<std::u16string> Decompose(int scalar);static std::optional<std::u16string> Accent(int scalar,bool text);static std::optional<std::u16string> Script(int scalar,bool& subscript);};')
    uni=source('UnicodeData.g.cs')
    for name in ['Decompose','Accent','Script']:
        part=uni.split(f' {name}(')[1]
        if name!='Script': body=part.split('{',1)[1].split('};',1)[0]
        else: body=part.split('return scalar switch',1)[1].split('{',1)[1].split('};',1)[0]
        extra=',bool text' if name=='Accent' else ',bool& subscript' if name=='Script' else ''
        cpp.append(f'std::optional<std::u16string> UnicodeData::{name}(int scalar{extra}) {{')
        if name=='Script':
            subs=part.split('subscript = scalar is ',1)[1].split(';',1)[0].split(' or ')
            cpp.append('subscript='+('||'.join('scalar=='+x.strip() for x in subs))+';')
        cpp.append('switch(scalar) {')
        for line in body.splitlines():
            m=re.match(r'\s*(\d+) => (.*),\s*$',line)
            if not m: continue
            cp,val=m.groups()
            val=re.sub(r'"(?:\\.|[^"\\])*"',lambda m:'u'+m[0],val)
            cpp.append(f'case {cp}:return std::u16string({val});')
        cpp.extend(['default:return std::nullopt;','}}'])
    header.append('namespace generated {bool has_main_metric(int cp);}')
    cpp.append('''namespace generated {bool has_main_metric(int cp) {
        const auto& font=FontData::Fonts[8];
        for(int i=font.MetricStart;i<font.MetricStart+font.MetricCount;i++)if(FontData::TexMetrics[i].CodePoint==cp)return true;
        TexGlyphMetric metric;return FallbackMetricData::Find(8,cp,false,metric);
    }}''')
    header.append('}')
    cpp.append('}')
    return {'data.hpp':'\n'.join(header)+'\n','data.cpp':'\n'.join(cpp)+'\n'}

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    OUT.mkdir(parents=True,exist_ok=True)
    for name,content in generate().items():
        path=OUT/name
        if args.check:
            if not path.exists() or path.read_text(encoding='utf-8')!=content:raise SystemExit('Native table drift: '+name)
        else:path.write_text(content,encoding='utf-8',newline='\n')
    print('Native locked tables '+('checked.' if args.check else 'generated.'))
