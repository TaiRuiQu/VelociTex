#include "cache.hpp"
#include <limits>
namespace velocitex {
DocumentCache::OptionsKey::OptionsKey(const MathOptions& o):
 FontSize(o.FontSize),MaxSize(o.MaxSize),MinRuleThickness(o.MinRuleThickness),DisplayWidth(o.DisplayWidth),
 MaxInputLength(o.MaxInputLength),MaxNodes(o.MaxNodes),MaxNesting(o.MaxNesting),MaxExpansion(o.MaxExpansion),MaxTokens(o.MaxTokens),EquationNumber(o.EquationNumber),
 DisplayMode(o.DisplayMode),ColorIsTextColor(o.ColorIsTextColor),GlobalGroup(o.GlobalGroup),Leqno(o.Leqno),Fleqn(o.Fleqn),StrictMode(o.StrictMode){}
size_t DocumentCache::Hash::operator()(const Key& key)const{
 size_t h=std::hash<std::u16string>{}(key.Source);
 auto add=[&](auto value){h^=std::hash<decltype(value)>{}(value)+size_t(0x9e3779b9)+(h<<6)+(h>>2);};
 const auto& o=key.Options;
 add(o.FontSize);add(o.MaxSize);add(o.MinRuleThickness);add(o.DisplayWidth);
 add(o.MaxInputLength);add(o.MaxNodes);add(o.MaxNesting);add(o.MaxExpansion);add(o.MaxTokens);add(o.EquationNumber);
 add(o.DisplayMode);add(o.ColorIsTextColor);add(o.GlobalGroup);add(o.Leqno);add(o.Fleqn);add(int(o.StrictMode));return h;
}
DocumentCache::DocumentCache(int64_t maximum_bytes,int maximum_items):maximumBytes_(maximum_bytes),maximumItems_(maximum_items){
 if(maximum_bytes<0||maximum_items<0)throw std::invalid_argument("Cache limits must be nonnegative.");
}
DocumentCache::~DocumentCache(){for(auto& item:recency_)release_document(item.Document);}
bool DocumentCache::ContainsDefinition(std::u16string_view source){
 constexpr std::u16string_view names[]={u"\\def",u"\\gdef",u"\\edef",u"\\xdef",u"\\let",u"\\futurelet",u"\\global",u"\\newcommand",u"\\renewcommand",u"\\providecommand"};
 for(auto name:names)if(source.find(name)!=source.npos)return true;return false;
}
bool DocumentCache::Eligible(std::u16string_view source,const MathOptions& o,bool has_macros)const{
 return maximumBytes_>0&&maximumItems_>0&&!has_macros&&o.Macros.empty()&&!o.Session&&!o.TextShaper&&!o.StrictHandler&&!ContainsDefinition(source);
}
MathDocument* DocumentCache::lookup(std::u16string_view source,const MathOptions& options,bool has_macros){
 check_cancel(options);
 if(!Eligible(source,options,has_macros))return nullptr;
 MathDocument* document=nullptr;
 {
  std::lock_guard lock(gate_);auto found=items_.find(Key{std::u16string(source),OptionsKey(options)});
  if(found!=items_.end()){recency_.splice(recency_.begin(),recency_,found->second);document=found->second->Document;retain_document(document);}
 }
 if(document){try{check_cancel(options);}catch(...){release_document(document);throw;}}
 return document;
}
std::vector<MathDocument*> DocumentCache::store(MathDocument* document,bool has_macros){
 std::vector<MathDocument*> evicted;
 if(!document||!document->HasLayout||!Eligible(document->Source,document->Options,has_macros))return evicted;
 const int64_t bytes=Estimate(*document);if(bytes>maximumBytes_)return evicted;
 Key key{document->Source,OptionsKey(document->Options)};
 std::lock_guard lock(gate_);
 if(items_.contains(key))return evicted;
 // Reserve before mutations so allocation failure cannot leak retained entries.
 evicted.reserve(items_.size());
 recency_.push_front(Entry{key,document,bytes});
 try{items_.emplace(std::move(key),recency_.begin());}catch(...){recency_.pop_front();throw;}
 retain_document(document);bytes_+=bytes;
 while(items_.size()>size_t(maximumItems_)||bytes_>maximumBytes_){
  auto last=std::prev(recency_.end());evicted.push_back(last->Document);bytes_-=last->Bytes;items_.erase(last->Identity);recency_.erase(last);
 }
 return evicted;
}
std::vector<MathDocument*> DocumentCache::Clear(){
 std::vector<MathDocument*> removed;
 std::lock_guard lock(gate_);removed.reserve(recency_.size());
 for(auto& entry:recency_)removed.push_back(entry.Document);
 items_.clear();recency_.clear();bytes_=0;return removed;
}
int DocumentCache::Count()const{std::lock_guard lock(gate_);return int(items_.size());}
int64_t DocumentCache::EstimatedRetainedBytes()const{std::lock_guard lock(gate_);return bytes_;}
int64_t DocumentCache::Estimate(const MathDocument& d){
 // Charge native containers and strings together with the corresponding managed
 // result payload. This is a conservative retention budget, not a heap measurement.
 int64_t bytes=1024+int64_t(sizeof(MathDocument))+4*int64_t(d.Source.size());
 auto strings=[&](const std::optional<std::u16string>& s){return int64_t(64)+(s?4*int64_t(s->size()):0);};
 bytes+=(int64_t(sizeof(SemanticNode))+int64_t(256))*int64_t(d.Nodes.size());
 for(const auto& n:d.Nodes)bytes+=strings(n.Text)+strings(n.Text2)+strings(n.Text3)+strings(n.Alignment);
 const auto& l=d.Layout;
 bytes+=(int64_t(sizeof(MathGlyph))+64)*int64_t(l.Glyphs.size());
 bytes+=(int64_t(sizeof(MathRule))+48)*int64_t(l.Rules.size());
 bytes+=(int64_t(sizeof(MathPath))+128)*int64_t(l.Paths.size());
 for(const auto& p:l.Paths)bytes+=(int64_t(sizeof(MathPathSegment))+64)*int64_t(p.Segments.size());
 bytes+=(int64_t(sizeof(MathTextGlyph))+128)*int64_t(l.TextGlyphs.size());
 for(const auto& t:l.TextGlyphs)bytes+=4*int64_t(t.FontFamily.size());
 bytes+=(int64_t(sizeof(MathTextRun))+128)*int64_t(l.TextRuns.size());
 for(const auto& r:l.TextRuns)bytes+=4*int64_t(r.Text.size());
 bytes+=(int64_t(sizeof(MathPaintCommand))+24)*int64_t(l.PaintCommands.size());
 bytes+=(int64_t(sizeof(MathPaintState))+80)*int64_t(l.PaintStates.size());
 bytes+=(int64_t(sizeof(MathWarning))+128)*int64_t(d.Warnings.size());
 for(const auto& w:d.Warnings)bytes+=4*int64_t(w.Code.size()+w.Message.size());
 // Cached UTF-8 MathML can be materialized after insertion. Reserve a bounded
 // worst-case expansion for both lazy serialized variants instead of omitting it.
 bytes+=512+24*int64_t(d.Source.size())+512*int64_t(d.Nodes.size());
 return bytes;
}
}
