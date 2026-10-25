// KaTeX-compatible HTML building helpers.
//
// Port of KaTeX 0.19.0 `src/buildCommon.ts`.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "domtree.hpp"
#include "options.hpp"

namespace velocitex::html {

// Information about a KaTeX symbol table entry.
struct SymbolInfo {
    int CodePoint{-1};       // replacement code point
    bool IsMain{true};       // "main" or "ams" font
    std::u16string_view Group;  // "mathord", "textord", "rel", ...
    bool IsMathOrd{false};
};

// Looks up a symbol in KaTeX's symbol table for the given mode.
const SymbolInfo* LookupSymbol(std::u16string_view name, Mode mode);

// KaTeX's `makeSymbol`.
NodePtr<HtmlSymbol> MakeSymbol(std::u16string_view value, FontId font, Mode mode,
                                       const HtmlOptions& options, ClassList&& classes = {});
// KaTeX's `mathsym`.  The symbol's own font decides whether it is rendered in
// Main-Regular or in AMS-Regular; the overload without `symbol_font` looks the
// font up in KaTeX's symbol table by name.
NodePtr<HtmlSymbol> MathSym(std::u16string_view value, Mode mode, const HtmlOptions& options,
                                     ClassList&& classes = {});
NodePtr<HtmlSymbol> MathSym(std::u16string_view value, Mode mode, const HtmlOptions& options,
                                     ClassList&& classes, MathFont symbol_font);

// Description of an "ord" (a single character with a mathord/textord role).
struct OrdSpec {
    std::u16string_view Text;
    Mode TextModeValue{Mode::Math};
    bool IsMathOrd{true};
    // The font that KaTeX's symbol table records for the character.  Only the
    // "textord" branch of `makeOrd` uses it.
    MathFont SymbolFont{MathFont::Default};
};
// KaTeX's `makeOrd`.
HtmlNodePtr MakeOrd(const OrdSpec& spec, const HtmlOptions& options);

// KaTeX's `tryCombineChars`.
NodeVector TryCombineChars(NodeVector chars);

// KaTeX's `makeSpan` / `makeSvgSpan`.
NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children);
// KaTeX's `makeSpan` with options: adds the "mtight" class and the current
// colour, exactly like KaTeX's `initNode`.
NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children,
                                   const HtmlOptions& options);
NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children,
                                   NodePtr<CssStyle> style);
// KaTeX's `makeLineSpan`.
NodePtr<HtmlSpan> MakeLineSpan(std::string_view class_name, const HtmlOptions& options,
                                        double thickness = 0.0);
// KaTeX's `makeAnchor`.
NodePtr<HtmlAnchor> MakeAnchor(std::string href, ClassList&& classes,
                                        NodeVector children);
// KaTeX's `makeFragment`.
NodePtr<HtmlFragment> MakeFragment(NodeVector children);
// KaTeX's `sizeElementFromChildren`: computes the height, the depth and the
// maximum font size of a node from its children.
void SizeElementFromChildren(HtmlNode& element);
// KaTeX's `wrapFragment`.
HtmlNodePtr WrapFragment(HtmlNodePtr group, const HtmlOptions& options);
// KaTeX's `makeGlue`.
NodePtr<HtmlSpan> MakeGlue(double size, const HtmlOptions& options);
// KaTeX's `calculateSize`.
double CalculateSize(double number, TexUnit unit, const HtmlOptions& options);

// Vertical list construction, mirroring KaTeX's `makeVList`.
struct VListElement {
    HtmlNodePtr Elem;
    bool IsKern{false};
    double KernSize{0.0};
    double Shift{0.0};
    // The margins are lengths in `em`.  They are kept as numbers and are
    // written in KaTeX's `makeEm` form when the declaration reaches the
    // markup, so a margin never needs a string of its own.
    std::optional<double> MarginLeft;
    std::optional<double> MarginRight;
    // The wrapper classes are the classes of the span that wraps the element
    // in the list.  They are a class list, so that a name is never copied
    // into the arena.
    ClassList WrapperClasses;
    NodePtr<CssStyle> WrapperStyle;
};

// The elements of a vlist.  They live in the arena of the nodes, so a vlist
// does not ask the heap for its storage.
using VListElements = std::vector<VListElement, ArenaAllocator<VListElement>>;

enum class VListPositionType { IndividualShift, Top, Bottom, Shift, FirstBaseline };

struct VListParams {
    VListPositionType PositionType{VListPositionType::FirstBaseline};
    double PositionData{0.0};
    VListElements Elements;        // top/bottom/shift/firstBaseline
    VListElements ShiftedElements;  // individualShift
};

// KaTeX's `makeVList`.
NodePtr<HtmlSpan> MakeVList(VListParams params, const HtmlOptions& options);

}  // namespace velocitex::html
