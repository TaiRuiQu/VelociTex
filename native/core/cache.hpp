#pragma once
#include "core.hpp"
#include <list>
#include <mutex>
#include <unordered_map>
namespace velocitex {
class DocumentCache {
public:
 explicit DocumentCache(int64_t maximum_bytes=16*1024*1024,int maximum_items=256);
 ~DocumentCache();
 DocumentCache(const DocumentCache&)=delete;
 DocumentCache& operator=(const DocumentCache&)=delete;
 MathDocument* lookup(std::u16string_view source,const MathOptions& options,bool has_macros);
 // Removed entries transfer the cache's retained reference to the caller.
 std::vector<MathDocument*> store(MathDocument* document,bool has_macros=false);
 std::vector<MathDocument*> Clear();
 int Count()const;
 int64_t EstimatedRetainedBytes()const;
 int64_t MaximumBytes()const{return maximumBytes_;}
 int MaximumItems()const{return maximumItems_;}
 static bool ContainsDefinition(std::u16string_view source);
 static int64_t Estimate(const MathDocument& document);
private:
 struct OptionsKey {
  double FontSize{},MaxSize{},MinRuleThickness{},DisplayWidth{};
  int MaxInputLength{},MaxNodes{},MaxNesting{},MaxExpansion{},MaxTokens{},EquationNumber{};
  bool DisplayMode{},ColorIsTextColor{},GlobalGroup{},Leqno{},Fleqn{};
  MathStrictMode StrictMode{};
  explicit OptionsKey(const MathOptions&);
  bool operator==(const OptionsKey&)const=default;
 };
 struct Key {std::u16string Source;OptionsKey Options;bool operator==(const Key&)const=default;};
 struct Hash {size_t operator()(const Key&)const;};
 struct Entry {Key Identity;MathDocument* Document{};int64_t Bytes{};};
 bool Eligible(std::u16string_view source,const MathOptions& options,bool has_macros)const;
 int64_t maximumBytes_,bytes_{};int maximumItems_;
 mutable std::mutex gate_;
 std::list<Entry> recency_;
 std::unordered_map<Key,std::list<Entry>::iterator,Hash> items_;
};
}
