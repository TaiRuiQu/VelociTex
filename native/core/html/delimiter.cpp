// KaTeX-compatible stretchy delimiters.
//
// Port of KaTeX 0.19.0 `src/delimiter.ts` plus the SVG geometry helpers
// `innerPath` and `tallDelim` from `src/svgGeometry.ts`.  The generated markup
// has to match KaTeX's byte for byte, so the SVG path templates below are
// KaTeX's original strings with the height substituted at every occurrence.
#include "delimiter.hpp"

#include <algorithm>
#include <cmath>
#include <string>

#include "build_common.hpp"
#include "jsnum.hpp"

namespace velocitex::html {
namespace {

// JavaScript's `Math.round`: halfway cases round towards positive infinity,
// unlike C++'s `std::round`, which rounds them away from zero.
int JsRound(double value) { return static_cast<int>(std::floor(value + 0.5)); }

// KaTeX's `lapInEms`: the overlap between the pieces of a stacked delimiter.
constexpr double kLapInEms = 0.008;

// KaTeX's `sizeToMaxHeight`.
constexpr double kSizeToMaxHeight[5] = {0.0, 1.2, 1.8, 2.4, 3.0};

// KaTeX's `makeSizedDelim` writes the class "size" + size and renders the
// delimiter in the font "Size" + size + "-Regular".  The sizes of a delimiter
// run from one to four, so the names are entries of a table: a class list
// holds them as views, and a view of a temporary string would dangle.
constexpr const char* kDelimSizeClassNames[] = {"size1", "size2", "size3", "size4"};
constexpr FontId kDelimSizeFonts[] = {FontId::Size1Regular, FontId::Size2Regular,
                                      FontId::Size3Regular, FontId::Size4Regular};

std::string_view DelimSizeClassName(int size) {
    if (size >= 1 && size <= 4) {
        return kDelimSizeClassNames[size - 1];
    }
    return CopyToNodeArena("size" + std::to_string(size));
}

FontId DelimSizeFont(int size) {
    if (size >= 1 && size <= 4) {
        return kDelimSizeFonts[size - 1];
    }
    // A size outside the table has no font of its own: KaTeX asks for
    // "Size5-Regular" and the table of the metrics has no such font.
    return FontId::Unknown;
}

// KaTeX's `Delimiter` type.
enum class DelimiterType { Small, Large, Stack };

struct Delimiter {
    DelimiterType Type{DelimiterType::Small};
    int Size{0};
    Style DelimiterStyle{};
};

// KaTeX's `stackNeverDelimiterSequence`.
const Delimiter kStackNeverSequence[] = {
    {DelimiterType::Small, 0, Style(6, 3, false)},  // SCRIPTSCRIPT
    {DelimiterType::Small, 0, Style(4, 2, false)},  // SCRIPT
    {DelimiterType::Small, 0, Style(2, 1, false)},  // TEXT
    {DelimiterType::Large, 1, Style(0, 0, false)},
    {DelimiterType::Large, 2, Style(0, 0, false)},
    {DelimiterType::Large, 3, Style(0, 0, false)},
    {DelimiterType::Large, 4, Style(0, 0, false)},
};

// KaTeX's `stackAlwaysDelimiterSequence`.
const Delimiter kStackAlwaysSequence[] = {
    {DelimiterType::Small, 0, Style(6, 3, false)},  // SCRIPTSCRIPT
    {DelimiterType::Small, 0, Style(4, 2, false)},  // SCRIPT
    {DelimiterType::Small, 0, Style(2, 1, false)},  // TEXT
    {DelimiterType::Stack, 0, Style(0, 0, false)},
};

// KaTeX's `stackLargeDelimiterSequence`.
const Delimiter kStackLargeSequence[] = {
    {DelimiterType::Small, 0, Style(6, 3, false)},  // SCRIPTSCRIPT
    {DelimiterType::Small, 0, Style(4, 2, false)},  // SCRIPT
    {DelimiterType::Small, 0, Style(2, 1, false)},  // TEXT
    {DelimiterType::Large, 1, Style(0, 0, false)},
    {DelimiterType::Large, 2, Style(0, 0, false)},
    {DelimiterType::Large, 3, Style(0, 0, false)},
    {DelimiterType::Large, 4, Style(0, 0, false)},
    {DelimiterType::Stack, 0, Style(0, 0, false)},
};

bool Contains(const std::u16string_view* set, size_t count, std::u16string_view value) {
    for (size_t i = 0; i < count; ++i) {
        if (set[i] == value) {
            return true;
        }
    }
    return false;
}

// KaTeX's `stackLargeDelimiters`: delimiters that stack when they get large.
bool IsStackLargeDelimiter(std::u16string_view delim) {
    static const std::u16string_view kSet[] = {
        u"(",       u"\\lparen", u")",       u"\\rparen", u"[",       u"\\lbrack",
        u"]",       u"\\rbrack", u"\\{",     u"\\lbrace", u"\\}",     u"\\rbrace",
        u"\\lfloor", u"\\rfloor", u"\u230a", u"\u230b",   u"\\lceil", u"\\rceil",
        u"\u2308",  u"\u2309",   u"\\surd",
    };
    return Contains(kSet, sizeof(kSet) / sizeof(kSet[0]), delim);
}

// KaTeX's `stackAlwaysDelimiters`: delimiters that always stack.
bool IsStackAlwaysDelimiter(std::u16string_view delim) {
    static const std::u16string_view kSet[] = {
        u"\\uparrow", u"\\downarrow", u"\\updownarrow", u"\\Uparrow", u"\\Downarrow",
        u"\\Updownarrow", u"|", u"\\|", u"\\vert", u"\\Vert", u"\\lvert", u"\\rvert",
        u"\\lVert", u"\\rVert", u"\\lgroup", u"\\rgroup", u"\u27ee", u"\u27ef",
        u"\\lmoustache", u"\\rmoustache", u"\u23b0", u"\u23b1",
    };
    return Contains(kSet, sizeof(kSet) / sizeof(kSet[0]), delim);
}

// KaTeX's `stackNeverDelimiters`: delimiters that never stack.
bool IsStackNeverDelimiter(std::u16string_view delim) {
    static const std::u16string_view kSet[] = {
        u"<", u">", u"\\langle", u"\\rangle", u"/", u"\\backslash", u"\\lt", u"\\gt",
    };
    return Contains(kSet, sizeof(kSet) / sizeof(kSet[0]), delim);
}

// KaTeX's `verts` and `doubleVerts`.
bool IsVert(std::u16string_view delim) {
    return delim == u"|"_u16 || delim == u"\\lvert"_u16 || delim == u"\\rvert"_u16 || delim == u"\\vert"_u16;
}

bool IsDoubleVert(std::u16string_view delim) {
    return delim == u"\\|"_u16 || delim == u"\\lVert"_u16 || delim == u"\\rVert"_u16 || delim == u"\\Vert"_u16;
}

// KaTeX's `getMetrics`: the metrics of a symbol after the symbol table's
// replacement, in the given font.
const CharMetrics* GetMetrics(std::u16string_view symbol, FontId font, Mode mode) {
    std::u16string_view replaced = symbol;
    char16_t replacement = 0;
    if (const SymbolInfo* info = LookupSymbol(symbol, Mode::Math)) {
        if (info->CodePoint >= 0) {
            replacement = static_cast<char16_t>(info->CodePoint);
            replaced = std::u16string_view(&replacement, 1);
        }
    }
    return GetCharacterMetrics(replaced, font, mode);
}

// The sum of a symbol's height and depth, as used by `makeStackedDelim` and
// `traverseSequence`.  KaTeX throws for symbols without metrics; the port
// treats them as empty instead.
double HeightDepth(const CharMetrics* metrics) {
    return metrics == nullptr ? 0.0 : metrics->Height + metrics->Depth;
}

// KaTeX's `delimTypeToFont`.  The names of the four size fonts are a table,
// so the font of a delimiter never needs a string of its own.
FontId DelimiterFont(const Delimiter& delimiter) {
    static constexpr FontId kSizeFonts[] = {FontId::Size1Regular, FontId::Size2Regular,
                                             FontId::Size3Regular, FontId::Size4Regular};
    switch (delimiter.Type) {
        case DelimiterType::Small: return FontId::MainRegular;
        case DelimiterType::Large:
            return delimiter.Size >= 1 && delimiter.Size <= 4 ? kSizeFonts[delimiter.Size - 1]
                                                            : FontId::Size1Regular;
        case DelimiterType::Stack: return FontId::Size4Regular;
    }
    return FontId::MainRegular;
}

// KaTeX's `styleWrap`: puts a delimiter span in a given style and adds the
// appropriate height, depth and maxFontSize.
NodePtr<HtmlSpan> StyleWrap(HtmlNodePtr delim, const Style& to_style, const HtmlOptions& options,
                                    ClassList&& classes) {
    const HtmlOptions new_options = options.HavingBaseStyle(to_style);
    ClassList all_classes = std::move(classes);
    new_options.AddSizingClasses(all_classes, options);
    NodeVector children;
    children.push_back(std::move(delim));
    auto span = MakeSpan(std::move(all_classes), std::move(children), options);
    const double delim_size_multiplier = new_options.SizeMultiplier / options.SizeMultiplier;
    span->Height *= delim_size_multiplier;
    span->Depth *= delim_size_multiplier;
    span->MaxFontSize = new_options.SizeMultiplier;
    return span;
}

// KaTeX's `centerSpan`: centres a delimiter around the axis.
void CenterSpan(HtmlSpan& span, const HtmlOptions& options, const Style& style) {
    const HtmlOptions new_options = options.HavingBaseStyle(style);
    const double shift =
        (1.0 - options.SizeMultiplier / new_options.SizeMultiplier) * options.Metrics().AxisHeight;
    span.AddClass("delimcenter");
    span.Style.SetEm("top", shift);
    span.Height -= shift;
    span.Depth += shift;
}

// KaTeX's `makeSmallDelim`: a delimiter from the Main-Regular font, restyled
// to text, script or scriptscript style.
NodePtr<HtmlSpan> MakeSmallDelim(std::u16string_view delim, const Style& style, bool center,
                                         const HtmlOptions& options, Mode mode,
                                         ClassList&& classes) {
    auto text = MakeSymbol(delim, FontId::MainRegular, mode, options);
    auto span = StyleWrap(std::move(text), style, options, std::move(classes));
    if (center) {
        CenterSpan(*span, options, style);
    }
    return span;
}

// KaTeX's `makeLargeDelim`: a delimiter from the Size1 to Size4 fonts, always
// rendered in textstyle.
NodePtr<HtmlSpan> MakeLargeDelim(std::u16string_view delim, int size, bool center,
                                         const HtmlOptions& options, Mode mode,
                                         ClassList&& classes) {
    auto inner = MakeSymbol(delim, DelimSizeFont(size), mode, options);
    NodeVector children;
    children.push_back(std::move(inner));
    auto delimsizing = MakeSpan({"delimsizing", DelimSizeClassName(size)}, std::move(children), options);
    auto span = StyleWrap(std::move(delimsizing), Style(2, 1, false), options, std::move(classes));
    if (center) {
        CenterSpan(*span, options, Style(2, 1, false));
    }
    return span;
}

// KaTeX's `makeGlyphSpan`: a span with a font glyph, used for the stacking
// pieces of a delimiter.
VListElement MakeGlyphSpan(std::u16string_view symbol, FontId font, Mode mode,
                            const HtmlOptions& options) {
    // Apply the correct CSS class to choose the right font.
    const std::string_view size_class = font == FontId::Size1Regular ? "delim-size1" : "delim-size4";
    NodeVector symbol_children;
    symbol_children.push_back(MakeSymbol(symbol, font, mode, options));
    NodeVector corner_children;
    corner_children.push_back(MakeSpan({}, std::move(symbol_children)));
    auto corner = MakeSpan({"delimsizinginner", size_class}, std::move(corner_children));
    VListElement element;
    element.Elem = std::move(corner);
    return element;
}

// KaTeX's `makeInner`: the inline SVG for the inner part of a tall stacked
// delimiter.
VListElement MakeInner(char16_t ch, double height, const HtmlOptions& options) {
    // The width comes from the Size4 font, falling back to the Size1 font.
    const std::u16string_view character(&ch, 1);
    const CharMetrics* size4 = GetCharacterMetrics(character, FontId::Size4Regular, Mode::Math);
    double width = 0.0;
    if (size4 != nullptr) {
        width = size4->Width;
    } else {
        const CharMetrics* size1 = GetCharacterMetrics(character, FontId::Size1Regular, Mode::Math);
        if (size1 != nullptr) {
            width = size1->Width;
        }
    }
    const int scaled_height = JsRound(1000.0 * height);
    auto path = MakeNode<HtmlPath>(InnerPath(ch, scaled_height));
    auto svg = MakeNode<HtmlSvg>();
    svg->SetAttributeEm("width", width);
    svg->SetAttributeEm("height", height);
    // Override the CSS rule `.katex svg { width: 100% }`.
    svg->SetAttribute("style", "width:" + MakeEm(width));
    svg->SetAttribute("viewBox", "0 0 " + JsNumberToString(1000.0 * width) + " " +
                                      std::to_string(scaled_height));
    svg->SetAttribute("preserveAspectRatio", "xMinYMin");
    svg->Children().push_back(std::move(path));

    NodeVector children;
    children.push_back(std::move(svg));
    auto span = MakeSpan({}, std::move(children), options);
    span->Height = height;
    span->Style.SetEm("height", height);
    span->Style.SetEm("width", width);
    VListElement element;
    element.Elem = std::move(span);
    return element;
}

// KaTeX's `makeStackedDelim`: a delimiter built from smaller pieces stacked
// on top of one another.
NodePtr<HtmlSpan> MakeStackedDelim(std::u16string_view delim, double height_total, bool center,
                                           const HtmlOptions& options, Mode mode,
                                           ClassList&& classes) {
    // There are four parts, the top, an optional middle, a repeated part, and
    // a bottom.
    // The parts of the delimiter are views.  The names are the literal names
    // of the symbol table, so a part never needs a string of its own, and the
    // comparison of a delimiter with a name becomes a memcmp instead of a
    // call into the string class.
    std::u16string_view top = delim;
    std::u16string_view middle;
    bool has_middle = false;
    std::u16string_view repeat = delim;
    std::u16string_view bottom = delim;
    const char* svg_label = nullptr;
    double view_box_width = 0.0;
    // Also keep track of what font the delimiters are in.
    FontId font = FontId::Size1Regular;

    // We set the parts and font based on the symbol.  Note that we use
    // '\u23d0' instead of '|' and '\u2016' instead of '\\|' for the repeats of
    // the arrows.
    if (delim == u"\\uparrow"_u16) {
        repeat = bottom = u"\u23d0";
    } else if (delim == u"\\Uparrow"_u16) {
        repeat = bottom = u"\u2016";
    } else if (delim == u"\\downarrow"_u16) {
        top = repeat = u"\u23d0";
    } else if (delim == u"\\Downarrow"_u16) {
        top = repeat = u"\u2016";
    } else if (delim == u"\\updownarrow"_u16) {
        top = u"\\uparrow";
        repeat = u"\u23d0";
        bottom = u"\\downarrow";
    } else if (delim == u"\\Updownarrow"_u16) {
        top = u"\\Uparrow";
        repeat = u"\u2016";
        bottom = u"\\Downarrow";
    } else if (IsVert(delim)) {
        repeat = u"\u2223";
        svg_label = "vert";
        view_box_width = 333.0;
    } else if (IsDoubleVert(delim)) {
        repeat = u"\u2225";
        svg_label = "doublevert";
        view_box_width = 556.0;
    } else if (delim == u"["_u16 || delim == u"\\lbrack"_u16) {
        top = u"\u23a1";
        repeat = u"\u23a2";
        bottom = u"\u23a3";
        font = FontId::Size4Regular;
        svg_label = "lbrack";
        view_box_width = 667.0;
    } else if (delim == u"]"_u16 || delim == u"\\rbrack"_u16) {
        top = u"\u23a4";
        repeat = u"\u23a5";
        bottom = u"\u23a6";
        font = FontId::Size4Regular;
        svg_label = "rbrack";
        view_box_width = 667.0;
    } else if (delim == u"\\lfloor"_u16 || delim == u"\u230a"_u16) {
        repeat = top = u"\u23a2";
        bottom = u"\u23a3";
        font = FontId::Size4Regular;
        svg_label = "lfloor";
        view_box_width = 667.0;
    } else if (delim == u"\\lceil"_u16 || delim == u"\u2308"_u16) {
        top = u"\u23a1";
        repeat = bottom = u"\u23a2";
        font = FontId::Size4Regular;
        svg_label = "lceil";
        view_box_width = 667.0;
    } else if (delim == u"\\rfloor"_u16 || delim == u"\u230b"_u16) {
        repeat = top = u"\u23a5";
        bottom = u"\u23a6";
        font = FontId::Size4Regular;
        svg_label = "rfloor";
        view_box_width = 667.0;
    } else if (delim == u"\\rceil"_u16 || delim == u"\u2309"_u16) {
        top = u"\u23a4";
        repeat = bottom = u"\u23a5";
        font = FontId::Size4Regular;
        svg_label = "rceil";
        view_box_width = 667.0;
    } else if (delim == u"("_u16 || delim == u"\\lparen"_u16) {
        top = u"\u239b";
        repeat = u"\u239c";
        bottom = u"\u239d";
        font = FontId::Size4Regular;
        svg_label = "lparen";
        view_box_width = 875.0;
    } else if (delim == u")"_u16 || delim == u"\\rparen"_u16) {
        top = u"\u239e";
        repeat = u"\u239f";
        bottom = u"\u23a0";
        font = FontId::Size4Regular;
        svg_label = "rparen";
        view_box_width = 875.0;
    } else if (delim == u"\\{"_u16 || delim == u"\\lbrace"_u16) {
        top = u"\u23a7";
        middle = u"\u23a8";
        has_middle = true;
        bottom = u"\u23a9";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    } else if (delim == u"\\}"_u16 || delim == u"\\rbrace"_u16) {
        top = u"\u23ab";
        middle = u"\u23ac";
        has_middle = true;
        bottom = u"\u23ad";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    } else if (delim == u"\\lgroup"_u16 || delim == u"\u27ee"_u16) {
        top = u"\u23a7";
        bottom = u"\u23a9";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    } else if (delim == u"\\rgroup"_u16 || delim == u"\u27ef"_u16) {
        top = u"\u23ab";
        bottom = u"\u23ad";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    } else if (delim == u"\\lmoustache"_u16 || delim == u"\u23b0"_u16) {
        top = u"\u23a7";
        bottom = u"\u23ad";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    } else if (delim == u"\\rmoustache"_u16 || delim == u"\u23b1"_u16) {
        top = u"\u23ab";
        bottom = u"\u23a9";
        repeat = u"\u23aa";
        font = FontId::Size4Regular;
    }

    // Get the metrics of the four sections.
    const double top_height_total = HeightDepth(GetMetrics(top, font, mode));
    const double repeat_height_total = HeightDepth(GetMetrics(repeat, font, mode));
    const double bottom_height_total = HeightDepth(GetMetrics(bottom, font, mode));
    double middle_height_total = 0.0;
    double middle_factor = 1.0;
    if (has_middle) {
        middle_height_total = HeightDepth(GetMetrics(middle, font, mode));
        middle_factor = 2.0;  // repeat symmetrically above and below middle
    }

    // Calculate the minimal height that the delimiter can have.  It is at
    // least the size of the top, bottom, and optional middle combined.
    const double min_height = top_height_total + bottom_height_total + middle_height_total;

    // Compute the number of copies of the repeat symbol we will need.
    const double repeat_count = std::max(
        0.0, std::ceil((height_total - min_height) / (middle_factor * repeat_height_total)));

    // Compute the total height of the delimiter including all the symbols.
    const double real_height_total = min_height + repeat_count * middle_factor * repeat_height_total;

    // The center of the delimiter is placed at the center of the axis.  Note
    // that in this context, "center" means that the delimiter should be
    // centered around the axis in the current style, while normally it is
    // centered around the axis in textstyle.
    double axis_height = options.Metrics().AxisHeight;
    if (center) {
        axis_height *= options.SizeMultiplier;
    }
    // Calculate the depth.
    const double depth = real_height_total / 2.0 - axis_height;

    // Now, we start building the pieces that will go into the vlist.
    VListParams params;
    params.PositionType = VListPositionType::Bottom;
    params.PositionData = depth;

    if (svg_label != nullptr) {
        // Instead of stacking glyphs, create a single SVG.  This evades
        // browser problems with imprecise positioning of spans.
        const double mid_height = real_height_total - top_height_total - bottom_height_total;
        const int view_box_height = JsRound(real_height_total * 1000.0);
        auto path = MakeNode<HtmlPath>(TallDelim(svg_label, JsRound(mid_height * 1000.0)));
        const std::string width = MakeEm(view_box_width / 1000.0);
        const std::string height = MakeEm(view_box_height / 1000.0);
        auto svg = MakeNode<HtmlSvg>();
        svg->SetAttribute("width", width);
        svg->SetAttribute("height", height);
        svg->SetAttribute("viewBox", "0 0 " + JsNumberToString(view_box_width) + " " +
                                          std::to_string(view_box_height));
        svg->Children().push_back(std::move(path));

        NodeVector children;
        children.push_back(std::move(svg));
        auto wrapper = MakeSpan({}, std::move(children), options);
        wrapper->Height = view_box_height / 1000.0;
        wrapper->Style.Set("width", width);
        wrapper->Style.Set("height", height);
        VListElement element;
        element.Elem = std::move(wrapper);
        params.Elements.push_back(std::move(element));
    } else {
        // Stack glyphs.  Start by adding the bottom symbol.
        params.Elements.push_back(MakeGlyphSpan(bottom, font, mode, options));
        VListElement lap;
        lap.IsKern = true;
        lap.KernSize = -kLapInEms;
        params.Elements.push_back(std::move(lap));

        if (!has_middle) {
            // The middle section will be an SVG.  Make it an extra 0.016em
            // tall.  We'll overlap by 0.008em at top and bottom.
            const double inner_height =
                real_height_total - top_height_total - bottom_height_total + 2.0 * kLapInEms;
            params.Elements.push_back(MakeInner(repeat[0], inner_height, options));
        } else {
            // When there is a middle bit, we need the middle part and two
            // repeated sections.
            const double inner_height = (real_height_total - top_height_total - bottom_height_total -
                                         middle_height_total) /
                                            2.0 +
                                        2.0 * kLapInEms;
            params.Elements.push_back(MakeInner(repeat[0], inner_height, options));
            // Now insert the middle of the brace.
            VListElement middle_lap;
            middle_lap.IsKern = true;
            middle_lap.KernSize = -kLapInEms;
            params.Elements.push_back(std::move(middle_lap));
            params.Elements.push_back(MakeGlyphSpan(middle, font, mode, options));
            VListElement middle_lap_two;
            middle_lap_two.IsKern = true;
            middle_lap_two.KernSize = -kLapInEms;
            params.Elements.push_back(std::move(middle_lap_two));
            params.Elements.push_back(MakeInner(repeat[0], inner_height, options));
        }

        // Add the top symbol.
        VListElement top_lap;
        top_lap.IsKern = true;
        top_lap.KernSize = -kLapInEms;
        params.Elements.push_back(std::move(top_lap));
        params.Elements.push_back(MakeGlyphSpan(top, font, mode, options));
    }

    // Finally, build the vlist.
    const HtmlOptions new_options = options.HavingBaseStyle(Style(2, 1, false));
    auto inner = MakeVList(std::move(params), new_options);
    NodeVector children;
    children.push_back(std::move(inner));
    auto delimsizing = MakeSpan({"delimsizing", "mult"}, std::move(children), new_options);
    return StyleWrap(std::move(delimsizing), Style(2, 1, false), options, std::move(classes));
}

// KaTeX's `traverseSequence`: decides which kind of delimiter to use for a
// given height.
Delimiter TraverseSequence(std::u16string_view delim, double height, const Delimiter* sequence, size_t count,
                           const HtmlOptions& options) {
    // In smaller sizes (which correspond to larger numbers in style.size) we
    // start earlier in the sequence.  Thus, scriptscript starts at index
    // 3-3=0, script starts at index 3-2=1, text starts at 3-1=2, and display
    // starts at min(2,3-0)=2.
    const int start = std::min(2, 3 - options.CurrentStyle.Size);
    for (int i = start; i < static_cast<int>(count); ++i) {
        const Delimiter& delim_type = sequence[i];
        if (delim_type.Type == DelimiterType::Stack) {
            // This is always the last delimiter, so we just break the loop.
            break;
        }
        const CharMetrics* metrics = GetMetrics(delim, DelimiterFont(delim_type), Mode::Math);
        double height_depth = HeightDepth(metrics);
        // Small delimiters are scaled down versions of the same font, so we
        // account for the style change size.
        if (delim_type.Type == DelimiterType::Small) {
            const HtmlOptions new_options = options.HavingBaseStyle(delim_type.DelimiterStyle);
            height_depth *= new_options.SizeMultiplier;
        }
        // Check if the delimiter at this size works for the given height.
        if (height_depth > height) {
            return delim_type;
        }
    }
    // If we reached the end of the sequence, return the last sequence element.
    return sequence[count - 1];
}

}  // namespace

// KaTeX's `makeCustomSizedDelim`: a delimiter of a given height and depth,
// with optional centering.
NodePtr<HtmlSpan> MakeCustomSizedDelim(std::u16string_view delim_in, double height, bool center,
                                               const HtmlOptions& options, Mode mode,
                                               ClassList&& classes) {
    std::u16string_view delim = delim_in;
    // < and > turn into \langle and \rangle in delimiters.
    if (delim == u"<"_u16 || delim == u"\\lt"_u16 || delim == u"\u27e8"_u16) {
        delim = u"\\langle";
    } else if (delim == u">"_u16 || delim == u"\\gt"_u16 || delim == u"\u27e9"_u16) {
        delim = u"\\rangle";
    }

    // Decide what sequence to use.
    const Delimiter* sequence = kStackAlwaysSequence;
    size_t count = sizeof(kStackAlwaysSequence) / sizeof(kStackAlwaysSequence[0]);
    if (IsStackNeverDelimiter(delim)) {
        sequence = kStackNeverSequence;
        count = sizeof(kStackNeverSequence) / sizeof(kStackNeverSequence[0]);
    } else if (IsStackLargeDelimiter(delim)) {
        sequence = kStackLargeSequence;
        count = sizeof(kStackLargeSequence) / sizeof(kStackLargeSequence[0]);
    }

    // Look through the sequence.
    const Delimiter delim_type = TraverseSequence(delim, height, sequence, count, options);

    // Get the delimiter from font glyphs.  Depending on the sequence element
    // we decided on, call the appropriate function.
    if (delim_type.Type == DelimiterType::Small) {
        return MakeSmallDelim(delim, delim_type.DelimiterStyle, center, options, mode, std::move(classes));
    }
    if (delim_type.Type == DelimiterType::Large) {
        return MakeLargeDelim(delim, delim_type.Size, center, options, mode, std::move(classes));
    }
    return MakeStackedDelim(delim, height, center, options, mode, std::move(classes));
}

// KaTeX's `makeSizedDelim`: a delimiter of a specific size, where `size` is
// 1, 2, 3 or 4.
NodePtr<HtmlSpan> MakeSizedDelim(std::u16string_view delim_in, int size, const HtmlOptions& options,
                                          Mode mode, ClassList&& classes) {
    std::u16string_view delim = delim_in;
    // < and > turn into \langle and \rangle in delimiters.
    if (delim == u"<"_u16 || delim == u"\\lt"_u16 || delim == u"\u27e8"_u16) {
        delim = u"\\langle";
    } else if (delim == u">"_u16 || delim == u"\\gt"_u16 || delim == u"\u27e9"_u16) {
        delim = u"\\rangle";
    }

    // Sized delimiters are never centered.
    if (IsStackLargeDelimiter(delim) || IsStackNeverDelimiter(delim)) {
        return MakeLargeDelim(delim, size, false, options, mode, std::move(classes));
    }
    if (IsStackAlwaysDelimiter(delim)) {
        return MakeStackedDelim(delim, kSizeToMaxHeight[size], false, options, mode, std::move(classes));
    }
    // An illegal delimiter renders as nothing.
    return MakeSpan({}, {});
}

// KaTeX's `makeLeftRightDelim`: the delimiter used by `\left` and `\right`.
NodePtr<HtmlSpan> MakeLeftRightDelim(std::u16string_view delim, double height, double depth,
                                              const HtmlOptions& options, Mode mode,
                                              ClassList&& classes) {
    // We always center \left/\right delimiters, so the axis is always shifted.
    const double axis_height = options.Metrics().AxisHeight * options.SizeMultiplier;

    // Taken from the TeX source, tex.web, function make_left_right.
    const double delimiter_factor = 901.0;
    const double delimiter_extend = 5.0 / options.Metrics().PtPerEm;

    const double max_dist_from_axis = std::max(height - axis_height, depth + axis_height);

    const double total_height = std::max(max_dist_from_axis / 500.0 * delimiter_factor,
                                         2.0 * max_dist_from_axis - delimiter_extend);

    // Finally, we defer to `makeCustomSizedDelim` with our calculated total
    // height.
    return MakeCustomSizedDelim(delim, total_height, true, options, mode, std::move(classes));
}

std::u16string DelimiterName(int code_point) {
    switch (code_point) {
        case '(': return u"(";
        case ')': return u")";
        case '[': return u"[";
        case ']': return u"]";
        case '{': return u"\\{";
        case '}': return u"\\}";
        case '|': return u"|";
        case '/': return u"/";
        case '\\': return u"\\backslash";
        case 0x2016: return u"\\|";
        case 0x2191: return u"\\uparrow";
        case 0x2193: return u"\\downarrow";
        case 0x2195: return u"\\updownarrow";
        case 0x21D1: return u"\\Uparrow";
        case 0x21D3: return u"\\Downarrow";
        case 0x21D5: return u"\\Updownarrow";
        case 0x2308: return u"\\lceil";
        case 0x2309: return u"\\rceil";
        case 0x230A: return u"\\lfloor";
        case 0x230B: return u"\\rfloor";
        case 0x23B0: return u"\\lmoustache";
        case 0x23B1: return u"\\rmoustache";
        case 0x27E8: return u"\\langle";
        case 0x27E9: return u"\\rangle";
        case 0x27EE: return u"\\lgroup";
        case 0x27EF: return u"\\rgroup";
        default: return {};
    }
}

namespace {

// KaTeX's `doubleBrushStroke`: doubles the path so that the inner part of a
// stacked delimiter looks like a glyph.
std::string DoubleBrushStroke(const std::string& path) { return path + " " + path; }

}  // namespace

// KaTeX's `innerPath` (`src/svgGeometry.ts`).
std::string InnerPath(char16_t name, int height) {
    switch (name) {
        case 0x239C:
            return DoubleBrushStroke("M291 0 H417 V" + std::to_string(height) + " H291z");
        case 0x2223:
            return DoubleBrushStroke("M145 0 H188 V" + std::to_string(height) + " H145z");
        case 0x2225:
            return DoubleBrushStroke("M145 0 H188 V" + std::to_string(height) + " H145z") +
                   DoubleBrushStroke("M367 0 H410 V" + std::to_string(height) + " H367z");
        case 0x239F:
            return DoubleBrushStroke("M457 0 H583 V" + std::to_string(height) + " H457z");
        case 0x23A2:
            return DoubleBrushStroke("M319 0 H403 V" + std::to_string(height) + " H319z");
        case 0x23A5:
            return DoubleBrushStroke("M263 0 H347 V" + std::to_string(height) + " H263z");
        case 0x23AA:
            return DoubleBrushStroke("M384 0 H504 V" + std::to_string(height) + " H384z");
        case 0x23D0:
            return DoubleBrushStroke("M312 0 H355 V" + std::to_string(height) + " H312z");
        case 0x2016:
            return DoubleBrushStroke("M257 0 H300 V" + std::to_string(height) + " H257z") +
                   DoubleBrushStroke("M478 0 H521 V" + std::to_string(height) + " H478z");
        default:
            return {};
    }
}

// KaTeX's `tallDelim` (`src/svgGeometry.ts`).
std::string TallDelim(std::string_view label, int mid_height) {
    if (same_text(label, "lbrack")) {
        return "M403 1759 V84 H666 V0 H319 V1759 v" + std::to_string(mid_height) + " v1759 v84 h347 v-84\n"
               "H403z M403 1759 V0 H319 V1759 v" + std::to_string(mid_height) + " v1759 v84 h84z";
    }
    if (same_text(label, "rbrack")) {
        return "M347 1759 V0 H0 V84 H263 V1759 v" + std::to_string(mid_height) + " v1759 H0 v84 H347z\n"
               "M347 1759 V0 H263 V1759 v" + std::to_string(mid_height) + " v1759 h84z";
    }
    if (same_text(label, "vert")) {
        return "M145 15 v585 v" + std::to_string(mid_height) + " v585 c2.667,10,9.667,15,21,15\n"
               "c10,0,16.667,-5,20,-15 v-585 v" + std::to_string(-mid_height) + " v-585 c-2.667,-10,-9.667,-15,-21,-15\n"
               "c-10,0,-16.667,5,-20,15z M188 15 H145 v585 v" + std::to_string(mid_height) + " v585 h43z";
    }
    if (same_text(label, "doublevert")) {
        return "M145 15 v585 v" + std::to_string(mid_height) + " v585 c2.667,10,9.667,15,21,15\n"
               "c10,0,16.667,-5,20,-15 v-585 v" + std::to_string(-mid_height) + " v-585 c-2.667,-10,-9.667,-15,-21,-15\n"
               "c-10,0,-16.667,5,-20,15z M188 15 H145 v585 v" + std::to_string(mid_height) + " v585 h43z\n"
               "M367 15 v585 v" + std::to_string(mid_height) + " v585 c2.667,10,9.667,15,21,15\n"
               "c10,0,16.667,-5,20,-15 v-585 v" + std::to_string(-mid_height) + " v-585 c-2.667,-10,-9.667,-15,-21,-15\n"
               "c-10,0,-16.667,5,-20,15z M410 15 H367 v585 v" + std::to_string(mid_height) + " v585 h43z";
    }
    if (same_text(label, "lfloor")) {
        return "M319 602 V0 H403 V602 v" + std::to_string(mid_height) + " v1715 h263 v84 H319z\n"
               "MM319 602 V0 H403 V602 v" + std::to_string(mid_height) + " v1715 H319z";
    }
    if (same_text(label, "rfloor")) {
        return "M319 602 V0 H403 V602 v" + std::to_string(mid_height) + " v1799 H0 v-84 H319z\n"
               "MM319 602 V0 H403 V602 v" + std::to_string(mid_height) + " v1715 H319z";
    }
    if (same_text(label, "lceil")) {
        return "M403 1759 V84 H666 V0 H319 V1759 v" + std::to_string(mid_height) + " v602 h84z\n"
               "M403 1759 V0 H319 V1759 v" + std::to_string(mid_height) + " v602 h84z";
    }
    if (same_text(label, "rceil")) {
        return "M347 1759 V0 H0 V84 H263 V1759 v" + std::to_string(mid_height) + " v602 h84z\n"
               "M347 1759 V0 h-84 V1759 v" + std::to_string(mid_height) + " v602 h84z";
    }
    if (same_text(label, "lparen")) {
        return "M863,9c0,-2,-2,-5,-6,-9c0,0,-17,0,-17,0c-12.7,0,-19.3,0.3,-20,1\n"
               "c-5.3,5.3,-10.3,11,-15,17c-242.7,294.7,-395.3,682,-458,1162c-21.3,163.3,-33.3,349,\n"
               "-36,557 l0," + std::to_string(mid_height + 84) + "c0.2,6,0,26,0,60c2,159.3,10,310.7,24,454c53.3,528,210,\n"
               "949.7,470,1265c4.7,6,9.7,11.7,15,17c0.7,0.7,7,1,19,1c0,0,18,0,18,0c4,-4,6,-7,6,-9\n"
               "c0,-2.7,-3.3,-8.7,-10,-18c-135.3,-192.7,-235.5,-414.3,-300.5,-665c-65,-250.7,-102.5,\n"
               "-544.7,-112.5,-882c-2,-104,-3,-167,-3,-189\n"
               "l0,-" + std::to_string(mid_height + 92) + "c0,-162.7,5.7,-314,17,-454c20.7,-272,63.7,-513,129,-723c65.3,\n"
               "-210,155.3,-396.3,270,-559c6.7,-9.3,10,-15.3,10,-18z";
    }
    if (same_text(label, "rparen")) {
        return "M76,0c-16.7,0,-25,3,-25,9c0,2,2,6.3,6,13c21.3,28.7,42.3,60.3,\n"
               "63,95c96.7,156.7,172.8,332.5,228.5,527.5c55.7,195,92.8,416.5,111.5,664.5\n"
               "c11.3,139.3,17,290.7,17,454c0,28,1.7,43,3.3,45l0," + std::to_string(mid_height + 9) + "\n"
               "c-3,4,-3.3,16.7,-3.3,38c0,162,-5.7,313.7,-17,455c-18.7,248,-55.8,469.3,-111.5,664\n"
               "c-55.7,194.7,-131.8,370.3,-228.5,527c-20.7,34.7,-41.7,66.3,-63,95c-2,3.3,-4,7,-6,11\n"
               "c0,7.3,5.7,11,17,11c0,0,11,0,11,0c9.3,0,14.3,-0.3,15,-1c5.3,-5.3,10.3,-11,15,-17\n"
               "c242.7,-294.7,395.3,-681.7,458,-1161c21.3,-164.7,33.3,-350.7,36,-558\n"
               "l0,-" + std::to_string(mid_height + 144) + "c-2,-159.3,-10,-310.7,-24,-454c-53.3,-528,-210,-949.7,\n"
               "-470,-1265c-4.7,-6,-9.7,-11.7,-15,-17c-0.7,-0.7,-6.7,-1,-18,-1z";
    }
    // KaTeX throws here; an unknown label renders as no path at all.
    return {};
}

}  // namespace velocitex::html
