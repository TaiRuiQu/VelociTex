// KaTeX-compatible rendering options.
//
// Port of KaTeX 0.19.0 `src/Options.ts`, `src/Style.ts` and the metric
// lookups from `src/fontMetrics.ts`.
#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "../types.hpp"

namespace velocitex::html {

class ClassList;

enum class Mode { Math, Text };

// TeX style, mirroring KaTeX's `Style` class.
struct Style {
    int Id{2};       // 0..7: D, Dc, T, Tc, S, Sc, SS, SSc
    int Size{1};     // 0 = text size, 1 = script, 2 = scriptscript
    bool Cramped{false};

    Style() = default;
    Style(int id, int size, bool cramped) : Id(id), Size(size), Cramped(cramped) {}

    Style Sup() const;
    Style Sub() const;
    Style FracNum() const;
    Style FracDen() const;
    Style Cramp() const;
    Style Text() const;
    bool IsTight() const { return Size >= 2; }
};

// Character metrics as returned by KaTeX's `getCharacterMetrics`.
struct CharMetrics {
    double Depth{0.0};
    double Height{0.0};
    double Italic{0.0};
    double Skew{0.0};
    double Width{0.0};
};

// Global (sigma/xi) metrics for a given size index.
struct GlobalMetrics {
    double Slant{}, Space{}, Stretch{}, Shrink{}, XHeight{}, Quad{}, ExtraSpace{};
    double Num1{}, Num2{}, Num3{}, Denom1{}, Denom2{};
    double Sup1{}, Sup2{}, Sup3{}, Sub1{}, Sub2{};
    double SupDrop{}, SubDrop{}, Delim1{}, Delim2{}, AxisHeight{};
    double DefaultRuleThickness{}, BigOpSpacing1{}, BigOpSpacing2{}, BigOpSpacing3{};
    double BigOpSpacing4{}, BigOpSpacing5{}, SqrtRuleThickness{}, PtPerEm{};
    double DoubleRuleSep{}, ArrayRuleWidth{}, Fboxsep{}, Fboxrule{}, CssEmPerMu{};
};

// KaTeX's `getGlobalMetrics(size)`.
// KaTeX's `getGlobalMetrics(size)`.  The table is shared, so the caller must
// not modify the result.
const GlobalMetrics& GetGlobalMetrics(int size);
// The fonts of KaTeX's font metrics.
//
// The metrics of a symbol are looked up by the index of its font, so the name
// of the font is resolved once instead of once per symbol: a formula has
// hundreds of symbols and they all use the same few fonts, and a lookup by
// name is a hash of the name on every symbol.
//
// `Unknown` stands for a font that KaTeX's table does not have.  KaTeX asks
// for such a font (the bold italic of a text font, for example), and the
// lookup returns nothing.
enum class FontId : uint8_t {
    Unknown = 0,
    AmsRegular,
    CaligraphicBold,
    CaligraphicRegular,
    FrakturBold,
    FrakturRegular,
    MainBold,
    MainBoldItalic,
    MainItalic,
    MainRegular,
    MathBoldItalic,
    MathItalic,
    SansSerifBold,
    SansSerifItalic,
    SansSerifRegular,
    ScriptRegular,
    Size1Regular,
    Size2Regular,
    Size3Regular,
    Size4Regular,
    TypewriterRegular,
};
// KaTeX's `getCharacterMetrics(character, font, mode)`.
const CharMetrics* GetCharacterMetrics(std::u16string_view character, FontId font, Mode mode);
// KaTeX's `scriptFromCodepoint`: the name of the script the character belongs
// to, used for the `<script>_fallback` class.  Returns nullptr for unknown
// scripts.
const char* ScriptFromCodepoint(int codepoint);
// The class of a symbol whose script has no metrics, "<script>_fallback".
// The name is an entry of a table, so it lives as long as the render.
const char* ScriptFallbackClass(int codepoint);
// Maps a KaTeX font name ("Main-Regular", ...) to the internal font index.
// Returns -1 when the font is unknown.
int FontIndexByName(std::string_view fontName);

// KaTeX's font command -> (MathML variant, font metrics) table.
struct FontMapEntry {
    std::string_view Variant;
    FontId Font;
    // KaTeX recognizes a monospace font by the name of its font and
    // deconstructs a ligature in it.
    bool IsTypewriter;
};
const FontMapEntry* LookupFontMap(std::string_view fontClass);
// KaTeX's `retrieveTextFontName`.  The name of the four known families and
// the four styles is a table, so the common case returns a view of a literal
// instead of building a string for every symbol.
//
// `is_typewriter` tells whether KaTeX would take the ligature path for the
// font.  A family of its own has no metrics, but KaTeX still deconstructs a
// ligature in it when the name of the font starts with "Typewriter".
FontId RetrieveTextFontId(std::string_view family, std::string_view weight, std::string_view shape,
                          bool* is_typewriter);

struct HtmlOptions {
    Style CurrentStyle{};
    // The colour, the font class and the text font of the options are views
    // and not strings: they point at a literal, at the semantic tree of the
    // render or at a string of the caller that outlives the render.  The
    // builder copies the options for every group it enters, so a copy that
    // has to move five strings is one of the costs of the tree build.
    std::string_view Color;
    bool HasColor{false};
    int Size{6};
    int TextSize{6};
    bool Phantom{false};
    std::string_view Font;        // "" or a math font class such as "mathbf"
    std::string_view FontFamily;  // text font family
    std::string_view FontWeight;
    std::string_view FontShape;
    double SizeMultiplier{1.0};
    double MaxSize{10.0};
    double MinRuleThickness{0.04};

    static constexpr int BaseSize = 6;

    HtmlOptions() { Refresh(); }

    static double SizeMultiplierFor(int size);
    static int SizeAtStyle(int size, const Style& style);

    HtmlOptions HavingStyle(const Style& style) const;
    HtmlOptions HavingCrampedStyle() const;
    HtmlOptions HavingSize(int size) const;
    HtmlOptions HavingBaseStyle(const Style& style) const;
    HtmlOptions HavingBaseSizing() const;
    HtmlOptions WithColor(std::string_view color) const;
    HtmlOptions WithPhantom() const;
    HtmlOptions WithFont(std::string_view font) const;
    HtmlOptions WithTextFontFamily(std::string_view family) const;
    HtmlOptions WithTextFontWeight(std::string_view weight) const;
    HtmlOptions WithTextFontShape(std::string_view shape) const;

    // The same classes, but written into a class list.  The names are built in
    // a buffer of the caller, so that a size change does not allocate a vector
    // of names.
    void AddSizingClasses(ClassList& classes, const HtmlOptions& old_options) const;
    void AddBaseSizingClasses(ClassList& classes) const;
    // The names of the sizing classes.  KaTeX writes "reset-size6" and
    // "size6"; the names are the entries of a table, so that a class list can
    // hold them without a buffer of its own.
    static std::string_view SizeClassName(int size);
    static std::string_view ResetSizeClassName(int size);

    // The colour of the options, or nothing when the options carry none.
    std::optional<std::string_view> GetColor() const;
    const GlobalMetrics& Metrics() const { return *metrics_; }

private:
    void Refresh();
    const GlobalMetrics* metrics_{nullptr};
};

}  // namespace velocitex::html
