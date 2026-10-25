#include "generated/data.hpp"
#include "parser.hpp"
#include "state.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>

namespace velocitex {
namespace {
using W = ParserWorkspace;
using O = MathOptions;
using Range = std::pair<int, int>;
[[noreturn]] void Fail(MathDiagnosticCode code, const LexToken& token, const char* message) {
    throw CompileFailure({code, token.Origin, utf16(message)});
}
template <class T>
int Count(const T& c) {
    return (int)c.size();
}
bool OneOf(std::u16string_view value, std::initializer_list<std::u16string_view> candidates) {
    return std::find(candidates.begin(), candidates.end(), value) != candidates.end();
}
// The symbol table holds more than two thousand entries, so the hot paths
// look names up in a hash set instead of scanning the table linearly.  The
// views point into the generated table, which outlives the sets.
const std::unordered_set<std::u16string_view>& SymbolNames() {
    static const auto names = [] {
        std::unordered_set<std::u16string_view> result;
        result.reserve(SymbolData::Symbols.size() * 2);
        for (const auto& symbol : SymbolData::Symbols)
            result.insert(symbol.Name);
        return result;
    }();
    return names;
}
// The math-mode binary and relation symbols, which KaTeX centers a `\dots`
// on when they follow it.
const std::unordered_set<std::u16string_view>& CenteredSymbols() {
    static const auto names = [] {
        std::unordered_set<std::u16string_view> result;
        result.reserve(64);
        for (const auto& symbol : SymbolData::Symbols)
            if (!symbol.TextMode && (symbol.Atom == MathAtom::Bin || symbol.Atom == MathAtom::Rel))
                result.insert(symbol.Name);
        return result;
    }();
    return names;
}
LexToken Pop(W& w) {
    auto token = std::move(w.Pending.back());
    w.Pending.pop_back();
    return token;
}
void Push(W& w, LexToken token, int max) {
    if (Count(w.Pending) >= max)
        Fail(MathDiagnosticCode::TokenLimit, token, "Pending token budget exceeded.");
    w.Pending.push_back(std::move(token));
}

// The token is moved into the stack of the pending tokens, so that the
// reference count of its source is not touched.  The token must not be used
// after it was pushed.
void PushMoved(W& w, LexToken& token, int max) {
    if (Count(w.Pending) >= max)
        Fail(MathDiagnosticCode::TokenLimit, token, "Pending token budget exceeded.");
    w.Pending.push_back(std::move(token));
}
void SkipSpaces(W& w) {
    while (!w.Pending.empty() && !w.Pending.back().Text().empty() && IsWhiteSpace(w.Pending.back().Text()[0]))
        Pop(w);
}
int CountArguments(const std::vector<LexToken>& body) {
    int parameters = 0;
    for (int i = 0; i < Count(body); i++) {
        if (!body[i].Is(u"#"_u16) || i + 1 >= Count(body))
            continue;
        if (body[i + 1].Is(u"#"_u16)) {
            i++;
            continue;
        }
        auto next = body[i + 1].Text();
        if (next.size() == 1 && next[0] >= u'1' && next[0] <= u'9')
            parameters |= 1 << (body[++i].Text()[0] - u'1');
    }
    int count = 0;
    while (count < 9 && (parameters & (1 << count)))
        count++;
    return count;
}
// The built-in macros are immutable, so the table is keyed by views into the
// generated data.  Returning a pointer instead of a copy keeps the token
// vector of every macro definition out of the expansion loop.  The byte-wise
// hash of the standard containers misses the cache on almost every lookup, so
// the names are hashed with a mixing function that reads the code units in 64
// bit chunks and looked up in an open addressing table.
inline uint64_t HashName(std::u16string_view name) {
    uint64_t hash = 0x9e3779b97f4a7c15ull ^ (uint64_t)name.size();
    const char16_t* data = name.data();
    size_t size = name.size();
    while (size >= 4) {
        uint64_t chunk = 0;
        std::memcpy(&chunk, data, sizeof(chunk));
        hash = (hash ^ chunk) * 0x100000001b3ull;
        data += 4;
        size -= 4;
    }
    if (size > 0) {
        uint64_t tail = 0;
        std::memcpy(&tail, data, size * sizeof(char16_t));
        hash = (hash ^ tail) * 0x100000001b3ull;
    }
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 29;
    return hash;
}

struct BuiltInMacro {
    std::u16string_view Name;
    MacroDefinition Definition;
};

class BuiltInMacroTable {
  public:
    BuiltInMacroTable() {
        size_t capacity = 16;
        while (capacity < SymbolData::Macros.size() * 2)
            capacity <<= 1;
        mask_ = capacity - 1;
        slots_.assign(capacity, 0);
        macros_.reserve(SymbolData::Macros.size());
        for (const auto& macro : SymbolData::Macros) {
            std::vector<LexToken> tokens;
            Tokenize(std::u16string(macro.Body), tokens, 65536, {});
            int arity = CountArguments(tokens);
            macros_.push_back({macro.Name, MacroDefinition{std::move(tokens), arity, true}});
            size_t slot = (size_t)HashName(macro.Name) & mask_;
            while (slots_[slot] != 0) {
                // The first macro with a name wins, like `emplace` does.
                if (macros_[slots_[slot] - 1].Name == macro.Name)
                    break;
                slot = (slot + 1) & mask_;
            }
            if (slots_[slot] == 0)
                slots_[slot] = (uint32_t)macros_.size();
        }
    }
    const MacroDefinition* Find(std::u16string_view name) const {
        size_t slot = (size_t)HashName(name) & mask_;
        while (true) {
            uint32_t index = slots_[slot];
            if (index == 0)
                return nullptr;
            const BuiltInMacro& macro = macros_[index - 1];
            if (macro.Name == name)
                return &macro.Definition;
            slot = (slot + 1) & mask_;
        }
    }

  private:
    std::vector<BuiltInMacro> macros_;
    std::vector<uint32_t> slots_;
    size_t mask_ = 0;
};

const BuiltInMacroTable& BuiltIns() {
    static const auto result = [] {
        return BuiltInMacroTable();
    }();
    return result;
}

// The expander dispatches on the name of the token it is looking at.  The
// names the expander handles itself are few, so they live in one table that is
// looked up once per token.  Scanning a dozen short lists per token, which is
// what the expander used to do, costs more than the rest of the expansion.
enum ExpandCommand : uint32_t {
    kCmdDollar = 1u << 0,
    kCmdBraceOpen = 1u << 1,
    kCmdBraceClose = 1u << 2,
    kCmdAmpersand = 1u << 3,
    kCmdMathOpen = 1u << 4,
    kCmdMathClose = 1u << 5,
    kCmdBraKet = 1u << 6,
    kCmdNoExpand = 1u << 7,
    kCmdGlobalLong = 1u << 8,
    kCmdLet = 1u << 9,
    kCmdFirstOfTwo = 1u << 10,
    kCmdChar = 1u << 11,
    kCmdMessage = 1u << 12,
    kCmdDots = 1u << 13,
    kCmdDef = 1u << 14,
    kCmdNewCommand = 1u << 15,
    kCmdGroup = 1u << 16,
    kCmdRowBreak = 1u << 17,
    kCmdRaiseBox = 1u << 18,
    kCmdTextArgument = 1u << 19,
    kCmdBegin = 1u << 20,
    kCmdEnd = 1u << 21,
};

// The characters that the expander treats like commands.
constexpr uint32_t kCmdActiveCharacters = kCmdDollar | kCmdBraceOpen | kCmdBraceClose | kCmdAmpersand;

struct ExpandCommandEntry {
    std::u16string_view Name;
    uint32_t Flags;
};

class ExpandCommandTable {
  public:
    ExpandCommandTable() {
        static constexpr ExpandCommandEntry kEntries[] = {
            {u"$", kCmdDollar},
            {u"{", kCmdBraceOpen},
            {u"}", kCmdBraceClose},
            // The ampersand ends a row and is also an active character.
            {u"&", kCmdAmpersand | kCmdRowBreak},
            {u"\\(", kCmdMathOpen},
            {u"\\)", kCmdMathClose},
            {u"\\bra@ket", kCmdBraKet},
            {u"\\bra@set", kCmdBraKet},
            {u"\\noexpand", kCmdNoExpand},
            {u"\\expandafter", kCmdNoExpand},
            {u"\\global", kCmdGlobalLong},
            {u"\\long", kCmdGlobalLong},
            {u"\\let", kCmdLet},
            {u"\\\\globallet", kCmdLet},
            {u"\\futurelet", kCmdLet},
            {u"\\\\globalfuture", kCmdLet},
            {u"\\@firstoftwo", kCmdFirstOfTwo},
            {u"\\@secondoftwo", kCmdFirstOfTwo},
            {u"\\@ifnextchar", kCmdFirstOfTwo},
            {u"\\TextOrMath", kCmdFirstOfTwo},
            {u"\\char", kCmdChar},
            {u"\\message", kCmdMessage},
            {u"\\errmessage", kCmdMessage},
            {u"\\show", kCmdMessage},
            {u"\\dots", kCmdDots},
            {u"\\dotso", kCmdDots},
            {u"\\dotsc", kCmdDots},
            {u"\\cdots", kCmdDots},
            {u"\\def", kCmdDef},
            {u"\\gdef", kCmdDef},
            {u"\\edef", kCmdDef},
            {u"\\xdef", kCmdDef},
            {u"\\newcommand", kCmdNewCommand},
            {u"\\renewcommand", kCmdNewCommand},
            {u"\\providecommand", kCmdNewCommand},
            {u"\\begingroup", kCmdGroup},
            {u"\\endgroup", kCmdGroup},
            {u"\\\\", kCmdRowBreak},
            {u"\\cr", kCmdRowBreak},
            {u"\\raisebox", kCmdRaiseBox},
            {u"\\colorbox", kCmdRaiseBox},
            {u"\\fcolorbox", kCmdRaiseBox},
            {u"\\text", kCmdTextArgument},
            {u"\\textrm", kCmdTextArgument},
            {u"\\textnormal", kCmdTextArgument},
            {u"\\textbf", kCmdTextArgument},
            {u"\\textmd", kCmdTextArgument},
            {u"\\textit", kCmdTextArgument},
            {u"\\textup", kCmdTextArgument},
            {u"\\textsf", kCmdTextArgument},
            {u"\\texttt", kCmdTextArgument},
            {u"\\emph", kCmdTextArgument},
            {u"\\hbox", kCmdTextArgument},
            {u"\\reflectbox", kCmdTextArgument},
            {u"\\fbox", kCmdTextArgument},
            {u"\\angl", kCmdTextArgument},
            {u"\\begin", kCmdBegin},
            {u"\\end", kCmdEnd},
        };
        size_t capacity = 16;
        while (capacity < sizeof(kEntries) / sizeof(kEntries[0]) * 2)
            capacity <<= 1;
        mask_ = capacity - 1;
        slots_.assign(capacity, 0);
        entries_.assign(std::begin(kEntries), std::end(kEntries));
        for (size_t i = 0; i < entries_.size(); ++i) {
            size_t slot = (size_t)HashName(entries_[i].Name) & mask_;
            while (slots_[slot] != 0)
                slot = (slot + 1) & mask_;
            slots_[slot] = (uint32_t)i + 1;
            // The single characters get a direct table, so that the common
            // token of a formula does not need a hash.
            if (entries_[i].Name.size() == 1) {
                const char16_t c = entries_[i].Name[0];
                if (c < 128) {
                    if (single_[c] == 0)
                        single_[c] = entries_[i].Flags;
                } else if (singleOther_.empty() || singleOther_.back().first != c) {
                    singleOther_.emplace_back(c, entries_[i].Flags);
                }
            }
        }
    }
    uint32_t Flags(std::u16string_view name) const {
        if (name.empty())
            return 0;
        // The table holds the single characters "$", "{", "}" and "&" and the
        // control sequences.  A symbol of a formula is usually a single
        // character, so the single characters are answered from a direct
        // table and a longer name that does not start with a backslash is
        // rejected without a hash.
        if (name.size() == 1) {
            const char16_t c = name[0];
            if (c < 128)
                return single_[c];
            for (const auto& entry : singleOther_)
                if (entry.first == c)
                    return entry.second;
            return 0;
        }
        if (name[0] != u'\\')
            return 0;
        size_t slot = (size_t)HashName(name) & mask_;
        while (true) {
            uint32_t index = slots_[slot];
            if (index == 0)
                return 0;
            if (entries_[index - 1].Name == name)
                return entries_[index - 1].Flags;
            slot = (slot + 1) & mask_;
        }
    }

  private:
    std::vector<ExpandCommandEntry> entries_;
    std::vector<uint32_t> slots_;
    size_t mask_ = 0;
    // The flags of the single character entries, so that a symbol does not
    // need a hash.
    std::array<uint32_t, 128> single_{};
    // The single character entries outside the ASCII range.
    std::vector<std::pair<char16_t, uint32_t>> singleOther_;
};

const ExpandCommandTable& ExpandCommands() {
    static const auto result = ExpandCommandTable();
    return result;
}

// The number of user defined macros up to which the workspace table is
// scanned directly.  A scan of a few definitions is cheaper than a hash
// lookup, and it needs no temporary key.
constexpr size_t UserMacroScanLimit = 8;

// The user defined macros are rare, so their lookup stays in its own function
// and the common path of the expander stays small enough to inline.
// The scratch key of the macro lookup.  It is reused between the lookups, so
// that a lookup does not allocate a key of its own.  It lives in a namespace
// scope object, so that an access to it does not pay for the initialization
// guard of a `thread_local`.
VELOCITEX_REUSED_STATE(std::u16string, g_macro_lookup_key);

const MacroDefinition* FindUserDefinition(W& w, std::u16string_view name) {
    if (w.Definitions.size() <= UserMacroScanLimit) {
        for (const auto& [key, definition] : w.Definitions)
            if (std::u16string_view(key) == name)
                return &definition;
        return nullptr;
    }
    // A larger table is searched through a scratch key, which reuses its
    // buffer instead of allocating one for every lookup.
    auto& key = g_macro_lookup_key;
    if (name.empty())
        key.clear();
    else
        key.assign(name.data(), name.size());
    auto it = w.Definitions.find(key);
    if (it == w.Definitions.end())
        return nullptr;
    return &it->second;
}

const MacroDefinition* FindBuiltIn(std::u16string_view name) {
    // The built-in macros are the control sequences and the single active
    // characters, so a longer name that does not start with a backslash is
    // not in the table.  The check saves the hash for the symbols of a
    // formula, which are most of the tokens.
    if (name.size() > 1 && name[0] != u'\\')
        return nullptr;
    return BuiltIns().Find(name);
}

inline const MacroDefinition* TryGetDefinition(W& w, std::u16string_view name) {
    // User defined macros are rare, so the workspace table is only consulted
    // when the workspace actually holds any.  A macro that the user did not
    // define does not hide the built-in one, so the table of the workspace is
    // searched first and the built-in table second.
    if (!w.Definitions.empty()) {
        if (const MacroDefinition* user = FindUserDefinition(w, name))
            return user;
    }
    if (name.size() == 1 && name[0] < 0x80 && name[0] != u'~')
        return nullptr;
    return FindBuiltIn(name);
}

bool HasDefinition(W& w, std::u16string_view name) {
    return TryGetDefinition(w, name) != nullptr;
}
void ValidateName(std::u16string_view name, SourceRange range) {
    // KaTeX checks the name of a macro with `checkControlSequence`
    // (`src/functions/def.ts`).
    if (name.empty() || OneOf(name, {u"\\", u"{", u"}", u"$", u"&", u"#", u"^", u"_"}))
        throw CompileFailure({MathDiagnosticCode::InvalidMacroDefinition, range,
                              u"Expected a control sequence"});
}
void OpenScope(W& w, const O& options, const LexToken& token, int kind) {
    if ((kind == 1 || kind == 4) && std::count_if(w.ScopeKinds.begin(), w.ScopeKinds.end(),
                                                  [](int k) { return k == 1 || k == 4; }) >= options.MaxNesting)
        Fail(MathDiagnosticCode::NestingLimit, token, "Group nesting budget exceeded.");
    w.Scopes.push_back(Count(w.Undo));
    w.ScopeKinds.push_back(kind);
}
void CloseScope(W& w) {
    int marker = w.Scopes.back();
    w.Scopes.pop_back();
    w.ScopeKinds.pop_back();
    for (int i = Count(w.Undo) - 1; i >= marker; i--) {
        const auto& undo = w.Undo[i];
        if (undo.Existed)
            w.Definitions[undo.Name] = undo.Previous;
        else
            w.Definitions.erase(undo.Name);
    }
    w.Undo.resize(marker);
}
void Assign(W& w, const std::u16string& name, MacroDefinition value, bool global) {
    if (!global && !w.Scopes.empty()) {
        auto it = w.Definitions.find(name);
        w.Undo.push_back({name, it != w.Definitions.end(), it != w.Definitions.end() ? it->second : MacroDefinition{}});
    }
    if (global)
        for (int i = Count(w.Undo) - 1; i >= 0; i--)
            if (w.Undo[i].Name == name) {
                w.Undo.erase(w.Undo.begin() + i);
                for (auto& scope : w.Scopes)
                    if (scope > i)
                        scope--;
            }
    w.Definitions[name] = std::move(value);
}
void RestoreBraketDefinition(W& w, std::u16string_view name) {
    for (int i = Count(w.Undo) - 1; i >= 0; i--) {
        const auto& undo = w.Undo[i];
        if (std::u16string_view(undo.Name) != name)
            continue;
        if (undo.Existed)
            w.Definitions[undo.Name] = undo.Previous;
        else
            w.Definitions.erase(undo.Name);
        return;
    }
}
bool IsParameterReference(const std::vector<LexToken>& body, int index) {
    auto t = body[index].Text();
    if (index == 0 || t.size() != 1 || t[0] < u'1' || t[0] > u'9')
        return false;
    int hashes = 0;
    for (int i = index - 1; i >= 0 && body[i].Is(u"#"_u16); i--)
        hashes++;
    return (hashes & 1) != 0;
}
std::vector<LexToken> Compact(const std::vector<LexToken>& body, SourceRange origin, int maximum) {
    std::u16string text;
    auto space = [](std::u16string_view t) {
        return t.size() > 1 && t[0] == u'\\' &&
               ((t[1] >= u'a' && t[1] <= u'z') || (t[1] >= u'A' && t[1] <= u'Z') || t[1] == u'@');
    };
    for (const auto& token : body) {
        text += token.Text();
        if (space(token.Text()))
            text += u' ';
    }
    auto owned = std::make_shared<const std::u16string>(std::move(text));
    std::vector<LexToken> result;
    result.reserve(body.size());
    int offset = 0;
    for (const auto& token : body) {
        result.emplace_back(owned, offset, token.Length, origin, token.NoExpand, token.TreatAsRelax);
        offset += token.Length;
        if (space(token.Text()))
            offset++;
    }
    if (Count(result) > maximum)
        throw CompileFailure({MathDiagnosticCode::TokenLimit, origin, u"Macro replacement token budget exceeded."});
    return result;
}
void ValidatePersistentBudget(const MacroDefinitions& definitions, const O& options) {
    int64_t units = 0, tokens = 0, names = 0;
    for (const auto& [name, definition] : definitions) {
        if (definition.BuiltIn)
            continue;
        units += name.size();
        names++;
        tokens += definition.Body.size();
        for (const auto& token : definition.Body)
            units += token.Length;
        for (const auto& delimiter : definition.Delimiters) {
            tokens += delimiter.size();
            for (const auto& token : delimiter)
                units += token.Length;
        }
        if (units > options.MaxInputLength)
            throw CompileFailure({MathDiagnosticCode::InputLimit, {}, u"Persistent macro source budget exceeded."});
        if (tokens > options.MaxTokens || names > options.MaxTokens)
            throw CompileFailure({MathDiagnosticCode::TokenLimit, {}, u"Persistent macro token budget exceeded."});
    }
}
Range ConsumeArgument(W& w, const O& options, const LexToken& command, const CancellationToken& cancel) {
    SkipSpaces(w);
    if (w.Pending.empty())
        // KaTeX scans the argument of a macro before it expands the macro
        // (`src/MacroExpander.ts`, `consumeArg`).
        throw CompileFailure({MathDiagnosticCode::MissingArgument, SourceRange{w.SourceLength, 0},
                              u"Unexpected end of input in a macro argument, expected '}'"});
    int start = Count(w.Arguments);
    auto token = Pop(w);
    if (!token.Is(u"{"_u16)) {
        if (token.Is(u"}"_u16))
            throw CompileFailure({MathDiagnosticCode::UnexpectedGroupEnd, token.Origin, u"Extra }"});
        if (Count(w.Arguments) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro argument token budget exceeded.");
        w.Arguments.push_back(token);
        return {start, 1};
    }
    int depth = 1;
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        token = Pop(w);
        if (token.Is(u"{"_u16)) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Macro argument nesting budget exceeded.");
        } else if (token.Is(u"}"_u16) && --depth == 0)
            return {start, Count(w.Arguments) - start};
        if (Count(w.Arguments) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro argument token budget exceeded.");
        w.Arguments.push_back(token);
    }
    throw CompileFailure({MathDiagnosticCode::UnclosedGroup, SourceRange{w.SourceLength, 0},
                          u"Unexpected end of input in a macro argument, expected '}'"});
}
Range ConsumeDelimitedArgument(W& w, const O& options, const LexToken& command, const std::vector<LexToken>& delimiter,
                               const CancellationToken& cancel) {
    int start = Count(w.Arguments), depth = 0, match = 0;
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        auto token = Pop(w);
        if (Count(w.Arguments) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Delimited argument token budget exceeded.");
        w.Arguments.push_back(token);
        if (token.Is(u"{"_u16)) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Delimited argument nesting budget exceeded.");
        } else if (token.Is(u"}"_u16) && --depth < 0)
            Fail(MathDiagnosticCode::UnexpectedGroupEnd, token,
                 "Unexpected closing brace in a delimited macro argument.");
        if ((depth == 0 || (depth == 1 && delimiter[match].Is(u"{"_u16))) && token.Text() == delimiter[match].Text()) {
            if (++match == Count(delimiter)) {
                w.Arguments.resize(w.Arguments.size() - match);
                int length = Count(w.Arguments) - start;
                if (length >= 2 && w.Arguments[start].Is(u"{"_u16) && w.Arguments.back().Is(u"}"_u16))
                    return {start + 1, length - 2};
                return {start, length};
            }
        } else
            match = 0;
    }
    Fail(MathDiagnosticCode::MissingArgument, command, "Missing delimited macro argument terminator.");
}
std::vector<LexToken> ReadDefinitionBody(W& w, const O& options, const LexToken& command,
                                         const CancellationToken& cancel) {
    SkipSpaces(w);
    if (w.Pending.empty() || !Pop(w).Is(u"{"_u16))
        Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected braced macro replacement.");
    std::vector<LexToken> body;
    int depth = 1;
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        auto token = Pop(w);
        if (token.Is(u"{"_u16)) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Macro replacement nesting budget exceeded.");
        } else if (token.Is(u"}"_u16) && --depth == 0)
            return body;
        if (Count(body) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro replacement token budget exceeded.");
        body.push_back(token);
    }
    // KaTeX scans the replacement text of a macro definition as an argument
    // (`src/functions/def.ts`).
    throw CompileFailure({MathDiagnosticCode::InvalidMacroDefinition, SourceRange{w.SourceLength, 0},
                          u"Unexpected end of input in a macro argument, expected '}'"});
}
void ExpandOne(W& w, const O& options, int& expansions, const CancellationToken& cancel) {
    if (w.Pending.empty())
        return;
    auto token = Pop(w);
    if (token.NoExpand) {
        PushMoved(w, token, options.MaxTokens);
        return;
    }
    if (token.Is(u"\\expandafter"_u16)) {
        std::vector<LexToken> deferred;
        while (token.Is(u"\\expandafter"_u16)) {
            if (w.Pending.size() < 2)
                Fail(MathDiagnosticCode::MissingArgument, token, "Expandafter requires two tokens.");
            if (++expansions > options.MaxExpansion)
                throw CompileFailure({MathDiagnosticCode::ExpansionLimit, SourceRange{-1, 0},
                              u"Too many expansions: infinite loop or need to increase maxExpand setting"});
            deferred.push_back(Pop(w));
            token = Pop(w);
        }
        PushMoved(w, token, options.MaxTokens);
        ExpandOne(w, options, expansions, cancel);
        for (int i = Count(deferred) - 1; i >= 0; i--)
            PushMoved(w, deferred[i], options.MaxTokens);
        return;
    }
    if (token.Is(u"\\noexpand"_u16)) {
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::MissingArgument, token, "Noexpand requires a token.");
        auto part = Pop(w);
        part.NoExpand = true;
        PushMoved(w, part, options.MaxTokens);
        return;
    }
    const MacroDefinition* macro = TryGetDefinition(w, token.Text());
    if (macro == nullptr) {
        PushMoved(w, token, options.MaxTokens);
        return;
    }
    if (++expansions > options.MaxExpansion)
        throw CompileFailure({MathDiagnosticCode::ExpansionLimit, SourceRange{-1, 0},
                              u"Too many expansions: infinite loop or need to increase maxExpand setting"});
    std::array<Range, 9> args{};
    if (!macro->Delimiters.empty())
        for (const auto& prefix : macro->Delimiters[0])
            if (w.Pending.empty() || Pop(w).Text() != prefix.Text())
                Fail(MathDiagnosticCode::MissingArgument, token, "Macro invocation does not match its prefix.");
    for (int a = 0; a < macro->Arguments; a++)
        args[a] = !macro->Delimiters.empty() && !macro->Delimiters[a + 1].empty()
                      ? ConsumeDelimitedArgument(w, options, token, macro->Delimiters[a + 1], cancel)
                      : ConsumeArgument(w, options, token, cancel);
    for (int i = Count(macro->Body) - 1; i >= 0; i--) {
        auto part = macro->Body[i];
        if (macro->Arguments > 0 && i > 0 && macro->Body[i - 1].Is(u"#"_u16) && part.Is(u"#"_u16)) {
            part.Origin = token.Origin;
            PushMoved(w, part, options.MaxTokens);
            i--;
        } else if (macro->Arguments > 0 && IsParameterReference(macro->Body, i)) {
            int index = part.Text()[0] - u'1';
            if (index >= macro->Arguments)
                Fail(MathDiagnosticCode::InvalidMacroDefinition, token, "Macro argument outside declared arity.");
            auto [start, length] = args[index];
            for (int j = length - 1; j >= 0; j--)
                Push(w, w.Arguments[start + j], options.MaxTokens);
            i--;
        } else {
            part.Origin = token.Origin;
            PushMoved(w, part, options.MaxTokens);
        }
    }
}
std::vector<LexToken> ExpandDefinition(W& w, const O& options, const std::vector<LexToken>& body, int& expansions,
                                       const CancellationToken& cancel) {
    int boundary = Count(w.Pending);
    for (int i = Count(body) - 1; i >= 0; i--)
        Push(w, body[i], options.MaxTokens);
    std::vector<LexToken> result;
    result.reserve(body.size());
    while (Count(w.Pending) > boundary) {
        cancel.ThrowIfCancellationRequested();
        auto token = w.Pending.back();
        if (!token.NoExpand && (token.Is(u"\\noexpand"_u16) || HasDefinition(w, token.Text()))) {
            ExpandOne(w, options, expansions, cancel);
            continue;
        }
        token = Pop(w);
        token.NoExpand = false;
        token.TreatAsRelax = false;
        result.push_back(token);
        if (Count(result) > options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Expanded definition token budget exceeded.");
    }
    if (Count(result) > options.MaxExpansion - expansions)
        throw CompileFailure({MathDiagnosticCode::ExpansionLimit, body.empty() ? SourceRange{} : body[0].Origin,
                              u"Expanded definition output budget exceeded."});
    expansions += Count(result);
    return result;
}
void Define(W& w, const O& options, bool global, const LexToken& command, int& expansions,
            const CancellationToken& cancel) {
    SkipSpaces(w);
    if (w.Pending.empty())
        // KaTeX reports the missing name of the macro with the same words it
        // uses for a name that is not a control sequence
        // (`src/functions/def.ts`, `checkControlSequence`).
        throw CompileFailure({MathDiagnosticCode::InvalidMacroDefinition,
                              SourceRange{w.SourceLength, 0}, u"Expected a control sequence"});
    auto nameToken = Pop(w);
    std::u16string name(nameToken.Text());
    ValidateName(name, nameToken.Origin);
    int count = 0;
    std::vector<std::vector<LexToken>> delimiters(1);
    std::optional<LexToken> insert;
    while (!w.Pending.empty() && !w.Pending.back().Is(u"{"_u16)) {
        auto hash = Pop(w);
        if (!hash.Is(u"#"_u16)) {
            delimiters[count].push_back(hash);
            continue;
        }
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::InvalidMacroDefinition, hash, "Expected a macro parameter number.");
        if (w.Pending.back().Is(u"{"_u16)) {
            insert = w.Pending.back();
            delimiters[count].push_back(*insert);
            break;
        }
        auto number = Pop(w);
        if (number.Text().size() != 1 || number.Text()[0] != u'1' + count || ++count > 9)
            Fail(MathDiagnosticCode::InvalidMacroDefinition, number, "Expected consecutive parameters #1 through #9.");
        delimiters.emplace_back();
    }
    auto body = ReadDefinitionBody(w, options, command, cancel);
    if (insert)
        body.push_back(*insert);
    if (command.Is(u"\\edef"_u16) || command.Is(u"\\xdef"_u16))
        body = ExpandDefinition(w, options, body, expansions, cancel);
    bool has = std::any_of(delimiters.begin(), delimiters.end(), [](const auto& d) { return !d.empty(); });
    if (has)
        for (auto& d : delimiters)
            d = Compact(d, command.Origin, options.MaxTokens);
    else
        delimiters.clear();
    Assign(w, name, {Compact(body, command.Origin, options.MaxTokens), count, false, std::move(delimiters)}, global);
}
void NewCommand(W& w, const O& options, const LexToken& command, int& expansions, const CancellationToken& cancel) {
    SkipSpaces(w);
    if (w.Pending.empty())
        // KaTeX's `\newcommand` is a macro, so a missing name is an argument
        // that runs out (`src/macros.ts`).
        throw CompileFailure({MathDiagnosticCode::MissingArgument, SourceRange{w.SourceLength, 0},
                              u"Unexpected end of input in a macro argument, expected '}'"});
    auto nameToken = Pop(w);
    bool braced = nameToken.Is(u"{"_u16);
    if (braced) {
        SkipSpaces(w);
        if (w.Pending.empty())
            throw CompileFailure({MathDiagnosticCode::MissingArgument, SourceRange{w.SourceLength, 0},
                                  u"Unexpected end of input in a macro argument, expected '}'"});
        nameToken = Pop(w);
        SkipSpaces(w);
        if (w.Pending.empty() || !Pop(w).Is(u"}"_u16))
            Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected closing brace after macro name.");
    }
    std::u16string name(nameToken.Text());
    ValidateName(name, nameToken.Origin);
    SkipSpaces(w);
    int count = 0;
    if (!w.Pending.empty() && w.Pending.back().Is(u"["_u16)) {
        Pop(w);
        std::u16string arity;
        while (!w.Pending.empty() && !w.Pending.back().Is(u"]"_u16)) {
            if (!w.Pending.back().NoExpand && HasDefinition(w, w.Pending.back().Text())) {
                ExpandOne(w, options, expansions, cancel);
                continue;
            }
            arity += Pop(w).Text();
        }
        auto raw = utf8(arity);
        size_t end = 0;
        bool valid = true;
        try {
            count = std::stoi(raw, &end);
            while (end < raw.size() && std::isspace((unsigned char)raw[end]))
                end++;
            valid = end == raw.size();
        } catch (...) {
            valid = false;
        }
        if (w.Pending.empty() || !Pop(w).Is(u"]"_u16) || !valid || count < 0 || count > 9)
            Fail(MathDiagnosticCode::InvalidMacroDefinition, command,
                 "Macro arity must be a bounded integer from zero through nine.");
    }
    auto body = ReadDefinitionBody(w, options, command, cancel);
    bool exists = HasDefinition(w, name);
    if (!exists)
        for (const auto& known : SymbolData::KnownCommands)
            if (known == name) {
                exists = true;
                break;
            }
    if (!exists && SymbolNames().contains(name))
        exists = true;
    if ((command.Is(u"\\newcommand"_u16) && exists) || (command.Is(u"\\renewcommand"_u16) && !exists))
        Fail(MathDiagnosticCode::InvalidMacroDefinition, nameToken,
             "Macro declaration conflicts with its existing definition.");
    if (!command.Is(u"\\providecommand"_u16) || !exists)
        Assign(w, name, {Compact(body, command.Origin, options.MaxTokens), count}, false);
}
void PushExpansion(W& w, const std::u16string& value, const LexToken& origin, const O& options,
                   const CancellationToken& cancel) {
    std::vector<LexToken> parts;
    Tokenize(value, parts, options.MaxTokens, cancel, origin.Origin, options.StrictMode, &w.Warnings,
             options.StrictHandler);
    for (int i = Count(parts) - 1; i >= 0; i--)
        PushMoved(w, parts[i], options.MaxTokens);
}
// The commands that KaTeX expands even though they are not macros.  The
// command table holds several hundred entries, so the lookup uses a hash set
// instead of scanning the table for every token.
const std::unordered_set<std::u16string_view>& ExpandableCommands() {
    static const auto names = [] {
        std::unordered_set<std::u16string_view> result;
        for (const auto& command : CommandData::Definitions)
            if (!command.Primitive)
                result.insert(command.Name);
        return result;
    }();
    return names;
}
bool IsExpandable(W& w, std::u16string_view name) {
    if (HasDefinition(w, name))
        return true;
    if (OneOf(name, {u"\\noexpand", u"\\expandafter", u"\\@firstoftwo", u"\\@secondoftwo", u"\\@ifnextchar",
                     u"\\TextOrMath", u"\\char", u"\\message", u"\\errmessage", u"\\show", u"\\dots", u"\\dotso",
                     u"\\dotsc", u"\\cdots", u"\\bra@ket", u"\\bra@set", u"\\tag@literal"}))
        return true;
    return ExpandableCommands().count(name) != 0;
}
bool IsDirectSpacing(std::u16string_view name) {
    static const auto names = [] {
        // The commands that the core implements directly instead of expanding
        // the KaTeX macro.  `\liminf` and `\limsup` are *not* part of the
        // list: KaTeX expands them to `\DOTSB\operatorname*{lim\,inf}` and
        // `\DOTSB\operatorname*{lim\,sup}`.
        std::unordered_set<std::u16string_view> result{
            u"\\,", u"\\:", u"\\;", u"\\!", u"\\quad", u"\\qquad", u"\\operatorname",
            u"\\bm", u"\\tag", u"\\notag", u"\\nonumber"};
        return result;
    }();
    return names.count(name) != 0;
}
const auto CellScopeMarker = std::make_shared<const std::u16string>(1, u'\0');
const auto TextModeMarker = std::make_shared<const std::u16string>(2, u'\0');
const auto RestoreModeMarker = std::make_shared<const std::u16string>(3, u'\0');
const auto BraketScopeEndMarker = std::make_shared<const std::u16string>(4, u'\0');
void MarkTextArgument(W& w, const LexToken& command, const O& options, int preceding = 0) {
    int begin = Count(w.Pending) - 1;
    while (begin >= 0 && !w.Pending[begin].Text().empty() && IsWhiteSpace(w.Pending[begin].Text()[0]))
        begin--;
    while (preceding-- > 0) {
        if (begin < 0)
            return;
        if (w.Pending[begin].Is(u"{"_u16)) {
            int depth = 1;
            begin--;
            while (begin >= 0) {
                if (w.Pending[begin].Is(u"{"_u16))
                    depth++;
                else if (w.Pending[begin].Is(u"}"_u16) && --depth == 0) {
                    begin--;
                    break;
                }
                begin--;
            }
        } else
            begin--;
        while (begin >= 0 && !w.Pending[begin].Text().empty() && IsWhiteSpace(w.Pending[begin].Text()[0]))
            begin--;
    }
    if (begin < 0)
        return;
    int end = begin;
    if (w.Pending[begin].Is(u"{"_u16)) {
        int depth = 1;
        for (end = begin - 1; end >= 0; end--) {
            if (w.Pending[end].Is(u"{"_u16))
                depth++;
            else if (w.Pending[end].Is(u"}"_u16) && --depth == 0)
                break;
        }
        if (end < 0)
            return;
    }
    if (Count(w.Pending) > options.MaxTokens - 2)
        Fail(MathDiagnosticCode::TokenLimit, command, "Text argument marker token budget exceeded.");
    w.Pending.insert(w.Pending.begin() + begin + 1, LexToken(TextModeMarker, 0, 0, command.Origin));
    w.Pending.insert(w.Pending.begin() + end, LexToken(RestoreModeMarker, 0, 0, command.Origin));
}
int Digit(std::u16string_view text, int radix) {
    if (text.size() != 1)
        return -1;
    auto c = text[0];
    int v = c >= u'0' && c <= u'9'   ? c - u'0'
            : c >= u'a' && c <= u'f' ? c - u'a' + 10
            : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                     : -1;
    return v >= 0 && v < radix ? v : -1;
}
int ReadCharacter(W& w, const LexToken& command) {
    if (w.Pending.empty())
        Fail(MathDiagnosticCode::MissingArgument, command, "Character command requires a code.");
    auto token = Pop(w);
    if (token.Is(u"`"_u16)) {
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::MissingArgument, command, "Backtick character command requires a character.");
        token = Pop(w);
        auto text = token.Text();
        if (!text.empty() && text[0] == u'\\')
            text.remove_prefix(1);
        if (text.empty())
            Fail(MathDiagnosticCode::UnexpectedToken, token, "Expected a character after backtick.");
        return RuneAt(text);
    }
    int radix = token.Is(u"'"_u16) ? 8 : token.Is(u"\""_u16) ? 16 : 10;
    if (radix != 10) {
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::MissingArgument, command, "Character command requires digits.");
        token = Pop(w);
    }
    int value = Digit(token.Text(), radix);
    if (value < 0)
        Fail(MathDiagnosticCode::UnexpectedToken, token, "Invalid character-code digit.");
    while (!w.Pending.empty()) {
        int next = Digit(w.Pending.back().Text(), radix);
        if (next < 0)
            break;
        if (value > (0x10ffff - next) / radix)
            Fail(MathDiagnosticCode::UnexpectedToken, command, "Character code is outside Unicode.");
        value = value * radix + next;
        Pop(w);
    }
    if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        Fail(MathDiagnosticCode::UnexpectedToken, command, "Character code is not a Unicode scalar.");
    return value;
}
} // namespace

void Expand(const std::u16string& source, const MathOptions& options, ParserWorkspace& w,
            const CancellationToken& cancel) {
    w.SourceLength = (int)source.size();
    if (options.Session)
        options.Session->CopyTo(w.Definitions);
    ValidatePersistentBudget(w.Definitions, options);
    int64_t units = 0, tokens = 0;
    for (const auto& [name, body] : options.Macros) {
        cancel.ThrowIfCancellationRequested();
        units += name.size() + body.size();
        if (units > options.MaxInputLength)
            throw CompileFailure(
                {MathDiagnosticCode::InputLimit, {}, u"Aggregate option macro input budget exceeded."});
        ValidateName(name, {});
        std::vector<LexToken> parts;
        Tokenize(body, parts, options.MaxTokens, cancel, SourceRange{});
        tokens += parts.size();
        if (tokens > options.MaxTokens)
            throw CompileFailure(
                {MathDiagnosticCode::TokenLimit, {}, u"Aggregate option macro token budget exceeded."});
        w.Definitions[name] = {Compact(parts, {}, options.MaxTokens), CountArguments(parts)};
    }
    ValidatePersistentBudget(w.Definitions, options);
    Tokenize(source, w.Pending, options.MaxTokens, cancel, {}, options.StrictMode, &w.Warnings, options.StrictHandler);
    std::reverse(w.Pending.begin(), w.Pending.end());
    if (!options.GlobalGroup) {
        // The root scope carries no text of its own: the token only has to be
        // one that is not a marker, so a shared empty string serves as its
        // source.  A copy of the whole source would be made for nothing.
        static const auto root_scope_source = std::make_shared<const std::u16string>();
        OpenScope(w, options, LexToken(root_scope_source, 0, 0, {}), 6);
    }
    if (options.ColorIsTextColor)
        Assign(w, u"\\color", {{LexToken(u"\\textcolor", 0, 10, {})}, 0}, false);
    bool textMode = false;
    std::vector<int> mathSwitches;
    int expansions = 0;
    std::array<Range, 9> arguments{};
    auto expansionBudget = [&](const LexToken& t) {
        if (++expansions > options.MaxExpansion)
            throw CompileFailure({MathDiagnosticCode::ExpansionLimit, SourceRange{-1, 0},
                              u"Too many expansions: infinite loop or need to increase maxExpand setting"});
    };
    auto emit = [&](LexToken&& t) {
        if (Count(w.Expanded) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, t, "Expanded token budget exceeded.");
        w.Expanded.push_back(std::move(t));
    };
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        auto token = Pop(w);
        if (token.Source == CellScopeMarker && token.Length == 0) {
            OpenScope(w, options, token, 3);
            continue;
        }
        if (token.Source == BraketScopeEndMarker && token.Length == 0) {
            CloseScope(w);
            continue;
        }
        if (token.Length == 0 && token.Source == TextModeMarker) {
            w.ExpansionModes.push_back(textMode);
            textMode = true;
            continue;
        }
        if (token.Length == 0 && token.Source == RestoreModeMarker) {
            textMode = w.ExpansionModes.back();
            w.ExpansionModes.pop_back();
            continue;
        }
        const std::u16string_view text = token.Text();
        // The names the expander handles itself are looked up once, so that the
        // dispatch below costs a single hash instead of a dozen scans.
        const uint32_t flags = ExpandCommands().Flags(text);
        const MacroDefinition* macro = (!token.NoExpand && !text.empty()) ? TryGetDefinition(w, text) : nullptr;
        bool isMacro = macro != nullptr;
        if (token.Length != 0 && !isMacro && !token.TreatAsRelax && text[0] != u'\\' &&
            !(flags & kCmdActiveCharacters)) {
            emit(std::move(token));
            continue;
        }
        if ((flags & kCmdDollar) && (textMode || (!mathSwitches.empty() && mathSwitches.back() == 1))) {
            if (textMode) {
                mathSwitches.push_back(1);
                textMode = false;
            } else {
                mathSwitches.pop_back();
                textMode = true;
            }
        } else if ((flags & kCmdMathOpen) && textMode) {
            mathSwitches.push_back(2);
            textMode = false;
        } else if ((flags & kCmdMathClose) && !mathSwitches.empty() && mathSwitches.back() == 2) {
            mathSwitches.pop_back();
            textMode = true;
        }
        bool customOverride = isMacro && !macro->BuiltIn;
        if (isMacro && macro->Callback >= 1 && macro->Callback <= 4) {
            expansionBudget(token);
            bool doubled = macro->Callback == 2 || macro->Callback == 4;
            if (!doubled && !macro->Delimiters.empty() && !macro->Delimiters[0].empty() && !w.Pending.empty() &&
                w.Pending.back().Is(u"|"_u16)) {
                Pop(w);
                doubled = true;
            }
            // The expansion is copied before the definition is restored:
            // restoring it replaces the entry in the definition map and would
            // leave `macro` dangling.
            const std::vector<LexToken> chosen =
                doubled && !macro->Delimiters.empty() ? macro->Delimiters[0] : macro->Body;
            if (macro->Callback == 3 || macro->Callback == 4) {
                RestoreBraketDefinition(w, u"|"_u16);
                RestoreBraketDefinition(w, u"\\|"_u16);
            }
            for (int i = Count(chosen) - 1; i >= 0; i--) {
                auto p = chosen[i];
                p.Origin = token.Origin;
                PushMoved(w, p, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride && (flags & kCmdBraKet)) {
            expansionBudget(token);
            std::array<Range, 5> ranges;
            for (auto& r : ranges)
                r = ConsumeArgument(w, options, token, cancel);
            auto part = [&](int i) {
                auto [start, length] = ranges[i];
                return std::vector<LexToken>(w.Arguments.begin() + start, w.Arguments.begin() + start + length);
            };
            auto left = part(0), middle = part(1), middleDouble = part(2), right = part(3), body = part(4);
            OpenScope(w, options, token, 5);
            bool one = token.Is(u"\\bra@set"_u16);
            Assign(w, u"|", {middle, 0, false, {middleDouble}, uint8_t(one ? 3 : 1)}, false);
            if (!middleDouble.empty())
                Assign(w, u"\\|", {middleDouble, 0, false, {middleDouble}, uint8_t(one ? 4 : 2)}, false);
            Push(w, LexToken(BraketScopeEndMarker, 0, 0, token.Origin), options.MaxTokens);
            for (const auto* parts : {&right, &body, &left})
                for (int i = Count(*parts) - 1; i >= 0; i--)
                    Push(w, (*parts)[i], options.MaxTokens);
            continue;
        }
        if (token.TreatAsRelax) {
            PushExpansion(w, u"\\relax", token, options, cancel);
            continue;
        }
        if (!token.NoExpand && !customOverride && (flags & kCmdNoExpand)) {
            expansionBudget(token);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Expansion control requires a following token.");
            auto deferred = Pop(w);
            if (token.Is(u"\\noexpand"_u16)) {
                deferred.NoExpand = true;
                deferred.TreatAsRelax = IsExpandable(w, deferred.Text());
                PushMoved(w, deferred, options.MaxTokens);
            } else {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Expandafter requires two following tokens.");
                ExpandOne(w, options, expansions, cancel);
                Push(w, deferred, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride && (flags & kCmdGlobalLong)) {
            SkipSpaces(w);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Macro prefix requires an assignment.");
            auto next = Pop(w);
            std::u16string value;
            if (token.Is(u"\\long"_u16)) {
                // KaTeX ignores `\long`, so only a macro prefix may follow it
                // (`src/functions/def.ts`, `globalMap`).
                if (!OneOf(next.Text(), {u"\\global", u"\\long", u"\\def", u"\\gdef", u"\\edef", u"\\xdef", u"\\let",
                                         u"\\futurelet"}))
                    Fail(MathDiagnosticCode::UnexpectedToken, next, "Invalid token after macro prefix");
                value = next.Text();
            } else {
                if (OneOf(next.Text(), {u"\\def", u"\\gdef"}))
                    value = u"\\gdef";
                else if (OneOf(next.Text(), {u"\\edef", u"\\xdef"}))
                    value = u"\\xdef";
                else if (next.Is(u"\\let"_u16))
                    value = u"\\\\globallet";
                else if (next.Is(u"\\futurelet"_u16))
                    value = u"\\\\globalfuture";
                else if (next.Is(u"\\long"_u16))
                    // KaTeX ignores the `\long` after a `\global` as well.
                    value = u"\\long";
                else if (next.Is(u"\\global"_u16))
                    value = u"\\global";
            }
            if (value.empty())
                // KaTeX names the token that does not follow the prefix
                // (`src/functions/def.ts`).
                Fail(MathDiagnosticCode::UnexpectedToken, next, "Invalid token after macro prefix");
            Push(w, LexToken(value, 0, Count(value), next.Origin), options.MaxTokens);
            continue;
        }
        if (!customOverride && (flags & kCmdLet)) {
            SkipSpaces(w);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::InvalidMacroDefinition, token, "Let requires a control sequence.");
            auto target = Pop(w);
            std::u16string name(target.Text());
            ValidateName(name, target.Origin);
            SkipSpaces(w);
            std::optional<LexToken> middle;
            if (token.Is(u"\\futurelet"_u16) || token.Is(u"\\\\globalfuture"_u16)) {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Futurelet requires two tokens.");
                middle = Pop(w);
            } else if (!w.Pending.empty() && w.Pending.back().Is(u"="_u16)) {
                Pop(w);
                SkipSpaces(w);
            }
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Let requires a right-hand token.");
            auto rhs = Pop(w);
            MacroDefinition value;
            if (const MacroDefinition* found = TryGetDefinition(w, rhs.Text())) {
                value = *found;
                value.BuiltIn = false;
            } else {
                auto p = rhs;
                p.NoExpand = true;
                value = {{p}, 0};
            }
            Assign(w, name, value, token.Is(u"\\\\globallet"_u16) || token.Is(u"\\\\globalfuture"_u16));
            if (middle) {
                Push(w, rhs, options.MaxTokens);
                Push(w, *middle, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride && (flags & kCmdFirstOfTwo)) {
            expansionBudget(token);
            int count = token.Is(u"\\@ifnextchar"_u16) ? 3 : 2;
            for (int a = 0; a < count; a++)
                arguments[a] = ConsumeArgument(w, options, token, cancel);
            int selected = token.Is(u"\\@secondoftwo"_u16) || (token.Is(u"\\TextOrMath"_u16) && !textMode) ? 1 : 0;
            if (count == 3) {
                SkipSpaces(w);
                auto first = arguments[0];
                selected = first.second == 1 && !w.Pending.empty() &&
                                   w.Pending.back().Text() == w.Arguments[first.first].Text()
                               ? 1
                               : 2;
            }
            auto [start, length] = arguments[selected];
            for (int i = length - 1; i >= 0; i--)
                Push(w, w.Arguments[start + i], options.MaxTokens);
            continue;
        }
        if (!customOverride && (flags & kCmdChar)) {
            expansionBudget(token);
            PushExpansion(w, u"\\@char{" + utf16(std::to_string(ReadCharacter(w, token))) + u"}", token, options,
                          cancel);
            continue;
        }
        if (!customOverride && (flags & kCmdMessage)) {
            expansionBudget(token);
            if (token.Is(u"\\show"_u16)) {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Show requires a token.");
                Pop(w);
            } else
                ConsumeArgument(w, options, token, cancel);
            continue;
        }
        if (!customOverride && (flags & kCmdDots)) {
            expansionBudget(token);
            if (token.Is(u"\\dots"_u16) && !w.Pending.empty())
                ExpandOne(w, options, expansions, cancel);
            auto next = w.Pending.empty() ? std::u16string_view{} : w.Pending.back().Text();
            if (token.Is(u"\\dots"_u16)) {
                bool centered =
                    OneOf(next,
                          {u"+",           u"=",          u"<",        u">",         u"-",           u"*",
                           u":",           u"\\DOTSB",    u"\\coprod", u"\\bigvee",  u"\\bigwedge",  u"\\biguplus",
                           u"\\bigcap",    u"\\bigcup",   u"\\prod",   u"\\sum",     u"\\bigotimes", u"\\bigoplus",
                           u"\\bigodot",   u"\\bigsqcup", u"\\And",    u"\\mathbin", u"\\mathrel",   u"\\xrightarrow",
                           u"\\xleftarrow"}) ||
                    next.starts_with(u"\\not"_u16);
                if (!centered && CenteredSymbols().contains(next))
                    centered = true;
                bool integral =
                    OneOf(next, {u"\\DOTSI", u"\\int", u"\\oint", u"\\iint", u"\\iiint", u"\\iiiint", u"\\idotsint"});
                // KaTeX's `dotsByToken` maps a following comma to `\dotsc`,
                // which does not insert the extra thin space.
                PushExpansion(w,
                              integral             ? u"\\dotsi"
                              : next == u","_u16        ? u"\\dotsc"
                              : centered           ? u"\\dotsb"
                              : next == u"\\DOTSX"_u16 ? u"\\dotsx"
                                                   : u"\\dotso",
                              token, options, cancel);
                continue;
            }
            bool extra = OneOf(next, {u")", u"]", u"\\rbrack", u"\\}", u"\\rbrace", u"\\rangle", u"\\rceil",
                                      u"\\rfloor", u"\\rgroup", u"\\rmoustache", u"\\right", u"\\bigr", u"\\biggr",
                                      u"\\Bigr", u"\\Biggr", u"$", u";", u".", u","});
            if (token.Is(u"\\dotsc"_u16) && next == u","_u16)
                extra = false;
            PushExpansion(w, std::u16string(token.Is(u"\\cdots"_u16) ? u"\\@cdots" : u"\\ldots") + (extra ? u"\\," : u""),
                          token, options, cancel);
            continue;
        }
        if (!customOverride && (flags & kCmdDef)) {
            Define(w, options, token.Is(u"\\gdef"_u16) || token.Is(u"\\xdef"_u16), token, expansions, cancel);
            continue;
        }
        if (!customOverride && (flags & kCmdNewCommand)) {
            NewCommand(w, options, token, expansions, cancel);
            continue;
        }
        if (!customOverride && (flags & kCmdGroup)) {
            if (token.Is(u"\\begingroup"_u16))
                OpenScope(w, options, token, 4);
            else {
                if (w.ScopeKinds.empty() || w.ScopeKinds.back() != 4)
                    Fail(MathDiagnosticCode::UnexpectedGroupEnd, token, "Unmatched endgroup.");
                CloseScope(w);
            }
        }
        if (flags & kCmdBraceOpen)
            OpenScope(w, options, token, 1);
        else if ((flags & kCmdBraceClose) && !w.Scopes.empty() && w.ScopeKinds.back() == 1)
            CloseScope(w);
        if (isMacro && !(macro->BuiltIn && IsDirectSpacing(token.Text()))) {
            expansionBudget(token);
            if (!macro->Delimiters.empty())
                for (const auto& prefix : macro->Delimiters[0])
                    if (w.Pending.empty() || Pop(w).Text() != prefix.Text())
                        Fail(MathDiagnosticCode::MissingArgument, token,
                             "Macro invocation does not match its parameter prefix.");
            for (int a = 0; a < macro->Arguments; a++)
                arguments[a] = !macro->Delimiters.empty() && !macro->Delimiters[a + 1].empty()
                                   ? ConsumeDelimitedArgument(w, options, token, macro->Delimiters[a + 1], cancel)
                                   : ConsumeArgument(w, options, token, cancel);
            for (int i = Count(macro->Body) - 1; i >= 0; i--) {
                auto part = macro->Body[i];
                if (macro->Arguments > 0 && i > 0 && macro->Body[i - 1].Is(u"#"_u16) && part.Is(u"#"_u16)) {
                    part.Origin = token.Origin;
                    Push(w, part, options.MaxTokens);
                    i--;
                } else if (macro->Arguments > 0 && IsParameterReference(macro->Body, i)) {
                    int index = part.Text()[0] - u'1';
                    if (index >= macro->Arguments)
                        Fail(MathDiagnosticCode::InvalidMacroDefinition, token,
                             "Macro parameter is outside its declared arity.");
                    auto [start, length] = arguments[index];
                    for (int j = length - 1; j >= 0; j--)
                        Push(w, w.Arguments[start + j], options.MaxTokens);
                    i--;
                } else {
                    part.Origin = token.Origin;
                    Push(w, part, options.MaxTokens);
                }
            }
            continue;
        }
        if (flags & kCmdBegin) {
            if (const MacroDefinition* stretchDefinition = TryGetDefinition(w, u"\\arraystretch"_u16)) {
                auto expanded = ExpandDefinition(w, options, stretchDefinition->Body, expansions, cancel);
                std::u16string value;
                for (const auto& p : expanded)
                    value += p.Text();
                double stretch = NAN;
                auto raw = utf8(value);
                char* end;
                double v = std::strtod(raw.c_str(), &end);
                while (*end && std::isspace((unsigned char)*end))
                    end++;
                if (end != raw.c_str() && !*end)
                    stretch = v;
                auto marker = token;
                marker.Length = 0;
                marker.InternalArrayStretch = stretch == 0 ? NAN : stretch;
                // The text of the stretch factor, so that the parser can
                // report the value that KaTeX rejects.
                marker.Source = std::make_shared<const std::u16string>(value);
                // The token caches the characters of its source.
                marker.Data = marker.Source->data();
                marker.Start = 0;
                marker.Length = (int)value.size();
                emit(std::move(marker));
            }
            OpenScope(w, options, token, 2);
            int depth = 0;
            for (int i = Count(w.Pending) - 1; i >= 0; i--) {
                if (w.Pending[i].Is(u"{"_u16))
                    depth++;
                else if (w.Pending[i].Is(u"}"_u16) && --depth == 0) {
                    if (Count(w.Pending) >= options.MaxTokens)
                        Fail(MathDiagnosticCode::TokenLimit, token, "Pending token budget exceeded.");
                    w.Pending.insert(w.Pending.begin() + i, LexToken(CellScopeMarker, 0, 0, token.Origin));
                    break;
                }
            }
        } else if (flags & kCmdEnd) {
            if (!w.ScopeKinds.empty() && w.ScopeKinds.back() == 3)
                CloseScope(w);
            if (!w.ScopeKinds.empty() && w.ScopeKinds.back() == 2)
                CloseScope(w);
        } else if ((flags & kCmdRowBreak) && !w.ScopeKinds.empty() && w.ScopeKinds.back() == 3) {
            CloseScope(w);
            OpenScope(w, options, token, 3);
        }
        if (flags & kCmdTextArgument)
            MarkTextArgument(w, token, options);
        else if (flags & kCmdRaiseBox)
            MarkTextArgument(w, token, options, token.Is(u"\\fcolorbox"_u16) ? 2 : 1);
        emit(std::move(token));
    }
    if (std::find(w.ScopeKinds.begin(), w.ScopeKinds.end(), 4) != w.ScopeKinds.end())
        throw CompileFailure({MathDiagnosticCode::UnclosedGroup, {Count(source), 0}, u"Unclosed begingroup."});
    if (w.ScopeKinds.size() == 1 && w.ScopeKinds[0] == 6)
        CloseScope(w);
    ValidatePersistentBudget(w.Definitions, options);
}
} // namespace velocitex
