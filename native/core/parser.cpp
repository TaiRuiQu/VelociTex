#include "parser.hpp"

#include "generated/data.hpp"

#include <array>
#include <climits>
#include <cstdlib>
namespace velocitex {

namespace {

template <class T, class F>
T changed(T value, F edit) {
    edit(value);
    return value;
}

std::u16string TrimEnd(std::u16string s, char16_t c) {
    while (!s.empty() && s.back() == c)
        s.pop_back();
    return s;
}

bool IsValidScalar(int c) {
    return c >= 0 && c <= 0x10ffff && !(c >= 0xd800 && c <= 0xdfff);
}

std::optional<std::u16string> UnicodeScript(int c) {
    bool sub = false;
    return UnicodeData::Script(c, sub);
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

int Add(ParserWorkspace& w, const MathOptions& options, SemanticNode node) {
    if ((int)w.Nodes.size() >= options.MaxNodes)
        Fail(MathDiagnosticCode::NodeLimit, node.Range, u"Semantic node budget exceeded.");
    int index = (int)w.Nodes.size();
    w.Nodes.push_back(std::move(node));
    return index;
}

void Push(ParserWorkspace& w, ParseTask task) {
    w.Tasks.push_back(task);
}

bool FindSymbol(std::u16string_view name, bool textMode, SymbolDefinition& definition) {
    static const auto maps = []() {
        std::array<std::unordered_map<std::u16string, SymbolDefinition>, 2> m;
        for (const auto& s : SymbolData::Symbols)
            m[s.TextMode ? 1 : 0].try_emplace(std::u16string(s.Name), s);
        return m;
    }();

    const auto& map = maps[textMode ? 1 : 0];
    auto it = map.find(std::u16string(name));
    if (it == map.end())
        return false;
    definition = it->second;
    return true;
}

bool FindCommand(std::u16string_view name, CommandDefinition& definition) {
    static const auto map = []() {
        std::unordered_map<std::u16string, CommandDefinition> m;
        for (const auto& c : CommandData::Definitions)
            m.try_emplace(std::u16string(c.Name), c);
        return m;
    }();
    auto it = map.find(std::u16string(name));
    if (it == map.end())
        return false;
    definition = it->second;
    return true;
}

static bool TryStyle(std::u16string_view name, MathStyle& style) {
    style = ((name == u"\\displaystyle")
                 ? MathStyle::Display
                 : ((name == u"\\textstyle")
                        ? MathStyle::Text
                        : ((name == u"\\scriptstyle")
                               ? MathStyle::Script
                               : ((name == u"\\scriptscriptstyle") ? MathStyle::ScriptScript : (MathStyle)(-1)))));
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
static int DelimiterFromText(std::u16string_view text, SourceRange range) {
    auto trimmedText = Trim(text);
    text = trimmedText;
    if (text.size() == 0 || text == u".")
        return 0;
    auto
        cp = (((text == u"(") || (text == u"\\lparen"))
                  ? u'('
                  : (((text == u")") || (text == u"\\rparen"))
                         ? u')'
                         : (((text == u"[") || (text == u"\\lbrack"))
                                ? u'['
                                : (((text == u"]") || (text == u"\\rbrack"))
                                       ? u']'
                                       : (((text == u"\\{") || (text == u"\\lbrace"))
                                              ? u'{'
                                              : (((text == u"\\}") || (text == u"\\rbrace"))
                                                     ? u'}'
                                                     : (((((text == u"|") || (text == u"\\vert")) ||
                                                          (text == u"\\lvert")) ||
                                                         (text == u"\\rvert"))
                                                            ? u'|'
                                                            : ((((((text == u"\\|") || (text == u"\\Vert")) ||
                                                                  (text == u"\\lVert")) ||
                                                                 (text == u"\\rVert")) ||
                                                                (text == u"‖"))
                                                                   ? 0x2016
                                                                   : (((((text == u"\\langle") || (text == u"<")) ||
                                                                        (text == u"\\lt")) ||
                                                                       (text == u"⟨"))
                                                                          ? 0x27e8
                                                                          : (((((text == u"\\rangle") ||
                                                                                (text == u">")) ||
                                                                               (text == u"\\gt")) ||
                                                                              (text == u"⟩"))
                                                                                 ? 0x27e9
                                                                                 : (((text == u"⟮") ||
                                                                                     (text == u"\\lgroup"))
                                                                                        ? 0x27ee
                                                                                        : (((text == u"⟯") ||
                                                                                            (text == u"\\rgroup"))
                                                                                               ? 0x27ef
                                                                                               : (((text == u"⎰") || (text == u"\\lmoustache")) ? 0x23b0
                                                                                                                                                : (((text == u"⎱") || (text == u"\\rmoustache")) ? 0x23b1
                                                                                                                                                                                                 : (((text == u"\\lfloor") || (text == u"⌊")) ? 0x230a
                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                    ((text == u"\\rfloor") || (text == u"⌋")) ? 0x230b
                                                                                                                                                                                                                                                                                              : (((text == u"\\lceil") || (text == u"⌈")) ? 0x2308
                                                                                                                                                                                                                                                                                                                                          : (
                                                                                                                                                                                                                                                                                                                                                ((text == u"\\rceil") || (text == u"⌉")) ? 0x2309
                                                                                                                                                                                                                                                                                                                                                                                         : (
                                                                                                                                                                                                                                                                                                                                                                                               (text == u"/") ? u'/'
                                                                                                                                                                                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                                                                                                                                                                                    (text == u"\\backslash") ? u'\\'
                                                                                                                                                                                                                                                                                                                                                                                                                                             : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                   ((text == u"\\uparrow") || (text == u"↑")) ? 0x2191
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                              : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ((text == u"\\downarrow") || (text == u"↓"))
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        ? 0x2193
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        : (((text == u"\\updownarrow") || (text == u"↕")) ? 0x2195
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          : (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    (
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        text ==
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                        u"\\Uparrow") ||
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    (text ==
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                     u"⇑"))
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    ? 0x21d1
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    : (((text == u"\\Downarrow") || (text == u"⇓")) ? 0x21d3
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    : (((text == u"\\Updownarrow") || (text == u"⇕")) ? 0x21d5
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      : -1))))))))))))))))))))))))));
    if (cp < 0)
        Fail(MathDiagnosticCode::UnexpectedToken, range, u"Expected a valid math delimiter.");
    return cp;
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
        ((stem == u"\\big") ? 1 : ((stem == u"\\Big") ? 2 : ((stem == u"\\bigg") ? 3 : ((stem == u"\\Bigg") ? 4 : 0))));
    return size != 0;
}
static double LengthEm(TexLength);

static TexLength ParseLength(std::u16string, SourceRange);

static double ReadDimension(std::u16string text, SourceRange range) {
    return LengthEm(ParseLength(text, range));
}
static TexLength ParseLength(std::u16string text, SourceRange range) {
    text = Trim(text);
    if (text.empty())
        return {0, TexUnit::Pt};
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
        Fail(MathDiagnosticCode::UnexpectedToken, range, u"Expected a finite TeX dimension.");

    auto unit = std::u16string_view(compact).substr(split);
    TexUnit kind = (TexUnit)-1;

    static constexpr std::u16string_view names[] = {u"em", u"ex", u"mu", u"pt", u"bp", u"pc", u"in", u"cm",
                                                    u"mm", u"dd", u"cc", u"nd", u"nc", u"sp", u"px"};

    for (int i = 0; i < 15; i++)
        if (unit == names[i])
            kind = (TexUnit)i;

    if ((int)kind < 0 || std::abs(number) > 1e8)
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
        ((name == u"\\tiny")
             ? 1
             : ((name == u"\\sixptsize")
                    ? 2
                    : ((name == u"\\scriptsize")
                           ? 3
                           : ((name == u"\\footnotesize")
                                  ? 4
                                  : ((name == u"\\small")
                                         ? 5
                                         : ((name == u"\\normalsize")
                                                ? 6
                                                : ((name == u"\\large")
                                                       ? 7
                                                       : ((name == u"\\Large")
                                                              ? 8
                                                              : ((name == u"\\LARGE")
                                                                     ? 9
                                                                     : ((name == u"\\huge")
                                                                            ? 10
                                                                            : ((name == u"\\Huge") ? 11 : 0)))))))))));
    return size != 0;
}
static bool TryLegacyFont(std::u16string_view name, MathFont& font) {
    font = ((name == u"\\rm")
                ? MathFont::MathRoman
                : ((name == u"\\sf")
                       ? MathFont::MathSans
                       : ((name == u"\\tt")
                              ? MathFont::MathTypewriter
                              : ((name == u"\\bf") ? MathFont::MathBold
                                                   : ((name == u"\\it") ? MathFont::MathItalic
                                                                        : ((name == u"\\cal") ? MathFont::Caligraphic
                                                                                              : (MathFont)(-1)))))));
    return (int)font >= 0;
}
static bool TryClass(std::u16string_view name, MathAtom& atom) {
    atom =
        ((name == u"\\mathord")
             ? MathAtom::Ord
             : ((name == u"\\mathbin")
                    ? MathAtom::Bin
                    : ((name == u"\\mathrel")
                           ? MathAtom::Rel
                           : ((name == u"\\mathopen")
                                  ? MathAtom::Open
                                  : ((name == u"\\mathclose")
                                         ? MathAtom::Close
                                         : ((name == u"\\mathpunct")
                                                ? MathAtom::Punct
                                                : ((name == u"\\mathinner") ? MathAtom::Inner : (MathAtom)(-1))))))));
    return (int)atom >= 0;
}
static bool IsEnvironment(std::u16string name) {
    return (((((((((((((((((((((((((((((((((name == u"matrix") || (name == u"pmatrix")) || (name == u"bmatrix")) ||
                                         (name == u"Bmatrix")) ||
                                        (name == u"vmatrix")) ||
                                       (name == u"Vmatrix")) ||
                                      (name == u"matrix*")) ||
                                     (name == u"pmatrix*")) ||
                                    (name == u"bmatrix*")) ||
                                   (name == u"Bmatrix*")) ||
                                  (name == u"vmatrix*")) ||
                                 (name == u"Vmatrix*")) ||
                                (name == u"array")) ||
                               (name == u"darray")) ||
                              (name == u"smallmatrix")) ||
                             (name == u"subarray")) ||
                            (name == u"cases")) ||
                           (name == u"dcases")) ||
                          (name == u"rcases")) ||
                         (name == u"drcases")) ||
                        (name == u"aligned")) ||
                       (name == u"alignedat")) ||
                      (name == u"gathered")) ||
                     (name == u"split")) ||
                    (name == u"align")) ||
                   (name == u"align*")) ||
                  (name == u"alignat")) ||
                 (name == u"alignat*")) ||
                (name == u"gather")) ||
               (name == u"gather*")) ||
              (name == u"equation")) ||
             (name == u"equation*")) ||
            (name == u"CD"));
}
static bool TryNamedOperator(std::u16string_view name, std::u16string& text, bool& limits) {
    limits = ((((((((((name == u"\\det") || (name == u"\\gcd")) || (name == u"\\inf")) || (name == u"\\lim")) ||
                   (name == u"\\max")) ||
                  (name == u"\\min")) ||
                 (name == u"\\Pr")) ||
                (name == u"\\sup")) ||
               (name == u"\\liminf")) ||
              (name == u"\\limsup"));
    auto recognized =
        limits ||
        ((((((((((((((((((((((((((((((((name == u"\\arcsin") || (name == u"\\arccos")) || (name == u"\\arctan")) ||
                                     (name == u"\\arctg")) ||
                                    (name == u"\\arcctg")) ||
                                   (name == u"\\arg")) ||
                                  (name == u"\\ch")) ||
                                 (name == u"\\cos")) ||
                                (name == u"\\cosec")) ||
                               (name == u"\\cosh")) ||
                              (name == u"\\cot")) ||
                             (name == u"\\cotg")) ||
                            (name == u"\\coth")) ||
                           (name == u"\\csc")) ||
                          (name == u"\\ctg")) ||
                         (name == u"\\cth")) ||
                        (name == u"\\deg")) ||
                       (name == u"\\dim")) ||
                      (name == u"\\exp")) ||
                     (name == u"\\hom")) ||
                    (name == u"\\ker")) ||
                   (name == u"\\lg")) ||
                  (name == u"\\ln")) ||
                 (name == u"\\log")) ||
                (name == u"\\sec")) ||
               (name == u"\\sin")) ||
              (name == u"\\sinh")) ||
             (name == u"\\sh")) ||
            (name == u"\\tan")) ||
           (name == u"\\tanh")) ||
          (name == u"\\tg")) ||
         (name == u"\\th"));
    text = !recognized             ? u""
           : (name == u"\\liminf") ? u"lim inf"
           : (name == u"\\limsup") ? u"lim sup"
                                   : std::u16string(name.substr(1));
    return recognized;
}
static bool TryAccent(std::u16string_view name, int& codePoint) {
    codePoint =
        (((name == u"\\acute") || (name == u"\\'"))
             ? 0x2ca
             : (((name == u"\\grave") || (name == u"\\`"))
                    ? 0x2cb
                    : (((name == u"\\ddot") || (name == u"\\\""))
                           ? 0xa8
                           : (((name == u"\\tilde") || (name == u"\\widetilde"))
                                  ? u'~'
                                  : ((name == u"\\~")
                                         ? 0x2dc
                                         : (((name == u"\\bar") || (name == u"\\="))
                                                ? 0x2c9
                                                : (((name == u"\\breve") || (name == u"\\u"))
                                                       ? 0x2d8
                                                       : ((((name == u"\\check") || (name == u"\\widecheck")) ||
                                                           (name == u"\\v"))
                                                              ? 0x2c7
                                                              : ((((name == u"\\hat") || (name == u"\\widehat")) ||
                                                                  (name == u"\\^"))
                                                                     ? u'^'
                                                                     : ((name == u"\\vec")
                                                                            ? 0x20d7
                                                                            : (((name == u"\\dot") || (name == u"\\."))
                                                                                   ? 0x2d9
                                                                                   : (((name == u"\\mathring") ||
                                                                                       (name == u"\\r"))
                                                                                          ? 0x2da
                                                                                          : ((name == u"\\H")
                                                                                                 ? 0x2dd
                                                                                                 : ((name == u"\\c")
                                                                                                        ? 0xb8
                                                                                                        : ((name ==
                                                                                                            u"\\textcir"
                                                                                                            u"cled")
                                                                                                               ? 0x25ef
                                                                                                               : (((((((((((((((((((((name ==
                                                                                                                                      u"\\overline") ||
                                                                                                                                     (name ==
                                                                                                                                      u"\\underline")) ||
                                                                                                                                    (name ==
                                                                                                                                     u"\\overbrace")) ||
                                                                                                                                   (name ==
                                                                                                                                    u"\\underbrace")) ||
                                                                                                                                  (name ==
                                                                                                                                   u"\\overbracket")) ||
                                                                                                                                 (name ==
                                                                                                                                  u"\\underbracket")) ||
                                                                                                                                (name ==
                                                                                                                                 u"\\utilde")) ||
                                                                                                                               (name ==
                                                                                                                                u"\\overrightarrow")) ||
                                                                                                                              (name ==
                                                                                                                               u"\\overleftarrow")) ||
                                                                                                                             (name ==
                                                                                                                              u"\\Overrightarrow")) ||
                                                                                                                            (name ==
                                                                                                                             u"\\overleftrightarrow")) ||
                                                                                                                           (name ==
                                                                                                                            u"\\underleftrightarrow")) ||
                                                                                                                          (name ==
                                                                                                                           u"\\underleftarrow")) ||
                                                                                                                         (name ==
                                                                                                                          u"\\underrightarrow")) ||
                                                                                                                        (name ==
                                                                                                                         u"\\overgroup")) ||
                                                                                                                       (name ==
                                                                                                                        u"\\undergroup")) ||
                                                                                                                      (name ==
                                                                                                                       u"\\overlinesegment")) ||
                                                                                                                     (name ==
                                                                                                                      u"\\underlinesegment")) ||
                                                                                                                    (name ==
                                                                                                                     u"\\overleftharpoon")) ||
                                                                                                                   (name ==
                                                                                                                    u"\\overrightharpoon"))
                                                                                                                      ? 0
                                                                                                                      : -1))))))))))))))));
    return codePoint >= 0;
}
static bool IsExtensibleArrow(std::u16string_view name) {
    return (
        (((((((((((((((((((((name == u"\\xleftarrow") || (name == u"\\xrightarrow")) || (name == u"\\xLeftarrow")) ||
                          (name == u"\\xRightarrow")) ||
                         (name == u"\\xleftrightarrow")) ||
                        (name == u"\\xLeftrightarrow")) ||
                       (name == u"\\xhookleftarrow")) ||
                      (name == u"\\xhookrightarrow")) ||
                     (name == u"\\xmapsto")) ||
                    (name == u"\\xleftharpoonup")) ||
                   (name == u"\\xleftharpoondown")) ||
                  (name == u"\\xrightharpoonup")) ||
                 (name == u"\\xrightharpoondown")) ||
                (name == u"\\xrightleftharpoons")) ||
               (name == u"\\xleftrightharpoons")) ||
              (name == u"\\xtwoheadleftarrow")) ||
             (name == u"\\xtwoheadrightarrow")) ||
            (name == u"\\xlongequal")) ||
           (name == u"\\xtofrom")) ||
          (name == u"\\xrightleftarrows")) ||
         (name == u"\\xrightequilibrium")) ||
        (name == u"\\xleftequilibrium"));
}
static bool TryFont(std::u16string_view name, MathFont& font) {
    font =
        ((name == u"\\mathrm")
             ? MathFont::MathRoman
             : ((name == u"\\mathit")
                    ? MathFont::MathItalic
                    : (((name == u"\\mathbf") || (name == u"\\bold"))
                           ? MathFont::MathBold
                           : ((name == u"\\mathnormal")
                                  ? MathFont::MathNormal
                                  : ((name == u"\\mathsf")
                                         ? MathFont::MathSans
                                         : ((name == u"\\mathsfit")
                                                ? MathFont::MathSansItalic
                                                : ((name == u"\\mathtt")
                                                       ? MathFont::MathTypewriter
                                                       : (((name == u"\\mathbb") || (name == u"\\Bbb"))
                                                              ? MathFont::AMS
                                                              : ((name == u"\\mathcal")
                                                                     ? MathFont::Caligraphic
                                                                     : (((name == u"\\mathfrak") || (name == u"\\frak"))
                                                                            ? MathFont::Fraktur
                                                                            : ((name == u"\\mathscr")
                                                                                   ? MathFont::Script
                                                                                   : (((name == u"\\boldsymbol") ||
                                                                                       (name == u"\\bm"))
                                                                                          ? MathFont::MathBoldItalic
                                                                                          : (MathFont)(-1)))))))))))));
    return (int)font >= 0;
}
static bool TryTextFont(std::u16string_view name, MathFont& font) {
    font = ((((((name == u"\\text") || (name == u"\\textrm")) || (name == u"\\textnormal")) || (name == u"\\textmd")) ||
             (name == u"\\textup"))
                ? MathFont::MathRoman
                : ((name == u"\\textbf")
                       ? MathFont::MathBold
                       : (((name == u"\\textit") || (name == u"\\emph"))
                              ? MathFont::MathItalic
                              : ((name == u"\\textsf")
                                     ? MathFont::MathSans
                                     : ((name == u"\\texttt") ? MathFont::MathTypewriter : (MathFont)(-1))))));
    return (int)font >= 0;
}

struct ParserState {
    const std::u16string& source;
    const MathOptions& options;
    const MathOptions& settings;
    ParserWorkspace& w;
    const CancellationToken& cancellationToken;
    int position = 0, parseEnd = 0;

    SemanticNode& Node(int index) { return w.Nodes.at(index); }

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
                Fail(MathDiagnosticCode::StrictViolation, range, message + u" [" + code + u"]");
            return true;
        }
        if (w.Warnings.size() < std::min(settings.MaxTokens, 1024))
            w.Warnings.push_back(MathWarning{code, range, message});
        return false;
    }
    int AddSymbol(std::u16string name, SourceRange range, ParseTask context) {
        SymbolDefinition definition{};
        if (FindSymbol(name, context.Text, definition))
            return Add(w, settings, changed(SemanticNode(NodeKind::Symbol, range), [&](auto& v) {
                           v.CodePoint = definition.CodePoint;
                           v.Atom = definition.Atom;
                           v.Font = context.Font == MathFont::Default ? definition.Font : context.Font;
                           v.SymbolFont = definition.Font;
                           v.TextMode = context.Text;
                           v.IsMathOrd = definition.IsMathOrd;
                       }));
        auto scalar = RuneAt(name);
        if (scalar >= 0x80 && (settings.TextShaper != nullptr))
            return Add(w, settings, changed(SemanticNode(NodeKind::Symbol, range), [&](auto& v) {
                           v.CodePoint = scalar;
                           v.Font = context.Font;
                           v.TextMode = true;
                       }));
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
    SourceRange CurrentRange() {
        return position < parseEnd ? w.Expanded[position].Origin : SourceRange{int(source.size()), 0};
    }
    void SkipMathSpaces(bool text) {
        if (!text)
            while (position < parseEnd && w.Expanded[position].Length > 0 &&
                   IsWhiteSpace(w.Expanded[position].Text()[0]))
                position++;
    }
    void PushArgument(int parent, int slot, ParseTask context, bool primitive = false) {
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::SetChild;
                 v.Node = parent;
                 v.Slot = slot;
             }));
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Argument;
                 v.Depth = context.Depth + 1;
                 v.Stop = 0;
                 v.Closing = false;
                 v.Slot = primitive ? -1 : 0;
                 v.PrimitiveArgument = primitive;
             }));
    }
    void PushHBoxArgument(int parent, ParseTask context) {
        auto style = Add(w, settings, changed(SemanticNode(NodeKind::Style, Node(parent).Range), [&](auto& v) {
                             v.Style = MathStyle::Text;
                             v.Font = MathFont::MathRoman;
                             v.TextMode = true;
                         }));
        Node(parent).FirstChild = style;
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::WrapHBoxArgument;
                 v.Node = style;
             }));
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Argument;
                 v.Depth = context.Depth + 1;
                 v.Stop = 0;
                 v.Closing = false;
                 v.Slot = 0;
                 v.PrimitiveArgument = false;
                 v.Text = true;
                 v.Font = MathFont::MathRoman;
             }));
    }
    std::u16string ReadRawGroup() {
        SkipMathSpaces(false);
        if (position >= parseEnd || !w.Expanded[position++].Is(u"{"))
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected a braced command argument.");
        auto builder = std::u16string{};
        auto nesting = 1;
        while (position < parseEnd) {
            cancellationToken.ThrowIfCancellationRequested();
            auto token = w.Expanded[position++];
            if (token.Is(u"{")) {
                if (++nesting > settings.MaxNesting)
                    Fail(MathDiagnosticCode::NestingLimit, token.Origin, u"Argument nesting budget exceeded.");
            } else if (token.Is(u"}") && --nesting == 0)
                return builder;
            builder.append(token.Text());
        }
        Fail(MathDiagnosticCode::UnclosedGroup, CurrentRange(), u"Unclosed braced command argument.");
        return u"";
    }
    std::u16string ReadRawBracket() {
        SkipMathSpaces(false);
        if (position >= parseEnd || !w.Expanded[position++].Is(u"["))
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected an optional argument.");
        auto builder = std::u16string{};
        auto braces = 0;
        while (position < parseEnd) {
            auto token = w.Expanded[position++];
            if (token.Is(u"]") && braces == 0)
                return builder;
            if (token.Is(u"{"))
                braces++;
            else if (token.Is(u"}") && --braces < 0)
                Fail(MathDiagnosticCode::UnexpectedGroupEnd, token.Origin,
                     u"Unexpected optional argument closing brace.");
            builder.append(token.Text());
        }
        Fail(MathDiagnosticCode::UnclosedGroup, CurrentRange(), u"Unclosed optional argument.");
        return u"";
    }
    int ReadDelimiter() {
        SkipMathSpaces(false);
        if (position >= parseEnd)
            Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Expected a delimiter.");
        auto token = w.Expanded[position++];
        return DelimiterFromText(token.Text(), token.Origin);
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
        if (position < parseEnd && w.Expanded[position].Is(u"{"))
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
               (w.Expanded[position].Is(u"\\hline") || w.Expanded[position].Is(u"\\hdashline"))) {
            auto dashed = w.Expanded[position++].Is(u"\\hdashline");
            if (count != 0 && (count < 0) != dashed)
                Fail(MathDiagnosticCode::UnsupportedCommand, CurrentRange(),
                     u"Mixed adjacent solid and dashed array rules are not implemented.");
            count += dashed ? -1 : 1;
            SkipMathSpaces(false);
        }
        return count;
    }
    void FinishEnvironment(int matrix) {
        position++;
        auto name = ReadRawGroup();
        if (name != Node(matrix).Text)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range, u"Environment end does not match begin.");
        auto expected = std::count_if(text(Node(matrix).Alignment).begin(), text(Node(matrix).Alignment).end(),
                                      [](char16_t c) { return c == u'l' || c == u'c' || c == u'r'; });
        if ((((Node(matrix).Text == u"array") || (Node(matrix).Text == u"darray")) ||
             (Node(matrix).Text == u"subarray")) &&
            Node(matrix).ColumnCount > expected)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range,
                 u"Array contains more cells than its column specification.");
        if (((((Node(matrix).Text == u"cases") || (Node(matrix).Text == u"dcases")) ||
              (Node(matrix).Text == u"rcases")) ||
             (Node(matrix).Text == u"drcases")) &&
            Node(matrix).ColumnCount > 2)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range,
                 u"Cases environments have at most two columns.");
        if (Node(matrix).Text == u"alignedat" && Node(matrix).ColumnCount > Node(matrix).Value * 2)
            Fail(MathDiagnosticCode::UnexpectedToken, Node(matrix).Range,
                 u"Alignedat contains more cells than its column pair count.");
        if (Node(matrix).Text == u"CD")
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
                int body = Add(w, settings,
                               changed(SemanticNode(NodeKind::Row, Node(row).Range), [&](auto& v) { v.Value = 1; })),
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
                    style = Add(w, settings, changed(SemanticNode(NodeKind::Style, Node(row).Range), [&](auto& v) {
                                    v.FirstChild = body;
                                    v.Style = MathStyle::Display;
                                }));
                int cell = Add(w, settings, changed(SemanticNode(NodeKind::MatrixCell, Node(row).Range), [&](auto& v) {
                                   v.FirstChild = style;
                               }));
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
                    arrow = Add(w, settings, changed(SemanticNode(NodeKind::Accent, Node(row).Range), [&](auto& v) {
                                    v.FirstChild = direction == u'=' ? -1 : above;
                                    v.SecondChild = direction == u'=' ? -1 : below;
                                    v.Atom = MathAtom::Rel;
                                    v.Text = direction == u'<'   ? u"\\\\cdleftarrow"
                                             : direction == u'>' ? u"\\\\cdrightarrow"
                                                                 : u"\\\\cdlongequal";
                                }));

                else if (direction == u'.')
                    arrow = Add(w, settings, changed(SemanticNode(NodeKind::Space, Node(row).Range), [&](auto& v) {
                                    v.Text = u"cdnull";
                                    v.Value = 1.0 / 3;
                                }));

                else if (direction == u'|')
                    arrow = Add(w, settings, changed(SemanticNode(NodeKind::Delimiter, Node(row).Range), [&](auto& v) {
                                    v.CodePoint = 0x2016;
                                    v.Value = 2;
                                }));

                else {
                    int left = Add(w, settings, changed(SemanticNode(NodeKind::Box, Node(row).Range), [&](auto& v) {
                                       v.Text = u"cdleft";
                                       v.FirstChild = above;
                                   }));
                    int shaft =
                        Add(w, settings, changed(SemanticNode(NodeKind::Delimiter, Node(row).Range), [&](auto& v) {
                                v.CodePoint = direction == u'A' ? 0x2191 : direction == u'V' ? 0x2193 : 0x2016;
                                v.Value = 2;
                            }));
                    int right = Add(w, settings, changed(SemanticNode(NodeKind::Box, Node(row).Range), [&](auto& v) {
                                        v.Text = u"cdright";
                                        v.FirstChild = below;
                                    }));
                    Node(left).NextSibling = shaft;
                    Node(shaft).NextSibling = right;
                    int body = Add(w, settings, changed(SemanticNode(NodeKind::Row, Node(row).Range), [&](auto& v) {
                                       v.FirstChild = left;
                                       v.Value = 1;
                                   }));
                    arrow = Add(w, settings, changed(SemanticNode(NodeKind::Box, Node(row).Range), [&](auto& v) {
                                    v.Text = u"cdparent";
                                    v.FirstChild = body;
                                }));
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
            Node(finalRow).NextSibling = Add(w, settings, SemanticNode(NodeKind::MatrixRow, Node(matrix).Range));
    }
    void CompleteMatrixRow(int matrix, int row) {
        auto env = Node(matrix).Text;
        if (Node(row).SecondChild >= 0 || Node(row).NeverLimits ||
            !((((((env == u"equation") || (env == u"align")) || (env == u"alignat")) || (env == u"gather")))))
            return;
        auto tag = Add(w, settings, changed(SemanticNode(NodeKind::Text, Node(row).Range), [&](auto& v) {
                           v.Font = MathFont::MathRoman;
                           v.TextMode = true;
                           v.Text = u"\\text";
                       }));
        auto body = Add(w, settings,
                        changed(SemanticNode(NodeKind::Row, Node(row).Range), [&](auto& v) { v.TextMode = true; }));
        Node(tag).FirstChild = body;
        auto last = -1;
        auto number = settings.EquationNumber;
        for (auto previous = Node(matrix).FirstChild; previous >= 0 && previous != row;
             previous = Node(previous).NextSibling)
            number++;
        for (auto character : u"(" + utf16(std::to_string(number)) + u")") {
            auto symbol = Add(w, settings, changed(SemanticNode(NodeKind::Symbol, Node(row).Range), [&](auto& v) {
                                  v.CodePoint = character;
                                  v.TextMode = true;
                                  v.Font = MathFont::MathRoman;
                              }));
            if (last < 0)
                Node(body).FirstChild = symbol;
            else
                Node(last).NextSibling = symbol;
            last = symbol;
        }
        Node(row).SecondChild = tag;
        Node(row).AlwaysLimits = true;
    }
    void AddMatrixCell(int matrix, int row, int previous, int column, ParseTask context) {
        if (context.Depth + 1 > settings.MaxNesting)
            Fail(MathDiagnosticCode::NestingLimit, CurrentRange(), u"Environment nesting budget exceeded.");
        auto cell = Add(w, settings, SemanticNode(NodeKind::MatrixCell, CurrentRange()));
        if (previous < 0)
            Node(row).FirstChild = cell;
        else
            Node(previous).NextSibling = cell;
        auto env = Node(matrix).Text;
        auto style = ((env == u"smallmatrix") || (env == u"subarray")) ? MathStyle::Script
                     : ((((((((((((((((env == u"aligned") || (env == u"alignedat")) || (env == u"align")) ||
                                    (env == u"align*")) ||
                                   (env == u"alignat")) ||
                                  (env == u"alignat*")) ||
                                 (env == u"gather")) ||
                                (env == u"gather*")) ||
                               (env == u"equation")) ||
                              (env == u"equation*")) ||
                             (env == u"CD")) ||
                            (env == u"gathered")) ||
                           (env == u"split")) ||
                          (env == u"darray")) ||
                         (env == u"dcases")) ||
                        (env == u"drcases"))
                         ? MathStyle::Display
                         : MathStyle::Text;
        auto wrapper = Add(w, settings, changed(SemanticNode(NodeKind::Style, CurrentRange()), [&](auto& v) {
                               v.Style = style;
                               v.Font = context.Font;
                           }));
        Node(cell).FirstChild = wrapper;
        auto body = Add(w, settings,
                        changed(SemanticNode(NodeKind::Row, CurrentRange()), [&](auto& v) { v.Font = context.Font; }));
        Node(wrapper).FirstChild = body;
        auto leading = -1;
        if ((column & 1) != 0 &&
            (((((((env == u"aligned") || (env == u"alignedat")) || (env == u"align")) || (env == u"align*")) ||
               (env == u"alignat")) ||
              (env == u"alignat*")) ||
             (env == u"split"))) {
            leading = Add(w, settings, changed(SemanticNode(NodeKind::Row, CurrentRange()), [&](auto& v) {
                              v.Value = 1;
                              v.Font = context.Font;
                          }));
            Node(body).FirstChild = leading;
        }
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::MatrixNextCell;
                 v.Node = matrix;
                 v.Last = row;
                 v.Previous = cell;
                 v.Slot = column;
             }));
        Push(w, changed(context, [&](auto& v) {
                 v.Kind = ParseTaskKind::Sequence;
                 v.Node = body;
                 v.Last = leading;
                 v.Previous = -1;
                 v.Slot = 0;
                 v.Closing = false;
                 v.Stop = 4;
                 v.Depth = context.Depth + 1;
             }));
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
        auto root = Add(w, options, SemanticNode(NodeKind::Row, SourceRange{0, int(source.size())}));
        Push(w, ParseTask(ParseTaskKind::Sequence, root));
        position = 0;
        auto result = -1;
        auto delimiterDepth = 0;
        auto pendingArrayStretch = 0.0;
        auto tokenCount = int(w.Expanded.size());
        parseEnd = tokenCount;
        while (w.Tasks.size() != 0) {
            cancellationToken.ThrowIfCancellationRequested();
            auto task = w.Tasks.back();
            w.Tasks.pop_back();
            parseEnd = std::min(tokenCount, task.Limit);
            while (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0)
                pendingArrayStretch = w.Expanded[position++].InternalArrayStretch;
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
                        : Add(w, options,
                              changed(SemanticNode(NodeKind::Row, result >= 0 ? Node(result).Range : CurrentRange()),
                                      [&](auto& v) {
                                          v.FirstChild = result;
                                          v.Value = 1;
                                          v.TextMode = true;
                                      }));
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
                if (position < parseEnd && w.Expanded[position].Is(u"{")) {
                    auto raw = ReadRawGroup();
                    blankThickness = raw.size() == 0;
                    barLength = ParseLength(raw, CurrentRange());
                } else
                    barLength = ReadLength();
                Node(task.Node).HasBar = blankThickness || barLength.Number > 0;
                Node(task.Node).Length1 = barLength;
                Node(task.Node).Value = blankThickness ? 0 : LengthEm(barLength);
                Node(task.Node).Text =
                    blankThickness ? std::optional<std::u16string>{} : std::optional<std::u16string>{u"\\genfrac"};
                SkipMathSpaces(false);
                std::u16string rawStyle = position < parseEnd && w.Expanded[position].Is(u"{") ? ReadRawGroup()
                                          : position < parseEnd ? std::u16string(w.Expanded[position++].Text())
                                                                : u"";
                auto outerFraction = task.Node;
                if (rawStyle.size() > 0 && ((rawStyle[0] >= u'0') && (rawStyle[0] <= u'3')))
                    outerFraction =
                        Add(w, options, changed(SemanticNode(NodeKind::Style, Node(task.Node).Range), [&](auto& v) {
                                v.FirstChild = task.Node;
                                v.Style = (MathStyle)(rawStyle[0] - u'0');
                                v.Font = task.Font;
                            }));
                Push(w, ParseTask(ParseTaskKind::Return, outerFraction));
                PushArgument(task.Node, 1, changed(task, [&](auto& v) { v.Text = false; }));
                PushArgument(task.Node, 0, changed(task, [&](auto& v) { v.Text = false; }));
                break;
            }
            case ParseTaskKind::FinishTag: {
                Node(task.Node).FirstChild =
                    result >= 0 && Node(result).Kind == NodeKind::Row ? Node(result).FirstChild : result;
                break;
            }
            case ParseTaskKind::FinishTopTag: {
                Node(task.Node).SecondChild = result;
                result = Add(w, options, SemanticNode(NodeKind::Row, Node(task.Node).Range));
                break;
            }
            case ParseTaskKind::NormalizeChild: {
                auto argument = Node(task.Node).FirstChild;
                if (argument >= 0 && Node(argument).Kind == NodeKind::Row && Node(argument).Value == 1) {
                    Node(task.Node).FirstChild = Node(argument).FirstChild;
                }
                result = task.Node;
                break;
            }
            case ParseTaskKind::FinishClass: {
                if ((((Node(task.Node).Text == u"\\overset") || (Node(task.Node).Text == u"\\underset")) ||
                     (Node(task.Node).Text == u"\\stackrel"))) {
                    auto shifted = Node(task.Node).FirstChild;
                    auto basis = Node(task.Node).SecondChild;
                    auto op =
                        Add(w, options, changed(SemanticNode(NodeKind::Operator, Node(task.Node).Range), [&](auto& v) {
                                v.FirstChild = Unwrap(basis);
                                v.Limits = true;
                                v.AlwaysLimits = true;
                                v.Text = u"\\mathop";
                                v.Atom = MathAtom::Op;
                                v.Value = Node(task.Node).Text == u"\\stackrel" ? 0 : 1;
                            }));
                    auto scripts =
                        Add(w, options, changed(SemanticNode(NodeKind::Script, Node(task.Node).Range), [&](auto& v) {
                                v.FirstChild = op;
                                v.SecondChild = Node(task.Node).Text == u"\\underset" ? -1 : shifted;
                                v.ThirdChild = Node(task.Node).Text == u"\\underset" ? shifted : -1;
                            }));
                    Node(task.Node).Atom = Node(task.Node).Text == u"\\stackrel" ? MathAtom::Rel : BinRel(basis);
                    Node(task.Node).FirstChild = scripts;
                    Node(task.Node).SecondChild = -1;
                } else if (Node(task.Node).Text == u"\\@binrel") {
                    Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild);
                    Node(task.Node).FirstChild = Unwrap(Node(task.Node).SecondChild);
                    Node(task.Node).SecondChild = -1;
                } else if ((((Node(task.Node).Text == u"\\boldsymbol") || (Node(task.Node).Text == u"\\bm")) ||
                            (Node(task.Node).Text == u"\\pmb")))
                    Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild);
                result = task.Node;
                break;
            }
            case ParseTaskKind::FinishDelimited: {
                if (position >= parseEnd || !w.Expanded[position].Is(u"\\right"))
                    Fail(MathDiagnosticCode::UnclosedGroup, Node(task.Node).Range,
                         u"A left delimiter requires a matching right delimiter.");
                position++;
                Node(task.Node).RightDelimiter = ReadDelimiter();
                delimiterDepth--;
                result = task.Node;
                break;
            }
            case ParseTaskKind::MatrixNextCell: {
                SkipMathSpaces(false);
                if (position >= parseEnd)
                    Fail(MathDiagnosticCode::UnclosedGroup, Node(task.Node).Range, u"Unclosed math environment.");
                if (w.Expanded[position].Is(u"&")) {
                    position++;
                    AddMatrixCell(task.Node, task.Last, task.Previous, task.Slot + 1, task);
                    break;
                }
                Node(task.Last).ColumnCount = task.Slot + 1;
                Node(task.Node).ColumnCount = std::max(Node(task.Node).ColumnCount, task.Slot + 1);
                CompleteMatrixRow(task.Node, task.Last);
                if (w.Expanded[position].Is(u"\\\\") || w.Expanded[position].Is(u"\\cr")) {
                    position++;
                    SkipMathSpaces(false);
                    if (position < parseEnd && w.Expanded[position].Is(u"[")) {
                        auto gap = ParseLength(ReadRawBracket(), CurrentRange());
                        Node(task.Last).Length1 = gap;
                        Node(task.Last).Value = LengthEm(gap);
                    }
                    SkipMathSpaces(false);
                    auto rules = ReadRules();
                    if (position < parseEnd && w.Expanded[position].Is(u"\\end")) {
                        Node(task.Node).RuleBelow = rules;
                        FinishEnvironment(task.Node);
                        result = task.Node;
                        break;
                    }
                    auto nextRow = Add(w, options,
                                       changed(SemanticNode(NodeKind::MatrixRow, CurrentRange()),
                                               [&](auto& v) { v.RuleAbove = rules; }));
                    Node(task.Last).NextSibling = nextRow;
                    AddMatrixCell(task.Node, nextRow, -1, 0, task);
                    break;
                }
                if (!w.Expanded[position].Is(u"\\end"))
                    Fail(MathDiagnosticCode::UnexpectedToken, CurrentRange(),
                         u"Expected a column separator, row separator, or environment end.");
                FinishEnvironment(task.Node);
                result = task.Node;
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
                    Push(w, changed(task, [&](auto& v) { v.Kind = ParseTaskKind::Sequence; }));
                    break;
                }
                if (task.Last < 0)
                    Node(task.Node).FirstChild = result;
                else
                    Node(task.Last).NextSibling = result;
                Push(w, changed(task, [&](auto& v) {
                         v.Kind = ParseTaskKind::Sequence;
                         v.Last = result;
                         v.Previous = task.Last;
                     }));
                break;
            }
            case ParseTaskKind::Sequence: {
                SkipMathSpaces(task.Text);
                if (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0) {
                    pendingArrayStretch = w.Expanded[position++].InternalArrayStretch;
                    Push(w, task);
                    break;
                }
                if (position < parseEnd && w.Expanded[position].Is(u"\\relax")) {
                    position++;
                    Push(w, task);
                    break;
                }
                if (position < parseEnd &&
                    (task.Stop == 2 && w.Expanded[position].Is(u"]") ||
                     task.Stop == 3 && w.Expanded[position].Is(u"\\right") ||
                     task.Stop == 5 && w.Expanded[position].Is(u"\\endgroup") ||
                     task.Stop == 6 && w.Expanded[position].Is(u"$") ||
                     task.Stop == 7 && w.Expanded[position].Is(u"\\)") ||
                     task.Stop == 4 && (w.Expanded[position].Is(u"&") || w.Expanded[position].Is(u"\\\\") ||
                                        w.Expanded[position].Is(u"\\cr") || w.Expanded[position].Is(u"\\end")))) {
                    if (((((task.Stop == 2) || (task.Stop == 5)) || (task.Stop == 6)) || (task.Stop == 7)))
                        position++;
                    if (task.Text)
                        FormLigatures(task.Node);
                    result = task.Node;
                    break;
                }
                if (position == parseEnd || w.Expanded[position].Is(u"}")) {
                    if (task.Stop != 0)
                        Fail(MathDiagnosticCode::UnclosedGroup, Node(task.Node).Range,
                             u"Unclosed delimiter, optional argument, or environment.");
                    if (position == parseEnd && task.Closing)
                        Fail(MathDiagnosticCode::UnclosedGroup, Node(task.Node).Range, u"Unclosed group.");
                    if (position < parseEnd) {
                        if (!task.Closing)
                            Fail(MathDiagnosticCode::UnexpectedGroupEnd, CurrentRange(), u"Unexpected closing brace.");
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
                auto next = w.Expanded[position];
                auto unicodeScript = !task.Text && next.Text()[0] != u'\\'
                                         ? UnicodeScript(RuneAt(*next.Source, next.Start))
                                         : std::nullopt;
                if (!((unicodeScript == std::nullopt))) {
                    auto scalar = RuneAt(*next.Source, next.Start);
                    UnicodeData::Script(scalar, subscript);
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = Add(w, options, SemanticNode(NodeKind::Row, next.Origin));
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = empty;
                                     }));
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = task.Last;
                                     }));
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if ((subscript ? Node(script).ThirdChild : Node(script).SecondChild) >= 0)
                        Fail(subscript ? MathDiagnosticCode::DoubleSubscript : MathDiagnosticCode::DoubleSuperscript,
                             next.Origin, u"Duplicate Unicode script.");
                    auto row = Add(w, options,
                                   changed(SemanticNode(NodeKind::Row, next.Origin), [&](auto& v) { v.Value = 1; }));
                    auto last = -1;
                    while (position < parseEnd) {
                        auto part = w.Expanded[position];
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
                    Push(w, changed(task, [&](auto& v) { v.Last = script; }));
                    break;
                }
                if (!task.Text && next.Is(u"'")) {
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = Add(w, options, SemanticNode(NodeKind::Row, next.Origin));
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = empty;
                                     }));
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = task.Last;
                                     }));
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if (Node(script).SecondChild >= 0)
                        Fail(MathDiagnosticCode::DoubleSuperscript, next.Origin,
                             u"A base cannot have both an explicit superscript and a prime superscript.");
                    auto primes = Add(w, options,
                                      changed(SemanticNode(NodeKind::Row, next.Origin), [&](auto& v) { v.Value = 1; }));
                    auto previousPrime = -1;
                    while (position < parseEnd && w.Expanded[position].Is(u"'")) {
                        auto primeToken = w.Expanded[position++];
                        auto prime =
                            Add(w, options, changed(SemanticNode(NodeKind::Symbol, primeToken.Origin), [&](auto& v) {
                                    v.CodePoint = 0x2032;
                                    v.Font = MathFont::Default;
                                }));
                        if (previousPrime < 0)
                            Node(primes).FirstChild = prime;
                        else
                            Node(previousPrime).NextSibling = prime;
                        previousPrime = prime;
                    }
                    Node(script).SecondChild = primes;
                    Push(w, changed(task, [&](auto& v) { v.Last = script; }));
                    if (position < parseEnd && w.Expanded[position].Is(u"^")) {
                        position++;
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::AppendPrimeArgument;
                                 v.Node = previousPrime;
                             }));
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Argument;
                                 v.Stop = 0;
                                 v.Closing = false;
                                 v.Depth = task.Depth + 1;
                                 v.PrimitiveArgument = true;
                                 v.Slot = -1;
                             }));
                    }
                    break;
                }
                if (!task.Text && (((next.Text() == u"\\limits") || (next.Text() == u"\\nolimits")) ||
                                   (next.Text() == u"\\displaylimits"))) {
                    position++;
                    auto op = task.Last;
                    if (op >= 0 && Node(op).Kind == NodeKind::Script)
                        op = Node(op).FirstChild;
                    if (op < 0 || Node(op).Kind != NodeKind::Operator)
                        Fail(MathDiagnosticCode::UnexpectedToken, next.Origin,
                             u"Limit modifiers must follow an operator.");
                    if (Node(op).Text != u"\\operatorname" || Node(op).Text2 == u"star") {
                        Node(op).AlwaysLimits = next.Is(u"\\limits");
                        Node(op).NeverLimits = next.Is(u"\\nolimits");
                        if (next.Is(u"\\displaylimits"))
                            Node(op).Limits = true;
                    }
                    Push(w, task);
                    break;
                }
                if (!task.Text &&
                    ((((((next.Text() == u"\\over") || (next.Text() == u"\\atop")) || (next.Text() == u"\\choose")) ||
                       (next.Text() == u"\\brace")) ||
                      (next.Text() == u"\\brack")) ||
                     (next.Text() == u"\\above"))) {
                    if (task.Slot == 1)
                        Fail(MathDiagnosticCode::UnexpectedToken, next.Origin,
                             u"Only one infix fraction is allowed in a group.");
                    position++;
                    auto numerator =
                        Add(w, options, changed(SemanticNode(NodeKind::Row, Node(task.Node).Range), [&](auto& v) {
                                v.FirstChild = Node(task.Node).FirstChild;
                                v.Font = task.Font;
                            }));
                    auto denominator =
                        Add(w, options,
                            changed(SemanticNode(NodeKind::Row, next.Origin), [&](auto& v) { v.Font = task.Font; }));
                    auto fraction =
                        Add(w, options, changed(SemanticNode(NodeKind::Fraction, next.Origin), [&](auto& v) {
                                v.FirstChild = numerator;
                                v.SecondChild = denominator;
                                v.HasBar = next.Is(u"\\over");
                                v.LeftDelimiter = next.Is(u"\\choose")  ? u'('
                                                  : next.Is(u"\\brace") ? u'{'
                                                  : next.Is(u"\\brack") ? u'['
                                                                        : 0;
                                v.RightDelimiter = next.Is(u"\\choose")  ? u')'
                                                   : next.Is(u"\\brace") ? u'}'
                                                   : next.Is(u"\\brack") ? u']'
                                                                         : 0;
                            }));
                    if (next.Is(u"\\above")) {
                        auto length = ReadLength();
                        Node(fraction).Length1 = length;
                        Node(fraction).Value = LengthEm(length);
                        Node(fraction).HasBar = length.Number > 0;
                        Node(fraction).Text = u"\\above";
                    }
                    Node(task.Node).FirstChild = fraction;
                    Push(w, ParseTask(ParseTaskKind::Return, task.Node));
                    Push(w, changed(task, [&](auto& v) {
                             v.Node = denominator;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Slot = 1;
                         }));
                    break;
                }
                if (!task.Text && (next.Is(u"^") || next.Is(u"_"))) {
                    position++;
                    auto superscript = next.Is(u"^");
                    auto script = task.Last;
                    if (script < 0) {
                        auto empty = Add(w, options, SemanticNode(NodeKind::Row, next.Origin));
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = empty;
                                         v.Font = task.Font;
                                     }));
                        Node(task.Node).FirstChild = script;
                    } else if (Node(script).Kind != NodeKind::Script) {
                        script = Add(w, options, changed(SemanticNode(NodeKind::Script, next.Origin), [&](auto& v) {
                                         v.FirstChild = task.Last;
                                         v.Font = task.Font;
                                     }));
                        if (task.Previous < 0)
                            Node(task.Node).FirstChild = script;
                        else
                            Node(task.Previous).NextSibling = script;
                    }
                    if ((superscript ? Node(script).SecondChild : Node(script).ThirdChild) != -1)
                        Fail(superscript ? MathDiagnosticCode::DoubleSuperscript : MathDiagnosticCode::DoubleSubscript,
                             next.Origin, u"A base cannot have two scripts of the same kind.");
                    Push(w, changed(task, [&](auto& v) { v.Last = script; }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::SetChild;
                             v.Node = script;
                             v.Slot = superscript ? 1 : 2;
                         }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::Argument;
                             v.Depth = task.Depth + 1;
                             v.Stop = 0;
                             v.Closing = false;
                             v.Slot = -1;
                             v.PrimitiveArgument = true;
                         }));
                    break;
                }
                if (TryStyle(next.Text(), style)) {
                    position++;
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Style, next.Origin), [&](auto& v) {
                                           v.Style = style;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       }));
                    auto inner = Add(w, options, changed(SemanticNode(NodeKind::Row, next.Origin), [&](auto& v) {
                                         v.Font = task.Font;
                                         v.TextMode = task.Text;
                                     }));
                    Node(wrapper).FirstChild = inner;
                    if (task.Last < 0)
                        Node(task.Node).FirstChild = wrapper;
                    else
                        Node(task.Last).NextSibling = wrapper;
                    Push(w, ParseTask(ParseTaskKind::Return, task.Node));
                    Push(w, changed(task, [&](auto& v) {
                             v.Node = inner;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Depth = task.Depth + 1;
                         }));
                    break;
                }
                auto sizing = 0;
                auto legacyFont = task.Font;
                if (TrySizing(next.Text(), sizing) || TryLegacyFont(next.Text(), legacyFont) ||
                    next.Is(u"\\color") && !options.ColorIsTextColor) {
                    position++;
                    auto kind = next.Is(u"\\color") ? NodeKind::Color : sizing > 0 ? NodeKind::Sizing : NodeKind::Font;
                    auto wrapper = Add(w, options, changed(SemanticNode(kind, next.Origin), [&](auto& v) {
                                           v.Value = sizing;
                                           v.Font = sizing > 0 ? task.Font : legacyFont;
                                           v.Text = next.Text();
                                           v.TextMode = task.Text;
                                       }));
                    if (kind == NodeKind::Color)
                        Node(wrapper).Text2 = ReadColor();
                    auto inner = Add(w, options, changed(SemanticNode(NodeKind::Row, next.Origin), [&](auto& v) {
                                         v.Font = kind == NodeKind::Font ? legacyFont : task.Font;
                                         v.TextMode = task.Text;
                                     }));
                    Node(wrapper).FirstChild = inner;
                    if (task.Last < 0)
                        Node(task.Node).FirstChild = wrapper;
                    else
                        Node(task.Last).NextSibling = wrapper;
                    Push(w, ParseTask(ParseTaskKind::Return, task.Node));
                    Push(w, changed(task, [&](auto& v) {
                             v.Node = inner;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Font = kind == NodeKind::Font ? legacyFont : task.Font;
                             v.Depth = task.Depth + 1;
                         }));
                    break;
                }
                Push(w, changed(task, [&](auto& v) { v.Kind = ParseTaskKind::AttachSequence; }));
                Push(w, changed(task, [&](auto& v) { v.Kind = ParseTaskKind::Atom; }));
                break;
            }
            case ParseTaskKind::Argument: {
                SkipMathSpaces(false);
                if (task.Slot == -1)
                    while (position < parseEnd && w.Expanded[position].Is(u"\\relax")) {
                        position++;
                        SkipMathSpaces(false);
                    }
                if (position == parseEnd || w.Expanded[position].Is(u"}") || w.Expanded[position].Is(u"^") ||
                    w.Expanded[position].Is(u"_"))
                    Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Missing command or script argument.");
                Push(w, changed(task, [&](auto& v) {
                         v.Kind = ParseTaskKind::Atom;
                         v.Limit = !task.PrimitiveArgument && !w.Expanded[position].Is(u"{")
                                       ? std::min(task.Limit, position + 1)
                                       : task.Limit;
                     }));
                break;
            }
            case ParseTaskKind::Atom: {
                if (position == parseEnd)
                    Fail(MathDiagnosticCode::MissingArgument, CurrentRange(), u"Missing argument.");
                auto token = w.Expanded[position++];
                if (!token.NoExpand && token.Text().size() == 1 && IsPlainAtomCharacter(token.Text()[0]) &&
                    FindSymbol(token.Text(), task.Text, plainDefinition) && plainDefinition.CodePoint >= 0 &&
                    !((((plainDefinition.Group == u"op-token") || (plainDefinition.Group == u"accent-token"))))) {
                    result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                     v.CodePoint = plainDefinition.CodePoint;
                                     v.FontIdOverride = -1;
                                     v.Atom = plainDefinition.Atom;
                                     v.Font = task.Font == MathFont::Default ? plainDefinition.Font : task.Font;
                                     v.SymbolFont = plainDefinition.Font;
                                     v.TextMode = task.Text;
                                     v.IsMathOrd = plainDefinition.IsMathOrd;
                                 }));
                    break;
                }
                if (FindCommand(token.Text(), commandRule)) {
                    if (task.Text && !commandRule.AllowedInText || !task.Text && !commandRule.AllowedInMath)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Command is not allowed in this input mode.");
                    if (task.Slot == -1 && !commandRule.AllowedInArgument)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"This command requires a braced script argument.");
                }
                if (token.Text().starts_with(u"\\verb") && token.Length > 5) {
                    auto content = token.Text().substr(5);
                    auto starred = content[0] == u'*';
                    if (starred)
                        content = content.substr(1);
                    result = Add(w, options, changed(SemanticNode(NodeKind::Verb, token.Origin), [&](auto& v) {
                                     v.Text = std::u16string(content.substr(1, content.size() - 2));
                                     v.Value = starred ? 1 : 0;
                                     v.TextMode = true;
                                     v.Font = MathFont::MathTypewriter;
                                 }));
                    break;
                }
                if (token.Is(u"\\@char")) {
                    auto number = ReadRawGroup();
                    if (!ParseInt(number, scalar) || !IsValidScalar(scalar))
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Character code must be a valid Unicode scalar.");
                    auto character = FromScalar(scalar);
                    if (FindSymbol(character, task.Text, characterDefinition))
                        result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                         v.CodePoint = scalar;
                                         v.Font = task.Font;
                                         v.SymbolFont = characterDefinition.Font;
                                         v.TextMode = task.Text;
                                         v.IsMathOrd = false;
                                     }));
                    else
                        result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                         v.CodePoint = scalar;
                                         v.Font = task.Font;
                                         v.TextMode = task.Text;
                                     }));
                    break;
                }
                if (token.Is(u"\\tag") || token.Is(u"\\tag@literal") || token.Is(u"\\tag@paren")) {
                    if (!options.DisplayMode)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Equation tags require display mode.");
                    SkipMathSpaces(false);
                    auto starred = token.Is(u"\\tag@literal") || position < parseEnd && w.Expanded[position].Is(u"*");
                    if (position < parseEnd && w.Expanded[position].Is(u"*"))
                        position++;
                    auto target = root;
                    for (auto i = int(w.Tasks.size()) - 1; i >= 0; i--)
                        if (w.Tasks[i].Kind == ParseTaskKind::MatrixNextCell) {
                            target = w.Tasks[i].Last;
                            break;
                        }
                    if (Node(target).SecondChild >= 0)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Multiple tags in one equation row.");
                    auto tag = Add(w, options, changed(SemanticNode(NodeKind::Text, token.Origin), [&](auto& v) {
                                       v.Font = MathFont::MathRoman;
                                       v.TextMode = true;
                                       v.Text = u"\\text";
                                   }));
                    if (starred) {
                        Node(target).SecondChild = tag;
                        Push(w, changed(task, [&](auto& v) { v.Kind = ParseTaskKind::ReturnInternal; }));
                        PushArgument(tag, 0, changed(task, [&](auto& v) {
                                         v.Text = true;
                                         v.Font = MathFont::MathRoman;
                                     }));
                    } else {
                        auto body = Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                            v.TextMode = true;
                                        }));
                        Node(tag).FirstChild = body;
                        auto left = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                            v.CodePoint = u'(';
                                            v.Font = MathFont::MathRoman;
                                            v.TextMode = true;
                                        }));
                        Node(body).FirstChild = left;
                        auto tagArgument =
                            Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                    v.TextMode = true;
                                    v.Font = MathFont::MathRoman;
                                    v.Value = 1;
                                }));
                        Node(left).NextSibling = tagArgument;
                        auto right =
                            Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                    v.CodePoint = u')';
                                    v.Font = MathFont::MathRoman;
                                    v.TextMode = true;
                                }));
                        Node(tagArgument).NextSibling = right;
                        Node(target).SecondChild = tag;
                        Push(w, changed(task, [&](auto& v) { v.Kind = ParseTaskKind::ReturnInternal; }));
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::FinishTag;
                                 v.Node = tagArgument;
                             }));
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Argument;
                                 v.Text = true;
                                 v.Font = MathFont::MathRoman;
                                 v.Depth = task.Depth + 1;
                                 v.Stop = 0;
                                 v.Closing = false;
                             }));
                    }
                    break;
                }
                if (token.Is(u"\\nonumber") || token.Is(u"\\notag")) {
                    for (auto i = int(w.Tasks.size()) - 1; i >= 0; i--)
                        if (w.Tasks[i].Kind == ParseTaskKind::MatrixNextCell) {
                            Node(w.Tasks[i].Last).NeverLimits = true;
                            break;
                        }
                    result = -1;
                    break;
                }
                if (token.Is(u"\\relax")) {
                    result = Add(w, options, SemanticNode(NodeKind::Row, token.Origin));
                    break;
                }
                if (((token.Text() == u"\\nobreak") || (token.Text() == u"\\allowbreak"))) {
                    result = Add(w, options, changed(SemanticNode(NodeKind::Space, token.Origin), [&](auto& v) {
                                     v.Text = token.Text();
                                 }));
                    break;
                }
                if (token.Is(u"\\\\")) {
                    SkipMathSpaces(false);
                    auto gap = position < parseEnd && w.Expanded[position].Is(u"[")
                                   ? ParseLength(ReadRawBracket(), token.Origin)
                                   : TexLength{};
                    auto newLine = !options.DisplayMode ||
                                   !Strict(u"newLineInDisplayMode", token.Origin,
                                           u"A line break in display mode is not a LaTeX line break.", false);
                    result = Add(w, options, changed(SemanticNode(NodeKind::Space, token.Origin), [&](auto& v) {
                                     v.Text = u"\\\\";
                                     v.Length1 = gap;
                                     v.Value = newLine ? 1 : 0;
                                     v.TextMode = task.Text;
                                 }));
                    break;
                }
                if (token.Is(u"\\mathchoice") || token.Is(u"\\html@mathml")) {
                    auto choice =
                        Add(w, options,
                            changed(SemanticNode(token.Is(u"\\mathchoice") ? NodeKind::Choice : NodeKind::MathMlChoice,
                                                 token.Origin),
                                    [&](auto& v) {
                                        v.Font = task.Font;
                                        v.TextMode = task.Text;
                                        v.Text = token.Text();
                                    }));
                    Push(w, ParseTask(ParseTaskKind::Return, choice));
                    if (token.Is(u"\\mathchoice")) {
                        PushArgument(choice, 3, task, true);
                        PushArgument(choice, 2, task, true);
                    }
                    PushArgument(choice, 1, task, token.Is(u"\\mathchoice"));
                    PushArgument(choice, 0, task, token.Is(u"\\mathchoice"));
                    break;
                }
                if (token.Is(u"\\textcolor") || token.Is(u"\\color") && options.ColorIsTextColor) {
                    auto color = ReadColor();
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Color, token.Origin), [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Text2 = color;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::NormalizeChild;
                             v.Node = wrapper;
                         }));
                    PushArgument(wrapper, 0, task);
                    break;
                }
                if (token.Is(u"\\rule")) {
                    SkipMathSpaces(false);
                    auto shift = position < parseEnd && w.Expanded[position].Is(u"[")
                                     ? ParseLength(ReadRawBracket(), token.Origin)
                                     : TexLength{};
                    result = Add(w, options, changed(SemanticNode(NodeKind::Rule, token.Origin), [&](auto& v) {
                                     v.Text = u"\\rule";
                                     v.Length1 = ReadLength();
                                     v.Length2 = ReadLength();
                                     v.Length3 = shift;
                                     v.TextMode = task.Text;
                                 }));
                    break;
                }
                if (((((token.Text() == u"\\kern") || (token.Text() == u"\\mkern")) || (token.Text() == u"\\hskip")) ||
                     (token.Text() == u"\\mskip"))) {
                    auto dimension = ReadLength();
                    if ((((token.Text() == u"\\mkern") || (token.Text() == u"\\mskip"))) !=
                        (dimension.Unit == TexUnit::Mu))
                        Strict(u"mathVsTextUnits", token.Origin,
                               u"Math and text spacing units use different TeX commands.");
                    result = Add(w, options, changed(SemanticNode(NodeKind::Space, token.Origin), [&](auto& v) {
                                     v.Text = token.Text();
                                     v.Length1 = dimension;
                                     v.Font = task.Font;
                                     v.TextMode = task.Text;
                                 }));
                    break;
                }
                if (((((((((((((token.Text() == u"\\phantom") || (token.Text() == u"\\vphantom")) ||
                              (token.Text() == u"\\smash")) ||
                             (token.Text() == u"\\mathllap")) ||
                            (token.Text() == u"\\mathrlap")) ||
                           (token.Text() == u"\\mathclap")) ||
                          (token.Text() == u"\\raisebox")) ||
                         (token.Text() == u"\\vcenter")) ||
                        (token.Text() == u"\\pmb")) ||
                       (token.Text() == u"\\reflectbox")) ||
                      (token.Text() == u"\\mathreflectbox")) ||
                     (token.Text() == u"\\hbox"))) {
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Box, token.Origin), [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       }));
                    if (token.Is(u"\\raisebox"))
                        Node(wrapper).Length1 = ReadLength();
                    if (token.Is(u"\\smash")) {
                        SkipMathSpaces(false);
                        Node(wrapper).Alignment =
                            position < parseEnd && w.Expanded[position].Is(u"[") ? ReadRawBracket() : u"tb";
                    }
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = token.Is(u"\\phantom") || token.Is(u"\\hbox") ? ParseTaskKind::NormalizeChild
                                      : token.Is(u"\\pmb")                          ? ParseTaskKind::FinishClass
                                                                                    : ParseTaskKind::Return;
                             v.Node = wrapper;
                         }));
                    auto textArgument = (((token.Text() == u"\\raisebox") || (token.Text() == u"\\reflectbox")) ||
                                         (token.Text() == u"\\hbox"));
                    if (((token.Text() == u"\\raisebox") || (token.Text() == u"\\reflectbox")))
                        PushHBoxArgument(wrapper, task);
                    else
                        PushArgument(wrapper, 0, changed(task, [&](auto& v) {
                                         v.Text = textArgument || task.Text;
                                         v.Font = textArgument ? MathFont::MathRoman : task.Font;
                                     }));
                    break;
                }
                if ((((((((((token.Text() == u"\\colorbox") || (token.Text() == u"\\fcolorbox")) ||
                           (token.Text() == u"\\fbox")) ||
                          (token.Text() == u"\\cancel")) ||
                         (token.Text() == u"\\bcancel")) ||
                        (token.Text() == u"\\xcancel")) ||
                       (token.Text() == u"\\phase")) ||
                      (token.Text() == u"\\sout")) ||
                     (token.Text() == u"\\angl"))) {
                    if (token.Is(u"\\sout") && !task.Text)
                        Strict(u"mathVsSout", token.Origin, u"Text strikeout used in math mode.");
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Enclose, token.Origin), [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       }));
                    if (token.Is(u"\\fcolorbox"))
                        Node(wrapper).Text3 = ReadColor();
                    if (token.Is(u"\\colorbox") || token.Is(u"\\fcolorbox"))
                        Node(wrapper).Text2 = ReadColor();
                    auto textArgument = ((((token.Text() == u"\\colorbox") || (token.Text() == u"\\fcolorbox")) ||
                                          (token.Text() == u"\\fbox")) ||
                                         (token.Text() == u"\\angl"));
                    Push(w, ParseTask(ParseTaskKind::Return, wrapper));
                    if (textArgument)
                        PushHBoxArgument(wrapper, task);
                    else
                        PushArgument(wrapper, 0, task);
                    break;
                }
                if (TryClass(token.Text(), atomClass) ||
                    ((((token.Text() == u"\\@binrel") || (token.Text() == u"\\overset")) ||
                      (token.Text() == u"\\underset")) ||
                     (token.Text() == u"\\stackrel"))) {
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Class, token.Origin), [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Atom = atomClass;
                                           v.Font = task.Font;
                                           v.TextMode = task.Text;
                                       }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishClass;
                             v.Node = wrapper;
                         }));
                    if (((((token.Text() == u"\\@binrel") || (token.Text() == u"\\overset")) ||
                          (token.Text() == u"\\underset")) ||
                         (token.Text() == u"\\stackrel")))
                        PushArgument(wrapper, 1, task);
                    PushArgument(wrapper, 0, task,
                                 (((((((token.Text() == u"\\mathord") || (token.Text() == u"\\mathbin")) ||
                                      (token.Text() == u"\\mathrel")) ||
                                     (token.Text() == u"\\mathopen")) ||
                                    (token.Text() == u"\\mathclose")) ||
                                   (token.Text() == u"\\mathpunct")) ||
                                  (token.Text() == u"\\mathinner")));
                    break;
                }
                if (((token.Text() == u"\\boldsymbol") || (token.Text() == u"\\bm"))) {
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Class, token.Origin), [&](auto& v) {
                                           v.Text = token.Text();
                                           v.Font = MathFont::MathBoldItalic;
                                       }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishClass;
                             v.Node = wrapper;
                         }));
                    PushArgument(wrapper, 0, changed(task, [&](auto& v) { v.Font = MathFont::MathBoldItalic; }));
                    break;
                }
                if (task.Text && (token.Is(u"$") || token.Is(u"\\("))) {
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Style, token.Origin), [&](auto& v) {
                                           v.Style = MathStyle::Text;
                                           v.Font = MathFont::Default;
                                           v.TextMode = true;
                                       }));
                    auto body = Add(w, options, SemanticNode(NodeKind::Row, token.Origin));
                    Node(wrapper).FirstChild = body;
                    Push(w, ParseTask(ParseTaskKind::Return, wrapper));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::Sequence;
                             v.Node = body;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Stop = token.Is(u"$") ? (int)6 : (int)7;
                             v.Closing = false;
                             v.Text = false;
                             v.Font = MathFont::Default;
                             v.Depth = task.Depth + 1;
                         }));
                    break;
                }
                if (token.Is(u"{") || token.Is(u"\\begingroup")) {
                    auto semisimple = token.Is(u"\\begingroup");
                    auto row = Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                       v.Value = semisimple ? 2 : 1;
                                       v.TextMode = task.Text;
                                       v.Font = task.Font;
                                   }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::Sequence;
                             v.Node = row;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Closing = !semisimple;
                             v.Stop = semisimple ? (int)5 : (int)0;
                             v.Slot = 0;
                             v.Depth = task.Depth + 1;
                         }));
                    break;
                }
                if (((((((((((token.Text() == u"\\frac") || (token.Text() == u"\\dfrac")) ||
                            (token.Text() == u"\\tfrac")) ||
                           (token.Text() == u"\\cfrac")) ||
                          (token.Text() == u"\\binom")) ||
                         (token.Text() == u"\\dbinom")) ||
                        (token.Text() == u"\\tbinom")) ||
                       (token.Text() == u"\\\\atopfrac")) ||
                      (token.Text() == u"\\\\bracefrac")) ||
                     (token.Text() == u"\\\\brackfrac"))) {
                    if (task.Text)
                        Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                             u"Fractions are only supported in math mode.");
                    auto binom = token.Text().ends_with(u"binom");
                    auto noBar = binom || token.Text().starts_with(u"\\\\");
                    auto fraction =
                        Add(w, options, changed(SemanticNode(NodeKind::Fraction, token.Origin), [&](auto& v) {
                                v.Font = task.Font;
                                v.HasBar = !noBar;
                                v.Text = token.Is(u"\\cfrac") ? std::optional<std::u16string>{u"\\cfrac"}
                                                              : std::optional<std::u16string>{};
                                v.LeftDelimiter = binom                        ? u'('
                                                  : token.Is(u"\\\\bracefrac") ? u'{'
                                                  : token.Is(u"\\\\brackfrac") ? u'['
                                                                               : 0;
                                v.RightDelimiter = binom                        ? u')'
                                                   : token.Is(u"\\\\bracefrac") ? u'}'
                                                   : token.Is(u"\\\\brackfrac") ? u']'
                                                                                : 0;
                            }));
                    auto outer = fraction;
                    if (((token.Text()[1] == u'd') || (token.Text()[1] == u't')) || token.Is(u"\\cfrac"))
                        outer = Add(w, options, changed(SemanticNode(NodeKind::Style, token.Origin), [&](auto& v) {
                                        v.FirstChild = fraction;
                                        v.Style = token.Text()[1] == u't' ? MathStyle::Text : MathStyle::Display;
                                        v.Font = task.Font;
                                    }));
                    Push(w, ParseTask(ParseTaskKind::Return, outer));
                    PushArgument(fraction, 1, task);
                    PushArgument(fraction, 0, task);
                    break;
                }
                if (token.Is(u"\\sqrt")) {
                    if (task.Text)
                        Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                             u"Radicals are only supported in math mode.");
                    auto radical = Add(w, options, changed(SemanticNode(NodeKind::Radical, token.Origin), [&](auto& v) {
                                           v.Font = task.Font;
                                       }));
                    SkipMathSpaces(false);
                    auto index = -1;
                    if (position < parseEnd && w.Expanded[position].Is(u"[")) {
                        position++;
                        index = Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                        v.Font = task.Font;
                                    }));
                        Node(radical).SecondChild = index;
                    }
                    Push(w, ParseTask(ParseTaskKind::Return, radical));
                    PushArgument(radical, 0, task, index < 0);
                    if (index >= 0)
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Sequence;
                                 v.Node = index;
                                 v.Last = -1;
                                 v.Previous = -1;
                                 v.Closing = false;
                                 v.Stop = 2;
                                 v.Slot = 0;
                                 v.Depth = task.Depth + 1;
                             }));
                    break;
                }
                if (token.Is(u"\\genfrac")) {
                    auto fraction = Add(
                        w, options,
                        changed(SemanticNode(NodeKind::Fraction, token.Origin), [&](auto& v) { v.Font = task.Font; }));
                    auto leftHolder = Add(w, options, SemanticNode(NodeKind::Row, token.Origin));
                    auto rightHolder = Add(w, options, SemanticNode(NodeKind::Row, token.Origin));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::GenfracAfterDelimiters;
                             v.Node = fraction;
                             v.Last = leftHolder;
                             v.Previous = rightHolder;
                         }));
                    PushArgument(rightHolder, 0, changed(task, [&](auto& v) { v.Text = false; }));
                    PushArgument(leftHolder, 0, changed(task, [&](auto& v) { v.Text = false; }));
                    break;
                }
                if (token.Is(u"\\left")) {
                    auto left = ReadDelimiter();
                    auto wrapper =
                        Add(w, options, changed(SemanticNode(NodeKind::Delimited, token.Origin), [&](auto& v) {
                                v.LeftDelimiter = left;
                                v.Atom = MathAtom::Inner;
                                v.Font = task.Font;
                            }));
                    auto body =
                        Add(w, options,
                            changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) { v.Font = task.Font; }));
                    Node(wrapper).FirstChild = body;
                    delimiterDepth++;
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::FinishDelimited;
                             v.Node = wrapper;
                         }));
                    Push(w, changed(task, [&](auto& v) {
                             v.Kind = ParseTaskKind::Sequence;
                             v.Node = body;
                             v.Last = -1;
                             v.Previous = -1;
                             v.Closing = false;
                             v.Stop = 3;
                             v.Slot = 0;
                             v.Depth = task.Depth + 1;
                         }));
                    break;
                }
                auto delimiterSize = 0;
                auto delimiterAtom = MathAtom::Ord;
                if (token.Is(u"\\middle") || TryDelimiterSize(token.Text(), delimiterSize, delimiterAtom)) {
                    if (token.Is(u"\\middle") && delimiterDepth == 0)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"A middle delimiter requires a left/right group.");
                    auto cp = ReadDelimiter();
                    result = Add(w, options, changed(SemanticNode(NodeKind::Delimiter, token.Origin), [&](auto& v) {
                                     v.CodePoint = cp;
                                     v.Value = token.Is(u"\\middle") ? 0 : delimiterSize;
                                     v.Atom = token.Is(u"\\middle") ? MathAtom::Rel : delimiterAtom;
                                     v.Font = task.Font;
                                 }));
                    break;
                }
                if (token.Is(u"\\begin")) {
                    auto environment = ReadRawGroup();
                    if ((((((((((environment == u"equation") || (environment == u"equation*")) ||
                               (environment == u"align")) ||
                              (environment == u"align*")) ||
                             (environment == u"alignat")) ||
                            (environment == u"alignat*")) ||
                           (environment == u"gather")) ||
                          (environment == u"gather*")) ||
                         (environment == u"CD")) &&
                        !options.DisplayMode)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"This environment requires display mode.");
                    if (!IsEnvironment(environment))
                        Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                             u"Math environment is not implemented: " + environment);
                    auto fixedStretch =
                        (((((((environment == u"cases") || (environment == u"dcases")) || (environment == u"rcases")) ||
                            (environment == u"drcases")) ||
                           (environment == u"smallmatrix")) ||
                          (environment == u"subarray")) ||
                         (environment == u"CD"));
                    if (!fixedStretch && pendingArrayStretch != 0 &&
                        (!std::isfinite(pendingArrayStretch) || pendingArrayStretch <= 0 || pendingArrayStretch > 1000))
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Invalid or excessive array stretch factor.");
                    if (fixedStretch)
                        pendingArrayStretch = 0;
                    auto alignment =
                        (((environment == u"array") || (environment == u"darray")) || (environment == u"subarray"))
                            ? ReadRawGroup()
                        : environment.ends_with(u'*') && position < parseEnd && w.Expanded[position].Is(u"[")
                            ? ReadRawBracket()
                            : u"c";
                    for (auto a : alignment)
                        if (!(((((((a == u'l') || (a == u'c')) || (a == u'r')) || (a == u'|')) || (a == u':')))))
                            Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                                 u"Array column specification supports l, c, r, and vertical rules.");
                    if (alignment.size() == 0)
                        Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                             u"Expected a nonempty column specification.");
                    auto matrix = Add(w, options, changed(SemanticNode(NodeKind::Matrix, token.Origin), [&](auto& v) {
                                          v.Text = environment;
                                          v.Alignment = alignment;
                                          v.Font = task.Font;
                                          v.Value2 = pendingArrayStretch;
                                      }));
                    pendingArrayStretch = 0;
                    auto envBase = TrimEnd(environment, u'*');
                    Node(matrix).LeftDelimiter =
                        ((envBase == u"pmatrix")
                             ? u'('
                             : ((envBase == u"bmatrix")
                                    ? u'['
                                    : ((((envBase == u"Bmatrix") || (envBase == u"cases")) || (envBase == u"dcases"))
                                           ? u'{'
                                           : ((envBase == u"vmatrix") ? u'|'
                                                                      : ((envBase == u"Vmatrix") ? 0x2016 : 0)))));
                    Node(matrix).RightDelimiter =
                        ((envBase == u"pmatrix")
                             ? u')'
                             : ((envBase == u"bmatrix")
                                    ? u']'
                                    : ((((envBase == u"Bmatrix") || (envBase == u"rcases")) || (envBase == u"drcases"))
                                           ? u'}'
                                           : ((envBase == u"vmatrix") ? u'|'
                                                                      : ((envBase == u"Vmatrix") ? 0x2016 : 0)))));
                    if ((((environment == u"alignedat") || (environment == u"alignat")) ||
                         (environment == u"alignat*"))) {
                        auto pairs = ReadRawGroup();
                        if (!ParseInt(pairs, pairCount) || pairCount < 1 || pairCount > options.MaxNodes / 2)
                            Fail(MathDiagnosticCode::UnexpectedToken, token.Origin,
                                 u"Alignedat requires a positive bounded column pair count.");
                        Node(matrix).Value = pairCount;
                    }
                    auto rules = ReadRules();
                    auto row = Add(w, options, changed(SemanticNode(NodeKind::MatrixRow, token.Origin), [&](auto& v) {
                                       v.RuleAbove = rules;
                                   }));
                    Node(matrix).FirstChild = row;
                    AddMatrixCell(matrix, row, -1, 0, task);
                    break;
                }
                if (((((token.Text() == u"\\operatorname") || (token.Text() == u"\\operatorname@")) ||
                      (token.Text() == u"\\operatornamewithlimits")) ||
                     (token.Text() == u"\\mathop"))) {
                    SkipMathSpaces(false);
                    auto starred = token.Is(u"\\operatornamewithlimits");
                    if (token.Is(u"\\operatorname") && position < parseEnd && w.Expanded[position].Is(u"*")) {
                        position++;
                        starred = true;
                    }
                    auto op = Add(w, options, changed(SemanticNode(NodeKind::Operator, token.Origin), [&](auto& v) {
                                      v.Text = token.Is(u"\\mathop") ? u"\\mathop" : u"\\operatorname";
                                      v.Text2 = starred ? std::optional<std::u16string>{u"star"}
                                                        : std::optional<std::u16string>{};
                                      v.Limits = starred;
                                      v.Atom = MathAtom::Op;
                                      v.Font = task.Font;
                                  }));
                    Push(w, ParseTask(ParseTaskKind::Return, op));
                    PushArgument(
                        op, 0,
                        changed(task,
                                [&](auto& v) { v.Font = token.Is(u"\\mathop") ? task.Font : MathFont::MathRoman; }),
                        token.Is(u"\\mathop"));
                    break;
                }
                if (TryNamedOperator(token.Text(), operatorText, operatorLimits)) {
                    auto op = Add(w, options, changed(SemanticNode(NodeKind::Operator, token.Origin), [&](auto& v) {
                                      v.Text = token.Text();
                                      v.Limits = operatorLimits;
                                      v.Atom = MathAtom::Op;
                                      v.Font = task.Font;
                                  }));
                    auto row = Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                       v.Font = MathFont::MathRoman;
                                   }));
                    Node(op).FirstChild = row;
                    auto previous = -1;
                    for (auto c : operatorText) {
                        auto letter =
                            Add(w, options,
                                c == u' ' ? changed(SemanticNode(NodeKind::Space, token.Origin),
                                                    [&](auto& v) { v.Value = 3.0 / 18; })
                                          : changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                                v.CodePoint = c;
                                                v.Font = MathFont::MathRoman;
                                                v.SymbolFont = MathFont::Default;
                                                v.IsMathOrd = true;
                                            }));
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
                    result = Add(w, options, changed(SemanticNode(NodeKind::Operator, token.Origin), [&](auto& v) {
                                     v.Text = unicodeOperator;
                                     v.CodePoint = opDefinition.CodePoint;
                                     v.Atom = MathAtom::Op;
                                     v.Limits = !(
                                         (((opDefinition.CodePoint >= 0x222b) && (opDefinition.CodePoint <= 0x2233))));
                                     v.Font = task.Font;
                                 }));
                    break;
                }
                if (TryAccent(token.Text(), accentCodePoint)) {
                    auto textAccent = (((((((((((((token.Text() == u"\\'") || (token.Text() == u"\\`")) ||
                                                 (token.Text() == u"\\^")) ||
                                                (token.Text() == u"\\~")) ||
                                               (token.Text() == u"\\=")) ||
                                              (token.Text() == u"\\u")) ||
                                             (token.Text() == u"\\.")) ||
                                            (token.Text() == u"\\\"")) ||
                                           (token.Text() == u"\\c")) ||
                                          (token.Text() == u"\\r")) ||
                                         (token.Text() == u"\\H")) ||
                                        (token.Text() == u"\\v")) ||
                                       (token.Text() == u"\\textcircled"));
                    if (textAccent && !task.Text)
                        Strict(u"mathVsTextAccents", token.Origin, u"Text accent used in math mode.");
                    auto accent = Add(w, options, changed(SemanticNode(NodeKind::Accent, token.Origin), [&](auto& v) {
                                          v.Text = token.Text();
                                          v.CodePoint = accentCodePoint;
                                          v.Font = task.Font;
                                          v.TextMode = textAccent || task.Text;
                                      }));
                    Push(w, ParseTask(ParseTaskKind::Return, accent));
                    PushArgument(accent, 0, task, textAccent);
                    break;
                }
                if (IsExtensibleArrow(token.Text())) {
                    auto arrow = Add(w, options, changed(SemanticNode(NodeKind::Accent, token.Origin), [&](auto& v) {
                                         v.Text = token.Text();
                                         v.Atom = MathAtom::Rel;
                                         v.Font = task.Font;
                                     }));
                    SkipMathSpaces(false);
                    auto below = -1;
                    if (position < parseEnd && w.Expanded[position].Is(u"[")) {
                        position++;
                        below = Add(w, options, changed(SemanticNode(NodeKind::Row, token.Origin), [&](auto& v) {
                                        v.Font = task.Font;
                                    }));
                        Node(arrow).SecondChild = below;
                    }
                    Push(w, ParseTask(ParseTaskKind::Return, arrow));
                    PushArgument(arrow, 0, task);
                    if (below >= 0)
                        Push(w, changed(task, [&](auto& v) {
                                 v.Kind = ParseTaskKind::Sequence;
                                 v.Node = below;
                                 v.Last = -1;
                                 v.Previous = -1;
                                 v.Closing = false;
                                 v.Stop = 2;
                                 v.Slot = 0;
                                 v.Depth = task.Depth + 1;
                             }));
                    break;
                }
                if (TryFont(token.Text(), font)) {
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Font, token.Origin), [&](auto& v) {
                                           v.Font = font;
                                           v.TextMode = task.Text;
                                       }));
                    Push(w, ParseTask(ParseTaskKind::Return, wrapper));
                    PushArgument(wrapper, 0, changed(task, [&](auto& v) { v.Font = font; }));
                    break;
                }
                if (TryTextFont(token.Text(), textFont)) {
                    auto family = task.TextFamily;
                    auto bold = task.TextBold;
                    auto italic = task.TextItalic;
                    if (((token.Text() == u"\\textrm") || (token.Text() == u"\\textnormal")))
                        family = 0;
                    else if (token.Is(u"\\textsf"))
                        family = 1;
                    else if (token.Is(u"\\texttt"))
                        family = 2;
                    if (token.Is(u"\\textbf"))
                        bold = true;
                    else if (token.Is(u"\\textmd"))
                        bold = false;
                    if (token.Is(u"\\textit"))
                        italic = true;
                    else if (token.Is(u"\\textup"))
                        italic = false;
                    else if (token.Is(u"\\emph"))
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
                    if (token.Is(u"\\text"))
                        textFont = task.Font;
                    auto wrapper = Add(w, options, changed(SemanticNode(NodeKind::Text, token.Origin), [&](auto& v) {
                                           v.Font = textFont;
                                           v.TextMode = true;
                                           v.Text = token.Text();
                                       }));
                    Push(w, ParseTask(ParseTaskKind::Return, wrapper));
                    PushArgument(wrapper, 0, changed(task, [&](auto& v) {
                                     v.Font = textFont;
                                     v.Text = true;
                                     v.TextFamily = family;
                                     v.TextBold = bold;
                                     v.TextItalic = italic;
                                 }));
                    break;
                }
                if (token.Is(u"\\,") || token.Is(u"\\:") || token.Is(u"\\;") || token.Is(u"\\!") ||
                    token.Is(u"\\quad") || token.Is(u"\\qquad")) {
                    auto width = token.Is(u"\\,")      ? 3.0 / 18
                                 : token.Is(u"\\:")    ? 4.0 / 18
                                 : token.Is(u"\\;")    ? 5.0 / 18
                                 : token.Is(u"\\!")    ? -3.0 / 18
                                 : token.Is(u"\\quad") ? 1
                                                       : 2;
                    if (task.Text)
                        width = token.Is(u"\\,")   ? .1667
                                : token.Is(u"\\:") ? .2222
                                : token.Is(u"\\;") ? .2777
                                : token.Is(u"\\!") ? -.1667
                                                   : width;
                    result = Add(w, options, changed(SemanticNode(NodeKind::Space, token.Origin), [&](auto& v) {
                                     v.Value = width;
                                     v.TextMode = task.Text;
                                     v.Font = task.Font;
                                 }));
                    break;
                }
                if (IsWhiteSpace(token.Text()[0]) && task.Text) {
                    result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                     v.CodePoint = 32;
                                     v.Font = task.Font;
                                     v.TextMode = true;
                                 }));
                    break;
                }
                if (token.Is(u"}") || token.Is(u"^") || token.Is(u"_") || token.Is(u"&") || token.Is(u"#") ||
                    token.Is(u"$"))
                    Fail(MathDiagnosticCode::UnexpectedToken, token.Origin, u"Unexpected TeX special token.");
                if (token.Text()[0] != u'\\' && (token.Text().size() > 1 || token.Text()[0] >= 0x80)) {
                    auto scalar = RuneAt(*token.Source, token.Start);
                    auto expansion = !FindSymbol(FromScalar(scalar), task.Text, definition)
                                         ? UnicodeData::Decompose(scalar)
                                         : std::nullopt;
                    if (!((expansion == std::nullopt)) && !task.Text)
                        Strict(u"unicodeTextInMathMode", token.Origin, u"Accented Unicode text used in math mode.");
                    std::u16string combined =
                        !expansion
                            ? std::u16string(token.Text())
                            : *expansion +
                                  std::u16string(token.Text().substr(RuneLength(RuneAt(*token.Source, token.Start))));
                    auto baseLength = RuneLength(RuneAt(combined));
                    if (baseLength < combined.size() &&
                        ((combined[baseLength] >= u'\u0300') && (combined[baseLength] <= u'\u036f'))) {
                        auto baseText = combined.substr(0, baseLength);
                        if (baseText == u"i")
                            baseText = u"ı";
                        else if (baseText == u"j")
                            baseText = u"ȷ";
                        auto basis = AddSymbol(baseText, token.Origin, task);
                        for (auto mark : combined.substr(baseLength)) {
                            auto command = UnicodeData::Accent(mark, task.Text);
                            auto point = 0;
                            if ((command == std::nullopt) || !TryAccent(*command, point))
                                Fail(MathDiagnosticCode::UnknownSymbol, token.Origin, u"Unsupported combining accent.");
                            basis = Add(w, options, changed(SemanticNode(NodeKind::Accent, token.Origin), [&](auto& v) {
                                            v.FirstChild = basis;
                                            v.Text = command;
                                            v.CodePoint = point;
                                            v.TextMode = task.Text;
                                            v.Font = task.Font;
                                        }));
                        }
                        result = basis;
                        break;
                    }
                }
                if (!FindSymbol(token.Text(), task.Text, definition)) {
                    if (token.Text()[0] >= 0x80 && (options.TextShaper != nullptr)) {
                        if (!task.Text)
                            Strict(u"unicodeTextInMathMode", token.Origin, u"Unicode text used in math mode.");
                        result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                         v.CodePoint = RuneAt(*token.Source, token.Start);
                                         v.Font = task.Font;
                                         v.TextMode = true;
                                     }));
                        break;
                    }
                    Fail(token.Text()[0] == u'\\' ? MathDiagnosticCode::UnsupportedCommand
                                                  : MathDiagnosticCode::UnknownSymbol,
                         token.Origin,
                         token.Text()[0] == u'\\' ? u"Command is not implemented."
                                                  : u"Symbol requires an available font or an explicit text shaper.");
                }
                if (definition.CodePoint < 0 || definition.Group == u"accent-token")
                    Fail(MathDiagnosticCode::UnsupportedCommand, token.Origin,
                         u"This symbol requires an unsupported construction rule.");
                if (definition.Group == u"op-token") {
                    result = Add(w, options, changed(SemanticNode(NodeKind::Operator, token.Origin), [&](auto& v) {
                                     v.Text = token.Text();
                                     v.CodePoint = definition.CodePoint;
                                     v.Atom = MathAtom::Op;
                                     v.Limits =
                                         token.Is(u"\\intop") || token.Is(u"\\smallint") ||
                                         !((((definition.CodePoint >= 0x222b) && (definition.CodePoint <= 0x2233))));
                                     v.Font = task.Font;
                                 }));
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
                result = Add(w, options, changed(SemanticNode(NodeKind::Symbol, token.Origin), [&](auto& v) {
                                 v.CodePoint = definition.CodePoint;
                                 v.OriginalCodePoint = wide;
                                 v.FontIdOverride = wideFont;
                                 v.Atom = definition.Atom;
                                 v.Font = task.Font == MathFont::Default ? definition.Font : task.Font;
                                 v.SymbolFont = definition.Font;
                                 v.TextMode = task.Text;
                                 v.IsMathOrd = definition.IsMathOrd;
                             }));
                break;
            }
            }
        }
        auto topTag = Node(root).SecondChild;
        if (topTag >= 0) {
            Node(root).SecondChild = -1;
            return Add(w, options,
                       changed(SemanticNode(NodeKind::Tag, SourceRange{0, int(source.size())}), [&](auto& v) {
                           v.FirstChild = root;
                           v.SecondChild = topTag;
                       }));
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
