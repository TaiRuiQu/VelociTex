#pragma once
#include "types.hpp"

#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>

namespace velocitex {
struct CancellationToken {
    std::function<void()> Check;
    CancellationToken() = default;
    CancellationToken(std::function<void()> callback) : Check(std::move(callback)) {}
    void ThrowIfCancellationRequested() const {
        if (Check)
            Check();
    }
};
struct LexToken {
    std::shared_ptr<const std::u16string> Source;
    // The characters of the source, cached next to the token.  The parser
    // reads the text of a token very often, and going through the shared
    // pointer costs two dependent loads every time.
    const char16_t* Data = nullptr;
    int Start = 0, Length = 0;
    SourceRange Origin{};
    bool NoExpand = false, TreatAsRelax = false;
    double InternalArrayStretch = 0;
    LexToken() = default;
    LexToken(std::shared_ptr<const std::u16string> source, int start, int length, SourceRange origin,
             bool noExpand = false, bool relax = false, double stretch = 0)
        : Source(std::move(source)), Start(start), Length(length), Origin(origin), NoExpand(noExpand),
          TreatAsRelax(relax), InternalArrayStretch(stretch) {
        Data = Source ? Source->data() : nullptr;
    }
    LexToken(std::u16string source, int start, int length, SourceRange origin, bool noExpand = false,
             bool relax = false, double stretch = 0)
        : LexToken(std::make_shared<const std::u16string>(std::move(source)), start, length, origin, noExpand, relax,
                   stretch) {}
    std::u16string_view Text() const {
        return Data ? std::u16string_view(Data + Start, Length) : std::u16string_view{};
    }
    bool Is(std::u16string_view value) const {
        // The parser compares a token against many command names.  Comparing
        // the length first rejects most of them without touching the shared
        // source string.
        if (static_cast<size_t>(Length) != value.size()) {
            return false;
        }
        // The names are short, so comparing them here is cheaper than the
        // call into memcmp that `operator==` would make.
        if (Length == 0) {
            return true;
        }
        const char16_t* data = Data + Start;
        for (size_t i = 0; i < value.size(); ++i) {
            if (data[i] != value[i]) {
                return false;
            }
        }
        return true;
    }
};
struct MacroDefinition {
    std::vector<LexToken> Body;
    int Arguments = 0;
    bool BuiltIn = false;
    std::vector<std::vector<LexToken>> Delimiters;
    uint8_t Callback = 0;
};
struct MacroUndo {
    std::u16string Name;
    bool Existed = false;
    MacroDefinition Previous;
};
using MacroDefinitions = std::unordered_map<std::u16string, MacroDefinition>;
struct MacroSession {
    std::atomic<bool> Busy{false};
    MacroDefinitions Definitions;
    bool Enter() {
        bool expected = false;
        return Busy.compare_exchange_strong(expected, true);
    }
    void Leave() { Busy.store(false); }
    void CopyTo(MacroDefinitions& destination) const {
        for (const auto& [name, definition] : Definitions)
            destination[name] = definition;
    }
    void Commit(const MacroDefinitions& source) {
        MacroDefinitions next;
        for (const auto& [name, definition] : source)
            if (!definition.BuiltIn)
                next.emplace(name, definition);
        Definitions.swap(next);
    }
};
// The way an argument reports a missing value.  KaTeX uses a different
// message for every kind of argument (`src/Parser.ts`, `parseGroupOfType`
// and `handleSupSubscript`).
enum class ArgumentStyle : uint8_t {
    Normal,    // a macro argument: "Unexpected end of input in a macro argument, expected '}'"
    Primitive, // a primitive argument: "Expected group as argument to '<name>'"
    Script,    // a script argument: "Expected group after '<symbol>'"
};
enum class ParseTaskKind {
    Sequence,
    AttachSequence,
    Argument,
    Atom,
    SetChild,
    Return,
    FinishDelimited,
    MatrixNextCell,
    NormalizeChild,
    FinishClass,
    FinishTag,
    FinishTopTag,
    ReturnInternal,
    GenfracAfterDelimiters,
    AppendPrimeArgument,
    WrapHBoxArgument,
    WrapArgument
};
struct ParseTask {
    ParseTaskKind Kind = ParseTaskKind::Sequence;
    MathFont Font = MathFont::Default;
    int Node = -1, Last = -1, Previous = -1, Slot = 0;
    int Depth = 0, Stop = 0, TextFamily = 0;
    int Limit = 2147483647;
    // The index of the closing brace of the innermost enclosing command
    // argument group.  KaTeX strips the braces of such a group from the
    // tokens of the argument, so the brace ends the argument instead of
    // being a stray closing brace.
    int GroupClose = -1;
    // The index of the token a script argument reports, the `^` or `_`.
    int ArgumentIndex = -1;
    // The name that the message about a missing argument mentions (`\sqrt`
    // for a primitive argument, `^` for a script).  It points into a literal
    // or into the source of a token, both of which outlive the task.
    std::u16string_view ArgumentName;
    bool Closing = false, Text = false;
    bool TextBold = false, TextItalic = false;
    bool PrimitiveArgument = false;
    // True while the parser reads a group that is a command argument.  KaTeX
    // scans such a group as a whole, so it reports a missing argument instead
    // of a missing closing brace.
    bool ArgumentGroup = false;
    // How a missing argument is reported.
    ArgumentStyle Style = ArgumentStyle::Normal;
    ParseTask() = default;
    // Explicit constructor instead of C++20 parenthesized aggregate
    // initialization, which older Clang releases (and therefore the
    // Emscripten toolchain) cannot parse.
    ParseTask(ParseTaskKind kind, int node) : Kind(kind), Node(node) {}
};
struct ParserWorkspace {
    std::vector<LexToken> Pending, Expanded, Arguments;
    std::vector<SemanticNode> Nodes;
    std::vector<ParseTask> Tasks;
    MacroDefinitions Definitions;
    std::vector<MacroUndo> Undo;
    std::vector<int> Scopes, ScopeKinds;
    std::vector<bool> ExpansionModes;
    std::vector<MathWarning> Warnings;
    // The length of the source, which is the position KaTeX reports for the
    // end of the input.
    int SourceLength = 0;
};
// True for the characters that KaTeX treats as white space.
//
// The test lives in the header, because the lexer, the macro expander and the
// parser ask for it for every character they read, and a call across the
// translation units costs more than the test itself.
inline bool IsWhiteSpace(char16_t value) {
    return (value >= 9 && value <= 13) || value == 32 || value == 0x85 || value == 0xa0 || value == 0x1680 ||
           (value >= 0x2000 && value <= 0x200a) || value == 0x2028 || value == 0x2029 || value == 0x202f ||
           value == 0x205f || value == 0x3000;
}
int RuneAt(std::u16string_view text, int position = 0);
int RuneLength(int scalar);
std::u16string FromScalar(int scalar);
// Writes the UTF-16 of a code point into the buffer and returns a view of it.
// The buffer must hold two code units.  A renderer asks for the text of every
// symbol, and a string for each of them asks the library for its storage.
std::u16string_view ToScalar(int scalar, char16_t* buffer);
void Tokenize(const std::u16string& source, std::vector<LexToken>& output, int maxTokens,
              const CancellationToken& cancellationToken, std::optional<SourceRange> origin = {},
              MathStrictMode strictMode = MathStrictMode::Ignore, std::vector<MathWarning>* warnings = nullptr,
              const std::function<MathStrictMode(const MathWarning&)>& strictHandler = {});
void Expand(const std::u16string& source, const MathOptions& options, ParserWorkspace& workspace,
            const CancellationToken& cancellationToken);
int Parse(const std::u16string& source, const MathOptions& options, ParserWorkspace& workspace,
          const CancellationToken& cancellationToken);
} // namespace velocitex
