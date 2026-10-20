#pragma once
#include "types.hpp"
#include <memory>
#include <optional>
#include <unordered_map>
#include <functional>

namespace velocitex {
struct CancellationToken {
    std::function<void()> Check;
    CancellationToken()=default;
    CancellationToken(std::function<void()> callback):Check(std::move(callback)){}
    void ThrowIfCancellationRequested() const {if(Check)Check();}
};
struct LexToken {
    std::shared_ptr<const std::u16string> Source;
    int Start=0, Length=0;
    SourceRange Origin{};
    bool NoExpand=false, TreatAsRelax=false;
    double InternalArrayStretch=0;
    LexToken() = default;
    LexToken(std::shared_ptr<const std::u16string> source,int start,int length,SourceRange origin,bool noExpand=false,bool relax=false,double stretch=0)
      : Source(std::move(source)),Start(start),Length(length),Origin(origin),NoExpand(noExpand),TreatAsRelax(relax),InternalArrayStretch(stretch) {}
    LexToken(std::u16string source,int start,int length,SourceRange origin,bool noExpand=false,bool relax=false,double stretch=0)
      : LexToken(std::make_shared<const std::u16string>(std::move(source)),start,length,origin,noExpand,relax,stretch) {}
    std::u16string_view Text() const { return Source ? std::u16string_view(*Source).substr(Start,Length) : std::u16string_view{}; }
    bool Is(std::u16string_view value) const { return Text()==value; }
};
struct MacroDefinition {
    std::vector<LexToken> Body;
    int Arguments=0;
    bool BuiltIn=false;
    std::vector<std::vector<LexToken>> Delimiters;
    uint8_t Callback=0;
};
struct MacroUndo { std::u16string Name; bool Existed=false; MacroDefinition Previous; };
using MacroDefinitions = std::unordered_map<std::u16string,MacroDefinition>;
struct MacroSession {
    std::atomic<bool> Busy{false};
    MacroDefinitions Definitions;
    bool Enter() {bool expected=false;return Busy.compare_exchange_strong(expected,true);}
    void Leave() {Busy.store(false);}
    void CopyTo(MacroDefinitions& destination) const {for(const auto& [name,definition]:Definitions)destination[name]=definition;}
    void Commit(const MacroDefinitions& source) {MacroDefinitions next;for(const auto& [name,definition]:source)if(!definition.BuiltIn)next.emplace(name,definition);Definitions.swap(next);}
};
enum class ParseTaskKind { Sequence, AttachSequence, Argument, Atom, SetChild, Return, FinishDelimited, MatrixNextCell, NormalizeChild, FinishClass, FinishTag, FinishTopTag, ReturnInternal, GenfracAfterDelimiters, AppendPrimeArgument, WrapHBoxArgument };
struct ParseTask {
    ParseTaskKind Kind=ParseTaskKind::Sequence;
    int Node=-1,Last=-1,Previous=-1,Slot=0;
    bool Closing=false,Text=false;
    MathFont Font=MathFont::Default;
    int Depth=0,Stop=0,TextFamily=0;
    bool TextBold=false,TextItalic=false;
    int Limit=2147483647;
    bool PrimitiveArgument=false;
};
struct ParserWorkspace {
    std::vector<LexToken> Pending,Expanded,Arguments;
    std::vector<SemanticNode> Nodes;
    std::vector<ParseTask> Tasks;
    MacroDefinitions Definitions;
    std::vector<MacroUndo> Undo;
    std::vector<int> Scopes,ScopeKinds;
    std::vector<bool> ExpansionModes;
    std::vector<MathWarning> Warnings;
};
bool IsWhiteSpace(char16_t value);
int RuneAt(std::u16string_view text,int position=0);
int RuneLength(int scalar);
std::u16string FromScalar(int scalar);
void Tokenize(const std::u16string& source,std::vector<LexToken>& output,int maxTokens,const CancellationToken& cancellationToken,std::optional<SourceRange> origin={},MathStrictMode strictMode=MathStrictMode::Ignore,std::vector<MathWarning>* warnings=nullptr,const std::function<MathStrictMode(const MathWarning&)>& strictHandler={});
void Expand(const std::u16string& source,const MathOptions& options,ParserWorkspace& workspace,const CancellationToken& cancellationToken);
int Parse(const std::u16string& source,const MathOptions& options,ParserWorkspace& workspace,const CancellationToken& cancellationToken);
}
