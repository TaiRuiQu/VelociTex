#include "svg.hpp"
#include "generated/svg_data.hpp"
#include <charconv>
#include <unordered_map>

namespace velocitex {
namespace {
constexpr size_t MaxOutputCharacters=16*1024*1024;
bool letter(char c){return (c>='a'&&c<='z')||(c>='A'&&c<='Z');}
void validate_prefix(std::string_view prefix){
    if(prefix.empty()||prefix.size()>96||!letter(prefix[0])||std::any_of(prefix.begin(),prefix.end(),[](char c){return !letter(c)&&!(c>='0'&&c<='9')&&c!='_'&&c!='-';}))
        throw std::invalid_argument("Use an ASCII letter followed by letters, digits, underscores or hyphens, at most 96 characters.");
}
void bound(const std::string& s){if(s.size()>MaxOutputCharacters)throw std::runtime_error("SVG output limit exceeded.");}
void append_number(std::string& output,double value){
    if(!std::isfinite(value))throw std::runtime_error("Unable to format an SVG coordinate.");
    if(value>-9e12&&value<9e12){
        int64_t scaled=int64_t(std::round(value*1'000'000));
        if(scaled==0){output+='0';return;}
        if(scaled<0){output+='-';scaled=-scaled;}
        char buffer[32];auto r=std::to_chars(buffer,buffer+sizeof(buffer),scaled/1'000'000);output.append(buffer,r.ptr);
        int64_t fraction=scaled%1'000'000;if(!fraction)return;
        char digits[6];for(int i=5;i>=0;--i){digits[i]=char('0'+fraction%10);fraction/=10;}
        int length=6;while(length>0&&digits[length-1]=='0')--length;output+='.';output.append(digits,size_t(length));return;
    }
    char buffer[64];auto r=std::to_chars(buffer,buffer+sizeof(buffer),value,std::chars_format::general,17);
    if(r.ec!=std::errc())throw std::runtime_error("Unable to format an SVG coordinate.");
    for(char* p=buffer;p!=r.ptr;++p)output+=*p=='e'?'E':*p;
}
void attribute(std::string& out,std::string_view name,double value){out+=' ';out+=name;out+="=\"";append_number(out,value);out+='"';}
void rect(std::string& out,double x,double y,double width,double height){out+="<rect";attribute(out,"x",x);attribute(out,"y",y);attribute(out,"width",width);attribute(out,"height",height);out+="/>";}
void escaped(std::string& out,std::string_view value){
    for(char c:value)switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '\r':out+="&#xD;";break;case '\n':out+="&#xA;";break;case '\t':out+="&#x9;";break;default:out+=c;break;}
}
std::string path_data(std::span<const MathPathSegment> segments){
    std::string data;data.reserve(segments.size()*24);
    for(const auto& s:segments){switch(s.Operation){
        case MathPathOperation::Move:case MathPathOperation::Line:data+=s.Operation==MathPathOperation::Move?'M':'L';append_number(data,s.X1);data+=' ';append_number(data,s.Y1);break;
        case MathPathOperation::Cubic:data+='C';append_number(data,s.X1);data+=' ';append_number(data,s.Y1);data+=' ';append_number(data,s.X2);data+=' ';append_number(data,s.Y2);data+=' ';append_number(data,s.X3);data+=' ';append_number(data,s.Y3);break;
        case MathPathOperation::Close:data+='Z';break;
    }bound(data);}return data;
}
std::string write_core(std::u16string_view source,const LayoutOutput& layout,std::string_view glyph_prefix,std::string_view clip_prefix,bool stable,bool include_definitions,std::string& definitions){
    auto bounds=svg_viewport(layout);std::string out;out.reserve(1024+layout.Glyphs.size()*96);out="<svg xmlns=\"http://www.w3.org/2000/svg\"";
    attribute(out,"width",bounds.Width);attribute(out,"height",bounds.Height);out+=" viewBox=\"";append_number(out,bounds.X);out+=' ';append_number(out,bounds.Y);out+=' ';append_number(out,bounds.Width);out+=' ';append_number(out,bounds.Height);out+="\" role=\"img\" fill=\"currentColor\"><title>";
    auto title=utf8(source);escaped(out,title);out+="</title><defs>";
    // The managed limit counts UTF-16 code units. Contours and markup are ASCII;
    // the title is the only UTF-8 payload and its excess byte count is constant.
    const size_t utf8_excess=title.size()-source.size();
    auto check_bound=[&](){if(out.size()-utf8_excess>MaxOutputCharacters)throw std::runtime_error("SVG output limit exceeded.");};
    std::unordered_map<uint64_t,std::string> ids;ids.reserve(layout.Glyphs.size());
    for(const auto& g:layout.Glyphs){
        auto key=(uint64_t(uint32_t(g.FontId))<<32)|g.GlyphId;if(ids.contains(key))continue;
        std::string id(glyph_prefix);id+='g';if(stable){id+=std::to_string(g.FontId);id+='_';id+=std::to_string(g.GlyphId);}else id+=std::to_string(ids.size());ids.emplace(key,id);
        const auto& font=generated::SvgFonts[size_t(g.FontId)];auto& sink=include_definitions?out:definitions;sink+="<path id=\"";sink+=id;sink+="\" d=\"";sink+=font.Paths[g.GlyphId];sink+="\"/>";check_bound();
    }
    for(size_t i=0;i<layout.Paths.size();++i){out+="<clipPath id=\"";out+=clip_prefix;out+='c';out+=std::to_string(i);out+="\" clipPathUnits=\"userSpaceOnUse\">";const auto& c=layout.Paths[i].ClipBounds;rect(out,c.X,c.Y,c.Width,c.Height);out+="</clipPath>";check_bound();}out+="</defs>";
    for(const auto& command:layout.PaintCommands){
        const auto& state=layout.PaintStates.at(command.StateIndex);if(state.Color&&state.Color->A==0)continue;
        bool transformed=state.Transform!=MathTransform::Identity(),wrapped=state.Color.has_value()||transformed;
        if(wrapped){out+="<g";if(state.Color){static constexpr char hex[]="0123456789abcdef";const auto& c=*state.Color;out+=" fill=\"#";for(auto b:{c.R,c.G,c.B}){out+=hex[b>>4];out+=hex[b&15];}out+='"';if(c.A!=255)attribute(out,"fill-opacity",c.A/255.0);}if(transformed){const auto& t=state.Transform;out+=" transform=\"matrix(";append_number(out,t.M11);out+=' ';append_number(out,t.M12);out+=' ';append_number(out,t.M21);out+=' ';append_number(out,t.M22);out+=' ';append_number(out,t.Dx);out+=' ';append_number(out,t.Dy);out+=")\"";}out+='>';}
        switch(command.Kind){
        case MathPaintKind::Glyph:{const auto& g=layout.Glyphs.at(command.Index);double scale=g.FontSize/generated::SvgFonts[size_t(g.FontId)].UnitsPerEm;auto key=(uint64_t(uint32_t(g.FontId))<<32)|g.GlyphId;out+="<use href=\"#";out+=ids.at(key);out+="\" transform=\"translate(";append_number(out,g.X);out+=' ';append_number(out,g.Y);out+=") scale(";append_number(out,scale);out+=")\"/>";break;}
        case MathPaintKind::Rule:{const auto& r=layout.Rules.at(command.Index);rect(out,r.X,r.Y,r.Width,r.Height);break;}
        case MathPaintKind::Path:{const auto& p=layout.Paths.at(command.Index);out+="<g clip-path=\"url(#";out+=clip_prefix;out+='c';out+=std::to_string(command.Index);out+=")\"><path transform=\"translate(";append_number(out,p.X);out+=' ';append_number(out,p.Y);out+=") scale(";append_number(out,p.ScaleX);out+=' ';append_number(out,p.ScaleY);out+=")\" d=\"";out+=path_data(p.Segments);out+="\"/></g>";break;}
        default:throw std::runtime_error("Unknown paint instruction.");
        }if(wrapped)out+="</g>";check_bound();
    }out+="</svg>";check_bound();return out;
}
}
MathBounds svg_viewport(const LayoutOutput& layout){
    const auto& ink=layout.InkBounds;double x=std::min(0.0,ink.X),y=std::min(0.0,ink.Y),width=std::max(layout.LogicalSize.Width,ink.X+ink.Width)-x,height=std::max(layout.LogicalSize.Height,ink.Y+ink.Height)-y;
    if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||width>32768||height>32768||width<0||height<0)throw std::runtime_error("SVG geometry exceeds the 32768 pixel export boundary.");
    return {x,y,std::max(1.0,width),std::max(1.0,height)};
}
std::string write_svg(std::u16string_view source,const LayoutOutput& layout,std::string_view prefix){validate_prefix(prefix);std::string definitions;return write_core(source,layout,prefix,prefix,false,true,definitions);}
SharedSvg write_shared_svg(std::u16string_view source,const LayoutOutput& layout,std::string_view glyph_prefix,std::string_view clip_prefix){validate_prefix(glyph_prefix);validate_prefix(clip_prefix);SharedSvg result;result.Svg=write_core(source,layout,glyph_prefix,clip_prefix,true,false,result.Definitions);return result;}
}
