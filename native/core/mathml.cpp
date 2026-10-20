#include "mathml.hpp"
#include "layout.hpp"
#include "generated/data.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace velocitex {
namespace {
using Opt = std::optional<std::string>;
std::string scalar(int cp) {
    std::string s;
    if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw std::invalid_argument("Invalid Unicode scalar.");
    if (cp < 0x80) s += char(cp);
    else if (cp < 0x800) { s += char(0xc0 | (cp >> 6)); s += char(0x80 | (cp & 63)); }
    else if (cp < 0x10000) { s += char(0xe0 | (cp >> 12)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63)); }
    else { s += char(0xf0 | (cp >> 18)); s += char(0x80 | ((cp >> 12) & 63)); s += char(0x80 | ((cp >> 6) & 63)); s += char(0x80 | (cp & 63)); }
    return s;
}
std::string mml_utf8(std::u16string_view source) {
    std::string s; s.reserve(source.size());
    for (size_t i = 0; i < source.size(); ++i) {
        int cp = source[i];
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < source.size() && source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (source[++i] - 0xdc00);
        else if (cp >= 0xd800 && cp <= 0xdfff) cp = 0xfffd;
        s += scalar(cp);
    }
    return s;
}
Opt mml_text(const std::optional<std::u16string>& s) { return s ? Opt(mml_utf8(*s)) : std::nullopt; }
bool eq(const std::optional<std::u16string>& s, std::u16string_view expected) { return s && *s == expected; }
bool oneof(std::string_view value, std::initializer_list<std::string_view> values) { return std::find(values.begin(), values.end(), value) != values.end(); }
bool oneof(const std::optional<std::u16string>& value, std::initializer_list<std::u16string_view> values) { return value && std::find(values.begin(), values.end(), *value) != values.end(); }
std::string number(double value, int decimals = 4) {
    // .NET custom numeric formats first retain 15 significant decimal digits,
    // then round the requested decimal positions away from zero.
    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), std::abs(value), std::chars_format::scientific, 14);
    if (result.ec != std::errc()) throw std::runtime_error("Unable to format MathML dimension.");
    std::string raw(buffer, result.ptr); auto e = raw.find('e');
    if(e==std::string::npos)return value<0?"-"+raw:raw;
    int exponent=std::stoi(raw.substr(e+1));std::string digits;
    for(size_t i=0;i<e;++i)if(raw[i]!='.')digits+=raw[i];
    int point=exponent+1, keep=point+decimals;
    if(keep<0)digits="0";
    else if(keep<int(digits.size())){
        bool up=digits[size_t(keep)]>='5';digits.resize(size_t(keep));
        if(up){int i=int(digits.size())-1;while(i>=0&&digits[size_t(i)]=='9')digits[size_t(i--)]='0';if(i>=0)++digits[size_t(i)];else {digits.insert(digits.begin(),'1');++point;}}
        if(digits.empty())digits="0";
    }
    std::string s;
    if(point<=0)s="0."+std::string(size_t(-point),'0')+digits;
    else {if(int(digits.size())<point)digits.append(size_t(point-int(digits.size())),'0');s=digits.substr(0,size_t(point));if(int(digits.size())>point)s+='.'+digits.substr(size_t(point));}
    if(auto p=s.find('.');p!=std::string::npos){while(s.back()=='0')s.pop_back();if(s.back()=='.')s.pop_back();}
    if(std::signbit(value))s.insert(s.begin(),'-');
    return s;
}
std::string em(double value) { return number(value) + "em"; }
void escaped(std::string& output, std::string_view s, bool attribute = false) {
    for (char c : s) switch (c) {
    case '&': output += "&amp;"; break; case '<': output += "&lt;"; break; case '>': output += "&gt;"; break;
    case '"': output += attribute ? "&quot;" : "\""; break;
    case '\r': output += "&#xD;"; break; case '\n': output += "&#xA;"; break; case '\t': output += "&#x9;"; break;
    default: output += c; break;
    }
}
void attr(std::string& output, std::string_view name, std::string_view value) { output += ' '; output += name; output += "=\""; escaped(output, value, true); output += '"'; }
int fence_cp(int cp) { return cp == '|' ? 0x2223 : cp == 0x2016 ? 0x2225 : cp; }
int accent_cp(std::string_view label, int fallback = 0) {
    if (oneof(label,{"\\overline","\\underline"})) return 0x203e;
    if (label == "\\widehat") return '^'; if (label == "\\widecheck") return 0x2c7;
    if (oneof(label,{"\\widetilde","\\utilde","\\tilde"})) return '~'; if (label == "\\^") return 0x2c6;
    if (oneof(label,{"\\overrightarrow","\\underrightarrow","\\xrightarrow","\\\\cdrightarrow"})) return 0x2192;
    if (oneof(label,{"\\overleftarrow","\\underleftarrow","\\xleftarrow","\\\\cdleftarrow"})) return 0x2190;
    if (oneof(label,{"\\overleftrightarrow","\\underleftrightarrow","\\xleftrightarrow"})) return 0x2194;
    if (oneof(label,{"\\Overrightarrow","\\xRightarrow"})) return 0x21d2;
    if (label == "\\xLeftarrow") return 0x21d0; if (label == "\\xLeftrightarrow") return 0x21d4;
    if (oneof(label,{"\\overleftharpoon","\\xleftharpoonup"})) return 0x21bc;
    if (oneof(label,{"\\overrightharpoon","\\xrightharpoonup"})) return 0x21c0;
    if (label == "\\xleftharpoondown") return 0x21bd; if (label == "\\xrightharpoondown") return 0x21c1;
    if (oneof(label,{"\\xrightleftharpoons","\\xrightequilibrium"})) return 0x21cc;
    if (oneof(label,{"\\xleftrightharpoons","\\xleftequilibrium"})) return 0x21cb;
    if (oneof(label,{"\\\\cdlongequal","\\xlongequal"})) return '=';
    if (label == "\\xhookleftarrow") return 0x21a9; if (label == "\\xhookrightarrow") return 0x21aa;
    if (label == "\\xmapsto") return 0x21a6; if (label == "\\xtwoheadleftarrow") return 0x219e;
    if (label == "\\xtwoheadrightarrow") return 0x21a0;
    if (oneof(label,{"\\xtofrom","\\xrightleftarrows"})) return 0x21c4;
    if (label == "\\overbrace") return 0x23de; if (label == "\\underbrace") return 0x23df;
    if (label == "\\overbracket") return 0x23b4; if (label == "\\underbracket") return 0x23b5;
    if (label == "\\overgroup") return 0x23e0; if (label == "\\undergroup") return 0x23e1;
    if (oneof(label,{"\\overlinesegment","\\underlinesegment"})) return 0x23af;
    return fallback == 0 ? '^' : fallback;
}
Opt variant(const SemanticNode& n) {
    if (n.OriginalCodePoint > 0) return n.OriginalCodePoint >= 0x1d7ce || (n.IsMathOrd && !n.TextMode) || n.TextMode ? Opt{} : Opt("normal");
    std::string value;
    switch (n.Font) {
    case MathFont::MathRoman: value = generated::has_main_metric(n.CodePoint) ? "normal" : n.IsMathOrd && !n.TextMode ? "italic" : "normal"; break;
    case MathFont::MathItalic: value = "italic"; break;
    case MathFont::MathBold: value = "bold"; break;
    case MathFont::MathBoldItalic: value = n.IsMathOrd ? "bold-italic" : "bold"; break;
    case MathFont::MainBoldItalic: value = "bold-italic"; break;
    case MathFont::MathSansBoldItalic: value = "sans-serif-bold-italic"; break;
    case MathFont::MathSans: value = "sans-serif"; break;
    case MathFont::MathSansBold: value = "bold-sans-serif"; break;
    case MathFont::MathSansItalic: value = "sans-serif-italic"; break;
    case MathFont::MathTypewriter: value = "monospace"; break;
    case MathFont::AMS: value = n.SymbolFont == MathFont::AMS ? n.IsMathOrd && !n.TextMode ? "italic" : "normal" : "double-struck"; break;
    case MathFont::Caligraphic: case MathFont::Script: value = "script"; break;
    case MathFont::Fraktur: value = "fraktur"; break;
    default: value = n.IsMathOrd ? "italic" : "normal"; break;
    }
    const std::string_view default_value = !n.IsMathOrd && (n.TextMode || (n.CodePoint >= '0' && n.CodePoint <= '9')) ? "normal" : "italic";
    if (n.Atom != MathAtom::Ord) return n.Atom == MathAtom::Bin && n.Font == MathFont::MathBoldItalic ? Opt("bold-italic") : Opt{};
    return value == default_value ? Opt{} : Opt(value);
}
struct MmlNode {
    std::string Tag;
    Opt Text, Variant, Width, LineThickness, Fence, Stretchy, MinSize, MaxSize, LSpace, RSpace, Accent, AccentUnder;
    Opt RowSpacing, ColumnSpacing, ColumnAlign, ColumnLines, RowLines, Notation;
    Opt MathColor, MathBackground, MathSize, Height, Depth, VOffset, CssStyle, ClassName, LineBreak, Buffer;
    int First = -1, Next = -1;
    bool Separator = false, NoStretch = false, SuppressSpacing = false, LargeOpFalse = false, ScriptLevelOnly = false;
    MathStyle Style{};
    explicit MmlNode(std::string tag, Opt value = {}, Opt variant = {}) : Tag(std::move(tag)), Text(std::move(value)), Variant(std::move(variant)) {}
};
std::string join(const std::vector<std::string>& parts) { std::string s; for (const auto& p : parts) { if (!s.empty()) s += ' '; s += p; } return s; }
class Writer {
    const MathOptions& options;
    std::span<const SemanticNode> semantic;
    std::vector<MmlNode> nodes;
    std::vector<int> mapped, parents, sizes;
    std::vector<MathStyle> styles;
    std::vector<Opt> colors;
    int add(MmlNode n) { nodes.push_back(std::move(n)); return int(nodes.size()) - 1; }
    MmlNode& ref(int id) { return nodes.at(id); }
    int unwrap(int id) { return id >= 0 && ref(id).Tag == "mrow" ? ref(id).First : id; }
    bool is_number(int id) { return ref(id).Tag == "mn" || (ref(id).Tag == "mi" && ref(id).Text == Opt(".")); }
    bool merge(int left, int right) {
        auto& a = ref(left); auto& b = ref(right);
        if ((a.Tag == "mtext" && b.Tag == "mtext" && a.Variant == b.Variant) || (a.Tag == "mn" && is_number(right)) || (a.Tag == "mi" && a.Text == Opt(".") && b.Tag == "mn")) {
            if (!a.Buffer) a.Buffer = a.Text.value_or(""); *a.Buffer += b.Buffer ? *b.Buffer : b.Text.value_or(""); a.Text.reset(); if (b.Tag == "mn") a.Tag = "mn"; return true;
        } return false;
    }
    int link(int first) {
        int result = -1, last = -1;
        for (int child = first; child >= 0; child = semantic[child].NextSibling) {
            int current = mapped[child]; if (current < 0) throw std::logic_error("An unvisited semantic child reached MathML linking.");
            if (last >= 0 && merge(last,current)) continue;
            if (result < 0) result = current; else ref(last).Next = current; last = current;
        } return result;
    }
    int fences(int content, int left, int right, bool unwrapping = false) {
        if ((left == 0 || left == '.') && (right == 0 || right == '.')) return content;
        int first = unwrapping ? unwrap(content) : content, last = first;
        if (last >= 0) while (ref(last).Next >= 0) last = ref(last).Next;
        if (left != 0 && left != '.') { MmlNode n("mo",scalar(fence_cp(left))); n.Fence="true"; int fence=add(std::move(n)); ref(fence).Next=first; first=fence; if(last<0)last=fence; }
        if (right != 0 && right != '.') { MmlNode n("mo",scalar(fence_cp(right))); n.Fence="true"; int fence=add(std::move(n)); if(last>=0)ref(last).Next=fence; else first=fence; }
        MmlNode row("mrow"); row.First=first; return add(std::move(row));
    }
    std::string content(int id) {
        std::string result; std::vector<int> stack{id};
        while(!stack.empty()) { int current=stack.back(); stack.pop_back(); auto& item=ref(current); if(item.Next>=0)stack.push_back(item.Next); result += item.Buffer ? *item.Buffer : item.Text.value_or(""); if(item.First>=0)stack.push_back(item.First); }
        return result;
    }
    bool plain_name(int first) {
        if(first<0)return true; if(semantic[first].Kind==NodeKind::Row)return plain_name(semantic[first].FirstChild);
        for(int child=first;child>=0;child=semantic[child].NextSibling)if(semantic[child].Kind!=NodeKind::Symbol&&semantic[child].Kind!=NodeKind::Space)return false; return true;
    }
    double length(TexLength value,int index) { return mathml_length(value,styles[index],uint8_t(sizes[index]),options); }
    int space(double width) {
        Opt value;
        if(width>=.05555&&width<=.05556)value="\u200a"; else if(width>=.1666&&width<=.1667)value="\u2009";
        else if(width>=.2222&&width<=.2223)value="\u2005"; else if(width>=.2777&&width<=.2778)value="\u2005\u200a";
        else if(width>=-.05556&&width<=-.05555)value="\u200a\u2063"; else if(width>=-.1667&&width<=-.1666)value="\u2009\u2063";
        else if(width>=-.2223&&width<=-.2222)value="\u205f\u2063"; else if(width>=-.2778&&width<=-.2777)value="\u2005\u2063";
        MmlNode n(value?"#text":"mspace",value); if(!value)n.Width=em(width); return add(std::move(n));
    }
    int box(const SemanticNode& b,int) {
        int body=b.FirstChild>=0?mapped[b.FirstChild]:add(MmlNode("mrow")); std::string label=mml_text(b.Text).value_or(""); MmlNode n("mpadded"); n.First=body;
        if(label=="\\phantom") { n.Tag="mphantom";n.First=link(b.FirstChild); }
        else if(label=="\\vphantom") { MmlNode p("mphantom");p.First=unwrap(body);n.First=add(std::move(p));n.Width="0px"; }
        else if(label=="\\smash") { auto selection=mml_text(b.Alignment).value_or("tb");bool valid=selection.find_first_not_of("tb")==std::string::npos;if(valid&&selection.find('t')!=std::string::npos)n.Height="0px";if(valid&&selection.find('b')!=std::string::npos)n.Depth="0px"; }
        else if(oneof(label,{"\\mathllap","\\mathclap","\\mathrlap"})) {n.Width="0px";if(label=="\\mathllap")n.LSpace="-1width";else if(label=="\\mathclap")n.LSpace="-0.5width";}
        else if(label=="\\raisebox") { static constexpr const char* units[]={"em","ex","mu","pt","bp","pc","in","cm","mm","dd","cc","nd","nc","sp","px"}; n.VOffset=number(b.Length1.Number,16)+units[int(b.Length1.Unit)]; }
        else if(label=="\\vcenter") { n.ClassName="vcenter";int padded=add(std::move(n));MmlNode row("mrow");row.First=padded;return add(std::move(row)); }
        else if(label=="\\hbox") {n.Tag="mrow";n.First=link(b.FirstChild);}
        else if(label=="\\pmb") {n.Tag="mstyle";n.First=unwrap(link(b.FirstChild));n.CssStyle="text-shadow: 0.02em 0.01em 0.04px";}
        else if(label=="cdparent")n.Tag="mrow";
        else if(label=="cdleft"||label=="cdright") {MmlNode row("mrow");row.First=body;n.First=add(std::move(row));n.Width="0";if(label=="cdleft")n.LSpace="-1width";n.VOffset="0.7em";int pad=add(std::move(n));MmlNode style("mstyle");style.First=pad;style.Style=MathStyle::Script;return add(std::move(style));}
        else return body;
        return add(std::move(n));
    }
    int enclose(const SemanticNode& e) {
        auto label=mml_text(e.Text).value_or("");bool color_box=label.find("colorbox")!=std::string::npos;MmlNode n(color_box?"mpadded":"menclose");n.First=mapped[e.FirstChild];n.MathBackground=mml_text(e.Text2);
        if(label=="\\cancel")n.Notation="updiagonalstrike";else if(label=="\\bcancel")n.Notation="downdiagonalstrike";else if(label=="\\xcancel")n.Notation="updiagonalstrike downdiagonalstrike";
        else if(label=="\\phase")n.Notation="phasorangle";else if(label=="\\sout")n.Notation="horizontalstrike";else if(label=="\\fbox")n.Notation="box";else if(label=="\\angl")n.Notation="actuarial";
        if(color_box){n.Width="+6pt";n.Height="+6pt";n.LSpace="3pt";n.VOffset="3pt";if(label=="\\fcolorbox")n.CssStyle="border: "+em(std::max(.04,options.MinRuleThickness))+" solid "+mml_text(e.Text3).value_or("");}
        return add(std::move(n));
    }
    int table_attributes(int table,const SemanticNode& matrix) {
        auto env=mml_text(matrix.Text).value_or("matrix"), spec=mml_text(matrix.Alignment).value_or("");
        bool small=oneof(env,{"smallmatrix","subarray"}), align=oneof(env,{"aligned","alignedat","split","align","align*","alignat","alignat*"}),gather=oneof(env,{"gathered","gather","gather*"}),cd=env=="CD",cases=env.find("cases")!=std::string::npos;
        ref(table).RowSpacing=em(small?.1:cases?.36:.16+(matrix.Value2>0?matrix.Value2-1:0)+(align||gather||cd?.09:0));
        std::vector<std::string> alignments, lines;bool previous=false;std::string notation;size_t start=0,end=spec.size();
        if(start<end&&(spec[start]=='|'||spec[start]==':')){notation+="top ";++start;}if(start<end&&(spec[end-1]=='|'||spec[end-1]==':')){notation+="bottom ";--end;}
        for(size_t i=start;i<end;++i){char c=spec[i];if(c=='c'||c=='l'||c=='r'){alignments.push_back(c=='c'?"center":c=='r'?"right":"left");if(previous)lines.push_back("none");previous=true;}else if((c=='|'||c==':')&&previous){lines.push_back(c=='|'?"solid":"dashed");previous=false;}}
        if(align||cases||cd||env.ends_with("matrix")||env.ends_with("matrix*")){auto def=alignments.size()==1?alignments[0]:"center";int columns=oneof(env,{"alignedat","alignat","alignat*"})?int(matrix.Value)*2:matrix.ColumnCount;alignments.clear();for(int c=0;c<(cases?2:columns);++c)alignments.push_back(cases?"left":align?(c%2==0?"right":"left"):def);}
        if(alignments.empty())for(int c=0;c<matrix.ColumnCount;++c)alignments.push_back(align?(c%2==0?"right":"left"):cases?"left":"center");
        if(!oneof(env,{"smallmatrix","equation","equation*"}))ref(table).ColumnAlign=join(alignments);
        if(std::any_of(lines.begin(),lines.end(),[](auto& v){return v!="none";}))ref(table).ColumnLines=join(lines);
        if(oneof(env,{"alignedat","alignat","alignat*"})||gather)ref(table).ColumnSpacing="0em";
        else if(align){std::vector<std::string> spacing;for(int c=1;c<matrix.ColumnCount;++c)spacing.push_back(c%2==1?"0em":"1em");ref(table).ColumnSpacing=join(spacing);}
        else ref(table).ColumnSpacing=cd?"0.5em":env=="smallmatrix"?"0.2778em":"1em";
        lines.clear();int count=0;for(int row=matrix.FirstChild;row>=0;row=semantic[row].NextSibling){if(count++==0){if(semantic[row].RuleAbove!=0)notation+="left ";}else lines.push_back(semantic[row].RuleAbove>0?"solid":semantic[row].RuleAbove<0?"dashed":"none");}
        if(std::any_of(lines.begin(),lines.end(),[](auto& v){return v!="none";}))ref(table).RowLines=join(lines);
        if(matrix.RuleBelow!=0)notation+="right ";if(!notation.empty()){notation.pop_back();MmlNode n("menclose");n.First=table;n.Notation=notation;table=add(std::move(n));}return table;
    }
    void build(int index,int root);
    void serialize(std::string& output,int root);
public:
    Writer(const MathOptions& o,std::span<const SemanticNode> s):options(o),semantic(s),mapped(s.size(),-1),parents(s.size(),-1),sizes(s.size()),styles(s.size()),colors(s.size()){nodes.reserve(s.size()*2+8);}
    std::string write(std::u16string_view source,int root,bool annotation) {
        sizes.at(root)=6;styles[root]=options.DisplayMode?MathStyle::Display:MathStyle::Text;
        std::vector<std::pair<int,bool>> stack{{root,false}};
        while(!stack.empty()){
            auto [index,visit]=stack.back();stack.pop_back();const auto& n=semantic[index];
            if(visit){build(index,root);continue;}
            stack.emplace_back(index,true);auto style=n.Kind==NodeKind::Style?n.Style:n.Kind==NodeKind::Sizing&&styles[index]>=MathStyle::Script?MathStyle::Text:styles[index];styles[index]=style;
            if(n.Kind==NodeKind::Sizing)sizes[index]=int(n.Value);if(n.Kind==NodeKind::Color)colors[index]=mml_text(n.Text2);
            auto child=[&](int c){if(c>=0){parents[c]=index;styles[c]=style;sizes[c]=sizes[index];colors[c]=colors[index];stack.emplace_back(c,false);}};
            if(n.Kind==NodeKind::Row||n.Kind==NodeKind::Matrix||n.Kind==NodeKind::MatrixRow||n.Kind==NodeKind::MatrixCell||n.Kind==NodeKind::Color||n.Kind==NodeKind::Sizing||n.Kind==NodeKind::Class||n.Kind==NodeKind::Operator||(n.Kind==NodeKind::Box&&oneof(n.Text,{u"\\phantom",u"\\hbox",u"\\pmb"}))){for(int c=n.FirstChild;c>=0;c=semantic[c].NextSibling)child(c);if(n.Kind==NodeKind::MatrixRow)child(n.SecondChild);}
            else {child(n.FirstChild);child(n.SecondChild);child(n.ThirdChild);child(n.FourthChild);}
        }
        std::string output;output.reserve(256+semantic.size()*40+source.size());output="<math xmlns=\"http://www.w3.org/1998/Math/MathML\"";if(options.DisplayMode)output+=" display=\"block\"";output+='>';if(annotation)output+="<semantics>";
        serialize(output,mapped[root]);if(annotation){output+="<annotation encoding=\"application/x-tex\">";escaped(output,mml_utf8(source));output+="</annotation></semantics>";}output+="</math>";return output;
    }
};
void Writer::build(int index,int root) {
    const auto& n=semantic[index];
    switch(n.Kind) {
    case NodeKind::Symbol: {
        std::string tag=n.IsMathOrd?"mi":((n.TextMode&&n.Atom==MathAtom::Ord)||n.CodePoint==32||n.CodePoint==160)?"mtext":(n.Atom!=MathAtom::Ord||n.CodePoint==0x2032)?"mo":(n.OriginalCodePoint==0&&n.CodePoint>='0'&&n.CodePoint<='9')?"mn":"mi";
        MmlNode item(tag,scalar(n.OriginalCodePoint>0?n.OriginalCodePoint:n.TextMode&&n.CodePoint==32?160:n.CodePoint),n.CodePoint==32||n.CodePoint==160?Opt{}:variant(n));
        item.Separator=n.Atom==MathAtom::Punct;item.NoStretch=n.Atom==MathAtom::Open||n.Atom==MathAtom::Close;
        item.SuppressSpacing=n.CodePoint==0x2032&&parents[index]>=0&&semantic[parents[index]].Kind==NodeKind::Row&&semantic[parents[index]].Value==1&&semantic[parents[index]].FirstChild==index&&n.NextSibling<0;
        mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Space: {
        MmlNode item("mspace");
        if(oneof(n.Text,{u"\\nobreak",u"\\allowbreak"})){mapped[index]=add(std::move(item));break;}
        if(eq(n.Text,u"cdnull")){mapped[index]=add(MmlNode("mi"," ","normal"));break;}
        if(eq(n.Text,u"\\\\")){if(!options.DisplayMode||options.StrictMode!=MathStrictMode::Error)item.LineBreak="newline";if(n.Length1.Number!=0)item.Height=em(length(n.Length1,index));mapped[index]=add(std::move(item));break;}
        if(oneof(n.Text,{u"\\kern",u"\\mkern",u"\\hskip",u"\\mskip"})){mapped[index]=space(length(n.Length1,index));break;}
        if(n.Value>=.1666&&n.Value<=.1667)item.Text="\u2009";else if(n.Value>=.2222&&n.Value<=.2223)item.Text="\u2005";
        else if(n.Value>=.2777&&n.Value<=.2778)item.Text="\u2005\u200a";else if(n.Value>=-.1667&&n.Value<=-.1666)item.Text="\u2009\u2063";
        if(item.Text)item.Tag="#text";else item.Width=em(n.Value);mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Row: {
        int first=-1,last=-1,count=0;
        for(int child=n.FirstChild;child>=0;child=semantic[child].NextSibling){
            int current=mapped[child];if(last>=0&&merge(last,current))continue;
            if(last>=0&&(ref(current).Tag=="msup"||ref(current).Tag=="msub")&&is_number(last)&&ref(ref(current).First).Tag=="mn"){
                int base_id=ref(current).First;auto prefix=ref(last).Buffer?*ref(last).Buffer:ref(last).Text.value_or("");prefix+=ref(base_id).Buffer?*ref(base_id).Buffer:ref(base_id).Text.value_or("");ref(base_id).Buffer=std::move(prefix);ref(base_id).Text.reset();
                if(first==last)first=last=current;else{int before=first;while(ref(before).Next!=last)before=ref(before).Next;ref(before).Next=current;last=current;}continue;
            }
            if(first<0)first=current;else ref(last).Next=current;last=current;++count;
        }
        bool literal=true;
        if(parents[index]>=0){const auto& owner=semantic[parents[index]];if(owner.Kind==NodeKind::Choice||owner.Kind==NodeKind::MathMlChoice||(owner.Kind==NodeKind::Box&&oneof(owner.Text,{u"\\vphantom",u"\\phantom"}))||(owner.Kind==NodeKind::Class&&!oneof(owner.Text,{u"\\boldsymbol",u"\\bm"})))literal=false;}
        if(count==1&&n.Value==1&&literal&&!n.TextMode&&ref(first).Tag=="mo"&&n.FirstChild>=0&&(semantic[n.FirstChild].Kind==NodeKind::Symbol||semantic[n.FirstChild].Kind==NodeKind::Operator))ref(first).SuppressSpacing=true;
        if(count==1&&(index!=root||ref(first).Tag=="mrow"))mapped[index]=first;else{MmlNode item("mrow");item.First=first;mapped[index]=add(std::move(item));}break;
    }
    case NodeKind::Font: {
        int child=mapped[n.FirstChild];
        if(n.Font==MathFont::MathBoldItalic){int atom=n.FirstChild;if(semantic[atom].Kind==NodeKind::Row&&semantic[atom].FirstChild>=0)atom=semantic[atom].FirstChild;auto tag=semantic[atom].Atom==MathAtom::Bin||semantic[atom].Atom==MathAtom::Rel?"mo":"mi";bool character=oneof(ref(child).Tag,{"mi","mn","mo"})&&ref(child).First<0;
            if(character){ref(child).Tag=tag;mapped[index]=child;}else{MmlNode item(tag);item.First=child;mapped[index]=add(std::move(item));}if(semantic[atom].Atom==MathAtom::Bin){ref(mapped[index]).LSpace="0.22em";ref(mapped[index]).RSpace="0.22em";}
        }else mapped[index]=child;break;
    }
    case NodeKind::Text: mapped[index]=mapped[n.FirstChild];break;
    case NodeKind::Style: {
        int child=mapped[n.FirstChild],parent=parents[index];
        bool cd_cell=parent>=0&&semantic[parent].Kind==NodeKind::MatrixCell&&parents[parent]>=0&&parents[parents[parent]]>=0&&eq(semantic[parents[parents[parent]]].Text,u"CD");
        if(cd_cell&&semantic[n.FirstChild].Kind==NodeKind::Row&&semantic[n.FirstChild].FirstChild<0)child=-1;
        if(child>=0&&semantic[n.FirstChild].Kind==NodeKind::Row&&semantic[n.FirstChild].Value==0&&ref(child).Tag=="mrow"&&(parent<0||semantic[parent].Kind!=NodeKind::MatrixCell)&&(semantic[n.FirstChild].FirstChild<0||semantic[semantic[n.FirstChild].FirstChild].NextSibling>=0))child=ref(child).First;
        MmlNode item("mstyle");item.First=child;item.Style=n.Style;mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Fraction: {
        int numerator=mapped[n.FirstChild],denominator=mapped[n.SecondChild];ref(numerator).Next=denominator;MmlNode item("mfrac");item.First=numerator;if(!n.HasBar)item.LineThickness="0px";else if(n.Value>0)item.LineThickness=em(eq(n.Text,u"\\genfrac")?length(n.Length1,index):n.Value);int fraction=add(std::move(item));mapped[index]=fences(fraction,n.LeftDelimiter,n.RightDelimiter);break;
    }
    case NodeKind::Radical: {
        int body=mapped[n.FirstChild];if(n.SecondChild>=0)ref(body).Next=mapped[n.SecondChild];MmlNode item(n.SecondChild>=0?"mroot":"msqrt");item.First=body;mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Script: {
        int base=mapped[n.FirstChild],last=base;if(n.ThirdChild>=0){int sub=mapped[n.ThirdChild];ref(last).Next=sub;last=sub;}if(n.SecondChild>=0)ref(last).Next=mapped[n.SecondChild];
        const auto& op=semantic[n.FirstChild];bool limits=op.Kind==NodeKind::Operator&&(eq(op.Text,u"\\operatorname")?(eq(op.Text2,u"star")&&(styles[index]==MathStyle::Display||(op.AlwaysLimits&&!op.NeverLimits))):(!op.NeverLimits&&(((op.Limits||op.AlwaysLimits)&&styles[index]==MathStyle::Display)||(op.AlwaysLimits&&(n.SecondChild<0||n.ThirdChild<0)))));
        bool brace=op.Kind==NodeKind::Accent&&((oneof(op.Text,{u"\\overbrace",u"\\overbracket"})&&n.ThirdChild<0)||(oneof(op.Text,{u"\\underbrace",u"\\underbracket"})&&n.SecondChild<0));
        std::string tag=limits||brace?(n.SecondChild<0?"munder":n.ThirdChild<0?"mover":"munderover"):(n.SecondChild<0?"msub":n.ThirdChild<0?"msup":"msubsup");MmlNode item(tag);item.First=base;mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Operator: {
        if(n.CodePoint>0){MmlNode item("mo",scalar(n.CodePoint));item.LargeOpFalse=eq(n.Text,u"\\smallint");mapped[index]=add(std::move(item));}
        else if(eq(n.Text,u"\\mathop")){MmlNode item("mo");item.First=unwrap(link(n.FirstChild));mapped[index]=add(std::move(item));}
        else{
            bool name=oneof(n.Text,{u"\\operatorname@",u"\\operatornamewithlimits",u"\\operatorname",u"\\operatorname*",u"\\liminf",u"\\limsup"});Opt value;
            if(eq(n.Text,u"\\liminf"))value="lim\u2009inf";else if(eq(n.Text,u"\\limsup"))value="lim\u2009sup";
            else if(name){value=content(mapped[n.FirstChild]);for(auto pair:{std::pair<std::string,std::string>{"\u2212","-"},{"\u2217","*"}}){size_t p=0;while((p=value->find(pair.first,p))!=std::string::npos){value->replace(p,pair.first.size(),pair.second);p+=pair.second.size();}}}
            else {value=mml_text(n.Text);if(value){auto p=value->find_first_not_of('\\');*value=p==std::string::npos?"":value->substr(p);}}
            bool plain=!name||plain_name(n.FirstChild);MmlNode identifier("mi",plain?value:Opt{},name?Opt("normal"):Opt{});identifier.First=plain?-1:unwrap(mapped[n.FirstChild]);int id=add(std::move(identifier)),apply=add(MmlNode("mo","\u2061"));ref(id).Next=apply;MmlNode item(parents[index]>=0&&semantic[parents[index]].Kind==NodeKind::Script?"mrow":"#fragment");item.First=id;mapped[index]=add(std::move(item));
        }break;
    }
    case NodeKind::Delimiter: {
        MmlNode item("mo",n.CodePoint==0||n.CodePoint=='.'?"":scalar(n.Value==0&&n.CodePoint=='|'?'|':fence_cp(n.CodePoint)));item.Fence=n.Value==0||n.Atom==MathAtom::Open||n.Atom==MathAtom::Close?"true":"false";
        if(n.Value>0){item.Stretchy="true";item.MinSize=item.MaxSize=em(n.Value==1?1.2:n.Value==2?1.8:n.Value==3?2.4:3);}if(n.Value==0){item.LSpace="0.05em";item.RSpace="0.05em";}mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Delimited: mapped[index]=fences(mapped[n.FirstChild],n.LeftDelimiter,n.RightDelimiter,true);break;
    case NodeKind::Accent: {
        auto label=mml_text(n.Text).value_or("");
        if(label.starts_with("\\x")||label.starts_with("\\\\cd")){
            MmlNode arrow("mo",scalar(accent_cp(label)));arrow.Stretchy="true";arrow.MinSize="3.0em";int a=add(std::move(arrow));
            MmlNode upper("mpadded");upper.First=n.FirstChild<0?-1:mapped[n.FirstChild];upper.Width="+0.6em";upper.LSpace="0.3em";int u=add(std::move(upper));
            if(n.SecondChild>=0){MmlNode lower("mpadded");lower.First=mapped[n.SecondChild];lower.Width="+0.6em";lower.LSpace="0.3em";int l=add(std::move(lower));ref(a).Next=l;ref(l).Next=u;}else ref(a).Next=u;
            MmlNode item(n.SecondChild>=0?"munderover":"mover");item.First=a;mapped[index]=add(std::move(item));
        }else{
            bool under=label.starts_with("\\under")||label=="\\utilde",brace=oneof(label,{"\\overbrace","\\underbrace","\\overbracket","\\underbracket"});
            bool stretchy=!oneof(label,{"\\acute","\\grave","\\ddot","\\tilde","\\bar","\\breve","\\check","\\hat","\\vec","\\dot","\\mathring","\\'","\\`","\\^","\\~","\\=","\\u","\\.","\\\"","\\c","\\r","\\H","\\v","\\textcircled"});
            MmlNode accent("mo",oneof(label,{"\\overlinesegment","\\underlinesegment"})?"undefined":scalar(accent_cp(label,n.CodePoint)));if(stretchy)accent.Stretchy="true";int a=add(std::move(accent)),body=mapped[n.FirstChild];ref(body).Next=a;MmlNode item(under?"munder":"mover");item.First=body;if(!under&&!brace)item.Accent="true";if(under&&!brace)item.AccentUnder="true";mapped[index]=add(std::move(item));
        }break;
    }
    case NodeKind::MatrixCell: {MmlNode item("mtd");item.First=mapped[n.FirstChild];mapped[index]=add(std::move(item));break;}
    case NodeKind::MatrixRow: {
        int children=link(n.FirstChild);
        if(n.SecondChild>=0||(n.AlwaysLimits&&!n.NeverLimits)){
            MmlNode glue("mtd");glue.ClassName="mtr-glue";int left=add(glue),right=add(std::move(glue));MmlNode tag("mtd");tag.ClassName="mml-eqn-num";int eq_tag=add(std::move(tag)),last=children;if(last>=0)while(ref(last).Next>=0)last=ref(last).Next;
            ref(left).Next=children;if(last>=0)ref(last).Next=right;else ref(left).Next=right;if(options.Leqno){ref(eq_tag).Next=left;children=eq_tag;}else{ref(right).Next=eq_tag;children=left;}
        }MmlNode item("mtr");item.First=children;mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Matrix: {
        MmlNode item("mtable");item.First=link(n.FirstChild);int table=table_attributes(add(std::move(item)),n);
        if(oneof(n.Text,{u"smallmatrix",u"subarray"})){MmlNode style("mstyle");style.First=table;style.ScriptLevelOnly=true;table=add(std::move(style));}mapped[index]=fences(table,n.LeftDelimiter,n.RightDelimiter);break;
    }
    case NodeKind::Color: {
        int first=link(n.FirstChild);if(n.FirstChild>=0&&semantic[n.FirstChild].Kind==NodeKind::Row&&semantic[n.FirstChild].Value==0){if(semantic[n.FirstChild].FirstChild<0)first=-1;else if(semantic[semantic[n.FirstChild].FirstChild].NextSibling>=0)first=unwrap(first);}MmlNode item("mstyle");item.First=first;item.MathColor=mml_text(n.Text2);mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Sizing: {MmlNode item("mstyle");item.First=unwrap(link(n.FirstChild));item.MathSize=em(mathml_size_scale(int(n.Value)));mapped[index]=add(std::move(item));break;}
    case NodeKind::Choice: mapped[index]=mapped[styles[index]==MathStyle::Display?n.FirstChild:styles[index]==MathStyle::Text?n.SecondChild:styles[index]==MathStyle::Script?n.ThirdChild:n.FourthChild];break;
    case NodeKind::MathMlChoice: mapped[index]=mapped[n.SecondChild];break;
    case NodeKind::Box: mapped[index]=box(n,index);break;
    case NodeKind::Class: {
        int first=link(n.FirstChild),declared=n.FirstChild;
        if(declared>=0&&semantic[declared].Kind==NodeKind::Row&&semantic[declared].FirstChild>=0&&semantic[semantic[declared].FirstChild].NextSibling<0)declared=semantic[declared].FirstChild;
        bool plain=declared>=0&&(semantic[declared].Kind==NodeKind::Row||semantic[declared].Kind==NodeKind::Symbol);if(!oneof(n.Text,{u"\\boldsymbol",u"\\bm"})&&plain)first=unwrap(first);
        bool character=first>=0&&ref(first).Next<0&&oneof(ref(first).Tag,{"mi","mo","mn"})&&ref(first).First<0;if(declared>=0&&semantic[declared].Kind==NodeKind::MathMlChoice)character=false;
        auto tag=n.Atom==MathAtom::Inner?"mpadded":n.Atom==MathAtom::Ord?"mi":"mo";if(character&&n.Atom!=MathAtom::Inner){ref(first).Tag=tag;mapped[index]=first;}else{MmlNode item(tag);item.First=first;mapped[index]=add(std::move(item));}
        auto& item=ref(mapped[index]);if(n.Atom==MathAtom::Bin){item.LSpace="0.22em";item.RSpace="0.22em";}else if(n.Atom==MathAtom::Punct){item.LSpace="0em";item.RSpace="0.17em";}else if((n.Atom==MathAtom::Open||n.Atom==MathAtom::Close)&&!eq(n.Text,u"\\html@mathml")){item.LSpace="0em";item.RSpace="0em";}break;
    }
    case NodeKind::Rule: {
        MmlNode rule("mspace");rule.MathBackground=colors[index].value_or("black");rule.Width=em(length(n.Length1,index));rule.Height=em(length(n.Length2,index));int r=add(std::move(rule));double shift=length(n.Length3,index);MmlNode item("mpadded");item.First=r;item.Height=em(shift);if(shift<0)item.Depth=em(-shift);item.VOffset=em(shift);mapped[index]=add(std::move(item));break;
    }
    case NodeKind::Enclose: mapped[index]=enclose(n);break;
    case NodeKind::Verb: {
        auto value=mml_text(n.Text2?n.Text2:n.Text).value_or("");std::string replaced;for(char c:value){if(c==' ')replaced+=n.Value!=0?"\u2423":"\u00a0";else replaced+=c;}mapped[index]=add(MmlNode("mtext",replaced,"monospace"));break;
    }
    case NodeKind::Tag: {
        int body=mapped[n.FirstChild],label=n.SecondChild>=0?mapped[n.SecondChild]:add(MmlNode("mrow"));MmlNode pad("mtd");pad.Width="50%";int left=add(pad),right=add(std::move(pad));MmlNode cell("mtd");cell.First=body;int formula=add(std::move(cell));MmlNode tag("mtd");tag.First=label;int tag_cell=add(std::move(tag));ref(left).Next=formula;ref(formula).Next=right;ref(right).Next=tag_cell;MmlNode row("mtr");row.First=left;int r=add(std::move(row));MmlNode table("mtable");table.First=r;table.Width="100%";mapped[index]=add(std::move(table));break;
    }
    }
}
void Writer::serialize(std::string& output,int root) {
    std::vector<std::pair<int,bool>> tasks{{root,false}};
    while(!tasks.empty()){
        auto[index,close]=tasks.back();tasks.pop_back();const auto& n=ref(index);
        if(close){output+="</";output+=n.Tag;output+='>';continue;}
        if(n.Next>=0)tasks.emplace_back(n.Next,false);
        if(n.Tag=="#text"){if(n.Text)escaped(output,*n.Text);continue;}
        if(n.Tag=="#fragment"){if(n.First>=0)tasks.emplace_back(n.First,false);continue;}
        output+='<';output+=n.Tag;
        auto attribute=[&](std::string_view name,const Opt& value){if(value)attr(output,name,*value);};
        attribute("mathvariant",n.Variant);if(n.Separator)output+=" separator=\"true\"";if(n.NoStretch)output+=" stretchy=\"false\"";
        if(n.SuppressSpacing){if(!n.LSpace)output+=" lspace=\"0em\"";if(!n.RSpace)output+=" rspace=\"0em\"";}
        attribute("width",n.Width);attribute("linethickness",n.LineThickness);attribute("fence",n.Fence);attribute("stretchy",n.Stretchy);attribute("minsize",n.MinSize);attribute("maxsize",n.MaxSize);attribute("lspace",n.LSpace);attribute("rspace",n.RSpace);attribute("accent",n.Accent);attribute("accentunder",n.AccentUnder);
        if(n.LargeOpFalse)output+=" largeop=\"false\"";
        attribute("rowspacing",n.RowSpacing);attribute("columnspacing",n.ColumnSpacing);attribute("columnalign",n.ColumnAlign);attribute("columnlines",n.ColumnLines);attribute("rowlines",n.RowLines);attribute("notation",n.Notation);attribute("mathcolor",n.MathColor);attribute("mathbackground",n.MathBackground);attribute("mathsize",n.MathSize);attribute("height",n.Height);attribute("depth",n.Depth);attribute("voffset",n.VOffset);attribute("style",n.CssStyle);attribute("class",n.ClassName);attribute("linebreak",n.LineBreak);
        if(n.Tag=="mstyle"&&!n.MathColor&&!n.MathSize&&!n.CssStyle){attr(output,"scriptlevel",n.ScriptLevelOnly?"1":n.Style==MathStyle::Display||n.Style==MathStyle::Text?"0":n.Style==MathStyle::Script?"1":"2");if(!n.ScriptLevelOnly)attr(output,"displaystyle",n.Style==MathStyle::Display?"true":"false");}
        output+='>';if(n.Buffer)escaped(output,*n.Buffer);else if(n.Text)escaped(output,*n.Text);tasks.emplace_back(index,true);if(n.First>=0)tasks.emplace_back(n.First,false);
    }
}
} // namespace
std::string write_mathml(std::u16string_view source,const MathOptions& options,std::span<const SemanticNode> semantic,int root,bool include_annotation) {
    return Writer(options,semantic).write(source,root,include_annotation);
}
} // namespace velocitex

