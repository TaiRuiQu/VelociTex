#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace velocitex {
enum class MathAtom { Ord, Op, Bin, Rel, Open, Close, Punct, Inner };
enum class MathStyle { Display, Text, Script, ScriptScript };
enum class MathFont {
    Default,
    Main,
    MathItalic,
    MathRoman,
    MathBold,
    MathBoldItalic,
    MathSans,
    MathSansBold,
    MathSansItalic,
    MathTypewriter,
    AMS,
    Caligraphic,
    Fraktur,
    Script,
    MathNormal,
    MainBoldItalic,
    MathSansBoldItalic
};
enum class MathStrictMode { Ignore, Warn, Error };
enum class MathDiagnosticCode {
    None,
    InvalidOptions,
    InputLimit,
    NodeLimit,
    NestingLimit,
    ExpansionLimit,
    TokenLimit,
    UnexpectedToken,
    MissingArgument,
    UnclosedGroup,
    UnexpectedGroupEnd,
    DoubleSuperscript,
    DoubleSubscript,
    UnsupportedCommand,
    UnknownSymbol,
    InvalidMacroDefinition,
    WorkspaceInUse,
    SessionInUse,
    Cancelled,
    StrictViolation
};
enum class NodeKind {
    Row,
    Symbol,
    Script,
    Fraction,
    Radical,
    Style,
    Font,
    Text,
    Space,
    Operator,
    Delimiter,
    Delimited,
    Accent,
    Matrix,
    MatrixRow,
    MatrixCell,
    Box,
    Color,
    Enclose,
    Rule,
    Sizing,
    Choice,
    MathMlChoice,
    Class,
    Tag,
    Verb
};
enum class TexUnit { Em, Ex, Mu, Pt, Bp, Pc, In, Cm, Mm, Dd, Cc, Nd, Nc, Sp, Px };
enum class MathPathOperation { Move, Line, Cubic, Close };
enum class MathPaintKind { Glyph, Rule, Path };
struct SourceRange {
    int Start{}, Length{};
    friend bool operator==(const SourceRange&, const SourceRange&) = default;
};
struct MathSize {
    double Width{}, Height{};
};
struct MathBounds {
    double X{}, Y{}, Width{}, Height{};
};
struct TexLength {
    double Number{};
    TexUnit Unit{};
};
struct MathColor {
    uint8_t R{}, G{}, B{}, A{255};
    friend bool operator==(const MathColor&, const MathColor&) = default;
};
struct MathTransform {
    double M11{1}, M12{}, M21{}, M22{1}, Dx{}, Dy{};
    static MathTransform Identity() { return {}; }
    friend bool operator==(const MathTransform&, const MathTransform&) = default;
};
struct MathGlyph {
    int FontId{};
    uint16_t GlyphId{};
    int CodePoint{};
    double X{}, Y{}, FontSize{};
};
struct MathRule {
    double X{}, Y{}, Width{}, Height{};
};
struct MathPathSegment {
    MathPathOperation Operation{};
    double X1{}, Y1{}, X2{}, Y2{}, X3{}, Y3{};
};
struct MathPath {
    std::vector<MathPathSegment> Segments;
    double X{}, Y{}, ScaleX{}, ScaleY{};
    MathBounds ClipBounds;
};
struct MathTextGlyph {
    int CodePoint{};
    double X{}, Y{}, FontSize{};
    std::u16string FontFamily;
};
struct MathTextRun {
    std::u16string Text;
    double X{}, Y{}, FontSize{};
    int FirstPath{}, PathCount{};
    double Advance{};
};
struct MathTextShape {
    double Advance{};
    std::vector<MathPathSegment> Segments;
    MathBounds InkBounds;
    std::u16string FontFamily;
};
struct MathTextRunShape {
    double Advance{};
    std::vector<MathPath> Paths;
    MathBounds InkBounds;
};
struct MathPaintCommand {
    MathPaintKind Kind{};
    int Index{}, StateIndex{};
};
struct MathPaintState {
    std::optional<MathColor> Color;
    MathTransform Transform;
    friend bool operator==(const MathPaintState&, const MathPaintState&) = default;
};
struct MathDiagnostic {
    MathDiagnosticCode Code{};
    SourceRange Range;
    std::u16string Message;
    bool IsError() const { return Code != MathDiagnosticCode::None; }
};
struct MathWarning {
    std::u16string Code;
    SourceRange Range;
    std::u16string Message;
};
struct IMathTextShaper {
    virtual ~IMathTextShaper() = default;
    virtual bool TryShape(int, MathFont, MathTextShape&) = 0;
    virtual bool TryShapeRun(std::u16string_view, MathFont, MathTextRunShape&) { return false; }
    virtual bool SupportsRuns() const { return false; }
};
struct MacroSession;
struct MathOptions {
    double FontSize{24};
    bool DisplayMode{};
    int MaxInputLength{32768}, MaxNodes{65536}, MaxNesting{128}, MaxExpansion{1000}, MaxTokens{65536};
    std::unordered_map<std::u16string, std::u16string> Macros;
    MacroSession* Session{};
    IMathTextShaper* TextShaper{};
    double MaxSize{std::numeric_limits<double>::infinity()}, MinRuleThickness{};
    bool ColorIsTextColor{}, GlobalGroup{}, Leqno{}, Fleqn{};
    double DisplayWidth{};
    int EquationNumber{1};
    // KaTeX's `throwOnError`: when it is false an unsupported command is
    // rendered in the error colour instead of failing the compilation.
    bool ThrowOnError{true};
    // KaTeX's `errorColor`: the colour of the unsupported command text.
    std::u16string ErrorColor{u"#cc0000"};
    MathStrictMode StrictMode{MathStrictMode::Warn};
    std::function<MathStrictMode(const MathWarning&)> StrictHandler;
    std::function<void()> CheckCancellation;
};
struct SemanticNode {
    NodeKind Kind{};
    SourceRange Range;
    int FirstChild{-1}, NextSibling{-1}, SecondChild{-1}, ThirdChild{-1}, FourthChild{-1}, CodePoint{},
        OriginalCodePoint{}, FontIdOverride{-1};
    MathAtom Atom{};
    MathFont Font{}, SymbolFont{};
    MathStyle Style{MathStyle::Text};
    bool TextMode{};
    double Value{}, Value2{}, Value3{};
    std::optional<std::u16string> Text, Alignment, Text2, Text3;
    TexLength Length1, Length2, Length3;
    int LeftDelimiter{}, RightDelimiter{}, ColumnCount{}, RuleAbove{}, RuleBelow{};
    bool HasBar{true}, Limits{}, AlwaysLimits{}, NeverLimits{}, IsMathOrd{};
    // KaTeX's `resetFont` flag on a "styling" group.  The "hbox" argument
    // type creates such a group (`src/Parser.ts`), and the group keeps the
    // "mord" span of the argument instead of flattening it.
    bool ResetFont{};
    SemanticNode() = default;
    SemanticNode(NodeKind kind, SourceRange range) : Kind(kind), Range(range) {}
};
struct LayoutOutput {
    std::vector<MathGlyph> Glyphs;
    std::vector<MathRule> Rules;
    std::vector<MathPath> Paths;
    MathSize LogicalSize;
    double Baseline{};
    MathBounds InkBounds;
    std::vector<MathTextGlyph> TextGlyphs;
    std::vector<MathPaintCommand> PaintCommands;
    std::vector<MathPaintState> PaintStates;
    std::vector<MathTextRun> TextRuns;
};
struct CompileFailure : std::exception {
    MathDiagnostic Diagnostic;
    explicit CompileFailure(MathDiagnostic diagnostic) : Diagnostic(std::move(diagnostic)) {}
    const char* what() const noexcept override { return "VelociTex compilation failed"; }
};
inline void check_cancel(const MathOptions& options) {
    if (options.CheckCancellation)
        options.CheckCancellation();
}
inline std::u16string_view text(const std::optional<std::u16string>& value) {
    return value ? std::u16string_view(*value) : std::u16string_view{};
}
// True when an optional text equals the given name.
//
// The comparison of an optional against a string literal would build a
// temporary string first, and the parser asks this question for every
// command that takes an argument.
inline bool text_is(const std::optional<std::u16string>& value, std::u16string_view expected) {
    return value && std::u16string_view(*value) == expected;
}
// Compares two short texts without calling into the C library.
//
// The comparison of two `string_view`s asks `memcmp` for the bytes, and the
// length it asks for is the shorter of the two lengths — a value that is only
// known at run time.  The call into the C library therefore stays, and in
// WebAssembly a call costs several times more than the comparison itself.
//
// The renderer asks the question for every atom of a formula: the class of a
// node decides the spacing of its neighbours and whether a binary operator is
// cancelled, and the tag of a MathML node decides how it is written.
//
// A comparison against a *literal* is different: the length of the literal is
// known at compile time, so the comparison is a load and a compare, with no
// call at all.  The literal is therefore taken as an array and not as a view,
// and the `_u16` suffix is not used with it: the suffix builds a view, and the
// view hides the length of the literal from the compiler.
//
// The bytes are compared four at a time by hand, because **the WebAssembly
// target does not expand a `memcmp` of a known length into loads**: the
// DataLayout of the target does not allow an unaligned access, so the
// expansion of the intrinsic is skipped and every comparison of the renderer
// becomes a call into the C library.  The renderer asks the question for
// every atom of a formula, so the calls add up.
//
// The copy of four bytes reads exactly the four bytes of the text, so nothing
// outside of the text is touched; the same holds for the last bytes, which are
// compared one at a time.
template <size_t N>
inline bool same_text(std::string_view a, const char (&b)[N]) {
    constexpr size_t kLength = N - 1;
    if (a.size() != kLength)
        return false;
    const char* data = a.data();
    size_t i = 0;
    for (; i + 4 <= kLength; i += 4) {
        uint32_t left, right;
        std::memcpy(&left, data + i, 4);
        std::memcpy(&right, b + i, 4);
        if (left != right)
            return false;
    }
    for (; i < kLength; ++i)
        if (data[i] != b[i])
            return false;
    return true;
}
template <size_t N>
inline bool same_text(std::u16string_view a, const char16_t (&b)[N]) {
    constexpr size_t kLength = N - 1;
    if (a.size() != kLength)
        return false;
    const char16_t* data = a.data();
    size_t i = 0;
    for (; i + 2 <= kLength; i += 2) {
        uint32_t left, right;
        std::memcpy(&left, data + i, 4);
        std::memcpy(&right, b + i, 4);
        if (left != right)
            return false;
    }
    for (; i < kLength; ++i)
        if (data[i] != b[i])
            return false;
    return true;
}
inline bool is_high_surrogate(char16_t c) {
    return c >= 0xd800 && c <= 0xdbff;
}
inline bool is_low_surrogate(char16_t c) {
    return c >= 0xdc00 && c <= 0xdfff;
}
inline int scalar_at(std::u16string_view source, size_t i, int& length) {
    length = 1;
    int c = source[i];
    if (is_high_surrogate(char16_t(c)) && i + 1 < source.size() && is_low_surrogate(source[i + 1])) {
        length = 2;
        return 0x10000 + ((c - 0xd800) << 10) + (source[i + 1] - 0xdc00);
    }
    return c;
}
inline void append_scalar(std::u16string& out, int cp) {
    if (cp <= 0xffff)
        out.push_back(char16_t(cp));
    else {
        cp -= 0x10000;
        out.push_back(char16_t(0xd800 + (cp >> 10)));
        out.push_back(char16_t(0xdc00 + (cp & 1023)));
    }
}

// A view of a UTF-16 string literal whose length is known at compile time.
//
// `std::u16string_view(const char16_t*)` asks `char_traits<char16_t>::length`
// for the length of the string, and that is a loop over the characters.  GCC
// has no constant folding pass for `char16_t`, so every comparison against a
// bare literal walks the literal at run time: the parser alone spent more
// than a third of its time in that loop.  The length of a literal is known at
// compile time, so the suffix turns the comparison into a length check
// followed by a straight line comparison of the code units.
//
//     name == u"display"_u16
constexpr std::u16string_view operator""_u16(const char16_t* text, size_t length) {
    return std::u16string_view(text, length);
}
std::string utf8(std::u16string_view source);
std::u16string utf16(std::string_view source);
bool try_parse_color(std::u16string_view source, MathColor& color);

// KaTeX's `scriptFromCodepoint` (`src/unicodeScripts.ts`): the name of the
// script the character belongs to.  The name marks the symbol with a
// `<script>_fallback` class, and a character outside every block is a
// character KaTeX does not support.
const char* UnicodeScriptFamily(int codepoint);
// The class of a symbol whose script has no metrics, "<script>_fallback".
const char* UnicodeScriptFallbackClass(int codepoint);
} // namespace velocitex
