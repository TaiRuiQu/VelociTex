#include "generated/data.hpp"
#include "parser.hpp"

#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>

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
void SkipSpaces(W& w) {
    while (!w.Pending.empty() && !w.Pending.back().Text().empty() && IsWhiteSpace(w.Pending.back().Text()[0]))
        Pop(w);
}
int CountArguments(const std::vector<LexToken>& body) {
    int parameters = 0;
    for (int i = 0; i < Count(body); i++) {
        if (!body[i].Is(u"#") || i + 1 >= Count(body))
            continue;
        if (body[i + 1].Is(u"#")) {
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
const MacroDefinitions& BuiltIns() {
    static const auto result = [] {
        MacroDefinitions m;
        for (const auto& [name, body] : SymbolData::Macros) {
            std::vector<LexToken> tokens;
            Tokenize(std::u16string(body), tokens, 65536, {});
            int arity = CountArguments(tokens);
            m.emplace(name, MacroDefinition{std::move(tokens), arity, true});
        }
        return m;
    }();
    return result;
}
bool TryGetDefinition(W& w, std::u16string_view name, MacroDefinition& macro) {
    auto key = std::u16string(name);
    auto it = w.Definitions.find(key);
    if (it != w.Definitions.end()) {
        macro = it->second;
        return true;
    }
    if (name.size() == 1 && name[0] < 0x80 && name[0] != u'~')
        return false;
    auto found = BuiltIns().find(key);
    if (found == BuiltIns().end())
        return false;
    macro = found->second;
    return true;
}
bool HasDefinition(W& w, std::u16string_view name) {
    MacroDefinition m;
    return TryGetDefinition(w, name, m);
}
void ValidateName(std::u16string_view name, SourceRange range) {
    if (name.empty() || OneOf(name, {u"\\", u"{", u"}", u"$", u"&", u"#", u"^", u"_"}))
        throw CompileFailure({MathDiagnosticCode::InvalidMacroDefinition, range,
                              u"Expected a valid control sequence or active character."});
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
void RestoreBraketDefinition(W& w, const std::u16string& name) {
    for (int i = Count(w.Undo) - 1; i >= 0; i--) {
        const auto& undo = w.Undo[i];
        if (undo.Name != name)
            continue;
        if (undo.Existed)
            w.Definitions[name] = undo.Previous;
        else
            w.Definitions.erase(name);
        return;
    }
}
bool IsParameterReference(const std::vector<LexToken>& body, int index) {
    auto t = body[index].Text();
    if (index == 0 || t.size() != 1 || t[0] < u'1' || t[0] > u'9')
        return false;
    int hashes = 0;
    for (int i = index - 1; i >= 0 && body[i].Is(u"#"); i--)
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
        Fail(MathDiagnosticCode::MissingArgument, command, "Missing macro argument.");
    int start = Count(w.Arguments);
    auto token = Pop(w);
    if (!token.Is(u"{")) {
        if (token.Is(u"}"))
            Fail(MathDiagnosticCode::MissingArgument, token, "Missing macro argument.");
        if (Count(w.Arguments) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro argument token budget exceeded.");
        w.Arguments.push_back(token);
        return {start, 1};
    }
    int depth = 1;
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        token = Pop(w);
        if (token.Is(u"{")) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Macro argument nesting budget exceeded.");
        } else if (token.Is(u"}") && --depth == 0)
            return {start, Count(w.Arguments) - start};
        if (Count(w.Arguments) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro argument token budget exceeded.");
        w.Arguments.push_back(token);
    }
    Fail(MathDiagnosticCode::UnclosedGroup, command, "Unclosed macro argument.");
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
        if (token.Is(u"{")) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Delimited argument nesting budget exceeded.");
        } else if (token.Is(u"}") && --depth < 0)
            Fail(MathDiagnosticCode::UnexpectedGroupEnd, token,
                 "Unexpected closing brace in a delimited macro argument.");
        if ((depth == 0 || (depth == 1 && delimiter[match].Is(u"{"))) && token.Text() == delimiter[match].Text()) {
            if (++match == Count(delimiter)) {
                w.Arguments.resize(w.Arguments.size() - match);
                int length = Count(w.Arguments) - start;
                if (length >= 2 && w.Arguments[start].Is(u"{") && w.Arguments.back().Is(u"}"))
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
    if (w.Pending.empty() || !Pop(w).Is(u"{"))
        Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected braced macro replacement.");
    std::vector<LexToken> body;
    int depth = 1;
    while (!w.Pending.empty()) {
        cancel.ThrowIfCancellationRequested();
        auto token = Pop(w);
        if (token.Is(u"{")) {
            if (++depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, token, "Macro replacement nesting budget exceeded.");
        } else if (token.Is(u"}") && --depth == 0)
            return body;
        if (Count(body) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, token, "Macro replacement token budget exceeded.");
        body.push_back(token);
    }
    Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Unclosed macro replacement.");
}
void ExpandOne(W& w, const O& options, int& expansions, const CancellationToken& cancel) {
    if (w.Pending.empty())
        return;
    auto token = Pop(w);
    if (token.NoExpand) {
        Push(w, token, options.MaxTokens);
        return;
    }
    if (token.Is(u"\\expandafter")) {
        std::vector<LexToken> deferred;
        while (token.Is(u"\\expandafter")) {
            if (w.Pending.size() < 2)
                Fail(MathDiagnosticCode::MissingArgument, token, "Expandafter requires two tokens.");
            if (++expansions > options.MaxExpansion)
                Fail(MathDiagnosticCode::ExpansionLimit, token, "Macro expansion budget exceeded.");
            deferred.push_back(Pop(w));
            token = Pop(w);
        }
        Push(w, token, options.MaxTokens);
        ExpandOne(w, options, expansions, cancel);
        for (int i = Count(deferred) - 1; i >= 0; i--)
            Push(w, deferred[i], options.MaxTokens);
        return;
    }
    if (token.Is(u"\\noexpand")) {
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::MissingArgument, token, "Noexpand requires a token.");
        auto part = Pop(w);
        part.NoExpand = true;
        Push(w, part, options.MaxTokens);
        return;
    }
    MacroDefinition macro;
    if (!TryGetDefinition(w, token.Text(), macro)) {
        Push(w, token, options.MaxTokens);
        return;
    }
    if (++expansions > options.MaxExpansion)
        Fail(MathDiagnosticCode::ExpansionLimit, token, "Macro expansion budget exceeded.");
    std::array<Range, 9> args{};
    if (!macro.Delimiters.empty())
        for (const auto& prefix : macro.Delimiters[0])
            if (w.Pending.empty() || Pop(w).Text() != prefix.Text())
                Fail(MathDiagnosticCode::MissingArgument, token, "Macro invocation does not match its prefix.");
    for (int a = 0; a < macro.Arguments; a++)
        args[a] = !macro.Delimiters.empty() && !macro.Delimiters[a + 1].empty()
                      ? ConsumeDelimitedArgument(w, options, token, macro.Delimiters[a + 1], cancel)
                      : ConsumeArgument(w, options, token, cancel);
    for (int i = Count(macro.Body) - 1; i >= 0; i--) {
        auto part = macro.Body[i];
        if (macro.Arguments > 0 && i > 0 && macro.Body[i - 1].Is(u"#") && part.Is(u"#")) {
            part.Origin = token.Origin;
            Push(w, part, options.MaxTokens);
            i--;
        } else if (macro.Arguments > 0 && IsParameterReference(macro.Body, i)) {
            int index = part.Text()[0] - u'1';
            if (index >= macro.Arguments)
                Fail(MathDiagnosticCode::InvalidMacroDefinition, token, "Macro argument outside declared arity.");
            auto [start, length] = args[index];
            for (int j = length - 1; j >= 0; j--)
                Push(w, w.Arguments[start + j], options.MaxTokens);
            i--;
        } else {
            part.Origin = token.Origin;
            Push(w, part, options.MaxTokens);
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
        if (!token.NoExpand && (token.Is(u"\\noexpand") || HasDefinition(w, token.Text()))) {
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
        Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected a macro name.");
    auto nameToken = Pop(w);
    std::u16string name(nameToken.Text());
    ValidateName(name, nameToken.Origin);
    int count = 0;
    std::vector<std::vector<LexToken>> delimiters(1);
    std::optional<LexToken> insert;
    while (!w.Pending.empty() && !w.Pending.back().Is(u"{")) {
        auto hash = Pop(w);
        if (!hash.Is(u"#")) {
            delimiters[count].push_back(hash);
            continue;
        }
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::InvalidMacroDefinition, hash, "Expected a macro parameter number.");
        if (w.Pending.back().Is(u"{")) {
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
    if (command.Is(u"\\edef") || command.Is(u"\\xdef"))
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
        Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected a macro name.");
    auto nameToken = Pop(w);
    bool braced = nameToken.Is(u"{");
    if (braced) {
        SkipSpaces(w);
        if (w.Pending.empty())
            Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected a macro name.");
        nameToken = Pop(w);
        SkipSpaces(w);
        if (w.Pending.empty() || !Pop(w).Is(u"}"))
            Fail(MathDiagnosticCode::InvalidMacroDefinition, command, "Expected closing brace after macro name.");
    }
    std::u16string name(nameToken.Text());
    ValidateName(name, nameToken.Origin);
    SkipSpaces(w);
    int count = 0;
    if (!w.Pending.empty() && w.Pending.back().Is(u"[")) {
        Pop(w);
        std::u16string arity;
        while (!w.Pending.empty() && !w.Pending.back().Is(u"]")) {
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
        if (w.Pending.empty() || !Pop(w).Is(u"]") || !valid || count < 0 || count > 9)
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
    if (!exists)
        for (const auto& symbol : SymbolData::Symbols)
            if (symbol.Name == name) {
                exists = true;
                break;
            }
    if ((command.Is(u"\\newcommand") && exists) || (command.Is(u"\\renewcommand") && !exists))
        Fail(MathDiagnosticCode::InvalidMacroDefinition, nameToken,
             "Macro declaration conflicts with its existing definition.");
    if (!command.Is(u"\\providecommand") || !exists)
        Assign(w, name, {Compact(body, command.Origin, options.MaxTokens), count}, false);
}
void PushExpansion(W& w, const std::u16string& value, const LexToken& origin, const O& options,
                   const CancellationToken& cancel) {
    std::vector<LexToken> parts;
    Tokenize(value, parts, options.MaxTokens, cancel, origin.Origin, options.StrictMode, &w.Warnings,
             options.StrictHandler);
    for (int i = Count(parts) - 1; i >= 0; i--)
        Push(w, parts[i], options.MaxTokens);
}
bool IsExpandable(W& w, std::u16string_view name) {
    if (HasDefinition(w, name))
        return true;
    if (OneOf(name, {u"\\noexpand", u"\\expandafter", u"\\@firstoftwo", u"\\@secondoftwo", u"\\@ifnextchar",
                     u"\\TextOrMath", u"\\char", u"\\message", u"\\errmessage", u"\\show", u"\\dots", u"\\dotso",
                     u"\\dotsc", u"\\cdots", u"\\bra@ket", u"\\bra@set", u"\\tag@literal"}))
        return true;
    for (const auto& command : CommandData::Definitions)
        if (!command.Primitive && name == command.Name)
            return true;
    return false;
}
bool IsTextArgumentCommand(std::u16string_view name) {
    return OneOf(name, {u"\\text", u"\\textrm", u"\\textnormal", u"\\textbf", u"\\textmd", u"\\textit", u"\\textup",
                        u"\\textsf", u"\\texttt", u"\\emph", u"\\hbox", u"\\reflectbox", u"\\fbox", u"\\angl"});
}
bool IsDirectSpacing(std::u16string_view name) {
    return OneOf(name, {u"\\,", u"\\:", u"\\;", u"\\!", u"\\quad", u"\\qquad", u"\\operatorname", u"\\liminf",
                        u"\\limsup", u"\\bm", u"\\tag", u"\\notag", u"\\nonumber"});
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
        if (w.Pending[begin].Is(u"{")) {
            int depth = 1;
            begin--;
            while (begin >= 0) {
                if (w.Pending[begin].Is(u"{"))
                    depth++;
                else if (w.Pending[begin].Is(u"}") && --depth == 0) {
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
    if (w.Pending[begin].Is(u"{")) {
        int depth = 1;
        for (end = begin - 1; end >= 0; end--) {
            if (w.Pending[end].Is(u"{"))
                depth++;
            else if (w.Pending[end].Is(u"}") && --depth == 0)
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
    if (token.Is(u"`")) {
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
    int radix = token.Is(u"'") ? 8 : token.Is(u"\"") ? 16 : 10;
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
    if (!options.GlobalGroup)
        OpenScope(w, options, LexToken(source, 0, 0, {}), 6);
    if (options.ColorIsTextColor)
        Assign(w, u"\\color", {{LexToken(u"\\textcolor", 0, 10, {})}, 0}, false);
    bool textMode = false;
    std::vector<int> mathSwitches;
    int expansions = 0;
    std::array<Range, 9> arguments{};
    auto expansionBudget = [&](const LexToken& t) {
        if (++expansions > options.MaxExpansion)
            Fail(MathDiagnosticCode::ExpansionLimit, t, "Macro expansion budget exceeded.");
    };
    auto emit = [&](const LexToken& t) {
        if (Count(w.Expanded) >= options.MaxTokens)
            Fail(MathDiagnosticCode::TokenLimit, t, "Expanded token budget exceeded.");
        w.Expanded.push_back(t);
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
        MacroDefinition macro;
        bool isMacro = !token.NoExpand && !token.Text().empty() && TryGetDefinition(w, token.Text(), macro);
        if (token.Length != 0 && !isMacro && !token.TreatAsRelax && token.Text()[0] != u'\\' &&
            !OneOf(token.Text(), {u"$", u"{", u"}", u"&"})) {
            emit(token);
            continue;
        }
        if (token.Is(u"$") && (textMode || (!mathSwitches.empty() && mathSwitches.back() == 1))) {
            if (textMode) {
                mathSwitches.push_back(1);
                textMode = false;
            } else {
                mathSwitches.pop_back();
                textMode = true;
            }
        } else if (token.Is(u"\\(") && textMode) {
            mathSwitches.push_back(2);
            textMode = false;
        } else if (token.Is(u"\\)") && !mathSwitches.empty() && mathSwitches.back() == 2) {
            mathSwitches.pop_back();
            textMode = true;
        }
        bool customOverride = isMacro && !macro.BuiltIn;
        if (isMacro && macro.Callback >= 1 && macro.Callback <= 4) {
            expansionBudget(token);
            bool doubled = macro.Callback == 2 || macro.Callback == 4;
            if (!doubled && !macro.Delimiters.empty() && !macro.Delimiters[0].empty() && !w.Pending.empty() &&
                w.Pending.back().Is(u"|")) {
                Pop(w);
                doubled = true;
            }
            if (macro.Callback == 3 || macro.Callback == 4) {
                RestoreBraketDefinition(w, u"|");
                RestoreBraketDefinition(w, u"\\|");
            }
            const auto& chosen = doubled && !macro.Delimiters.empty() ? macro.Delimiters[0] : macro.Body;
            for (int i = Count(chosen) - 1; i >= 0; i--) {
                auto p = chosen[i];
                p.Origin = token.Origin;
                Push(w, p, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\bra@ket", u"\\bra@set"})) {
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
            bool one = token.Is(u"\\bra@set");
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
        if (!token.NoExpand && !customOverride && OneOf(token.Text(), {u"\\noexpand", u"\\expandafter"})) {
            expansionBudget(token);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Expansion control requires a following token.");
            auto deferred = Pop(w);
            if (token.Is(u"\\noexpand")) {
                deferred.NoExpand = true;
                deferred.TreatAsRelax = IsExpandable(w, deferred.Text());
                Push(w, deferred, options.MaxTokens);
            } else {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Expandafter requires two following tokens.");
                ExpandOne(w, options, expansions, cancel);
                Push(w, deferred, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\global", u"\\long"})) {
            SkipSpaces(w);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Macro prefix requires an assignment.");
            auto next = Pop(w);
            std::u16string value;
            if (token.Is(u"\\long"))
                value = next.Text();
            else if (OneOf(next.Text(), {u"\\def", u"\\gdef"}))
                value = u"\\gdef";
            else if (OneOf(next.Text(), {u"\\edef", u"\\xdef"}))
                value = u"\\xdef";
            else if (next.Is(u"\\let"))
                value = u"\\\\globallet";
            else if (next.Is(u"\\futurelet"))
                value = u"\\\\globalfuture";
            else if (OneOf(next.Text(), {u"\\global", u"\\long"}))
                value = u"\\global";
            if (value.empty())
                Fail(MathDiagnosticCode::UnexpectedToken, next, "Invalid assignment after a macro prefix.");
            Push(w, LexToken(value, 0, Count(value), next.Origin), options.MaxTokens);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\let", u"\\\\globallet", u"\\futurelet", u"\\\\globalfuture"})) {
            SkipSpaces(w);
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::InvalidMacroDefinition, token, "Let requires a control sequence.");
            auto target = Pop(w);
            std::u16string name(target.Text());
            ValidateName(name, target.Origin);
            SkipSpaces(w);
            std::optional<LexToken> middle;
            if (token.Is(u"\\futurelet") || token.Is(u"\\\\globalfuture")) {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Futurelet requires two tokens.");
                middle = Pop(w);
            } else if (!w.Pending.empty() && w.Pending.back().Is(u"=")) {
                Pop(w);
                SkipSpaces(w);
            }
            if (w.Pending.empty())
                Fail(MathDiagnosticCode::MissingArgument, token, "Let requires a right-hand token.");
            auto rhs = Pop(w);
            MacroDefinition value;
            if (TryGetDefinition(w, rhs.Text(), value))
                value.BuiltIn = false;
            else {
                auto p = rhs;
                p.NoExpand = true;
                value = {{p}, 0};
            }
            Assign(w, name, value, token.Is(u"\\\\globallet") || token.Is(u"\\\\globalfuture"));
            if (middle) {
                Push(w, rhs, options.MaxTokens);
                Push(w, *middle, options.MaxTokens);
            }
            continue;
        }
        if (!customOverride &&
            OneOf(token.Text(), {u"\\@firstoftwo", u"\\@secondoftwo", u"\\@ifnextchar", u"\\TextOrMath"})) {
            expansionBudget(token);
            int count = token.Is(u"\\@ifnextchar") ? 3 : 2;
            for (int a = 0; a < count; a++)
                arguments[a] = ConsumeArgument(w, options, token, cancel);
            int selected = token.Is(u"\\@secondoftwo") || (token.Is(u"\\TextOrMath") && !textMode) ? 1 : 0;
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
        if (!customOverride && token.Is(u"\\char")) {
            expansionBudget(token);
            PushExpansion(w, u"\\@char{" + utf16(std::to_string(ReadCharacter(w, token))) + u"}", token, options,
                          cancel);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\message", u"\\errmessage", u"\\show"})) {
            expansionBudget(token);
            if (token.Is(u"\\show")) {
                if (w.Pending.empty())
                    Fail(MathDiagnosticCode::MissingArgument, token, "Show requires a token.");
                Pop(w);
            } else
                ConsumeArgument(w, options, token, cancel);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\dots", u"\\dotso", u"\\dotsc", u"\\cdots"})) {
            expansionBudget(token);
            if (token.Is(u"\\dots") && !w.Pending.empty())
                ExpandOne(w, options, expansions, cancel);
            auto next = w.Pending.empty() ? std::u16string_view{} : w.Pending.back().Text();
            if (token.Is(u"\\dots")) {
                bool centered =
                    OneOf(next,
                          {u"+",           u"=",          u"<",        u">",         u"-",           u"*",
                           u":",           u"\\DOTSB",    u"\\coprod", u"\\bigvee",  u"\\bigwedge",  u"\\biguplus",
                           u"\\bigcap",    u"\\bigcup",   u"\\prod",   u"\\sum",     u"\\bigotimes", u"\\bigoplus",
                           u"\\bigodot",   u"\\bigsqcup", u"\\And",    u"\\mathbin", u"\\mathrel",   u"\\xrightarrow",
                           u"\\xleftarrow"}) ||
                    next.starts_with(u"\\not");
                if (!centered)
                    for (const auto& symbol : SymbolData::Symbols)
                        if (!symbol.TextMode && symbol.Name == next &&
                            (symbol.Atom == MathAtom::Bin || symbol.Atom == MathAtom::Rel)) {
                            centered = true;
                            break;
                        }
                bool integral =
                    OneOf(next, {u"\\DOTSI", u"\\int", u"\\oint", u"\\iint", u"\\iiint", u"\\iiiint", u"\\idotsint"});
                PushExpansion(w,
                              integral             ? u"\\dotsi"
                              : centered           ? u"\\dotsb"
                              : next == u"\\DOTSX" ? u"\\dotsx"
                                                   : u"\\dotso",
                              token, options, cancel);
                continue;
            }
            bool extra = OneOf(next, {u")", u"]", u"\\rbrack", u"\\}", u"\\rbrace", u"\\rangle", u"\\rceil",
                                      u"\\rfloor", u"\\rgroup", u"\\rmoustache", u"\\right", u"\\bigr", u"\\biggr",
                                      u"\\Bigr", u"\\Biggr", u"$", u";", u".", u","});
            if (token.Is(u"\\dotsc") && next == u",")
                extra = false;
            PushExpansion(w, std::u16string(token.Is(u"\\cdots") ? u"\\@cdots" : u"\\ldots") + (extra ? u"\\," : u""),
                          token, options, cancel);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\def", u"\\gdef", u"\\edef", u"\\xdef"})) {
            Define(w, options, token.Is(u"\\gdef") || token.Is(u"\\xdef"), token, expansions, cancel);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\newcommand", u"\\renewcommand", u"\\providecommand"})) {
            NewCommand(w, options, token, expansions, cancel);
            continue;
        }
        if (!customOverride && OneOf(token.Text(), {u"\\begingroup", u"\\endgroup"})) {
            if (token.Is(u"\\begingroup"))
                OpenScope(w, options, token, 4);
            else {
                if (w.ScopeKinds.empty() || w.ScopeKinds.back() != 4)
                    Fail(MathDiagnosticCode::UnexpectedGroupEnd, token, "Unmatched endgroup.");
                CloseScope(w);
            }
        }
        if (token.Is(u"{"))
            OpenScope(w, options, token, 1);
        else if (token.Is(u"}") && !w.Scopes.empty() && w.ScopeKinds.back() == 1)
            CloseScope(w);
        if (isMacro && !(macro.BuiltIn && IsDirectSpacing(token.Text()))) {
            expansionBudget(token);
            if (!macro.Delimiters.empty())
                for (const auto& prefix : macro.Delimiters[0])
                    if (w.Pending.empty() || Pop(w).Text() != prefix.Text())
                        Fail(MathDiagnosticCode::MissingArgument, token,
                             "Macro invocation does not match its parameter prefix.");
            for (int a = 0; a < macro.Arguments; a++)
                arguments[a] = !macro.Delimiters.empty() && !macro.Delimiters[a + 1].empty()
                                   ? ConsumeDelimitedArgument(w, options, token, macro.Delimiters[a + 1], cancel)
                                   : ConsumeArgument(w, options, token, cancel);
            for (int i = Count(macro.Body) - 1; i >= 0; i--) {
                auto part = macro.Body[i];
                if (macro.Arguments > 0 && i > 0 && macro.Body[i - 1].Is(u"#") && part.Is(u"#")) {
                    part.Origin = token.Origin;
                    Push(w, part, options.MaxTokens);
                    i--;
                } else if (macro.Arguments > 0 && IsParameterReference(macro.Body, i)) {
                    int index = part.Text()[0] - u'1';
                    if (index >= macro.Arguments)
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
        if (token.Is(u"\\begin")) {
            MacroDefinition stretchDefinition;
            if (TryGetDefinition(w, u"\\arraystretch", stretchDefinition)) {
                auto expanded = ExpandDefinition(w, options, stretchDefinition.Body, expansions, cancel);
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
                emit(marker);
            }
            OpenScope(w, options, token, 2);
            int depth = 0;
            for (int i = Count(w.Pending) - 1; i >= 0; i--) {
                if (w.Pending[i].Is(u"{"))
                    depth++;
                else if (w.Pending[i].Is(u"}") && --depth == 0) {
                    if (Count(w.Pending) >= options.MaxTokens)
                        Fail(MathDiagnosticCode::TokenLimit, token, "Pending token budget exceeded.");
                    w.Pending.insert(w.Pending.begin() + i, LexToken(CellScopeMarker, 0, 0, token.Origin));
                    break;
                }
            }
        } else if (token.Is(u"\\end")) {
            if (!w.ScopeKinds.empty() && w.ScopeKinds.back() == 3)
                CloseScope(w);
            if (!w.ScopeKinds.empty() && w.ScopeKinds.back() == 2)
                CloseScope(w);
        } else if (OneOf(token.Text(), {u"&", u"\\\\", u"\\cr"}) && !w.ScopeKinds.empty() && w.ScopeKinds.back() == 3) {
            CloseScope(w);
            OpenScope(w, options, token, 3);
        }
        if (IsTextArgumentCommand(token.Text()))
            MarkTextArgument(w, token, options);
        else if (OneOf(token.Text(), {u"\\raisebox", u"\\colorbox", u"\\fcolorbox"}))
            MarkTextArgument(w, token, options, token.Is(u"\\fcolorbox") ? 2 : 1);
        emit(token);
    }
    if (std::find(w.ScopeKinds.begin(), w.ScopeKinds.end(), 4) != w.ScopeKinds.end())
        throw CompileFailure({MathDiagnosticCode::UnclosedGroup, {Count(source), 0}, u"Unclosed begingroup."});
    if (w.ScopeKinds.size() == 1 && w.ScopeKinds[0] == 6)
        CloseScope(w);
    ValidatePersistentBudget(w.Definitions, options);
}
} // namespace velocitex
