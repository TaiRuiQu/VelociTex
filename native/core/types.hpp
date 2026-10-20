#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
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
std::string utf8(std::u16string_view source);
std::u16string utf16(std::string_view source);
bool try_parse_color(std::u16string_view source, MathColor& color);
} // namespace velocitex
