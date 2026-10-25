#include "parser.hpp"

#include "generated/data.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <iterator>
namespace velocitex {

namespace {

std::u16string TrimEnd(std::u16string s, char16_t c) {
    while (!s.empty() && s.back() == c)
        s.pop_back();
    return s;
}

// Builds a diagnostic message out of UTF-16 parts.
//
// The standard library of the reference toolchain has no `operator+` for a
// literal on the left of a view, so the parts are appended one by one.
std::u16string JoinMessage(std::initializer_list<std::u16string_view> parts) {
    std::u16string text;
    for (auto part : parts)
        text.append(part);
    return text;
}

// The decimal form of a number, as UTF-16.
std::u16string NumberText(int value) {
    const std::string digits = std::to_string(value);
    std::u16string text;
    text.reserve(digits.size());
    for (char c : digits)
        text.push_back(static_cast<char16_t>(c));
    return text;
}

bool IsValidScalar(int c) {
    return c >= 0 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff);
}

// True when the character belongs to a script that KaTeX marks with a
// fallback class.
//
// The parser only asks whether there is a script, so the name itself is not
// built.  The script table holds the characters that KaTeX marks, and the
// smallest of them is U+00B2, so an ASCII character never has a script.
bool HasUnicodeScript(int c) {
    if (c < 0xB2)
        return false;
    bool sub = false;
    return UnicodeData::Script(c, sub).has_value();
}

bool IsDigit(char16_t c) {
    return c >= u'0' && c <= u'9';
}

bool IsAsciiLetter(char16_t c) {
    return (c >= u'a' && c <= u'z') || (c >= u'A' && c <= u'Z');
}

std::u16string Trim(std::u16string_view s) {
    while (!s.empty() && IsWhiteSpace(s.front()))
        s.remove_prefix(1);
    while (!s.empty() && IsWhiteSpace(s.back()))
        s.remove_suffix(1);
    return std::u16string(s);
}

bool ParseInt(std::u16string_view s, int& value) {
    auto raw = utf8(s);
    char* end;
    long v = std::strtol(raw.c_str(), &end, 10);
    while (*end && std::isspace((unsigned char)*end))
        end++;
    if (end == raw.c_str() || *end || v < INT_MIN || v > INT_MAX)
        return false;
    value = (int)v;
    return true;
}

bool ParseDouble(std::u16string_view s, double& value) {
    auto raw = utf8(s);
    char* end;
    value = std::strtod(raw.c_str(), &end);
    while (*end && std::isspace((unsigned char)*end))
        end++;
    return end != raw.c_str() && !*end;
}

[[noreturn]] void Fail(MathDiagnosticCode code, SourceRange range, std::u16string message) {
    throw CompileFailure({code, range, std::move(message)});
}

// A semantic node is several hundred bytes large.  The node is therefore
// built directly in the tree of the workspace and the edit that fills it is
// applied in place; the parser used to build every node on the stack, move it
// into the tree and move it once more.
template <class F>
int AddEdited(ParserWorkspace& w, const MathOptions& options, NodeKind kind, SourceRange range, F&& edit) {
    if ((int)w.Nodes.size() >= options.MaxNodes)
        Fail(MathDiagnosticCode::NodeLimit, range, u"Semantic node budget exceeded.");
    const int index = (int)w.Nodes.size();
    w.Nodes.emplace_back(kind, range);
    edit(w.Nodes.back());
    return index;
}

// Adds a node to the semantic tree without an edit.
int AddNode(ParserWorkspace& w, const MathOptions& options, NodeKind kind, SourceRange range) {
    if ((int)w.Nodes.size() >= options.MaxNodes)
        Fail(MathDiagnosticCode::NodeLimit, range, u"Semantic node budget exceeded.");
    const int index = (int)w.Nodes.size();
    w.Nodes.emplace_back(kind, range);
    return index;
}

// The task is taken by reference and copied into the stack of the tasks.  The
// caller hands over a task it keeps for itself.
//
// The task is a plain structure without a move constructor, so a "move" copies
// it as well: taking the parameter by value copied the task twice, once into
// the parameter and once into the stack.
void Push(ParserWorkspace& w, ParseTask&& task) {
    w.Tasks.push_back(task);
}

// Pushes a task onto the stack of the tasks, with an edit applied to it.
//
// The task is copied into the stack and the edit is applied to the copy in
// place.  Building the copy with `changed()` and handing it over by value
// copied the task three times (into the parameter of `changed()`, out of it
// and into the stack), and the copy of the task was one of the hottest lines
// of the parser.
template <class F>
void PushEdited(ParserWorkspace& w, const ParseTask& task, F edit) {
    w.Tasks.push_back(task);
    edit(w.Tasks.back());
}

// Edits the task on the top of the stack in place.
//
// The successor of a task is the task itself with a few fields changed, so
// the task does not have to be copied out of the stack and back into it.
template <class F>
void EditTask(ParseTask& task, bool& replaced, F edit) {
    edit(task);
    replaced = true;
}

// Pushes a task that has to run after the current one and continues with the
// current task.
//
// The marker has to end up below the continuation, so the task leaves the
// stack and comes back.  The edit is applied to the copy before the task
// leaves the stack, because the lambdas read the fields of the task.
template <class F>
void PushAfter(ParserWorkspace& w, ParseTask& task, bool& replaced, ParseTask&& after, F edit) {
    ParseTask copy = task;
    edit(copy);
    w.Tasks.pop_back();
    w.Tasks.push_back(std::move(after));
    w.Tasks.push_back(copy);
    replaced = true;
}

// Pushes a task that has to run after the current one and continues with the
// arguments of the current task.
//
// The marker has to end up below the tasks of the argument, so the task
// leaves the stack and the argument is built from a copy of it.
template <class F>
void PushAfterArguments(ParserWorkspace& w, ParseTask& task, bool& replaced, ParseTask&& after, F push) {
    ParseTask context = task;
    w.Tasks.pop_back();
    w.Tasks.push_back(std::move(after));
    push(context);
    replaced = true;
}

// The symbol and command tables are indexed by name.  The keys are views into
// the generated tables, so a lookup never allocates a temporary string.  The
// byte-wise hash of the standard containers misses the cache on almost every
// lookup, so the names are hashed with a mixing function that reads the code
// units in 64 bit chunks and looked up in an open addressing table.
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

// An open addressing table that maps a name to an entry of a generated table.
// The entries are referenced instead of copied, so the table stays small and
// the lookup never allocates.
template <class T>
class NameTable {
  public:
    void Reserve(size_t count) {
        size_t capacity = 16;
        while (capacity < count * 2)
            capacity <<= 1;
        slots_.assign(capacity, 0);
        entries_.reserve(count);
    }
    void Insert(const T* entry) {
        size_t mask = slots_.size() - 1;
        size_t slot = (size_t)HashName(entry->Name) & mask;
        while (slots_[slot] != 0) {
            // The first entry with a name wins, like `try_emplace` does.
            if (entries_[slots_[slot] - 1]->Name == entry->Name)
                return;
            slot = (slot + 1) & mask;
        }
        entries_.push_back(entry);
        slots_[slot] = (uint32_t)entries_.size();
    }
    const T* Find(std::u16string_view name) const {
        if (slots_.empty())
            return nullptr;
        size_t mask = slots_.size() - 1;
        size_t slot = (size_t)HashName(name) & mask;
        while (true) {
            uint32_t index = slots_[slot];
            if (index == 0)
                return nullptr;
            const T* entry = entries_[index - 1];
            if (entry->Name == name)
                return entry;
            slot = (slot + 1) & mask;
        }
    }

  private:
    std::vector<const T*> entries_;
    std::vector<uint32_t> slots_;
};

// The symbols that consist of a single ASCII character are the most common
// lookup, so they are resolved through a direct table.  Every other name
// lives in an open addressing table.  Both tables are built per mode, because
// a name can exist in the math mode and in the text mode at the same time.
struct SymbolIndexes {
    std::array<const SymbolDefinition*, 128> ascii[2];
    NameTable<SymbolDefinition> names[2];
};

const SymbolIndexes& Symbols() {
    static const auto indexes = []() {
        SymbolIndexes result{};
        size_t counts[2] = {0, 0};
        for (const auto& symbol : SymbolData::Symbols)
            if (!(symbol.Name.size() == 1 && symbol.Name[0] < 128))
                counts[symbol.TextMode ? 1 : 0]++;
        result.names[0].Reserve(counts[0]);
        result.names[1].Reserve(counts[1]);
        for (const auto& symbol : SymbolData::Symbols) {
            int mode = symbol.TextMode ? 1 : 0;
            if (symbol.Name.size() == 1 && symbol.Name[0] < 128) {
                auto& slot = result.ascii[mode][symbol.Name[0]];
                if (slot == nullptr)
                    slot = &symbol;
                continue;
            }
            result.names[mode].Insert(&symbol);
        }
        return result;
    }();
    return indexes;
}

bool FindSymbol(std::u16string_view name, bool textMode, SymbolDefinition& definition) {
    const auto& indexes = Symbols();
    int mode = textMode ? 1 : 0;
    if (name.size() == 1 && name[0] < 128) {
        const SymbolDefinition* symbol = indexes.ascii[mode][name[0]];
        if (symbol == nullptr)
            return false;
        definition = *symbol;
        return true;
    }
    const SymbolDefinition* symbol = indexes.names[mode].Find(name);
    if (symbol == nullptr)
        return false;
    definition = *symbol;
    return true;
}

bool FindCommand(std::u16string_view name, CommandDefinition& definition) {
    // The command table holds the control sequences and a few single
    // characters, so a longer name that does not start with a backslash is
    // not in it.  The check saves the hash for the symbols of a formula,
    // which are most of the tokens.
    if (name.size() > 1 && name[0] != u'\\')
        return false;
    static const auto table = []() {
        NameTable<CommandDefinition> result;
        result.Reserve(CommandData::Definitions.size());
        for (const auto& command : CommandData::Definitions)
            result.Insert(&command);
        return result;
    }();
    const CommandDefinition* command = table.Find(name);
    if (command == nullptr)
        return false;
    definition = *command;
    return true;
}

static bool TryStyle(std::u16string_view name, MathStyle& style) {
    style = ((name == u"\\displaystyle"_u16)
                 ? MathStyle::Display
                 : ((name == u"\\textstyle"_u16)
                        ? MathStyle::Text
                        : ((name == u"\\scriptstyle"_u16)
                               ? MathStyle::Script
                               : ((name == u"\\scriptscriptstyle"_u16) ? MathStyle::ScriptScript : (MathStyle)(-1)))));
    return (int)style >= 0;
}
static bool IsPlainAtomCharacter(char16_t value) {
    return (
        ((value > u'\u0020') && (value < u'\u007f')) &&
        !(((((((((((value == u'\\') || (value == u'{')) || (value == u'}')) || (value == u'^')) || (value == u'_')) ||
               (value == u'&')) ||
              (value == u'\'')) ||
             (value == u'$')) ||
            (value == u'#')))));
}
static int DelimiterFromText(std::u16string_view text, SourceRange range, std::u16string_view funcName) {
    auto trimmedText = Trim(text);
    text = trimmedText;
    if (text.size() == 0 || text == u"."_u16)
        return 0;
    auto
        cp = (((text == u"("_u16) || (text == u"\\lparen"_u16))
                  ? u'('
                  : (((text == u")"_u16) || (text == u"\\rparen"_u16))
                         ? u')'
                         : (((text == u"["_u16) || (text == u"\\lbrack"_u16))
                                ? u'['
                                : (((text == u"]"_u16) || (text == u"\\rbrack"_u16))
                                       ? u']'
                                       : (((text == u"\\{"_u16) || (text == u"\\lbrace"_u16))
                                              ? u'{'
                                              : (((text == u"\\}"_u16) || (text == u"\\rbrace"_u16))
                                                     ? u'}'
                                                     : (((((text == u"|"_u16) || (text == u"\\vert"_u16)) ||
                                                          (text == u"\\lvert"_u16)) ||
                                                         (text == u"\\rvert"_u16))
                                                            ? u'|'
                                                            : ((((((text == u"\\|"_u16) || (text == u"\\Vert"_u16)) ||
                                                                  (text == u"\\lVert"_u16)) ||
                                                                 (text == u"\\rVert"_u16)) ||
                                                                (text == u"‖"_u16))
                                                                   ? 0x2016
                                                                   : (((((text == u"\\langle"_u16) || (text == u"<"_u16)) ||
                                                                        (text == u"\\lt"_u16)) ||
                                                                       (text == u"⟨"_u16))
                                                                          ? 0x27e8
                                                                          : (((((text == u"\\rangle"_u16) ||
                                                                                (text == u">"_u16)) ||
                                                                               (text == u"\\gt"_u16)) ||
                                                                              (text == u"⟩"_u16))
                                                                                 ? 0x27e9
                                                                                 : (((text == u"⟮"_u16) ||
                                                                                     (text == u"\\lgroup"_u16))
                                                                                        ? 0x27ee
                                                                                        : (((text == u"⟯"_u16) ||
                                                                                            (text == u"\\rgroup"_u16))
                                                                                               ? 0x27ef
                                                                                               : (((text == u"⎰"_u16) || (text == u"\\lmoustache"_u16)) ? 0x23b0
                                                                                                                                                : (((text == u"⎱"_u16) || (text == u"\\rmoustache"_u16)) ? 0x23b1
                                                                                                                                                                                                 : (((text == u"\\lfloor"_u16) || (text == u"⌊"_u16)) ? 0x230a
                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                    ((text == u"\\rfloor"_u16) || (text == u"⌋"_u16)) ? 0x230b
                                                                                                                                                                                                                                                                                              : (((text == u"\\lceil"_u16) || (text == u"⌈"_u16)) ? 0x2308
                                                                                                                                                                                                                                                                                                                                          : (
                                                                                                                                                                                                                                                                                                                                                ((text == u"\\rceil"_u16) || (text == u"⌉"_u16)) ? 0x2309
                                                                                                                                                                                                                                                                                                                                                                                         : (
                                                                                                                                                                                                                                                                                                                                                                                               (text == u"/"_u16) ? u'/'
                                                                                                                                                                                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                                                                                                                                                                                    (text == u"\\backslash"_u16) ? u'\\'
                                                                                                                                                                                                                                                                                                                                                                                                                                             : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                   ((text == u"\\uparrow"_u16) || (text == u"↑"_u16)) ? 0x2191
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ((text == u"\\downarrow"_u16) || (text == u"↓"_u16))
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        ? 0x2193
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        : (((text == u"\\updownarrow"_u16) || (text == u"↕"_u16)) ? 0x2195
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        text == u"\\Uparrow"_u16) ||
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    (text == u"⇑"_u16))
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ? 0x21d1
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    : (((text == u"\\Downarrow"_u16) || (text == u"⇓"_u16)) ? 0x21d3
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    : (((text == u"\\Updownarrow"_u16) || (text == u"⇕"_u16)) ? 0x21d5
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      : -1))))))))))))))))))))))))));
    if (cp < 0)
        // KaTeX reports the delimiter that does not name a valid delimiter
        // (`src/functions/delimsizing.ts`, `checkDelimiter`).
        Fail(MathDiagnosticCode::UnexpectedToken, range,
             u"Invalid delimiter '" + std::u16string(text) + u"' after '" + std::u16string(funcName) + u"'");
    return cp;
}
// The tokens that end an expression in KaTeX (`src/Parser.ts`,
// `Parser.endOfExpression`).
static bool IsEndOfExpression(std::u16string_view text) {
    // The check only matches a control sequence or the ampersand, so the
    // first character rejects the tokens of a formula that are neither.
    if (text.empty() || (text[0] != u'\\' && text[0] != u'&'))
        return false;
    return text == u"\\endgroup"_u16 || text == u"\\end"_u16 || text == u"\\right"_u16 || text == u"&"_u16;
}
static bool TryDelimiterSize(std::u16string_view name, int& size, MathAtom& atom) {
    size = 0;
    atom = MathAtom::Ord;
    if (name.size() < 4 || name[0] != u'\\')
        return false;
    auto stem = name;
    if ((((name.back() == u'l') || (name.back() == u'r')) || (name.back() == u'm'))) {
        atom = name.back() == u'l' ? MathAtom::Open : name.back() == u'r' ? MathAtom::Close : MathAtom::Rel;
        stem = name.substr(0, name.size() - 1);
    }
    size =
        ((stem == u"\\big"_u16) ? 1 : ((stem == u"\\Big"_u16) ? 2 : ((stem == u"\\bigg"_u16) ? 3 : ((stem == u"\\Bigg"_u16) ? 4 : 0))));
    return size != 0;
}
static double LengthEm(TexLength);

// The names of the TeX length units, in the order of `TexUnit`.  KaTeX
// writes the unit of a length into its strict diagnostics (`src/units.ts`,
// `src/functions/kern.ts`), so the parser needs the name of a unit.
static constexpr std::u16string_view kTexUnitNames[] = {
    u"em"_u16, u"ex"_u16, u"mu"_u16,  u"pt"_u16, u"bp"_u16, u"pc"_u16, u"in"_u16, u"cm"_u16,
    u"mm"_u16, u"dd"_u16, u"cc"_u16, u"nd"_u16, u"nc"_u16, u"sp"_u16, u"px"_u16};

static std::u16string_view UnitName(TexUnit unit) {
    auto index = static_cast<size_t>(unit);
    return index < std::size(kTexUnitNames) ? kTexUnitNames[index] : u""_u16;
}

static TexLength ParseLength(std::u16string, SourceRange);

static double ReadDimension(std::u16string text, SourceRange range) {
    return LengthEm(ParseLength(text, range));
}
static TexLength ParseLength(std::u16string text, SourceRange range) {
    text = Trim(text);
    if (text.empty())
        return {0, TexUnit::Pt};
    auto source = text;
    std::u16string compact;
    for (auto c : text)
        if (!IsWhiteSpace(c))
            compact += c;

    size_t split = 0;
    while (split < compact.size() &&
           (IsDigit(compact[split]) || compact[split] == u'.' || compact[split] == u'+' || compact[split] == u'-'))
        split++;

    double number = 0;
    if (split == 0 || !ParseDouble(std::u16string_view(compact).substr(0, split), number) || !std::isfinite(number))
        // KaTeX reports a size it cannot read (`src/Parser.ts`,
        // `parseSizeGroup`).
        Fail(MathDiagnosticCode::UnexpectedToken, range, u"Invalid size: '" + source + u"'");

    auto unit = std::u16string_view(compact).substr(split);
    TexUnit kind = (TexUnit)-1;

    for (size_t i = 0; i < std::size(kTexUnitNames); i++)
        if (unit == kTexUnitNames[i])
            kind = (TexUnit)i;

    if ((int)kind < 0)
        // KaTeX reports the unit it does not know (`src/Parser.ts`,
        // `parseSizeGroup`).
        Fail(MathDiagnosticCode::UnexpectedToken, range, u"Invalid unit: '" + std::u16string(unit) + u"'");
    if (std::abs(number) > 1e8)
        Fail(MathDiagnosticCode::UnexpectedToken, range, u"Unsupported or excessive TeX dimension.");
    return {number, kind};
}
static double LengthEm(TexLength length) {
    return length.Number *
           (((length.Unit == TexUnit::Em)
                 ? 1.0
                 : ((length.Unit == TexUnit::Ex)
                        ? .431
                        : ((length.Unit == TexUnit::Mu)
                               ? 1.0 / 18
                               : ((length.Unit == TexUnit::Pt)
                                      ? .1
                                      : ((length.Unit == TexUnit::Bp)
                                             ? .100375
                                             : ((length.Unit == TexUnit::Pc)
                                                    ? 1.2
                                                    : ((length.Unit == TexUnit::In)
                                                           ? 7.227
                                                           : ((length.Unit == TexUnit::Cm)
                                                                  ? 7.227 / 2.54
                                                                  : ((length.Unit == TexUnit::Mm)
                                                                         ? 7.227 / 25.4
                                                                         : ((length.Unit == TexUnit::Dd)
                                                                                ? .1070001
                                                                                : ((length.Unit == TexUnit::Cc)
                                                                                       ? 1.284001
                                                                                       : ((length.Unit == TexUnit::Nd)
                                                                                              ? .106978
                                                                                              : ((length.Unit ==
                                                                                                  TexUnit::Nc)
                                                                                                     ? 1.283736
                                                                                                     : ((length.Unit ==
                                                                                                         TexUnit::Sp)
                                                                                                            ? .1 / 65536
                                                                                                            : 1)))))))))))))));
}
static bool TrySizing(std::u16string_view name, int& size) {
    size =
        ((name == u"\\tiny"_u16)
             ? 1
             : ((name == u"\\sixptsize"_u16)
                    ? 2
                    : ((name == u"\\scriptsize"_u16)
                           ? 3
                           : ((name == u"\\footnotesize"_u16)
                                  ? 4
                                  : ((name == u"\\small"_u16)
                                         ? 5
                                         : ((name == u"\\normalsize"_u16)
                                                ? 6
                                                : ((name == u"\\large"_u16)
                                                       ? 7
                                                       : ((name == u"\\Large"_u16)
                                                              ? 8
                                                              : ((name == u"\\LARGE"_u16)
                                                                     ? 9
                                                                     : ((name == u"\\huge"_u16)
                                                                            ? 10
                                                                            : ((name == u"\\Huge"_u16) ? 11 : 0)))))))))));
    return size != 0;
}
static bool TryLegacyFont(std::u16string_view name, MathFont& font) {
    font = ((name == u"\\rm"_u16)
                ? MathFont::MathRoman
                : ((name == u"\\sf"_u16)
                       ? MathFont::MathSans
                       : ((name == u"\\tt"_u16)
                              ? MathFont::MathTypewriter
                              : ((name == u"\\bf"_u16) ? MathFont::MathBold
                                                   : ((name == u"\\it"_u16) ? MathFont::MathItalic
                                                                        : ((name == u"\\cal"_u16) ? MathFont::Caligraphic
                                                                                              : (MathFont)(-1)))))));
    return (int)font >= 0;
}
static bool TryClass(std::u16string_view name, MathAtom& atom) {
    atom =
        ((name == u"\\mathord"_u16)
             ? MathAtom::Ord
             : ((name == u"\\mathbin"_u16)
                    ? MathAtom::Bin
                    : ((name == u"\\mathrel"_u16)
                           ? MathAtom::Rel
                           : ((name == u"\\mathopen"_u16)
                                  ? MathAtom::Open
                                  : ((name == u"\\mathclose"_u16)
                                         ? MathAtom::Close
                                         : ((name == u"\\mathpunct"_u16)
                                                ? MathAtom::Punct
                                                : ((name == u"\\mathinner"_u16) ? MathAtom::Inner : (MathAtom)(-1))))))));
    return (int)atom >= 0;
}
static bool IsEnvironment(std::u16string name) {
    return (((((((((((((((((((((((((((((((((name == u"matrix"_u16) || (name == u"pmatrix"_u16)) || (name == u"bmatrix"_u16)) ||
                                         (name == u"Bmatrix"_u16)) ||
                                        (name == u"vmatrix"_u16)) ||
                                       (name == u"Vmatrix"_u16)) ||
                                      (name == u"matrix*"_u16)) ||
                                     (name == u"pmatrix*"_u16)) ||
                                    (name == u"bmatrix*"_u16)) ||
                                   (name == u"Bmatrix*"_u16)) ||
                                  (name == u"vmatrix*"_u16)) ||
                                 (name == u"Vmatrix*"_u16)) ||
                                (name == u"array"_u16)) ||
                               (name == u"darray"_u16)) ||
                              (name == u"smallmatrix"_u16)) ||
                             (name == u"subarray"_u16)) ||
                            (name == u"cases"_u16)) ||
                           (name == u"dcases"_u16)) ||
                          (name == u"rcases"_u16)) ||
                         (name == u"drcases"_u16)) ||
                        (name == u"aligned"_u16)) ||
                       (name == u"alignedat"_u16)) ||
                      (name == u"gathered"_u16)) ||
                     (name == u"split"_u16)) ||
                    (name == u"align"_u16)) ||
                   (name == u"align*"_u16)) ||
                  (name == u"alignat"_u16)) ||
                 (name == u"alignat*"_u16)) ||
                (name == u"gather"_u16)) ||
               (name == u"gather*"_u16)) ||
              (name == u"equation"_u16)) ||
             (name == u"equation*"_u16)) ||
            (name == u"CD"_u16));
}
// KaTeX's named operators (`src/functions/op.ts`).  `\liminf` and `\limsup`
// are not part of that list: KaTeX defines them as macros that expand to
// `\DOTSB\operatorname*{lim\,inf}` and `\DOTSB\operatorname*{lim\,sup}`.
static bool TryNamedOperator(std::u16string_view name, std::u16string& text, bool& limits) {
    limits = ((((((((((name == u"\\det"_u16) || (name == u"\\gcd"_u16)) || (name == u"\\inf"_u16)) || (name == u"\\lim"_u16)) ||
                   (name == u"\\max"_u16)) ||
                  (name == u"\\min"_u16)) ||
                 (name == u"\\Pr"_u16)) ||
                (name == u"\\sup"_u16))));
    auto recognized =
        limits ||
        ((((((((((((((((((((((((((((((((name == u"\\arcsin"_u16) || (name == u"\\arccos"_u16)) || (name == u"\\arctan"_u16)) ||
                                     (name == u"\\arctg"_u16)) ||
                                    (name == u"\\arcctg"_u16)) ||
                                   (name == u"\\arg"_u16)) ||
                                  (name == u"\\ch"_u16)) ||
                                 (name == u"\\cos"_u16)) ||
                                (name == u"\\cosec"_u16)) ||
                               (name == u"\\cosh"_u16)) ||
                              (name == u"\\cot"_u16)) ||
                             (name == u"\\cotg"_u16)) ||
                            (name == u"\\coth"_u16)) ||
                           (name == u"\\csc"_u16)) ||
                          (name == u"\\ctg"_u16)) ||
                         (name == u"\\cth"_u16)) ||
                        (name == u"\\deg"_u16)) ||
                       (name == u"\\dim"_u16)) ||
                      (name == u"\\exp"_u16)) ||
                     (name == u"\\hom"_u16)) ||
                    (name == u"\\ker"_u16)) ||
                   (name == u"\\lg"_u16)) ||
                  (name == u"\\ln"_u16)) ||
                 (name == u"\\log"_u16)) ||
                (name == u"\\sec"_u16)) ||
               (name == u"\\sin"_u16)) ||
              (name == u"\\sinh"_u16)) ||
             (name == u"\\sh"_u16)) ||
            (name == u"\\tan"_u16)) ||
           (name == u"\\tanh"_u16)) ||
          (name == u"\\tg"_u16)) ||
         (name == u"\\th"_u16));
    text = !recognized ? u"" : std::u16string(name.substr(1));
    return recognized;
}
// The command tables of the parser.  They are the single source of truth
// for the commands they hold; the parser looks a name up once per token.

// The accent commands and their code points.
struct AccentEntry {
    std::u16string_view Name;
    int Value;
};
const AccentEntry* FindAccent(std::u16string_view name) {
    static const std::array<AccentEntry, 47> table = {{
        {u"\\\"", 168},
        {u"\\'", 714},
        {u"\\.", 729},
        {u"\\=", 713},
        {u"\\H", 733},
        {u"\\Overrightarrow", 0},
        {u"\\^", 94},
        {u"\\`", 715},
        {u"\\acute", 714},
        {u"\\bar", 713},
        {u"\\breve", 728},
        {u"\\c", 184},
        {u"\\check", 711},
        {u"\\ddot", 168},
        {u"\\dot", 729},
        {u"\\grave", 715},
        {u"\\hat", 94},
        {u"\\mathring", 730},
        {u"\\overbrace", 0},
        {u"\\overbracket", 0},
        {u"\\overgroup", 0},
        {u"\\overleftarrow", 0},
        {u"\\overleftharpoon", 0},
        {u"\\overleftrightarrow", 0},
        {u"\\overline", 0},
        {u"\\overlinesegment", 0},
        {u"\\overrightarrow", 0},
        {u"\\overrightharpoon", 0},
        {u"\\r", 730},
        {u"\\textcircled", 9711},
        {u"\\tilde", 126},
        {u"\\u", 728},
        {u"\\underbrace", 0},
        {u"\\underbracket", 0},
        {u"\\undergroup", 0},
        {u"\\underleftarrow", 0},
        {u"\\underleftrightarrow", 0},
        {u"\\underline", 0},
        {u"\\underlinesegment", 0},
        {u"\\underrightarrow", 0},
        {u"\\utilde", 0},
        {u"\\v", 711},
        {u"\\vec", 8407},
        {u"\\widecheck", 711},
        {u"\\widehat", 94},
        {u"\\widetilde", 126},
        {u"\\~", 732},
    }};
    auto it = std::lower_bound(table.begin(), table.end(), name,
                                [](const AccentEntry& entry, std::u16string_view value) {
                                    return entry.Name < value;
                                });
    if (it == table.end() || it->Name != name)
        return nullptr;
    return &*it;
}

static bool TryAccent(std::u16string_view name, int& codePoint) {
    if (const AccentEntry* found = FindAccent(name)) {
        codePoint = found->Value;
        return true;
    }
    return false;
}

// The extensible arrow commands, sorted by name.
static bool IsExtensibleArrow(std::u16string_view name) {
    static const std::array<std::u16string_view, 22> table = {{
        u"\\xLeftarrow",
        u"\\xLeftrightarrow",
        u"\\xRightarrow",
        u"\\xhookleftarrow",
        u"\\xhookrightarrow",
        u"\\xleftarrow",
        u"\\xleftequilibrium",
        u"\\xleftharpoondown",
        u"\\xleftharpoonup",
        u"\\xleftrightarrow",
        u"\\xleftrightharpoons",
        u"\\xlongequal",
        u"\\xmapsto",
        u"\\xrightarrow",
        u"\\xrightequilibrium",
        u"\\xrightharpoondown",
        u"\\xrightharpoonup",
        u"\\xrightleftarrows",
        u"\\xrightleftharpoons",
        u"\\xtofrom",
        u"\\xtwoheadleftarrow",
        u"\\xtwoheadrightarrow",
    }};
    auto it = std::lower_bound(table.begin(), table.end(), name);
    return it != table.end() && *it == name;
}

// The accents that KaTeX accepts in text mode.
static bool IsTextAccent(std::u16string_view name) {
    static const std::array<std::u16string_view, 13> table = {{
        u"\\\"",
        u"\\'",
        u"\\.",
        u"\\=",
        u"\\H",
        u"\\^",
        u"\\`",
        u"\\c",
        u"\\r",
        u"\\textcircled",
        u"\\u",
        u"\\v",
        u"\\~",
    }};
    auto it = std::lower_bound(table.begin(), table.end(), name);
    return it != table.end() && *it == name;
}

// The font commands and the font they select.
struct FontEntry {
    std::u16string_view Name;
    MathFont Value;
};
const FontEntry* FindFont(std::u16string_view name) {
    static const std::array<FontEntry, 16> table = {{
        {u"\\Bbb", MathFont::AMS},
        {u"\\bm", MathFont::MathBoldItalic},
        {u"\\bold", MathFont::MathBold},
        {u"\\boldsymbol", MathFont::MathBoldItalic},
        {u"\\frak", MathFont::Fraktur},
        {u"\\mathbb", MathFont::AMS},
        {u"\\mathbf", MathFont::MathBold},
        {u"\\mathcal", MathFont::Caligraphic},
        {u"\\mathfrak", MathFont::Fraktur},
        {u"\\mathit", MathFont::MathItalic},
        {u"\\mathnormal", MathFont::MathNormal},
        {u"\\mathrm", MathFont::MathRoman},
        {u"\\mathscr", MathFont::Script},
        {u"\\mathsf", MathFont::MathSans},
        {u"\\mathsfit", MathFont::MathSansItalic},
        {u"\\mathtt", MathFont::MathTypewriter},
    }};
    auto it = std::lower_bound(table.begin(), table.end(), name,
                                [](const FontEntry& entry, std::u16string_view value) {
                                    return entry.Name < value;
                                });
    if (it == table.end() || it->Name != name)
        return nullptr;
    return &*it;
}

static bool TryFont(std::u16string_view name, MathFont& font) {
    if (const FontEntry* found = FindFont(name)) {
        font = found->Value;
        return true;
    }
    return false;
}

// The text font commands and the font they select.
struct TextFontEntry {
    std::u16string_view Name;
    MathFont Value;
};
const TextFontEntry* FindTextFont(std::u16string_view name) {
    static const std::array<TextFontEntry, 10> table = {{
        {u"\\emph", MathFont::MathItalic},
        {u"\\text", MathFont::MathRoman},
        {u"\\textbf", MathFont::MathBold},
        {u"\\textit", MathFont::MathItalic},
        {u"\\textmd", MathFont::MathRoman},
        {u"\\textnormal", MathFont::MathRoman},
        {u"\\textrm", MathFont::MathRoman},
        {u"\\textsf", MathFont::MathSans},
        {u"\\texttt", MathFont::MathTypewriter},
        {u"\\textup", MathFont::MathRoman},
    }};
    auto it = std::lower_bound(table.begin(), table.end(), name,
                                [](const TextFontEntry& entry, std::u16string_view value) {
                                    return entry.Name < value;
                                });
    if (it == table.end() || it->Name != name)
        return nullptr;
    return &*it;
}

static bool TryTextFont(std::u16string_view name, MathFont& font) {
    if (const TextFontEntry* found = FindTextFont(name)) {
        font = found->Value;
        return true;
    }
    return false;
}


struct ParserState {
    const std::u16string& source;
    const MathOptions& options;
    const MathOptions& settings;
    ParserWorkspace& w;
    const CancellationToken& cancellationToken;
    int position = 0, parseEnd = 0;

    // The parser asks for a node for every step it takes, so the lookup is
    // made without the range check of `at()`.  The index is the one the
    // parser itself handed out, so it is always a node of the tree.
    SemanticNode& Node(int index) { return w.Nodes[static_cast<size_t>(index)]; }

    bool Strict(std::u16string code, SourceRange range, std::u16string message, bool report = true) {
        auto behavior = settings.StrictMode;
        if (bool(settings.StrictHandler)) {
            if (report)
                behavior = settings.StrictHandler(MathWarning{code, range, message});
            else {
                try {
                    behavior = settings.StrictHandler(MathWarning{code, range, message});
                } catch (...) {
                    behavior = MathStrictMode::Error;
                }
            }
        }
        if (behavior == MathStrictMode::Ignore)
            return false;
        if (behavior == MathStrictMode::Error) {
            if (report)
                // KaTeX words the error of a strict violation this way
                // (`src/strict.ts`).
                Fail(MathDiagnosticCode::StrictViolation, range,
                     u"LaTeX-incompatible input and strict mode is set to 'error': " + message + u" [" + code + u"]");
            return true;
        }
        if (w.Warnings.size() < std::min(settings.MaxTokens, 1024))
            w.Warnings.push_back(MathWarning{code, range, message});
        return false;
    }
    int AddSymbol(std::u16string name, SourceRange range, ParseTask context) {
        SymbolDefinition definition{};
        if (FindSymbol(name, context.Text, definition))
            return AddEdited(w, settings, NodeKind::Symbol, range, [&](auto& v) {
                           v.CodePoint = definition.CodePoint;
                           v.Atom = definition.Atom;
                           v.Font = context.Font == MathFont::Default ? definition.Font : context.Font;
                           v.SymbolFont = definition.Font;
                           v.TextMode = context.Text;
                           v.IsMathOrd = definition.IsMathOrd;
                       });
        auto scalar = RuneAt(name);
        // KaTeX keeps characters it has no metrics for as text ordinals; the
        // layout stage decides whether they can actually be rendered.
        if (scalar >= 0x80)
            return AddEdited(w, settings, NodeKind::Symbol, range, [&](auto& v) {
                           v.CodePoint = scalar;
                           v.Font = context.Font;
                           v.TextMode = true;
                       });
        Fail(MathDiagnosticCode::UnknownSymbol, range, u"Unicode base symbol has no available font.");
        return -1;
    }
    void FormLigatures(int row) {
        for (auto child = Node(row).FirstChild; child >= 0; child = Node(child).NextSibling) {
            if (Node(child).Kind != NodeKind::Symbol || Node(child).Font == MathFont::MathTypewriter)
                continue;
            auto next = Node(child).NextSibling;
            if (next < 0 || Node(next).Kind != NodeKind::Symbol)
                continue;
            auto cp = Node(child).CodePoint;
            if (cp == u'-' && Node(next).CodePoint == u'-') {
                auto after = Node(next).NextSibling;
                auto third = after >= 0 && Node(after).Kind == NodeKind::Symbol && Node(after).CodePoint == u'-';
                Node(child).CodePoint = third ? 0x2014 : 0x2013;
                Node(child).NextSibling = third ? Node(after).NextSibling : Node(next).NextSibling;
            } else if (((((cp == u'\'') || (cp == u'`')) || (cp == 0x2018)) || (cp == 0x2019)) &&
                       Node(next).CodePoint == cp) {
                Node(child).CodePoint = ((cp == u'\'') || (cp == 0x2019)) ? 0x201d : 0x201c;
                Node(child).NextSibling = Node(next).NextSibling;
            }
        }
    }
    int Unwrap(int index) {
        return index >= 0 && Node(index).Kind == NodeKind::Row && Node(index).Value == 1 ? Node(index).FirstChild
                                                                                         : index;
    }
    int NormalizedSingle(int index) {
        return index >= 0 && Node(index).Kind == NodeKind::Row
                   ? Node(index).FirstChild >= 0 && Node(Node(index).FirstChild).NextSibling < 0
                         ? Node(index).FirstChild
                         : -1
                   : index;
    }
    MathAtom BinRel(int index) {
        auto child = Unwrap(index);
        if (child >= 0 && ((Node(child).Atom == MathAtom::Bin) || (Node(child).Atom == MathAtom::Rel)))
            return Node(child).Atom;
        return MathAtom::Ord;
    }
    // The command that KaTeX's `mclass` functions use for a given math class
    // (`\mathord`, `\mathbin`, ...).  The HTML builder picks the mclass
    // rendering from the command label, so an mclass group that KaTeX builds
    // through a different command (`\@binrel`) is relabelled with it.
    static std::u16string MathClassCommand(MathAtom atom) {
        switch (atom) {
            case MathAtom::Bin: return u"\\mathbin";
            case MathAtom::Rel: return u"\\mathrel";
            case MathAtom::Open: return u"\\mathopen";
            case MathAtom::Close: return u"\\mathclose";
            case MathAtom::Punct: return u"\\mathpunct";
            case MathAtom::Inner: return u"\\mathinner";
            case MathAtom::Op: return u"\\mathop";
            case MathAtom::Ord: break;
        }
        return u"\\mathord";
    }
    SourceRange CurrentRange() {
        return position < parseEnd ? w.Expanded[position].Origin : SourceRange{int(source.size()), 0};
    }
    // The location KaTeX reports for the end of the input: the end of the
    // source, which is where its end of file token sits
    // (`src/Lexer.ts`).
    SourceRange SourceEndRange() {
        return SourceRange{int(source.size()), 0};
    }
    // The location KaTeX reports when an argument runs out of tokens.  Every
    // argument ends with a sentinel token that carries the location of the
    // argument's last token (`src/MacroExpander.ts`, `scanArgument`), so the
    // C++ reports the token in front of the end of the argument.  Only an
    // argument that reaches the end of the source reports the source end.
    SourceRange ArgumentEndRange() {
        if (position < parseEnd)
            return w.Expanded[position].Origin;
        if (parseEnd >= (int)w.Expanded.size())
            return SourceRange{int(source.size()), 0};
        return w.Expanded[parseEnd - 1].Origin;
    }
    // The message KaTeX's `expect` produces: "Expected '<what>', got '<found>'".
    std::u16string ExpectedMessage(std::u16string_view expected, std::u16string_view found) {
        return u"Expected '" + std::u16string(expected) + u"', got '" + std::u16string(found) + u"'";
    }
    // The message KaTeX reports when a sequence ends before the token that
    // terminates it (`src/Parser.ts`, `parseGroup` and `parseArray`).
    std::u16string StopMessage(int stop) {
        switch (stop) {
            case 2: return u"Unexpected end of input in a macro argument, expected ']'";
            case 3: return ExpectedMessage(u"\\right", u"EOF");
            case 4: return u"Expected & or \\\\ or \\cr or \\end";
            case 5: return ExpectedMessage(u"\\endgroup", u"EOF");
            case 6: return ExpectedMessage(u"$", u"EOF");
            case 7: return ExpectedMessage(u"\\)", u"EOF");
            default: return u"Expected '}', got 'EOF'";
        }
    }
    // The index of the closing brace of the group that starts at `start`, or
    // -1 when the group is not closed.  KaTeX scans the tokens of a braced
    // argument before it parses them (`src/MacroExpander.ts`, `scanArgument`),
    // so nothing inside the group may reach past the closing brace.
    int GroupEnd(int start) {
        auto depth = 0;
        for (auto i = start; i < parseEnd; i++) {
            if (w.Expanded[i].Is(u"{"_u16))
                depth++;
            else if (w.Expanded[i].Is(u"}"_u16) && --depth == 0)
                return i;
        }
        return -1;
    }
    void SkipMathSpaces(bool text) {
        if (!text)
            while (position < parseEnd && w.Expanded[position].Length > 0 &&
                   IsWhiteSpace(w.Expanded[position].Text()[0]))
                position++;
    }
    // The context is taken by reference for the same reason as in `Push`: the
    // caller hands over a task it keeps for itself.
    void PushArgument(int parent, int slot, const ParseTask& context, bool primitive = false,
                      ArgumentStyle style = ArgumentStyle::Normal, std::u16string_view name = {}) {
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::SetChild;
                 v.Node = parent;
                 v.Slot = slot;
             });
        if (!primitive) {
            // KaTeX's `parseArgumentGroup` always wraps the argument in an
            // ordgroup, even when it consists of a single token.  The wrapper
            // is what makes the group render as a "mord" span.
            PushEdited(w, context, [&](auto& v) { v.Kind = ParseTaskKind::WrapArgument; });
        }
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Argument;
                 v.Depth = context.Depth + 1;
                 v.Stop = 0;
                 v.Closing = false;
                 v.Slot = primitive ? -1 : 0;
                 v.PrimitiveArgument = primitive;
                 v.Style = style;
                 v.ArgumentName = name;
             });
    }
    // The same, with an edit applied to the context before the tasks are
    // built.  The edit is applied to a copy of the context, so that the
    // caller keeps its own task unchanged.
    template <class F>
    void PushArgumentEdited(int parent, int slot, const ParseTask& context, F edit, bool primitive = false,
                            ArgumentStyle style = ArgumentStyle::Normal, std::u16string_view name = {}) {
        ParseTask edited = context;
        edit(edited);
        PushArgument(parent, slot, edited, primitive, style, name);
    }
    void PushHBoxArgument(int parent, const ParseTask& context) {
        auto style = AddEdited(w, settings, NodeKind::Style, Node(parent).Range, [&](auto& v) {
                             v.Style = MathStyle::Text;
                             v.Font = MathFont::MathRoman;
                             v.TextMode = true;
                             // KaTeX's "hbox" argument type builds a "styling"
                             // group with `resetFont: true` that keeps the
                             // argument group intact (`src/Parser.ts`).
                             v.ResetFont = true;
                         });
        Node(parent).FirstChild = style;
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::WrapHBoxArgument;
                 v.Node = style;
             });
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Argument;
                 v.Depth = context.Depth + 1;
                 v.Stop = 0;
                 v.Closing = false;
                 v.Slot = 0;
                 v.PrimitiveArgument = false;
                 v.Text = true;
                 v.Font = MathFont::MathRoman;
             });
    }
    // KaTeX only renders \href, \url, \includegraphics and the \html* commands
    // when the caller marks them as trusted.  Without that trust they are
    // shown as an unsupported command.
    static bool IsUntrustedCommand(std::u16string_view name) {
        return name == u"\\href"_u16 || name == u"\\url"_u16 || name == u"\\htmlClass"_u16 || name == u"\\htmlId"_u16 ||
               name == u"\\htmlStyle"_u16 || name == u"\\htmlData"_u16 || name == u"\\includegraphics"_u16;
    }
    // KaTeX's formatUnsupportedCmd: the command name is shown as red text.
    int FormatUnsupportedCmd(const LexToken& token, const ParseTask& task, int arguments = -1) {
        // The arguments are consumed first, exactly like KaTeX does before it
        // rejects the command.  An undefined control sequence has no arguments
        // to consume: KaTeX renders the command text itself.
        if (arguments < 0) {
            if (token.Is(u"\\includegraphics"_u16)) {
                // KaTeX reads the optional argument of `\includegraphics` from
                // brackets and the required one from braces
                // (`src/functions/includegraphics.ts`).
                SkipMathSpaces(false);
                if (position < parseEnd && w.Expanded[position].Is(u"["_u16))
                    ReadRawBracket();
                ReadRawGroup();
            } else {
                arguments = token.Is(u"\\url"_u16) ? 1 : 2;
                for (int i = 0; i < arguments; ++i)
                    ReadRawGroup();
            }
        } else {
            for (int i = 0; i < arguments; ++i)
                ReadRawGroup();
        }
        auto color = AddEdited(w, options, NodeKind::Color, token.Origin, [&](auto& v) {
                                              v.Text2 = options.ErrorColor;
                                              v.Font = task.Font;
                                              v.TextMode = true;
                                          });
        auto text = AddEdited(w, options, NodeKind::Text, token.Origin, [&](auto& v) {
                                            v.Font = task.Font;
                                            v.TextMode = true;
                                        });
        int first = -1;
        int last = -1;
        for (const char16_t c : token.Text()) {
            auto ord = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                                    v.CodePoint = c;
                                                    v.Font = task.Font;
                                                    v.TextMode = true;
                                                });
            if (first < 0)
                first = ord;
            else
                Node(last).NextSibling = ord;
            last = ord;
        }
        // The text node holds a row with the characters, the colour node holds
        // a row with the text node.  That is the shape KaTeX produces.
        auto characters = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                                  v.Font = task.Font;
                                                  v.TextMode = true;
                                              });
        Node(characters).FirstChild = first;
        Node(text).FirstChild = characters;
        auto row = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                           v.Font = task.Font;
                                           v.TextMode = true;
                                       });
        Node(row).FirstChild = text;
        Node(color).FirstChild = row;
        return color;
    }
    // Reads a braced group of tokens.  `range` receives the range that KaTeX
    // reports for the group; it spans the braces (`src/MacroExpander.ts`,
    // `scanArgument`).  `characters` receives the range of every character of
    // the group.
    std::u16string ReadRawGroup(SourceRange* range = nullptr, std::vector<SourceRange>* characters = nullptr) {
        SkipMathSpaces(false);
        if (position >= parseEnd || !w.Expanded[position++].Is(u"{"_u16))
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected a braced command argument.");
        auto start = w.Expanded[position - 1].Origin.Start;
        auto builder = std::u16string{};
        auto nesting = 1;
        while (position < parseEnd) {
            cancellationToken.ThrowIfCancellationRequested();
            const LexToken& token = w.Expanded[position++];
            if (token.Is(u"{"_u16)) {
                if (++nesting > settings.MaxNesting)
                    Fail(MathDiagnosticCode::NestingLimit, token.Origin, u"Argument nesting budget exceeded.");
            } else if (token.Is(u"}"_u16) && --nesting == 0) {
                if (range) {
                    auto end = token.Origin.Start + token.Origin.Length;
                    *range = SourceRange{start, end - start};
                }
                return builder;
            }
            if (characters) {
                auto count = (int)token.Text().size();
                for (int i = 0; i < count; i++)
                    characters->push_back(token.Origin);
            }
            builder.append(token.Text());
        }
        // KaTeX scans such a group as a whole (`src/MacroExpander.ts`,
        // `consumeArg`), so an unterminated group reports the missing
        // argument.
        Fail(MathDiagnosticCode::UnclosedGroup, CurrentRange(),
             u"Unexpected end of input in a macro argument, expected '}'");
        return u"";
    }
    // The column alignment of an array environment.  KaTeX accepts a braced
    // group of symbols or a bare symbol (`src/environments/array.ts`).
    std::u16string ReadAlignment(std::vector<SourceRange>& characters) {
        SkipMathSpaces(false);
        characters.clear();
        if (position < parseEnd && w.Expanded[position].Is(u"{"_u16)) {
            auto text = ReadRawGroup(nullptr, &characters);
            return text;
        }
        if (position < parseEnd) {
            const LexToken& token = w.Expanded[position++];
            for (size_t i = 0; i < token.Text().size(); i++)
                characters.push_back(token.Origin);
            return std::u16string(token.Text());
        }
        Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected a column specification.");
        return u"";
    }
    std::u16string ReadRawBracket() {
        SkipMathSpaces(false);
        if (position >= parseEnd || !w.Expanded[position++].Is(u"["_u16))
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected an optional argument.");
        auto builder = std::u16string{};
        auto braces = 0;
        while (position < parseEnd) {
            const LexToken& token = w.Expanded[position++];
            if (token.Is(u"]"_u16) && braces == 0)
                return builder;
            if (token.Is(u"{"_u16))
                braces++;
            else if (token.Is(u"}"_u16) && --braces < 0)
                Fail(MathDiagnosticCode::UnexpectedGroupEnd, token.Origin,
                     u"Unexpected optional argument closing brace.");
            builder.append(token.Text());
        }
        Fail(MathDiagnosticCode::UnclosedGroup, CurrentRange(), u"Unclosed optional argument.");
        return u"";
    }
    // Reads the delimiter of `\left`, `\right`, `\middle` and the `\big`
    // family.  KaTeX parses the argument as a primitive group
    // (`src/functions/delimsizing.ts`), so a brace opens a group instead of
    // naming a delimiter.  The `\big` family takes the single element of the
    // group (`src/utils.ts`, `normalizeArgument`), the other commands do not.
    int ReadDelimiter(std::u16string_view funcName, SourceRange* range = nullptr, bool normalize = false) {
        SkipMathSpaces(false);
        if (position >= parseEnd)
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected a delimiter.");
        const LexToken& token = w.Expanded[position++];
        if (range)
            *range = token.Origin;
        if (token.Is(u"{"_u16)) {
            const int close = GroupEnd(position - 1);
            if (close < 0)
                // The group is not closed; KaTeX reports the missing brace at
                // the end of the input (`src/Parser.ts`, `parseGroup`).
                Fail(MathDiagnosticCode::UnclosedGroup, SourceEndRange(), u"Expected '}', got 'EOF'");
            if (normalize && close == position) {
                const LexToken& inner = w.Expanded[position];
                auto cp = DelimiterFromText(inner.Text(), inner.Origin, funcName);
                // The whole group belongs to the argument.
                position = close + 1;
                return cp;
            }
            // A group that is not a single symbol is not a delimiter at all.
            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Invalid delimiter type 'ordgroup'");
        }
        return DelimiterFromText(token.Text(), token.Origin, funcName);
    }
    std::u16string ReadColor() {
        auto value = Trim(ReadRawGroup());
        auto hex = [](char16_t c) {
            return (c >= u'0' && c <= u'9') || (c >= u'a' && c <= u'f') || (c >= u'A' && c <= u'F');
        };

        bool valid = value.starts_with(u'#')
                         ? (value.size() == 4 || value.size() == 5 || value.size() == 7 || value.size() == 9) &&
                               std::all_of(value.begin() + 1, value.end(), hex)
                         : (value.size() == 6 && std::all_of(value.begin(), value.end(), hex)) ||
                               (!value.empty() && std::all_of(value.begin(), value.end(), IsAsciiLetter));

        if (!valid)
            Fail(MathDiagnosticCode::UnexpectedToken, CurrentRange(), u"Invalid TeX color token.");

        if (value.size() == 6 && std::all_of(value.begin(), value.end(), hex))
            value = u"#" + value;
        return value;
    }
    TexLength ReadLength() {
        SkipMathSpaces(false);
        if (position < parseEnd && w.Expanded[position].Is(u"{"_u16))
            return ParseLength(ReadRawGroup(), CurrentRange());
        auto value = std::u16string{};
        auto range = CurrentRange();
        while (position < parseEnd) {
            auto part = w.Expanded[position].Text();
            if (part.size() > 0 && IsWhiteSpace(part[0])) {
                value.push_back(u' ');
                position++;
                continue;
            }
            if (part.size() != 1 ||
                !(IsDigit(part[0]) || (((part[0] == u'.') || (part[0] == u'+')) || (part[0] == u'-'))))
                break;
            value.append(part);
            position++;
        }
        SkipMathSpaces(false);
        for (auto unitLetter = 0; unitLetter < 2 && position < parseEnd; unitLetter++) {
            auto part = w.Expanded[position].Text();
            if (part.size() != 1 || !IsAsciiLetter(part[0]))
                break;
            value.append(part);
            position++;
        }
        SkipMathSpaces(false);
        return ParseLength(value, range);
    }
    int ReadRules() {
        SkipMathSpaces(false);
        auto count = 0;
        while (position < parseEnd &&
               (w.Expanded[position].Is(u"\\hline"_u16) || w.Expanded[position].Is(u"\\hdashline"_u16))) {
            auto dashed = w.Expanded[position++].Is(u"\\hdashline"_u16);
            if (count != 0 && (count < 0) != dashed)
                Fail(MathDiagnosticCode::UnsupportedCommand, CurrentRange(),
                     u"Mixed adjacent solid and dashed array rules are not implemented.");
            count += dashed ? -1 : 1;
            SkipMathSpaces(false);
        }
        return count;
    }
    void FinishEnvironment(int matrix) {
        // KaTeX reports the `\end` token when the environment does not match
        // (`src/functions/environment.ts`).
        auto endToken = position < parseEnd ? w.Expanded[position].Origin : CurrentRange();
        position++;
        auto name = ReadRawGroup();
        if (name != Node(matrix).Text)
            Fail(MathDiagnosticCode::UnexpectedToken, endToken,
                 u"Mismatch: \\begin{" + std::u16string(text(Node(matrix).Text)) + u"} matched by \\end{" + name +
                     u"}");
        auto expected = std::count_if(text(Node(matrix).Alignment).begin(), text(Node(matrix).Alignment).end(),
                                      [](char16_t c) { return c == u'l' || c == u'c' || c == u'r'; });
        if ((((text_is(Node(matrix).Text, u"array")) || (text_is(Node(matrix).Text, u"darray"))) ||
             (text_is(Node(matrix).Text, u"subarray"))) &&
            Node(matrix).ColumnCount > expected)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range,
                 u"Array contains more cells than its column specification.");
        // KaTeX's `cases` environment uses as many columns as the widest row
        // has cells, so a row with more cells than the column description
        // is not an error.
        if (text_is(Node(matrix).Text, u"alignedat") && Node(matrix).ColumnCount > Node(matrix).Value * 2)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range,
                 u"Alignedat contains more cells than its column pair count.");
        if (text_is(Node(matrix).Text, u"CD"))
            TransformCommutativeDiagram(matrix);
    }
    void TransformCommutativeDiagram(int matrix) {
        int rowNumber = 0, finalRow = -1;

        for (int row = Node(matrix).FirstChild; row >= 0; row = Node(row).NextSibling, rowNumber++) {
            finalRow = row;
            std::vector<int> values;

            for (int cell = Node(row).FirstChild; cell >= 0; cell = Node(cell).NextSibling) {
                int style = Node(cell).FirstChild, body = Node(style).FirstChild;
                for (int part = Node(body).FirstChild; part >= 0; part = Node(part).NextSibling)
                    values.push_back(part);
            }

            Node(row).FirstChild = -1;
            int lastCell = -1, column = 0;
            std::vector<int> contents;

            auto IsCharacter = [&](int index, int cp) {
                return Node(index).Kind == NodeKind::Symbol && Node(index).CodePoint == cp;
            };

            auto MakeRow = [&](const std::vector<int>& parts) {
                int body = AddEdited(w, settings, NodeKind::Row, Node(row).Range, [&](auto& v) { v.Value = 1; }),
                    last = -1;
                for (int part : parts) {
                    Node(part).NextSibling = -1;
                    if (last < 0)
                        Node(body).FirstChild = part;
                    else
                        Node(last).NextSibling = part;
                    last = part;
                }
                return body;
            };

            auto AppendCell = [&](const std::vector<int>& parts) {
                int body = MakeRow(parts),
                    style = AddEdited(w, settings, NodeKind::Style, Node(row).Range, [&](auto& v) {
                                    v.FirstChild = body;
                                    v.Style = MathStyle::Display;
                                });
                int cell = AddEdited(w, settings, NodeKind::MatrixCell, Node(row).Range, [&](auto& v) {
                                   v.FirstChild = style;
                               });
                if (lastCell < 0)
                    Node(row).FirstChild = cell;
                else
                    Node(lastCell).NextSibling = cell;
                lastCell = cell;
                column++;
            };

            for (int i = 0; i < (int)values.size(); i++) {
                if (!IsCharacter(values[i], u'@')) {
                    contents.push_back(values[i]);
                    continue;
                }

                AppendCell(contents);
                contents.clear();
                if (++i >= (int)values.size() || Node(values[i]).Kind != NodeKind::Symbol)
                    Fail(MathDiagnosticCode::UnexpectedToken, Node(row).Range,
                         u"CD arrow requires one of <>AV=|. after @.");

                int direction = Node(values[i]).CodePoint;
                if (direction == 0x2223)
                    direction = u'|';
                std::vector<int> labels[2];

                if (direction == u'<' || direction == u'>' || direction == u'A' || direction == u'V') {
                    for (int label = 0; label < 2; label++) {
                        bool closed = false;
                        while (++i < (int)values.size()) {
                            if (IsCharacter(values[i], direction)) {
                                closed = true;
                                break;
                            }
                            if (IsCharacter(values[i], u'@'))
                                Fail(MathDiagnosticCode::UnexpectedToken, Node(values[i]).Range,
                                     u"CD arrow label is missing its terminator.");
                            labels[label].push_back(values[i]);
                        }
                        if (!closed)
                            Fail(MathDiagnosticCode::UnclosedGroup, Node(row).Range,
                                 u"CD arrow label is missing its terminator.");
                    }
                }

                else if (direction != u'=' && direction != u'|' && direction != u'.')
                    Fail(MathDiagnosticCode::UnexpectedToken, Node(values[i]).Range,
                         u"CD arrow direction is unavailable.");

                int above = MakeRow(labels[0]), below = MakeRow(labels[1]), arrow;

                if (direction == u'<' || direction == u'>' || direction == u'=')
                    arrow = AddEdited(w, settings, NodeKind::Accent, Node(row).Range, [&](auto& v) {
                                    v.FirstChild = direction == u'=' ? -1 : above;
                                    v.SecondChild = direction == u'=' ? -1 : below;
                                    v.Atom = MathAtom::Rel;
                                    v.Text = direction == u'<'   ? u"\\\\cdleftarrow"
                                             : direction == u'>' ? u"\\\\cdrightarrow"
                                                                 : u"\\\\cdlongequal";
                                });

                else if (direction == u'.')
                    // KaTeX's CD environment renders the `@.` arrow as a
                    // textord space (`src/environments/cd.ts`).
                    arrow = AddEdited(w, settings, NodeKind::Symbol, Node(row).Range, [&](auto& v) {
                                    v.CodePoint = 0x20;
                                    v.Atom = MathAtom::Ord;
                                });

                else if (direction == u'|')
                    arrow = AddEdited(w, settings, NodeKind::Delimiter, Node(row).Range, [&](auto& v) {
                                    v.CodePoint = 0x2016;
                                    v.Value = 2;
                                });

                else {
                    int left = AddEdited(w, settings, NodeKind::Box, Node(row).Range, [&](auto& v) {
                                       v.Text = u"cdleft";
                                       v.FirstChild = above;
                                   });
                    int shaft =
                        AddEdited(w, settings, NodeKind::Delimiter, Node(row).Range, [&](auto& v) {
                                v.CodePoint = direction == u'A' ? 0x2191 : direction == u'V' ? 0x2193 : 0x2016;
                                v.Value = 2;
                            });
                    int right = AddEdited(w, settings, NodeKind::Box, Node(row).Range, [&](auto& v) {
                                        v.Text = u"cdright";
                                        v.FirstChild = below;
                                    });
                    Node(left).NextSibling = shaft;
                    Node(shaft).NextSibling = right;
                    int body = AddEdited(w, settings, NodeKind::Row, Node(row).Range, [&](auto& v) {
                                       v.FirstChild = left;
                                       v.Value = 1;
                                   });
                    arrow = AddEdited(w, settings, NodeKind::Box, Node(row).Range, [&](auto& v) {
                                    v.Text = u"cdparent";
                                    v.FirstChild = body;
                                });
                }

                AppendCell({arrow});
            }

            if ((rowNumber & 1) == 0)
                AppendCell(contents);
            else if (Node(row).FirstChild >= 0) {
                Node(row).FirstChild = Node(Node(row).FirstChild).NextSibling;
                column--;
            }

            Node(row).ColumnCount = column;
            Node(matrix).ColumnCount = std::max(Node(matrix).ColumnCount, column);
        }

        if (finalRow >= 0)
            Node(finalRow).NextSibling = AddNode(w, settings, NodeKind::MatrixRow, Node(matrix).Range);
    }
    void CompleteMatrixRow(int matrix, int row) {
        // The name of the environment is only read, so it is not copied.
        const auto& env = Node(matrix).Text;
        if (Node(row).SecondChild >= 0 || Node(row).NeverLimits ||
            !((((((env == u"equation"_u16) || (env == u"align"_u16)) || (env == u"alignat"_u16)) || (env == u"gather"_u16)))))
            return;
        auto tag = AddEdited(w, settings, NodeKind::Text, Node(row).Range, [&](auto& v) {
                           v.Font = MathFont::MathRoman;
                           v.TextMode = true;
                           v.Text = u"\\text";
                       });
        auto body = AddEdited(w, settings, NodeKind::Row, Node(row).Range, [&](auto& v) { v.TextMode = true; });
        Node(tag).FirstChild = body;
        auto last = -1;
        auto number = settings.EquationNumber;
        for (auto previous = Node(matrix).FirstChild; previous >= 0 && previous != row;
             previous = Node(previous).NextSibling)
            number++;
        for (auto character : u"(" + utf16(std::to_string(number)) + u")") {
            auto symbol = AddEdited(w, settings, NodeKind::Symbol, Node(row).Range, [&](auto& v) {
                                  v.CodePoint = character;
                                  v.TextMode = true;
                                  v.Font = MathFont::MathRoman;
                              });
            if (last < 0)
                Node(body).FirstChild = symbol;
            else
                Node(last).NextSibling = symbol;
            last = symbol;
        }
        Node(row).SecondChild = tag;
        Node(row).AlwaysLimits = true;
    }
    void AddMatrixCell(int matrix, int row, int previous, int column, const ParseTask& context) {
        if (context.Depth + 1 > settings.MaxNesting)
            Fail(MathDiagnosticCode::NestingLimit, CurrentRange(), u"Environment nesting budget exceeded.");
        auto cell = AddNode(w, settings, NodeKind::MatrixCell, CurrentRange());
        if (previous < 0)
            Node(row).FirstChild = cell;
        else
            Node(previous).NextSibling = cell;
        // The name of the environment is only read, so it is not copied.
        const auto& env = Node(matrix).Text;
        auto style = ((env == u"smallmatrix"_u16) || (env == u"subarray"_u16)) ? MathStyle::Script
                     : ((((((((((((((((env == u"aligned"_u16) || (env == u"alignedat"_u16)) || (env == u"align"_u16)) ||
                                    (env == u"align*"_u16)) ||
                                   (env == u"alignat"_u16)) ||
                                  (env == u"alignat*"_u16)) ||
                                 (env == u"gather"_u16)) ||
                                (env == u"gather*"_u16)) ||
                               (env == u"equation"_u16)) ||
                              (env == u"equation*"_u16)) ||
                             (env == u"CD"_u16)) ||
                            (env == u"gathered"_u16)) ||
                           (env == u"split"_u16)) ||
                          (env == u"darray"_u16)) ||
                         (env == u"dcases"_u16)) ||
                        (env == u"drcases"_u16))
                         ? MathStyle::Display
                         : MathStyle::Text;
        auto wrapper = AddEdited(w, settings, NodeKind::Style, CurrentRange(), [&](auto& v) {
                               v.Style = style;
                               v.Font = context.Font;
                           });
        Node(cell).FirstChild = wrapper;
        auto body = AddEdited(w, settings, NodeKind::Row, CurrentRange(), [&](auto& v) { v.Font = context.Font; });
        Node(wrapper).FirstChild = body;
        auto leading = -1;
        if ((column & 1) != 0 &&
            (((((((env == u"aligned"_u16) || (env == u"alignedat"_u16)) || (env == u"align"_u16)) || (env == u"align*"_u16)) ||
               (env == u"alignat"_u16)) ||
              (env == u"alignat*"_u16)) ||
             (env == u"split"_u16))) {
            leading = AddEdited(w, settings, NodeKind::Row, CurrentRange(), [&](auto& v) {
                              v.Value = 1;
                              v.Font = context.Font;
                          });
            Node(body).FirstChild = leading;
        }
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::MatrixNextCell;
                 v.Node = matrix;
                 v.Last = row;
                 v.Previous = cell;
                 v.Slot = column;
             });
        PushEdited(w, context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Sequence;
                 v.Node = body;
                 v.Last = leading;
                 v.Previous = -1;
                 v.Slot = 0;
                 v.Closing = false;
                 v.Stop = 4;
                 v.Depth = context.Depth + 1;
             });
    }

    int Run() {
        SymbolDefinition plainDefinition{};
        CommandDefinition commandRule{};
        SymbolDefinition characterDefinition{};
        SymbolDefinition opDefinition{};
        SymbolDefinition definition{};
        MathStyle style{};
        MathAtom atomClass{};
        std::u16string operatorText{};
        bool operatorLimits{};
        int accentCodePoint{};
        MathFont font{};
        MathFont textFont{};
        int scalar{};
        int pairCount{};
        bool subscript{};
        bool isSub{};
        auto root = AddNode(w, options, NodeKind::Row, SourceRange{0, int(source.size())});
        Push(w, ParseTask(ParseTaskKind::Sequence, root));
        position = 0;
        auto result = -1;
        auto delimiterDepth = 0;
        auto pendingArrayStretch = 0.0;
        // The text of the pending `\arraystretch` factor, which KaTeX quotes
        // when it rejects the factor.
        auto pendingArrayStretchText = std::u16string{};
        auto tokenCount = int(w.Expanded.size());
        parseEnd = tokenCount;
        while (w.Tasks.size() != 0) {
            cancellationToken.ThrowIfCancellationRequested();
            // The task stays on the stack while it is handled: a case that
            // continues with the task edits it in place, and a case that
            // consumes it pops it at the end of the iteration.
            ParseTask& task = w.Tasks.back();
            // Set when the case has left the stack in the state it wants, so
            // that the loop does not pop the task again.
            bool replaced = false;
            parseEnd = std::min(tokenCount, task.Limit);
            while (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0) {
                pendingArrayStretch = w.Expanded[position].InternalArrayStretch;
                pendingArrayStretchText = std::u16string(w.Expanded[position].Text());
                position++;
            }
            if (task.Depth > options.MaxNesting)
                Fail(MathDiagnosticCode::NestingLimit, CurrentRange(), u"Syntax nesting budget exceeded.");
            switch (task.Kind) {
            case ParseTaskKind::Return: {
                result = task.Node;
                break;
            }
            case ParseTaskKind::ReturnInternal: {
                result = -1;
                break;
            }
            case ParseTaskKind::AppendPrimeArgument: {
                Node(task.Node).NextSibling = result;
                break;
            }
            case ParseTaskKind::WrapHBoxArgument: {
                Node(task.Node).FirstChild =
                    result >= 0 && Node(result).Kind == NodeKind::Row
                        ? result
                        : AddEdited(w, options, NodeKind::Row, result >= 0 ? Node(result).Range : CurrentRange(), [&](auto& v) {
                                          v.FirstChild = result;
                                          v.Value = 1;
                                          v.TextMode = true;
                                      });
                break;
            }
            case ParseTaskKind::GenfracAfterDelimiters: {
                auto leftArgument = NormalizedSingle(Node(task.Last).FirstChild);
                auto rightArgument = NormalizedSingle(Node(task.Previous).FirstChild);
                if (leftArgument >= 0 && Node(leftArgument).Kind == NodeKind::Symbol &&
                    Node(leftArgument).Atom == MathAtom::Open)
                    Node(task.Node).LeftDelimiter = Node(leftArgument).CodePoint;
                if (rightArgument >= 0 && Node(rightArgument).Kind == NodeKind::Symbol &&
                    Node(rightArgument).Atom == MathAtom::Close)
                    Node(task.Node).RightDelimiter = Node(rightArgument).CodePoint;
                SkipMathSpaces(false);
                auto blankThickness = false;
                TexLength barLength;
                if (position < parseEnd && w.Expanded[position].Is(u"{"_u16)) {
                    SourceRange groupRange{};
                    auto raw = ReadRawGroup(&groupRange);
                    blankThickness = raw.size() == 0;
                    barLength = ParseLength(raw, groupRange);
                } else
                    barLength = ReadLength();
                Node(task.Node).HasBar = blankThickness || barLength.Number > 0;
                Node(task.Node).Length1 = barLength;
                Node(task.Node).Value = blankThickness ? 0 : LengthEm(barLength);
                Node(task.Node).Text =
                    blankThickness ? std::optional<std::u16string>{} : std::optional<std::u16string>{u"\\genfrac"};
                SkipMathSpaces(false);
                std::u16string rawStyle = position < parseEnd && w.Expanded[position].Is(u"{"_u16) ? ReadRawGroup()
                                          : position < parseEnd ? std::u16string(w.Expanded[position++].Text())
                                                                : u"";
                auto outerFraction = task.Node;
                if (rawStyle.size() > 0 && ((rawStyle[0] >= u'0') && (rawStyle[0] <= u'3')))
                    outerFraction =
                        AddEdited(w, options, NodeKind::Style, Node(task.Node).Range, [&](auto& v) {
                                v.FirstChild = task.Node;
                                v.Style = (MathStyle)(rawStyle[0] - u'0');
                                v.Font = task.Font;
                            });
                PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, outerFraction),
                                   [&](const ParseTask& context) {
                                       PushArgumentEdited(context.Node, 1, context, [&](auto& v) { v.Text = false; });
                                       PushArgumentEdited(context.Node, 0, context, [&](auto& v) { v.Text = false; });
                                   });
                break;
            }
            case ParseTaskKind::FinishTag: {
                Node(task.Node).FirstChild =
                    result >= 0 && Node(result).Kind == NodeKind::Row ? Node(result).FirstChild : result;
                break;
            }
            case ParseTaskKind::FinishTopTag: {
                Node(task.Node).SecondChild = result;
                result = AddNode(w, options, NodeKind::Row, Node(task.Node).Range);
                break;
            }
            case ParseTaskKind::WrapArgument: {
                // KaTeX's `parseArgumentGroup` (`src/Parser.ts`) wraps every
                // command argument in an ordgroup.  A braced argument already
                // is one; a single token argument has to be wrapped here.
                if (result >= 0 && Node(result).Kind != NodeKind::Row) {
                    auto row = AddEdited(w, options, NodeKind::Row, Node(result).Range, [&](auto& v) {
                                       v.FirstChild = result;
                                       v.Value = 1;
                                       v.TextMode = Node(result).TextMode;
                                       v.Font = Node(result).Font;
                                   });
                    result = row;
                }
                break;
            }
            case ParseTaskKind::NormalizeChild: {
                auto argument = Node(task.Node).FirstChild;
                if (argument >= 0 && Node(argument).Kind == NodeKind::Row && Node(argument).Value == 1 &&
                    Node(argument).FirstChild >= 0 && Node(Node(argument).FirstChild).NextSibling < 0) {
                    Node(task.Node).FirstChild = Node(argument).FirstChild;
                }
                result = task.Node;
                break;
            }
            case ParseTaskKind::FinishClass: {
                if ((((text_is(Node(task.Node).Text, u"\\overset")) || (text_is(Node(task.Node).Text, u"\\underset"))) ||
                     (text_is(Node(task.Node).Text, u"\\stackrel")))) {
                    auto shifted = Node(task.Node).FirstChild;
                    auto basis = Node(task.Node).SecondChild;
                    auto op =
                        AddEdited(w, options, NodeKind::Operator, Node(task.Node).Range, [&](auto& v) {
                                // KaTeX's `mclass` builder takes the base of
                                // `\overset`/`\underset` from `ordargument`,
                                // i.e. the whole body of the argument group and
                                // not just its first child
                                // (`src/functions/mclass.ts`).
                                v.FirstChild = basis;
                                v.Limits = true;
                                v.AlwaysLimits = true;
                                v.Text = u"\\mathop";
                                v.Atom = MathAtom::Op;
                                v.Value = text_is(Node(task.Node).Text, u"\\stackrel") ? 0 : 1;
                            });
                    auto scripts =
                        AddEdited(w, options, NodeKind::Script, Node(task.Node).Range, [&](auto& v) {
                                v.FirstChild = op;
                                v.SecondChild = text_is(Node(task.Node).Text, u"\\underset") ? -1 : shifted;
                                v.ThirdChild = text_is(Node(task.Node).Text, u"\\underset") ? shifted : -1;
                            });
                    Node(task.Node).Atom = text_is(Node(task.Node).Text, u"\\stackrel") ? MathAtom::Rel : BinRel(basis);
                    Node(task.Node).FirstChild = scripts;
                    Node(task.Node).SecondChild = -1;
                } else if (text_is(Node(task.Node).Text, u"\\@binrel")) {
                    // KaTeX's `\@binrel` is an `mclass` function whose class is
                    // `binrelClass(args[0])` and whose body is
                    // `ordargument(args[1])` (`src/functions/mclass.ts`).  The
                    // HTML builder dispatches an mclass group on its command
                    // label, so the node carries the math class command that
                    // matches the class KaTeX would have used; the class
                    // itself stays in `Atom`.
                    Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild);
                    Node(task.Node).FirstChild = Node(task.Node).SecondChild;
                    Node(task.Node).SecondChild = -1;
                    Node(task.Node).Text = MathClassCommand(Node(task.Node).Atom);
                } else if ((((text_is(Node(task.Node).Text, u"\\boldsymbol")) || (text_is(Node(task.Node).Text, u"\\bm"))) ||
                            (text_is(Node(task.Node).Text, u"\\pmb"))))
                    Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild);
                result = task.Node;
                break;
            }
            case ParseTaskKind::FinishDelimited: {
                if (position >= parseEnd || !w.Expanded[position].Is(u"\\right"_u16))
                    Fail(MathDiagnosticCode::UnclosedGroup, Node(task.Node).Range,
                         u"A left delimiter requires a matching right delimiter.");
                position++;
                Node(task.Node).RightDelimiter = ReadDelimiter(u"\\right");
                delimiterDepth--;
                result = task.Node;
                break;
            }
            case ParseTaskKind::MatrixNextCell: {
                // The cell pushes the tasks of the next cell, so the task
                // leaves the stack before they are pushed.
                ParseTask context = task;
                w.Tasks.pop_back();
                replaced = true;
                SkipMathSpaces(false);
                if (position >= parseEnd)
                    Fail(MathDiagnosticCode::UnclosedGroup, Node(context.Node).Range, u"Unclosed math environment.");
                if (w.Expanded[position].Is(u"&"_u16)) {
                    position++;
                    AddMatrixCell(context.Node, context.Last, context.Previous, context.Slot + 1, context);
                    break;
                }
                Node(context.Last).ColumnCount = context.Slot + 1;
                Node(context.Node).ColumnCount = std::max(Node(context.Node).ColumnCount, context.Slot + 1);
                CompleteMatrixRow(context.Node, context.Last);
                if (w.Expanded[position].Is(u"\\\\"_u16) || w.Expanded[position].Is(u"\\cr"_u16)) {
                    position++;
                    SkipMathSpaces(false);
                    if (position < parseEnd && w.Expanded[position].Is(u"["_u16)) {
                        auto gap = ParseLength(ReadRawBracket(), CurrentRange());
                        Node(context.Last).Length1 = gap;
                        Node(context.Last).Value = LengthEm(gap);
                    }
                    SkipMathSpaces(false);
                    auto rules = ReadRules();
                    if (position < parseEnd && w.Expanded[position].Is(u"\\end"_u16)) {
                        Node(context.Node).RuleBelow = rules;
                        FinishEnvironment(context.Node);
                        result = context.Node;
                        break;
                    }
                    auto nextRow = AddEdited(w, options, NodeKind::MatrixRow, CurrentRange(), [&](auto& v) { v.RuleAbove = rules; });
                    Node(context.Last).NextSibling = nextRow;
                    AddMatrixCell(context.Node, nextRow, -1, 0, context);
                    break;
                }
                if (!w.Expanded[position].Is(u"\\end"_u16))
                    Fail(MathDiagnosticCode::UnexpectedToken, CurrentRange(),
                         u"Expected a column separator, row separator, or environment end.");
                FinishEnvironment(context.Node);
                result = context.Node;
                break;
            }
            case ParseTaskKind::SetChild: {
                auto& parent = Node(task.Node);
                if (task.Slot == 0)
                    parent.FirstChild = result;
                else if (task.Slot == 1)
                    parent.SecondChild = result;
                else if (task.Slot == 2)
                    parent.ThirdChild = result;
                else
                    parent.FourthChild = result;
                break;
            }
            case ParseTaskKind::AttachSequence: {
                if (result < 0) {
                    EditTask(task, replaced, [&](auto& v) { v.Kind = ParseTaskKind::Sequence; });
                    break;
                }
                if (task.Last < 0)
                    Node(task.Node).FirstChild = result;
                else
                    Node(task.Last).NextSibling = result;
                // The task is edited in place, so the value of `Last` has to
                // be read before the field is written.
                const int previous = task.Last;
                EditTask(task, replaced, [&](auto& v) {
                         v.Kind = ParseTaskKind::Sequence;
                         v.Last = result;
                         v.Previous = previous;
                     });
                break;
            }
            case ParseTaskKind::Sequence: {
                SkipMathSpaces(task.Text);
                if (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0) {
                    pendingArrayStretch = w.Expanded[position].InternalArrayStretch;
                    pendingArrayStretchText = std::u16string(w.Expanded[position].Text());
                    position++;
                    // The task continues unchanged, so it stays on the stack.
                    replaced = true;
                    break;
                }
                if (position < parseEnd && w.Expanded[position].Is(u"\\relax"_u16)) {
                    position++;
                    // The task continues unchanged, so it stays on the stack.
                    replaced = true;
                    break;
                }
                if (position < parseEnd &&
                    (task.Stop == 2 && w.Expanded[position].Is(u"]"_u16) ||
                     task.Stop == 3 && w.Expanded[position].Is(u"\\right"_u16) ||
                     task.Stop == 5 && w.Expanded[position].Is(u"\\endgroup"_u16) ||
                     task.Stop == 6 && w.Expanded[position].Is(u"$"_u16) ||
                     task.Stop == 7 && w.Expanded[position].Is(u"\\)"_u16) ||
                     task.Stop == 4 && (w.Expanded[position].Is(u"&"_u16) || w.Expanded[position].Is(u"\\\\"_u16) ||
                                        w.Expanded[position].Is(u"\\cr"_u16) || w.Expanded[position].Is(u"\\end"_u16)))) {
                    if (((((task.Stop == 2) || (task.Stop == 5)) || (task.Stop == 6)) || (task.Stop == 7)))
                        position++;
                    if (task.Text)
                        FormLigatures(task.Node);
                    result = task.Node;
                    break;
                }
                if (position == parseEnd || w.Expanded[position].Is(u"}"_u16)) {
                    if (task.Stop != 0)
                        Fail(MathDiagnosticCode::UnclosedGroup, ArgumentEndRange(), StopMessage(task.Stop));
                    if (position == parseEnd && task.Closing) {
                        // KaTeX checks the closing brace of a group with `expect`
                        // (`src/Parser.ts`, `parseGroup`).  A group that is a
                        // command argument is scanned as a whole, so it reports
                        // the missing argument instead.  A primitive argument
                        // and a script argument are parsed token by token, so
                        // they report the missing brace.
                        if (task.ArgumentGroup && !task.PrimitiveArgument)
                            Fail(MathDiagnosticCode::UnclosedGroup, ArgumentEndRange(),
                                 u"Unexpected end of input in a macro argument, expected '}'");
                        Fail(MathDiagnosticCode::UnclosedGroup, ArgumentEndRange(), u"Expected '}', got 'EOF'");
                    }
                    if (position < parseEnd) {
                        if (!task.Closing)
                            Fail(MathDiagnosticCode::UnexpectedGroupEnd, w.Expanded[position].Origin,
                                 ExpectedMessage(u"EOF", u"}"));
                        auto closing = w.Expanded[position++].Origin;
                        auto old = Node(task.Node).Range;
                        Node(task.Node).Range =
                            SourceRange{old.Start, std::max(old.Length, closing.Start + closing.Length - old.Start)};
                    }
                    if (task.Text)
                        FormLigatures(task.Node);
                    result = task.Node;
                    break;
                }
                if (task.Stop == 0 && IsEndOfExpression(w.Expanded[position].Text())) {
                    // KaTeX stops an expression at `}`, `\endgroup`, `\end`,
                    // `\right` and `&` and then checks the token it expected
                    // (`src/Parser.ts`, `Parser.endOfExpression`).
                    auto text = w.Expanded[position].Text();
                    Fail(MathDiagnosticCode::UnexpectedToken, w.Expanded[position].Origin,
                         ExpectedMessage(task.Closing ? u"}" : u"EOF", text));
                }
                const LexToken& next = w.Expanded[position];
                // The scalar of the token is only needed for the characters
                // that carry a script, so it is not decoded for the others.
                const bool hasScript = !task.Text && next.Text()[0] != u'\\' &&
                                       HasUnicodeScript(RuneAt(*next.Source, next.Start));
                if (hasScript) {
                    auto scalar = RuneAt(*next.Source, next.Start);
                    UnicodeData::Script(scalar, subscript);
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = AddNode(w, options, NodeKind::Row, next.Origin);
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = empty;
                                     });
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = task.Last;
                                     });
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if ((subscript ? Node(script).ThirdChild : Node(script).SecondChild) >= 0)
                        Fail(subscript ? MathDiagnosticCode::DoubleSubscript : MathDiagnosticCode::DoubleSuperscript,
                             next.Origin, u"Duplicate Unicode script.");
                    auto row = AddEdited(w, options, NodeKind::Row, next.Origin, [&](auto& v) { v.Value = 1; });
                    auto last = -1;
                    while (position < parseEnd) {
                        const LexToken& part = w.Expanded[position];
                        if (part.Text()[0] == u'\\')
                            break;
                        auto mapped = UnicodeData::Script(RuneAt(*part.Source, part.Start), isSub);
                        if ((mapped == std::nullopt) || isSub != subscript)
                            break;
                        auto value = AddSymbol(*mapped, part.Origin, task);
                        if (last < 0)
                            Node(row).FirstChild = value;
                        else
                            Node(last).NextSibling = value;
                        last = value;
                        position++;
                    }
                    if (subscript)
                        Node(script).ThirdChild = row;
                    else
                        Node(script).SecondChild = row;
                    EditTask(task, replaced, [&](auto& v) { v.Last = script; });
                    break;
                }
                if (!task.Text && next.Is(u"'"_u16)) {
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = AddNode(w, options, NodeKind::Row, next.Origin);
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = empty;
                                     });
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = task.Last;
                                     });
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if (Node(script).SecondChild >= 0)
                        Fail(MathDiagnosticCode::DoubleSuperscript, next.Origin,
                             u"A base cannot have both an explicit superscript and a prime superscript.");
                    auto primes = AddEdited(w, options, NodeKind::Row, next.Origin, [&](auto& v) { v.Value = 1; });
                    auto previousPrime = -1;
                    while (position < parseEnd && w.Expanded[position].Is(u"'"_u16)) {
                        const LexToken& primeToken = w.Expanded[position++];
                        auto prime =
                            AddEdited(w, options, NodeKind::Symbol, primeToken.Origin, [&](auto& v) {
                                    v.CodePoint = 0x2032;
                                    v.Font = MathFont::Default;
                                });
                        if (previousPrime < 0)
                            Node(primes).FirstChild = prime;
                        else
                            Node(previousPrime).NextSibling = prime;
                        previousPrime = prime;
                    }
                    Node(script).SecondChild = primes;
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) { v.Last = script; });
                    if (position < parseEnd && w.Expanded[position].Is(u"^"_u16)) {
                        position++;
                        PushEdited(w, copy, [&](auto& v) {
                                 v.Kind = ParseTaskKind::AppendPrimeArgument;
                                 v.Node = previousPrime;
                             });
                        PushEdited(w, copy, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Argument;
                                 v.Stop = 0;
                                 v.Closing = false;
                                 v.Depth = copy.Depth + 1;
                                 v.PrimitiveArgument = true;
                                 v.Slot = -1;
                                 v.Style = ArgumentStyle::Script;
                                 v.ArgumentName = u"^";
                                 v.ArgumentIndex = position - 1;
                             });
                    }
                    break;
                }
                if (!task.Text && (((next.Text() == u"\\limits"_u16) || (next.Text() == u"\\nolimits"_u16)) ||
                                   (next.Text() == u"\\displaylimits"_u16))) {
                    position++;
                    auto op = task.Last;
                    if (op >= 0 && Node(op).Kind == NodeKind::Script)
                        op = Node(op).FirstChild;
                    if (op < 0 || Node(op).Kind != NodeKind::Operator)
                        Fail(MathDiagnosticCode::UnexpectedToken, next.Origin,
                             u"Limit controls must follow a math operator");
                    if (Node(op).Text != u"\\operatorname"_u16 || Node(op).Text2 == u"star"_u16) {
                        Node(op).AlwaysLimits = next.Is(u"\\limits"_u16);
                        Node(op).NeverLimits = next.Is(u"\\nolimits"_u16);
                        // KaTeX sets `limits` on the operator, so that the
                        // super/subscripts are rendered as limits
                        // (`src/Parser.ts`, `parseAtom`).
                        if (next.Is(u"\\limits"_u16)) {
                            Node(op).Limits = true;
                        } else if (next.Is(u"\\nolimits"_u16)) {
                            Node(op).Limits = false;
                        } else if (next.Is(u"\\displaylimits"_u16)) {
                            Node(op).Limits = true;
                        }
                    }
                    // The task continues unchanged, so it stays on the stack.
                    replaced = true;
                    break;
                }
                if (!task.Text &&
                    ((((((next.Text() == u"\\over"_u16) || (next.Text() == u"\\atop"_u16)) || (next.Text() == u"\\choose"_u16)) ||
                       (next.Text() == u"\\brace"_u16)) ||
                      (next.Text() == u"\\brack"_u16)) ||
                     (next.Text() == u"\\above"_u16))) {
                    if (task.Slot == 1)
                        Fail(MathDiagnosticCode::UnexpectedToken, next.Origin,
                             u"only one infix operator per group");
                    position++;
                    auto numerator =
                        AddEdited(w, options, NodeKind::Row, Node(task.Node).Range, [&](auto& v) {
                                v.FirstChild = Node(task.Node).FirstChild;
                                v.Font = task.Font;
                            });
                    auto denominator =
                        AddEdited(w, options, NodeKind::Row, next.Origin, [&](auto& v) { v.Font = task.Font; });
                    auto fraction =
                        AddEdited(w, options, NodeKind::Fraction, next.Origin, [&](auto& v) {
                                v.FirstChild = numerator;
                                v.SecondChild = denominator;
                                v.HasBar = next.Is(u"\\over"_u16);
                                v.LeftDelimiter = next.Is(u"\\choose"_u16)  ? u'('
                                                  : next.Is(u"\\brace"_u16) ? u'{'
                                                  : next.Is(u"\\brack"_u16) ? u'['
                                                                        : 0;
                                v.RightDelimiter = next.Is(u"\\choose"_u16)  ? u')'
                                                   : next.Is(u"\\brace"_u16) ? u'}'
                                                   : next.Is(u"\\brack"_u16) ? u']'
                                                                         : 0;
                            });
                    if (next.Is(u"\\above"_u16)) {
                        auto length = ReadLength();
                        Node(fraction).Length1 = length;
                        Node(fraction).Value = LengthEm(length);
                        Node(fraction).HasBar = length.Number > 0;
                        Node(fraction).Text = u"\\above";
                    }
                    Node(task.Node).FirstChild = fraction;
                    PushAfter(w, task, replaced, ParseTask(ParseTaskKind::Return, task.Node),
                              [&](auto& v) {
                                  v.Node = denominator;
                                  v.Last = -1;
                                  v.Previous = -1;
                                  v.Slot = 1;
                              });
                    break;
                }
                if (!task.Text && (next.Is(u"^"_u16) || next.Is(u"_"_u16))) {
                    position++;
                    auto superscript = next.Is(u"^"_u16);
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = AddNode(w, options, NodeKind::Row, next.Origin);
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = empty;
                                         v.Font = task.Font;
                                     });
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = AddEdited(w, options, NodeKind::Script, next.Origin, [&](auto& v) {
                                         v.FirstChild = task.Last;
                                         v.Font = task.Font;
                                     });
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if ((superscript ? Node(script).SecondChild : Node(script).ThirdChild) != -1)
                        Fail(superscript ? MathDiagnosticCode::DoubleSuperscript : MathDiagnosticCode::DoubleSubscript,
                             next.Origin, superscript ? u"Double superscript" : u"Double subscript");
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) { v.Last = script; });
                    PushEdited(w, copy, [&](auto& v) {
                             v.Kind = ParseTaskKind::SetChild;
                             v.Node = script;
                             v.Slot = superscript ? 1 : 2;
                         });
                    PushEdited(w, copy, [&](auto& v) {
                             v.Kind = ParseTaskKind::Argument;
                             v.Depth = copy.Depth + 1;
                             v.Stop = 0;
                             v.Closing = false;
                             v.Slot = -1;
                             v.PrimitiveArgument = true;
                             v.Style = ArgumentStyle::Script;
                             v.ArgumentName = superscript ? u"^" : u"_";
                             v.ArgumentIndex = position - 1;
                         });
                    break;
                }
                if (TryStyle(next.Text(), style)) {
                    position++;
                    auto wrapper = AddEdited(w, options, NodeKind::Style, next.Origin, [&](auto& v) {
                                           v.Style = style;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       });
                    auto inner = AddEdited(w, options, NodeKind::Row, next.Origin, [&](auto& v) {
                                         v.Font = task.Font;
                                         v.TextMode = task.Text;
                                     });
                    Node(wrapper).FirstChild = inner;
                    if (task.Last < 0)
                        Node(task.Node).FirstChild = wrapper;
                    else
                        Node(task.Last).NextSibling = wrapper;
                    PushAfter(w, task, replaced, ParseTask(ParseTaskKind::Return, task.Node),
                              [&](auto& v) {
                                  v.Node = inner;
                                  v.Last = -1;
                                  v.Previous = -1;
                                  v.Depth = task.Depth + 1;
                              });
                    break;
                }
                auto sizing = 0;
                auto legacyFont = task.Font;
                if (TrySizing(next.Text(), sizing) || TryLegacyFont(next.Text(), legacyFont) ||
                    next.Is(u"\\color"_u16) && !options.ColorIsTextColor) {
                    position++;
                    auto kind = next.Is(u"\\color"_u16) ? NodeKind::Color : sizing > 0 ? NodeKind::Sizing : NodeKind::Font;
                    auto wrapper = AddEdited(w, options, kind, next.Origin, [&](auto& v) {
                                           v.Value = sizing;
                                           v.Font = sizing > 0 ? task.Font : legacyFont;
                                           v.Text = next.Text();
                                           v.TextMode = task.Text;
                                       });
                    if (kind == NodeKind::Color)
                        Node(wrapper).Text2 = ReadColor();
                    auto inner = AddEdited(w, options, NodeKind::Row, next.Origin, [&](auto& v) {
                                         v.Font = kind == NodeKind::Font ? legacyFont : task.Font;
                                         v.TextMode = task.Text;
                                     });
                    Node(wrapper).FirstChild = inner;
                    if (task.Last < 0)
                        Node(task.Node).FirstChild = wrapper;
                    else
                        Node(task.Last).NextSibling = wrapper;
                    PushAfter(w, task, replaced, ParseTask(ParseTaskKind::Return, task.Node),
                              [&](auto& v) {
                                  v.Node = inner;
                                  v.Last = -1;
                                  v.Previous = -1;
                                  v.Font = kind == NodeKind::Font ? legacyFont : task.Font;
                                  v.Depth = task.Depth + 1;
                              });
                    break;
                }
                // The continuation is the task itself, so it stays on the
                // stack; the task that runs before it is a copy of it.
                ParseTask copy = task;
                EditTask(task, replaced, [&](auto& v) { v.Kind = ParseTaskKind::AttachSequence; });
                PushEdited(w, copy, [&](auto& v) {
                         v.Kind = ParseTaskKind::Atom;
                         // An atom of an expression is never a command argument.
                         v.ArgumentGroup = false;
                     });
                break;
            }
            case ParseTaskKind::Argument: {
                SkipMathSpaces(false);
                if (task.Slot == -1)
                    while (position < parseEnd && w.Expanded[position].Is(u"\\relax"_u16)) {
                        position++;
                        SkipMathSpaces(false);
                    }
                if (position == parseEnd || w.Expanded[position].Is(u"}"_u16) || w.Expanded[position].Is(u"^"_u16) ||
                    w.Expanded[position].Is(u"_"_u16)) {
                    // KaTeX reports every kind of missing argument differently
                    // (`src/Parser.ts`, `parseGroupOfType` and
                    // `handleSupSubscript`).
                    if (task.Style == ArgumentStyle::Script)
                        Fail(MathDiagnosticCode::MissingArgument,
                             task.ArgumentIndex >= 0 && task.ArgumentIndex < (int)w.Expanded.size()
                                 ? w.Expanded[task.ArgumentIndex].Origin
                                 : CurrentRange(),
                             u"Expected group after '" + std::u16string(task.ArgumentName) + u"'");
                    if (task.Style == ArgumentStyle::Primitive)
                        Fail(MathDiagnosticCode::MissingArgument, ArgumentEndRange(),
                             u"Expected group as argument to '" + std::u16string(task.ArgumentName) + u"'");
                    if (position < parseEnd && w.Expanded[position].Is(u"}"_u16)) {
                        if (position == task.GroupClose) {
                            // The brace closes the enclosing argument group.
                            // KaTeX strips the braces of such a group from the
                            // tokens of the argument, so the argument ends
                            // here (`src/MacroExpander.ts`, `consumeArg`).
                            Fail(MathDiagnosticCode::MissingArgument, ArgumentEndRange(),
                                 u"Unexpected end of input in a macro argument, expected '}'");
                        }
                        Fail(MathDiagnosticCode::UnexpectedGroupEnd, ArgumentEndRange(), u"Extra }");
                    }
                    if (position < parseEnd)
                        Fail(MathDiagnosticCode::MissingArgument, ArgumentEndRange(),
                             ExpectedMessage(u"EOF", w.Expanded[position].Text()));
                    Fail(MathDiagnosticCode::MissingArgument, ArgumentEndRange(),
                         u"Unexpected end of input in a macro argument, expected '}'");
                }
                EditTask(task, replaced, [&](auto& v) {
                         v.Kind = ParseTaskKind::Atom;
                         v.ArgumentGroup = true;
                         // A braced argument is scanned as a whole, so the
                         // tokens of the group end the argument.
                         if (!task.PrimitiveArgument && w.Expanded[position].Is(u"{"_u16)) {
                             auto close = GroupEnd(position);
                             v.GroupClose = close;
                             v.Limit = close < 0 ? task.Limit : std::min(task.Limit, close + 1);
                         } else {
                             v.Limit = !task.PrimitiveArgument ? std::min(task.Limit, position + 1) : task.Limit;
                         }
                     });
                break;
            }
            case ParseTaskKind::Atom: {
                if (position == parseEnd)
                    Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Missing argument.");
                const LexToken& token = w.Expanded[position++];
                if (!token.NoExpand && token.Text().size() == 1 && IsPlainAtomCharacter(token.Text()[0]) &&
                    FindSymbol(token.Text(), task.Text, plainDefinition) && plainDefinition.CodePoint >= 0 &&
                    !((((plainDefinition.Group == u"op-token"_u16) || (plainDefinition.Group == u"accent-token"_u16))))) {
                    result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                     v.CodePoint = plainDefinition.CodePoint;
                                     v.FontIdOverride = -1;
                                     v.Atom = plainDefinition.Atom;
                                     v.Font = task.Font == MathFont::Default ? plainDefinition.Font : task.Font;
                                     v.SymbolFont = plainDefinition.Font;
                                     v.TextMode = task.Text;
                                     v.IsMathOrd = plainDefinition.IsMathOrd;
                                 });
                    break;
                }
                if (FindCommand(token.Text(), commandRule)) {
                    // KaTeX rejects a function that cannot take arguments
                    // before it rejects the mode the function is used in
                    // (`src/Parser.ts`, `parseFunction`).
                    if (task.Slot == -1 && !commandRule.AllowedInArgument) {
                        if (task.Style == ArgumentStyle::Script)
                            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                                 u"Got function '" + std::u16string(token.Text()) + u"' with no arguments as " +
                                     std::u16string(task.ArgumentName == u"^"_u16 ? u"superscript" : u"subscript"));
                        if (task.Style == ArgumentStyle::Primitive)
                            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                                 u"Got function '" + std::u16string(token.Text()) +
                                     u"' with no arguments as argument to '" + std::u16string(task.ArgumentName) + u"'");
                    }
                    if (task.Text && !commandRule.AllowedInText)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Can't use function '" + std::u16string(token.Text()) + u"' in text mode");
                    if (!task.Text && !commandRule.AllowedInMath)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Can't use function '" + std::u16string(token.Text()) + u"' in math mode");
                }
                if (token.Is(u"\\hline"_u16) || token.Is(u"\\hdashline"_u16)) {
                    // KaTeX only accepts a horizontal rule inside an array
                    // environment (`src/environments/array.ts`).
                    Fail(MathDiagnosticCode::UnsupportedCommand, SourceRange{-1, 0},
                         std::u16string(token.Text()) + u" valid only within array environment");
                }
                if (token.Text().starts_with(u"\\verb"_u16) && token.Length > 5) {
                    auto content = token.Text().substr(5);
                    auto starred = content[0] == u'*';
                    if (starred)
                        content = content.substr(1);
                    result = AddEdited(w, options, NodeKind::Verb, token.Origin, [&](auto& v) {
                                     v.Text = std::u16string(content.substr(1, content.size() - 2));
                                     v.Value = starred ? 1 : 0;
                                     v.TextMode = true;
                                     v.Font = MathFont::MathTypewriter;
                                 });
                    break;
                }
                if (token.Is(u"\\@char"_u16)) {
                    auto number = ReadRawGroup();
                    if (!ParseInt(number, scalar) || !IsValidScalar(scalar))
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Character code must be a valid Unicode scalar.");
                    auto character = FromScalar(scalar);
                    if (FindSymbol(character, task.Text, characterDefinition))
                        result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                         v.CodePoint = scalar;
                                         v.Font = task.Font;
                                         v.SymbolFont = characterDefinition.Font;
                                         v.TextMode = task.Text;
                                         v.IsMathOrd = false;
                                     });
                    else
                        result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                         v.CodePoint = scalar;
                                         v.Font = task.Font;
                                         v.TextMode = task.Text;
                                     });
                    break;
                }
                if (token.Is(u"\\tag"_u16) || token.Is(u"\\tag@literal"_u16) || token.Is(u"\\tag@paren"_u16)) {
                    if (!options.DisplayMode)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Equation tags require display mode.");
                    SkipMathSpaces(false);
                    auto starred = token.Is(u"\\tag@literal"_u16) || position < parseEnd && w.Expanded[position].Is(u"*"_u16);
                    if (position < parseEnd && w.Expanded[position].Is(u"*"_u16))
                        position++;
                    auto target = root;
                    for (auto i = int(w.Tasks.size()) - 1; i >= 0; i--)
                        if (w.Tasks[i].Kind == ParseTaskKind::MatrixNextCell) {
                            target = w.Tasks[i].Last;
                            break;
                        }
                    if (Node(target).SecondChild >= 0)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Multiple tags in one equation row.");
                    auto tag = AddEdited(w, options, NodeKind::Text, token.Origin, [&](auto& v) {
                                       v.Font = MathFont::MathRoman;
                                       v.TextMode = true;
                                       v.Text = u"\\text";
                                   });
                    if (starred) {
                        Node(target).SecondChild = tag;
                        // The continuation is the task itself, so it stays on
                        // the stack; the tasks that run before it are copies.
                        ParseTask copy = task;
                        EditTask(task, replaced, [&](auto& v) { v.Kind = ParseTaskKind::ReturnInternal; });
                        PushArgumentEdited(tag, 0, copy, [&](auto& v) {
                                         v.Text = true;
                                         v.Font = MathFont::MathRoman;
                                     });
                    } else {
                        auto body = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                            v.TextMode = true;
                                        });
                        Node(tag).FirstChild = body;
                        auto left = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                            v.CodePoint = u'(';
                                            v.Font = MathFont::MathRoman;
                                            v.TextMode = true;
                                        });
                        Node(body).FirstChild = left;
                        auto tagArgument =
                            AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                    v.TextMode = true;
                                    v.Font = MathFont::MathRoman;
                                    v.Value = 1;
                                });
                        Node(left).NextSibling = tagArgument;
                        auto right =
                            AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                    v.CodePoint = u')';
                                    v.Font = MathFont::MathRoman;
                                    v.TextMode = true;
                                });
                        Node(tagArgument).NextSibling = right;
                        Node(target).SecondChild = tag;
                        // The continuation is the task itself, so it stays on
                        // the stack; the tasks that run before it are copies.
                        ParseTask copy = task;
                        EditTask(task, replaced, [&](auto& v) { v.Kind = ParseTaskKind::ReturnInternal; });
                        PushEdited(w, copy, [&](auto& v) {
                                 v.Kind = ParseTaskKind::FinishTag;
                                 v.Node = tagArgument;
                             });
                        PushEdited(w, copy, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Argument;
                                 v.Text = true;
                                 v.Font = MathFont::MathRoman;
                                 v.Depth = copy.Depth + 1;
                                 v.Stop = 0;
                                 v.Closing = false;
                             });
                    }
                    break;
                }
                if (token.Is(u"\\nonumber"_u16) || token.Is(u"\\notag"_u16)) {
                    for (auto i = int(w.Tasks.size()) - 1; i >= 0; i--)
                        if (w.Tasks[i].Kind == ParseTaskKind::MatrixNextCell) {
                            Node(w.Tasks[i].Last).NeverLimits = true;
                            break;
                        }
                    result = -1;
                    break;
                }
                if (token.Is(u"\\relax"_u16)) {
                    result = AddNode(w, options, NodeKind::Row, token.Origin);
                    break;
                }
                if (((token.Text() == u"\\nobreak"_u16) || (token.Text() == u"\\allowbreak"_u16))) {
                    result = AddEdited(w, options, NodeKind::Space, token.Origin, [&](auto& v) {
                                     v.Text = token.Text();
                                 });
                    break;
                }
                if (token.Is(u"\\\\"_u16)) {
                    SkipMathSpaces(false);
                    auto gap = position < parseEnd && w.Expanded[position].Is(u"["_u16)
                                   ? ParseLength(ReadRawBracket(), token.Origin)
                                   : TexLength{};
                    auto newLine = !options.DisplayMode ||
                                   !Strict(u"newLineInDisplayMode", token.Origin,
                                           u"In LaTeX, \\ or \\newline does nothing in display mode", false);
                    result = AddEdited(w, options, NodeKind::Space, token.Origin, [&](auto& v) {
                                     v.Text = u"\\\\";
                                     v.Length1 = gap;
                                     v.Value = newLine ? 1 : 0;
                                     v.TextMode = task.Text;
                                 });
                    break;
                }
                if (token.Is(u"\\mathchoice"_u16) || token.Is(u"\\html@mathml"_u16)) {
                    auto choice =
                        AddEdited(w, options, token.Is(u"\\mathchoice"_u16) ? NodeKind::Choice : NodeKind::MathMlChoice, token.Origin, [&](auto& v) {
                                        v.Font = task.Font;
                                        v.TextMode = task.Text;
                                        v.Text = token.Text();
                                    });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, choice),
                                       [&](const ParseTask& context) {
                                           if (token.Is(u"\\mathchoice"_u16)) {
                                               PushArgument(choice, 3, context, true);
                                               PushArgument(choice, 2, context, true);
                                           }
                                           PushArgument(choice, 1, context, token.Is(u"\\mathchoice"_u16));
                                           PushArgument(choice, 0, context, token.Is(u"\\mathchoice"_u16));
                                       });
                    break;
                }
                if (token.Is(u"\\textcolor"_u16) || token.Is(u"\\color"_u16) && options.ColorIsTextColor) {
                    auto color = ReadColor();
                    auto wrapper = AddEdited(w, options, NodeKind::Color, token.Origin, [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Text2 = color;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       });
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::NormalizeChild;
                             v.Node = wrapper;
                         });
                    PushArgument(wrapper, 0, copy);
                    break;
                }
                if (token.Is(u"\\rule"_u16)) {
                    SkipMathSpaces(false);
                    auto shift = position < parseEnd && w.Expanded[position].Is(u"["_u16)
                                     ? ParseLength(ReadRawBracket(), token.Origin)
                                     : TexLength{};
                    result = AddEdited(w, options, NodeKind::Rule, token.Origin, [&](auto& v) {
                                     v.Text = u"\\rule";
                                     v.Length1 = ReadLength();
                                     v.Length2 = ReadLength();
                                     v.Length3 = shift;
                                     v.TextMode = task.Text;
                                 });
                    break;
                }
                if (((((token.Text() == u"\\kern"_u16) || (token.Text() == u"\\mkern"_u16)) || (token.Text() == u"\\hskip"_u16)) ||
                     (token.Text() == u"\\mskip"_u16))) {
                    auto dimension = ReadLength();
                    // KaTeX words the three cases of the spacing units
                    // separately (`src/functions/kern.ts`).  The violations
                    // are raised without a token, so they carry no position.
                    {
                        const bool mathFunction =
                            (token.Text() == u"\\mkern"_u16) || (token.Text() == u"\\mskip"_u16);
                        const SourceRange no_token{-1, 0};
                        if (mathFunction) {
                            if (dimension.Unit != TexUnit::Mu)
                                Strict(u"mathVsTextUnits", no_token,
                                       JoinMessage({u"LaTeX's "_u16, token.Text(),
                                                    u" supports only mu units, not "_u16,
                                                    UnitName(dimension.Unit), u" units"_u16}));
                            if (!task.Text)
                                Strict(u"mathVsTextUnits", no_token,
                                       JoinMessage({u"LaTeX's "_u16, token.Text(),
                                                    u" works only in math mode"_u16}));
                        } else if (dimension.Unit == TexUnit::Mu) {
                            Strict(u"mathVsTextUnits", no_token,
                                   JoinMessage({u"LaTeX's "_u16, token.Text(),
                                                u" doesn't support mu units"_u16}));
                        }
                    }
                    result = AddEdited(w, options, NodeKind::Space, token.Origin, [&](auto& v) {
                                     v.Text = token.Text();
                                     v.Length1 = dimension;
                                     v.Font = task.Font;
                                     v.TextMode = task.Text;
                                 });
                    break;
                }
                if (((((((((((((token.Text() == u"\\phantom"_u16) || (token.Text() == u"\\vphantom"_u16)) ||
                              (token.Text() == u"\\smash"_u16)) ||
                             (token.Text() == u"\\mathllap"_u16)) ||
                            (token.Text() == u"\\mathrlap"_u16)) ||
                           (token.Text() == u"\\mathclap"_u16)) ||
                          (token.Text() == u"\\raisebox"_u16)) ||
                         (token.Text() == u"\\vcenter"_u16)) ||
                        (token.Text() == u"\\pmb"_u16)) ||
                       (token.Text() == u"\\reflectbox"_u16)) ||
                      (token.Text() == u"\\mathreflectbox"_u16)) ||
                     (token.Text() == u"\\hbox"_u16))) {
                    auto wrapper = AddEdited(w, options, NodeKind::Box, token.Origin, [&](auto& v) {
                                           // KaTeX gives `\mathreflectbox` the
                                           // same builders as `\reflectbox`
                                           // (`src/functions/reflectbox.ts`),
                                           // so both share the label.
                                           v.Text = token.Is(u"\\mathreflectbox"_u16) ? std::u16string(u"\\reflectbox"_u16)
                                                                                   : token.Text();
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       });
                    if (token.Is(u"\\raisebox"_u16))
                        Node(wrapper).Length1 = ReadLength();
                    if (token.Is(u"\\smash"_u16)) {
                        SkipMathSpaces(false);
                        Node(wrapper).Alignment =
                            position < parseEnd && w.Expanded[position].Is(u"["_u16) ? ReadRawBracket() : u"tb";
                    }
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = token.Is(u"\\phantom"_u16) || token.Is(u"\\hbox"_u16) ? ParseTaskKind::NormalizeChild
                                      : token.Is(u"\\pmb"_u16)                          ? ParseTaskKind::FinishClass
                                                                                    : ParseTaskKind::Return;
                             v.Node = wrapper;
                         });
                    auto textArgument = (((token.Text() == u"\\raisebox"_u16) || (token.Text() == u"\\reflectbox"_u16)) ||
                                         (token.Text() == u"\\hbox"_u16));
                    if (((token.Text() == u"\\raisebox"_u16) || (token.Text() == u"\\reflectbox"_u16)))
                        PushHBoxArgument(wrapper, copy);
                    else
                        PushArgumentEdited(wrapper, 0, copy, [&](auto& v) {
                                         v.Text = textArgument || copy.Text;
                                         v.Font = textArgument ? MathFont::MathRoman : copy.Font;
                                     });
                    break;
                }
                if ((((((((((token.Text() == u"\\colorbox"_u16) || (token.Text() == u"\\fcolorbox"_u16)) ||
                           (token.Text() == u"\\fbox"_u16)) ||
                          (token.Text() == u"\\cancel"_u16)) ||
                         (token.Text() == u"\\bcancel"_u16)) ||
                        (token.Text() == u"\\xcancel"_u16)) ||
                       (token.Text() == u"\\phase"_u16)) ||
                      (token.Text() == u"\\sout"_u16)) ||
                     (token.Text() == u"\\angl"_u16))) {
                    if (token.Is(u"\\sout"_u16) && !task.Text)
                        // KaTeX raises the violation without a token
                        // (`src/functions/enclose.ts`).
                        Strict(u"mathVsSout", SourceRange{-1, 0},
                               u"LaTeX's \\sout works only in text mode");
                    auto wrapper = AddEdited(w, options, NodeKind::Enclose, token.Origin, [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       });
                    if (token.Is(u"\\fcolorbox"_u16))
                        Node(wrapper).Text3 = ReadColor();
                    if (token.Is(u"\\colorbox"_u16) || token.Is(u"\\fcolorbox"_u16))
                        Node(wrapper).Text2 = ReadColor();
                    auto textArgument = ((((token.Text() == u"\\colorbox"_u16) || (token.Text() == u"\\fcolorbox"_u16)) ||
                                          (token.Text() == u"\\fbox"_u16)) ||
                                         (token.Text() == u"\\angl"_u16));
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, wrapper),
                                       [&](const ParseTask& context) {
                                           if (textArgument)
                                               PushHBoxArgument(wrapper, context);
                                           else
                                               PushArgument(wrapper, 0, context);
                                       });
                    break;
                }
                if (TryClass(token.Text(), atomClass) ||
                    ((((token.Text() == u"\\@binrel"_u16) || (token.Text() == u"\\overset"_u16)) ||
                      (token.Text() == u"\\underset"_u16)) ||
                     (token.Text() == u"\\stackrel"_u16))) {
                    auto wrapper = AddEdited(w, options, NodeKind::Class, token.Origin, [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Atom = atomClass;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       });
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishClass;
                             v.Node = wrapper;
                         });
                    if (((((token.Text() == u"\\@binrel"_u16) || (token.Text() == u"\\overset"_u16)) ||
                          (token.Text() == u"\\underset"_u16)) ||
                         (token.Text() == u"\\stackrel"_u16)))
                        PushArgument(wrapper, 1, copy);
                    // The `mclass` functions are primitives in KaTeX, so their
                    // base is parsed as a primitive argument
                    // (`src/functions/mclass.ts`).
                    auto mclass = (token.Text() == u"\\mathord"_u16) || (token.Text() == u"\\mathbin"_u16) ||
                                  (token.Text() == u"\\mathrel"_u16) || (token.Text() == u"\\mathopen"_u16) ||
                                  (token.Text() == u"\\mathclose"_u16) || (token.Text() == u"\\mathpunct"_u16) ||
                                  (token.Text() == u"\\mathinner"_u16);
                    PushArgument(wrapper, 0, copy, mclass,
                                 mclass ? ArgumentStyle::Primitive : ArgumentStyle::Normal, token.Text());
                    break;
                }
                if (((token.Text() == u"\\boldsymbol"_u16) || (token.Text() == u"\\bm"_u16))) {
                    auto wrapper = AddEdited(w, options, NodeKind::Class, token.Origin, [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Font = MathFont::MathBoldItalic;
                                       });
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishClass;
                             v.Node = wrapper;
                         });
                    PushArgumentEdited(wrapper, 0, copy, [&](auto& v) { v.Font = MathFont::MathBoldItalic; });
                    break;
                }
                if (task.Text && (token.Is(u"$"_u16) || token.Is(u"\\("_u16))) {
                    auto wrapper = AddEdited(w, options, NodeKind::Style, token.Origin, [&](auto& v) {
                                           v.Style = MathStyle::Text;
                                           v.Font = MathFont::Default;
                                           v.TextMode = true;
                                       });
                    auto body = AddNode(w, options, NodeKind::Row, token.Origin);
                    Node(wrapper).FirstChild = body;
                    PushAfter(w, task, replaced, ParseTask(ParseTaskKind::Return, wrapper),
                              [&](auto& v) {
                                  v.Kind = ParseTaskKind::Sequence;
                                  v.Node = body;
                                  v.Last = -1;
                                  v.Previous = -1;
                                  v.Stop = token.Is(u"$"_u16) ? (int)6 : (int)7;
                                  v.Closing = false;
                                  v.Text = false;
                                  v.Font = MathFont::Default;
                                  v.Depth = task.Depth + 1;
                              });
                    break;
                }
                if (task.Text && (token.Is(u"\\)"_u16) || token.Is(u"\\]"_u16))) {
                    // KaTeX reports a closing math delimiter that has no
                    // opening one (`src/functions/math.ts`).
                    Fail(MathDiagnosticCode::UnexpectedToken, SourceRange{-1, 0},
                         u"Mismatched " + std::u16string(token.Text()));
                }
                if (token.Is(u"{"_u16) || token.Is(u"\\begingroup"_u16)) {
                    auto semisimple = token.Is(u"\\begingroup"_u16);
                    auto row = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                       v.Value = semisimple ? 2 : 1;
                                       v.TextMode = task.Text;
                                       v.Font = task.Font;
                                   });
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::Sequence;
                             v.Node = row;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Closing = !semisimple;
                             v.Stop = semisimple ? (int)5 : (int)0;
                             v.Slot = 0;
                             v.ArgumentGroup = task.ArgumentGroup;
                             v.GroupClose = task.ArgumentGroup ? task.GroupClose : -1;
                             v.Depth = task.Depth + 1;
                         });
                    break;
                }
                if (((((((((((token.Text() == u"\\frac"_u16) || (token.Text() == u"\\dfrac"_u16)) ||
                            (token.Text() == u"\\tfrac"_u16)) ||
                           (token.Text() == u"\\cfrac"_u16)) ||
                          (token.Text() == u"\\binom"_u16)) ||
                         (token.Text() == u"\\dbinom"_u16)) ||
                        (token.Text() == u"\\tbinom"_u16)) ||
                       (token.Text() == u"\\\\atopfrac"_u16)) ||
                      (token.Text() == u"\\\\bracefrac"_u16)) ||
                     (token.Text() == u"\\\\brackfrac"_u16))) {
                    if (task.Text)
                        Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                             u"Fractions are only supported in math mode.");
                    auto binom = token.Text().ends_with(u"binom"_u16);
                    auto noBar = binom || token.Text().starts_with(u"\\\\"_u16);
                    auto fraction =
                        AddEdited(w, options, NodeKind::Fraction, token.Origin, [&](auto& v) {
                                v.Font = task.Font;
                                v.HasBar = !noBar;
                                v.Text = token.Is(u"\\cfrac"_u16) ? std::optional<std::u16string>{u"\\cfrac"}
                                                              : std::optional<std::u16string>{};
                                v.LeftDelimiter = binom                        ? u'('
                                                  : token.Is(u"\\\\bracefrac"_u16) ? u'{'
                                                  : token.Is(u"\\\\brackfrac"_u16) ? u'['
                                                                               : 0;
                                v.RightDelimiter = binom                        ? u')'
                                                   : token.Is(u"\\\\bracefrac"_u16) ? u'}'
                                                   : token.Is(u"\\\\brackfrac"_u16) ? u']'
                                                                                : 0;
                            });
                    auto outer = fraction;
                    if (((token.Text()[1] == u'd') || (token.Text()[1] == u't')) || token.Is(u"\\cfrac"_u16))
                        outer = AddEdited(w, options, NodeKind::Style, token.Origin, [&](auto& v) {
                                        v.FirstChild = fraction;
                                        v.Style = token.Text()[1] == u't' ? MathStyle::Text : MathStyle::Display;
                                        v.Font = task.Font;
                                    });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, outer),
                                       [&](const ParseTask& context) {
                                           PushArgument(fraction, 1, context);
                                           PushArgument(fraction, 0, context);
                                       });
                    break;
                }
                if (token.Is(u"\\sqrt"_u16)) {
                    if (task.Text)
                        Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                             u"Radicals are only supported in math mode.");
                    auto radical = AddEdited(w, options, NodeKind::Radical, token.Origin, [&](auto& v) {
                                           v.Font = task.Font;
                                       });
                    SkipMathSpaces(false);
                    auto index = -1;
                    if (position < parseEnd && w.Expanded[position].Is(u"["_u16)) {
                        position++;
                        index = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                        v.Font = task.Font;
                                    });
                        Node(radical).SecondChild = index;
                    }
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, radical),
                                       [&](const ParseTask& context) {
                                           // KaTeX parses the body of a radical as a primitive
                                           // argument when the radical has no index
                                           // (`src/Parser.ts`, `parseArguments`).
                                           PushArgument(radical, 0, context, index < 0,
                                                        index < 0 ? ArgumentStyle::Primitive : ArgumentStyle::Normal, u"\\sqrt");
                                           if (index >= 0)
                                               PushEdited(w, context, [&](auto& v) {
                                                   v.Kind = ParseTaskKind::Sequence;
                                                   v.Node = index;
                                                   v.Last = -1;
                                                   v.Previous = -1;
                                                   v.Closing = false;
                                                   v.Stop = 2;
                                                   v.Slot = 0;
                                                   v.Depth = context.Depth + 1;
                                               });
                                       });
                    break;
                }
                if (token.Is(u"\\genfrac"_u16)) {
                    auto fraction = AddEdited(
                        w, options, NodeKind::Fraction, token.Origin, [&](auto& v) { v.Font = task.Font; });
                    auto leftHolder = AddNode(w, options, NodeKind::Row, token.Origin);
                    auto rightHolder = AddNode(w, options, NodeKind::Row, token.Origin);
                    // The continuation is the task itself, so it stays on the
                    // stack; the tasks that run before it are copies of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::GenfracAfterDelimiters;
                             v.Node = fraction;
                             v.Last = leftHolder;
                             v.Previous = rightHolder;
                         });
                    PushArgumentEdited(rightHolder, 0, copy, [&](auto& v) { v.Text = false; });
                    PushArgumentEdited(leftHolder, 0, copy, [&](auto& v) { v.Text = false; });
                    break;
                }
                if (token.Is(u"\\left"_u16)) {
                    auto left = ReadDelimiter(u"\\left");
                    auto wrapper =
                        AddEdited(w, options, NodeKind::Delimited, token.Origin, [&](auto& v) {
                                v.LeftDelimiter = left;
                                v.Atom = MathAtom::Inner;
                                v.Font = task.Font;
                            });
                    auto body =
                        AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) { v.Font = task.Font; });
                    Node(wrapper).FirstChild = body;
                    delimiterDepth++;
                    // The continuation is the task itself, so it stays on the
                    // stack; the task that runs before it is a copy of it.
                    ParseTask copy = task;
                    EditTask(task, replaced, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishDelimited;
                             v.Node = wrapper;
                         });
                    PushEdited(w, copy, [&](auto& v) {
                             v.Kind = ParseTaskKind::Sequence;
                             v.Node = body;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Closing = false;
                             v.Stop = 3;
                             v.Slot = 0;
                             v.Depth = copy.Depth + 1;
                         });
                    break;
                }
                auto delimiterSize = 0;
                auto delimiterAtom = MathAtom::Ord;
                if (token.Is(u"\\middle"_u16) || TryDelimiterSize(token.Text(), delimiterSize, delimiterAtom)) {
                    auto middle = token.Is(u"\\middle"_u16);
                    SourceRange delimiterRange{};
                    // The `\big` family takes the single element of a braced
                    // argument, `\middle` does not
                    // (`src/functions/delimsizing.ts`).
                    auto cp = ReadDelimiter(middle ? u"\\middle" : token.Text(), &delimiterRange, !middle);
                    if (middle && delimiterDepth == 0)
                        // KaTeX reports the delimiter of a `\middle` that has no
                        // `\left` (`src/functions/delimsizing.ts`).
                        Fail(MathDiagnosticCode::UnexpectedToken, delimiterRange,
                             u"\\middle without preceding \\left");
                    result = AddEdited(w, options, NodeKind::Delimiter, token.Origin, [&](auto& v) {
                                     v.CodePoint = cp;
                                     v.Value = token.Is(u"\\middle"_u16) ? 0 : delimiterSize;
                                     v.Atom = token.Is(u"\\middle"_u16) ? MathAtom::Rel : delimiterAtom;
                                     v.Font = task.Font;
                                 });
                    break;
                }
                if (token.Is(u"\\begin"_u16)) {
                    SourceRange environmentRange{};
                    auto environment = ReadRawGroup(&environmentRange);
                    if ((((((((((environment == u"equation"_u16) || (environment == u"equation*"_u16)) ||
                               (environment == u"align"_u16)) ||
                              (environment == u"align*"_u16)) ||
                             (environment == u"alignat"_u16)) ||
                            (environment == u"alignat*"_u16)) ||
                           (environment == u"gather"_u16)) ||
                          (environment == u"gather*"_u16)) ||
                         (environment == u"CD"_u16)) &&
                        !options.DisplayMode)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"This environment requires display mode.");
                    if (!IsEnvironment(environment))
                        // KaTeX reports the name of the environment it does not
                        // know (`src/functions/environment.ts`).
                        Fail(MathDiagnosticCode::UnsupportedCommand, environmentRange,
                             u"No such environment: " + environment);
                    auto fixedStretch =
                        (((((((environment == u"cases"_u16) || (environment == u"dcases"_u16)) || (environment == u"rcases"_u16)) ||
                            (environment == u"drcases"_u16)) ||
                           (environment == u"smallmatrix"_u16)) ||
                          (environment == u"subarray"_u16)) ||
                         (environment == u"CD"_u16));
                    if (!fixedStretch && pendingArrayStretch != 0) {
                        // KaTeX rejects a stretch factor that is not a positive
                        // number (`src/environments/array.ts`).
                        if (!std::isfinite(pendingArrayStretch) || pendingArrayStretch <= 0)
                            Fail(MathDiagnosticCode::UnexpectedToken, SourceRange{-1, 0},
                                 u"Invalid \\arraystretch: " + pendingArrayStretchText);
                        if (pendingArrayStretch > 1000)
                            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                                 u"Array stretch factor exceeds the supported range.");
                    }
                    if (fixedStretch)
                        pendingArrayStretch = 0;
                    std::vector<SourceRange> alignmentRanges;
                    auto alignment =
                        (((environment == u"array"_u16) || (environment == u"darray"_u16)) || (environment == u"subarray"_u16))
                            ? ReadAlignment(alignmentRanges)
                        : environment.ends_with(u'*') && position < parseEnd && w.Expanded[position].Is(u"["_u16)
                            ? ReadRawBracket()
                            : u"c";
                    for (auto i = 0u; i < alignment.size(); i++) {
                        auto a = alignment[i];
                        if (!(((((((a == u'l') || (a == u'c')) || (a == u'r')) || (a == u'|')) || (a == u':')))))
                            // KaTeX reports the column alignment it does not
                            // know (`src/environments/array.ts`).
                            Fail(MathDiagnosticCode::UnsupportedCommand,
                                 i < alignmentRanges.size() ? alignmentRanges[i] : token.Origin,
                                 u"Unknown column alignment: " + std::u16string(1, a));
                    }
                    if (alignment.size() == 0)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Expected a nonempty column specification.");
                    auto matrix = AddEdited(w, options, NodeKind::Matrix, token.Origin, [&](auto& v) {
                                          v.Text = environment;
                                          v.Alignment = alignment;
                                          v.Font = task.Font;
                                          v.Value2 = pendingArrayStretch;
                                      });
                    pendingArrayStretch = 0;
                    auto envBase = TrimEnd(environment, u'*');
                    Node(matrix).LeftDelimiter =
                        ((envBase == u"pmatrix"_u16)
                             ? u'('
                             : ((envBase == u"bmatrix"_u16)
                                    ? u'['
                                    : ((((envBase == u"Bmatrix"_u16) || (envBase == u"cases"_u16)) || (envBase == u"dcases"_u16))
                                           ? u'{'
                                           : ((envBase == u"vmatrix"_u16) ? u'|'
                                                                      : ((envBase == u"Vmatrix"_u16) ? 0x2016 : 0)))));
                    Node(matrix).RightDelimiter =
                        ((envBase == u"pmatrix"_u16)
                             ? u')'
                             : ((envBase == u"bmatrix"_u16)
                                    ? u']'
                                    : ((((envBase == u"Bmatrix"_u16) || (envBase == u"rcases"_u16)) || (envBase == u"drcases"_u16))
                                           ? u'}'
                                           : ((envBase == u"vmatrix"_u16) ? u'|'
                                                                      : ((envBase == u"Vmatrix"_u16) ? 0x2016 : 0)))));
                    if ((((environment == u"alignedat"_u16) || (environment == u"alignat"_u16)) ||
                         (environment == u"alignat*"_u16))) {
                        auto pairs = ReadRawGroup();
                        if (!ParseInt(pairs, pairCount) || pairCount < 1 || pairCount > options.MaxNodes / 2)
                            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                                 u"Alignedat requires a positive bounded column pair count.");
                        Node(matrix).Value = pairCount;
                    }
                    auto rules = ReadRules();
                    auto row = AddEdited(w, options, NodeKind::MatrixRow, token.Origin, [&](auto& v) {
                                       v.RuleAbove = rules;
                                   });
                    Node(matrix).FirstChild = row;
                    // The cell pushes the tasks of the cell, so the task
                    // leaves the stack before they are pushed.
                    ParseTask context = task;
                    w.Tasks.pop_back();
                    replaced = true;
                    AddMatrixCell(matrix, row, -1, 0, context);
                    break;
                }
                if (((((token.Text() == u"\\operatorname"_u16) || (token.Text() == u"\\operatorname@"_u16)) ||
                      (token.Text() == u"\\operatornamewithlimits"_u16)) ||
                     (token.Text() == u"\\mathop"_u16))) {
                    SkipMathSpaces(false);
                    auto starred = token.Is(u"\\operatornamewithlimits"_u16);
                    if (token.Is(u"\\operatorname"_u16) && position < parseEnd && w.Expanded[position].Is(u"*"_u16)) {
                        position++;
                        starred = true;
                    }
                    auto op = AddEdited(w, options, NodeKind::Operator, token.Origin, [&](auto& v) {
                                      v.Text = token.Is(u"\\mathop"_u16) ? u"\\mathop" : u"\\operatorname";
                                      v.Text2 = starred ? std::optional<std::u16string>{u"star"}
                                                        : std::optional<std::u16string>{};
                                      // KaTeX's `operatorname` always has
                                      // `limits: false`; the star only sets
                                      // `alwaysHandleSupSub`
                                      // (`src/functions/operatorname.ts`).
                                      v.Limits = false;
                                      v.Atom = MathAtom::Op;
                                      v.Font = task.Font;
                                  });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, op),
                                       [&](const ParseTask& context) {
                                           PushArgumentEdited(op, 0, context, [&](auto& v) {
                                               v.Font = token.Is(u"\\mathop"_u16) ? context.Font : MathFont::MathRoman;
                                           }, token.Is(u"\\mathop"_u16), token.Is(u"\\mathop"_u16) ? ArgumentStyle::Primitive : ArgumentStyle::Normal, u"\\mathop");
                                       });
                    break;
                }
                if (TryNamedOperator(token.Text(), operatorText, operatorLimits)) {
                    auto op = AddEdited(w, options, NodeKind::Operator, token.Origin, [&](auto& v) {
                                      v.Text = token.Text();
                                      v.Limits = operatorLimits;
                                      v.Atom = MathAtom::Op;
                                      v.Font = task.Font;
                                  });
                    auto row = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                       v.Font = MathFont::MathRoman;
                                   });
                    Node(op).FirstChild = row;
                    auto previous = -1;
                    for (auto c : operatorText) {
                        auto letter =
                            c == u' ' ? AddEdited(w, options, NodeKind::Space, token.Origin,
                                                 [&](auto& v) { v.Value = 3.0 / 18; })
                                      : AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                            v.CodePoint = c;
                                            v.Font = MathFont::MathRoman;
                                            v.SymbolFont = MathFont::Default;
                                            v.IsMathOrd = true;
                                        });
                        if (previous < 0)
                            Node(row).FirstChild = letter;
                        else
                            Node(previous).NextSibling = letter;
                        previous = letter;
                    }
                    result = op;
                    break;
                }
                auto unicodeOperator =
                    token.Text().size() == 1
                        ? ((token.Text()[0] == u'∏')
                               ? u"\\prod"
                               : ((token.Text()[0] == u'∐')
                                      ? u"\\coprod"
                                      : ((token.Text()[0] == u'∑')
                                             ? u"\\sum"
                                             : ((token.Text()[0] == u'⋀')
                                                    ? u"\\bigwedge"
                                                    : ((token.Text()[0] == u'⋁')
                                                           ? u"\\bigvee"
                                                           : ((token.Text()[0] == u'⋂')
                                                                  ? u"\\bigcap"
                                                                  : ((token.Text()[0] == u'⋃')
                                                                         ? u"\\bigcup"
                                                                         : ((token.Text()[0] == u'⨀')
                                                                                ? u"\\bigodot"
                                                                                : ((token.Text()[0] == u'⨁')
                                                                                       ? u"\\bigoplus"
                                                                                       : ((token.Text()[0] == u'⨂')
                                                                                              ? u"\\bigotimes"
                                                                                              : ((token.Text()[0] ==
                                                                                                  u'⨄')
                                                                                                     ? u"\\biguplus"
                                                                                                     : ((token.Text()
                                                                                                             [0] ==
                                                                                                         u'⨆')
                                                                                                            ? u"\\bigsq"
                                                                                                              u"cup"
                                                                                                            : ((token.Text()
                                                                                                                    [0] ==
                                                                                                                u'∫')
                                                                                                                   ? u"\\int"
                                                                                                                   : ((token
                                                                                                                           .Text()
                                                                                                                               [0] ==
                                                                                                                       u'∬')
                                                                                                                          ? u"\\iint"
                                                                                                                          : ((token
                                                                                                                                  .Text()
                                                                                                                                      [0] ==
                                                                                                                              u'∭')
                                                                                                                                 ? u"\\iiint"
                                                                                                                                 : ((token
                                                                                                                                         .Text()
                                                                                                                                             [0] ==
                                                                                                                                     u'∮')
                                                                                                                                        ? u"\\oint"
                                                                                                                                        : ((token
                                                                                                                                                .Text()
                                                                                                                                                    [0] ==
                                                                                                                                            u'∯')
                                                                                                                                               ? u"\\oiint"
                                                                                                                                               : ((token
                                                                                                                                                       .Text()
                                                                                                                                                           [0] ==
                                                                                                                                                   u'∰')
                                                                                                                                                      ? std::optional<
                                                                                                                                                            std::
                                                                                                                                                                u16string>{u"\\oiiint"}
                                                                                                                                                      : std::optional<
                                                                                                                                                            std::
                                                                                                                                                                u16string>{}))))))))))))))))))
                        : std::nullopt;
                if (!((unicodeOperator == std::nullopt)) && FindSymbol(*unicodeOperator, false, opDefinition)) {
                    result = AddEdited(w, options, NodeKind::Operator, token.Origin, [&](auto& v) {
                                     v.Text = unicodeOperator;
                                     v.CodePoint = opDefinition.CodePoint;
                                     v.Atom = MathAtom::Op;
                                     v.Limits = !(
                                         (((opDefinition.CodePoint >= 0x222b) && (opDefinition.CodePoint <= 0x2233))));
                                     v.Font = task.Font;
                                 });
                    break;
                }
                if (TryAccent(token.Text(), accentCodePoint)) {
                    // The accents that KaTeX accepts in text mode.  The
                    // lookup is a table because the parser asks for every
                    // token.
                    const std::u16string_view tokenText = token.Text();
                    bool textAccent = false;
                    {
                        static const std::vector<std::u16string> table = [] {
                            const char* const names[] = {"\\'", "\\`", "\\^", "\\~", "\\=", "\\u",
                                                         "\\.", "\\\"", "\\c", "\\r", "\\H", "\\v",
                                                         "\\textcircled"};
                            std::vector<std::u16string> entries;
                            for (const char* name : names)
                                entries.push_back(utf16(name));
                            std::sort(entries.begin(), entries.end());
                            return entries;
                        }();
                        textAccent = std::binary_search(table.begin(), table.end(), tokenText);
                        if (!textAccent)
                            textAccent = IsTextAccent(tokenText);
                    }
                    if (textAccent && !task.Text)
                        // KaTeX raises the violation without a token
                        // (`src/functions/accent.ts`).
                        Strict(u"mathVsTextAccents", SourceRange{-1, 0},
                               JoinMessage({u"LaTeX's accent "_u16, token.Text(),
                                            u" works only in text mode"_u16}));
                    auto accent = AddEdited(w, options, NodeKind::Accent, token.Origin, [&](auto& v) {
                                          v.Text = token.Text();
                                          v.CodePoint = accentCodePoint;
                                          v.Font = task.Font;
                                          v.TextMode = textAccent || task.Text;
                                      });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, accent),
                                       [&](const ParseTask& context) {
                                           PushArgument(accent, 0, context, textAccent);
                                       });
                    break;
                }
                if (IsExtensibleArrow(token.Text())) {
                    auto arrow = AddEdited(w, options, NodeKind::Accent, token.Origin, [&](auto& v) {
                                         v.Text = token.Text();
                                         v.Atom = MathAtom::Rel;
                                         v.Font = task.Font;
                                     });
                    SkipMathSpaces(false);
                    auto below = -1;
                    if (position < parseEnd && w.Expanded[position].Is(u"["_u16)) {
                        position++;
                        below = AddEdited(w, options, NodeKind::Row, token.Origin, [&](auto& v) {
                                        v.Font = task.Font;
                                    });
                        Node(arrow).SecondChild = below;
                    }
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, arrow),
                                       [&](const ParseTask& context) {
                                           PushArgument(arrow, 0, context);
                                           if (below >= 0)
                                               PushEdited(w, context, [&](auto& v) {
                                                   v.Kind = ParseTaskKind::Sequence;
                                                   v.Node = below;
                                                   v.Last = -1;
                                                   v.Previous = -1;
                                                   v.Closing = false;
                                                   v.Stop = 2;
                                                   v.Slot = 0;
                                                   v.Depth = context.Depth + 1;
                                               });
                                       });
                    break;
                }
                if (TryFont(token.Text(), font)) {
                    auto wrapper = AddEdited(w, options, NodeKind::Font, token.Origin, [&](auto& v) {
                                           v.Font = font;
                                           v.TextMode = task.Text;
                                       });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, wrapper),
                                       [&](const ParseTask& context) {
                                           PushArgumentEdited(wrapper, 0, context, [&](auto& v) { v.Font = font; });
                                       });
                    break;
                }
                if (TryTextFont(token.Text(), textFont)) {
                    auto family = task.TextFamily;
                    auto bold = task.TextBold;
                    auto italic = task.TextItalic;
                    if (((token.Text() == u"\\textrm"_u16) || (token.Text() == u"\\textnormal"_u16)))
                        family = 0;
                    else if (token.Is(u"\\textsf"_u16))
                        family = 1;
                    else if (token.Is(u"\\texttt"_u16))
                        family = 2;
                    if (token.Is(u"\\textbf"_u16))
                        bold = true;
                    else if (token.Is(u"\\textmd"_u16))
                        bold = false;
                    if (token.Is(u"\\textit"_u16))
                        italic = true;
                    else if (token.Is(u"\\textup"_u16))
                        italic = false;
                    else if (token.Is(u"\\emph"_u16))
                        italic = !italic;
                    textFont = family == 2      ? MathFont::MathTypewriter
                               : family == 1    ? bold && italic ? MathFont::MathSansBoldItalic
                                                  : bold         ? MathFont::MathSansBold
                                                  : italic       ? MathFont::MathSansItalic
                                                                 : MathFont::MathSans
                               : bold && italic ? MathFont::MainBoldItalic
                               : bold           ? MathFont::MathBold
                               : italic         ? MathFont::MathItalic
                                                : MathFont::MathRoman;
                    if (token.Is(u"\\text"_u16))
                        textFont = task.Font;
                    auto wrapper = AddEdited(w, options, NodeKind::Text, token.Origin, [&](auto& v) {
                                           v.Font = textFont;
                                           v.TextMode = true;
                                           v.Text = token.Text();
                                       });
                    PushAfterArguments(w, task, replaced, ParseTask(ParseTaskKind::Return, wrapper),
                                       [&](const ParseTask& context) {
                                           PushArgumentEdited(wrapper, 0, context, [&](auto& v) {
                                               v.Font = textFont;
                                               v.Text = true;
                                               v.TextFamily = family;
                                               v.TextBold = bold;
                                               v.TextItalic = italic;
                                           });
                                       });
                    break;
                }
                if (token.Is(u"\\,"_u16) || token.Is(u"\\:"_u16) || token.Is(u"\\;"_u16) || token.Is(u"\\!"_u16) ||
                    token.Is(u"\\quad"_u16) || token.Is(u"\\qquad"_u16)) {
                    auto width = token.Is(u"\\,"_u16)      ? 3.0 / 18
                                 : token.Is(u"\\:"_u16)    ? 4.0 / 18
                                 : token.Is(u"\\;"_u16)    ? 5.0 / 18
                                 : token.Is(u"\\!"_u16)    ? -3.0 / 18
                                 : token.Is(u"\\quad"_u16) ? 1
                                                       : 2;
                    if (task.Text)
                        width = token.Is(u"\\,"_u16)   ? .1667
                                : token.Is(u"\\:"_u16) ? .2222
                                : token.Is(u"\\;"_u16) ? .2777
                                : token.Is(u"\\!"_u16) ? -.1667
                                                   : width;
                    result = AddEdited(w, options, NodeKind::Space, token.Origin, [&](auto& v) {
                                     v.Value = width;
                                     v.TextMode = task.Text;
                                     v.Font = task.Font;
                                 });
                    break;
                }
                if (IsWhiteSpace(token.Text()[0]) && task.Text) {
                    result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                     v.CodePoint = 32;
                                     v.Font = task.Font;
                                     v.TextMode = true;
                                 });
                    break;
                }
                if (token.Is(u"}"_u16) || token.Is(u"^"_u16) || token.Is(u"_"_u16) || token.Is(u"&"_u16) || token.Is(u"#"_u16) ||
                    token.Is(u"$"_u16))
                    Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Unexpected TeX special token.");
                if (token.Text()[0] != u'\\' && (token.Text().size() > 1 || token.Text()[0] >= 0x80)) {
                    auto scalar = RuneAt(*token.Source, token.Start);
                    auto expansion = !FindSymbol(FromScalar(scalar), task.Text, definition)
                                         ? UnicodeData::Decompose(scalar)
                                         : std::nullopt;
                    if (!((expansion == std::nullopt)) && !task.Text)
                        Strict(u"unicodeTextInMathMode", token.Origin,
                               u"Accented Unicode text character \"" + std::u16string(1, token.Text()[0]) +
                                   u"\" used in math mode");
                    std::u16string combined =
                        !expansion
                            ? std::u16string(token.Text())
                            : *expansion +
                                  std::u16string(token.Text().substr(RuneLength(RuneAt(*token.Source, token.Start))));
                    auto baseLength = RuneLength(RuneAt(combined));
                    if (baseLength < combined.size() &&
                        ((combined[baseLength] >= u'\u0300') && (combined[baseLength] <= u'\u036f'))) {
                        auto baseText = combined.substr(0, baseLength);
                        if (baseText.size() == 1 && baseText[0] == u'i')
                            baseText = u"ı";
                        else if (baseText.size() == 1 && baseText[0] == u'j')
                            baseText = u"ȷ";
                        auto basis = AddSymbol(baseText, token.Origin, task);
                        for (auto mark : combined.substr(baseLength)) {
                            auto command = UnicodeData::Accent(mark, task.Text);
                            auto point = 0;
                            if ((command == std::nullopt) || !TryAccent(*command, point))
                                Fail(MathDiagnosticCode::UnknownSymbol, token.Origin, u"Unsupported combining accent.");
                            basis = AddEdited(w, options, NodeKind::Accent, token.Origin, [&](auto& v) {
                                            v.FirstChild = basis;
                                            v.Text = command;
                                            v.CodePoint = point;
                                            v.TextMode = task.Text;
                                            v.Font = task.Font;
                                        });
                        }
                        result = basis;
                        break;
                    }
                }
                if (token.Text()[0] == u'\\' && IsUntrustedCommand(token.Text())) {
                    result = FormatUnsupportedCmd(token, task);
                    break;
                }
                if (!FindSymbol(token.Text(), task.Text, definition)) {
                    // KaTeX renders every character it does not know as a text
                    // ordinal, so the HTML and MathML output stays lossless.
                    // The layout stage still rejects the character when the
                    // locked fonts have no glyph for it.
                    if (token.Text()[0] >= 0x80) {
                        // KaTeX reports a character it does not know before it
                        // reports a character that does not belong in math mode
                        // (`src/Parser.ts`).
                        if (UnicodeScriptFamily(static_cast<int>(token.Text()[0])) == nullptr)
                            Strict(u"unknownSymbol", token.Origin,
                                   JoinMessage({u"Unrecognized Unicode character \""_u16,
                                                std::u16string(1, token.Text()[0]), u"\" ("_u16,
                                                NumberText(static_cast<int>(token.Text()[0])), u")"_u16}));
                        else if (!task.Text)
                            Strict(u"unicodeTextInMathMode", token.Origin,
                                   JoinMessage({u"Unicode text character \""_u16,
                                                std::u16string(1, token.Text()[0]),
                                                u"\" used in math mode"_u16}));
                        result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                         v.CodePoint = RuneAt(*token.Source, token.Start);
                                         v.Font = task.Font;
                                         v.TextMode = true;
                                     });
                        break;
                    }
                    if (!options.ThrowOnError && token.Text()[0] == u'\\') {
                        // KaTeX renders an undefined control sequence as red
                        // text when `throwOnError` is false (`src/Parser.ts`,
                        // `formatUnsupportedCmd`).  The command text is not
                        // followed by its arguments, exactly like KaTeX.
                        result = FormatUnsupportedCmd(token, task, 0);
                        break;
                    }
                    Fail(token.Text()[0] == u'\\' ? MathDiagnosticCode::UnsupportedCommand
                                                  : MathDiagnosticCode::UnknownSymbol,
                         token.Origin,
                         token.Text()[0] == u'\\' ? u"Command is not implemented."
                                                  : u"Symbol requires an available font or an explicit text shaper.");
                }
                if (definition.CodePoint < 0 || definition.Group == u"accent-token"_u16)
                    Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                         u"This symbol requires an unsupported construction rule.");
                // KaTeX keeps three Latin characters for the text mode only
                // (`src/symbols.ts`, `extraLatin`) and reports their use in
                // math mode.
                if (!task.Text && (token.Text() == u"\u00d0"_u16 || token.Text() == u"\u00de"_u16 ||
                                   token.Text() == u"\u00fe"_u16))
                    Strict(u"unicodeTextInMathMode", token.Origin,
                           JoinMessage({u"Latin-1/Unicode text character \""_u16,
                                        std::u16string(1, token.Text()[0]),
                                        u"\" used in math mode"_u16}));
                if (definition.Group == u"op-token"_u16) {
                    result = AddEdited(w, options, NodeKind::Operator, token.Origin, [&](auto& v) {
                                     v.Text = token.Text();
                                     v.CodePoint = definition.CodePoint;
                                     v.Atom = MathAtom::Op;
                                     v.Limits =
                                         token.Is(u"\\intop"_u16) || token.Is(u"\\smallint"_u16) ||
                                         !((((definition.CodePoint >= 0x222b) && (definition.CodePoint <= 0x2233))));
                                     v.Font = task.Font;
                                 });
                    break;
                }
                auto wide =
                    token.Text()[0] == 55349 && token.Text().size() >= 2 ? RuneAt(*token.Source, token.Start) : 0;
                auto wideFont =
                    ((wide >= 0x1d400) && (wide < 0x1d6a4))
                        ? (((((wide - 0x1d400) / 26) == 0) || (((wide - 0x1d400) / 26) == 1))
                               ? 5
                               : (((((wide - 0x1d400) / 26) == 2) || (((wide - 0x1d400) / 26) == 3))
                                      ? 10
                                      : (((((wide - 0x1d400) / 26) == 4) || (((wide - 0x1d400) / 26) == 5))
                                             ? 6
                                             : ((((wide - 0x1d400) / 26) == 6)
                                                    ? 14
                                                    : (((((((wide - 0x1d400) / 26) == 10) ||
                                                          (((wide - 0x1d400) / 26) == 11)) ||
                                                         (((wide - 0x1d400) / 26) == 14)) ||
                                                        (((wide - 0x1d400) / 26) == 15))
                                                           ? 4
                                                           : (((((wide - 0x1d400) / 26) == 12) ||
                                                               (((wide - 0x1d400) / 26) == 13))
                                                                  ? 0
                                                                  : (((((wide - 0x1d400) / 26) == 16) ||
                                                                      (((wide - 0x1d400) / 26) == 17))
                                                                         ? 13
                                                                         : (((((wide - 0x1d400) / 26) == 18) ||
                                                                             (((wide - 0x1d400) / 26) == 19))
                                                                                ? 11
                                                                                : (((((wide - 0x1d400) / 26) == 20) ||
                                                                                    (((wide - 0x1d400) / 26) == 21))
                                                                                       ? 12
                                                                                       : (((((wide - 0x1d400) / 26) ==
                                                                                            24) ||
                                                                                           (((wide - 0x1d400) / 26) ==
                                                                                            25))
                                                                                              ? 19
                                                                                              : -1))))))))))
                    : ((wide >= 0x1d7ce) && (wide <= 0x1d7ff))
                        ? ((((wide - 0x1d7ce) / 10) == 0)
                               ? 5
                               : ((((wide - 0x1d7ce) / 10) == 2)
                                      ? 13
                                      : ((((wide - 0x1d7ce) / 10) == 3) ? 11
                                                                        : ((((wide - 0x1d7ce) / 10) == 4) ? 19 : -1))))
                        : -1;
                result = AddEdited(w, options, NodeKind::Symbol, token.Origin, [&](auto& v) {
                                 v.CodePoint = definition.CodePoint;
                                 v.OriginalCodePoint = wide;                                 v.FontIdOverride = wideFont;
                                 v.Atom = definition.Atom;
                                 v.Font = task.Font == MathFont::Default ? definition.Font : task.Font;
                                 v.SymbolFont = definition.Font;
                                 v.TextMode = task.Text;
                                 v.IsMathOrd = definition.IsMathOrd;
                                 // KaTeX's spacing group keeps the name of the
                                 // symbol, because `\nobreakspace` and `~` add
                                 // the "nobreak" class while the other spaces
                                 // do not.
                                 if (definition.Group == u"spacing"_u16)
                                     v.Text = std::u16string(definition.Name);
                             });
                break;
            }
            }
            if (!replaced)
                w.Tasks.pop_back();
        }
        auto topTag = Node(root).SecondChild;
        if (topTag >= 0) {
            Node(root).SecondChild = -1;
            return AddEdited(w, options, NodeKind::Tag, SourceRange{0, int(source.size())}, [&](auto& v) {
                           v.FirstChild = root;
                           v.SecondChild = topTag;
                       });
        }
        return root;
    }
};

} // namespace

int Parse(const std::u16string& source, const MathOptions& options, ParserWorkspace& workspace,
          const CancellationToken& cancel) {
    return ParserState{source, options, options, workspace, cancel}.Run();
}

} // namespace velocitex
