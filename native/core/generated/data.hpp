// Generated from locked KaTeX 0.19.0 data. Do not edit.
#pragma once
#include "../types.hpp"
#include <array>
namespace velocitex {
struct FontDescriptor {std::u16string_view Name,ResourceFileName; int UnitsPerEm,GlyphStart,GlyphCount,MetricStart,MetricCount;};
struct FontGlyph {int CodePoint,GlyphId,AdvanceUnits,LeftSideBearingUnits;};
struct TexGlyphMetric {int CodePoint{}; double Depth{},Height{},ItalicCorrection{},Skew{},Width{};};
struct GlyphInk {int Left,Bottom,Right,Top;};
struct SymbolDefinition {std::u16string_view Name;int CodePoint;MathAtom Atom;MathFont Font;bool IsMathOrd,TextMode;std::u16string_view Group;};
struct MacroString {std::u16string_view Name,Body;};
struct CommandDefinition {std::u16string_view Name;bool AllowedInText,AllowedInMath,AllowedInArgument,Primitive;};
struct FallbackMetric {int Font,CodePoint;bool TextMode;TexGlyphMetric Metric;};
struct FontData {
static const std::span<const FontDescriptor> Fonts;
static const std::span<const FontGlyph> Glyphs;
static const std::span<const TexGlyphMetric> TexMetrics;
};
struct GlyphInkData {
static const std::span<const GlyphInk> Bounds;
static const std::span<const double> FontDescents;
};
struct SymbolData {
static const std::span<const SymbolDefinition> Symbols;
static const std::span<const MacroString> Macros;
static const std::span<const std::u16string_view> KnownCommands;
};
struct CommandData {
static const std::span<const CommandDefinition> Definitions;
};
struct FallbackMetricData {static bool Find(int font,int cp,bool text,TexGlyphMetric& metric);};
struct MathConstants {
static double slant(int index) {return index==0?0.25:index==1?0.25:0.25;}
static double space(int index) {return index==0?0:index==1?0:0;}
static double stretch(int index) {return index==0?0:index==1?0:0;}
static double shrink(int index) {return index==0?0:index==1?0:0;}
static double xHeight(int index) {return index==0?0.431:index==1?0.431:0.431;}
static double quad(int index) {return index==0?1:index==1?1.171:1.472;}
static double extraSpace(int index) {return index==0?0:index==1?0:0;}
static double num1(int index) {return index==0?0.677:index==1?0.732:0.925;}
static double num2(int index) {return index==0?0.394:index==1?0.384:0.387;}
static double num3(int index) {return index==0?0.444:index==1?0.471:0.504;}
static double denom1(int index) {return index==0?0.686:index==1?0.752:1.025;}
static double denom2(int index) {return index==0?0.345:index==1?0.344:0.532;}
static double sup1(int index) {return index==0?0.413:index==1?0.503:0.504;}
static double sup2(int index) {return index==0?0.363:index==1?0.431:0.404;}
static double sup3(int index) {return index==0?0.289:index==1?0.286:0.294;}
static double sub1(int index) {return index==0?0.15:index==1?0.143:0.2;}
static double sub2(int index) {return index==0?0.247:index==1?0.286:0.4;}
static double supDrop(int index) {return index==0?0.386:index==1?0.353:0.494;}
static double subDrop(int index) {return index==0?0.05:index==1?0.071:0.1;}
static double delim1(int index) {return index==0?2.39:index==1?1.7:1.98;}
static double delim2(int index) {return index==0?1.01:index==1?1.157:1.42;}
static double axisHeight(int index) {return index==0?0.25:index==1?0.25:0.25;}
static double defaultRuleThickness(int index) {return index==0?0.04:index==1?0.049:0.049;}
static double bigOpSpacing1(int index) {return index==0?0.111:index==1?0.111:0.111;}
static double bigOpSpacing2(int index) {return index==0?0.166:index==1?0.166:0.166;}
static double bigOpSpacing3(int index) {return index==0?0.2:index==1?0.2:0.2;}
static double bigOpSpacing4(int index) {return index==0?0.6:index==1?0.611:0.611;}
static double bigOpSpacing5(int index) {return index==0?0.1:index==1?0.143:0.143;}
static double sqrtRuleThickness(int index) {return index==0?0.04:index==1?0.04:0.04;}
static double ptPerEm(int index) {return index==0?10:index==1?10:10;}
static double doubleRuleSep(int index) {return index==0?0.2:index==1?0.2:0.2;}
static double arrayRuleWidth(int index) {return index==0?0.04:index==1?0.04:0.04;}
static double fboxsep(int index) {return index==0?0.3:index==1?0.3:0.3;}
static double fboxrule(int index) {return index==0?0.04:index==1?0.04:0.04;}
};
struct UnicodeData {static std::optional<std::u16string> Decompose(int scalar);static std::optional<std::u16string> Accent(int scalar,bool text);static std::optional<std::u16string> Script(int scalar,bool& subscript);};
namespace generated {bool has_main_metric(int cp);}
}
