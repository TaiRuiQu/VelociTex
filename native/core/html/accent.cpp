// KaTeX-compatible rendering of accents, horizontal braces and arrows.
//
// Port of KaTeX 0.19.0 `src/functions/accent.ts`,
// `src/functions/horizBrace.ts` and `src/functions/arrow.ts`.
//
// The semantic tree folds every one of these commands into a single
// `NodeKind::Accent` node, so the module dispatches on the command name
// stored in `SemanticNode::Text` and reproduces the KaTeX module that owns
// the command.  The KaTeX modules `src/functions/overline.ts`,
// `src/functions/underline.ts` and `src/functions/accentunder.ts` are ported
// here as well, because the C++ parser routes their commands to the same
// node kind.
#include "accent.hpp"

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../generated/html_paths.hpp"
#include "build_common.hpp"
#include "jsnum.hpp"
#include "options.hpp"

namespace velocitex::html {
namespace {

// ---------------------------------------------------------------------------
// KaTeX's SVG images
// ---------------------------------------------------------------------------

// One entry of KaTeX's `katexImagesData` (`src/stretchy.ts`).  `Label` is the
// accent label without its leading backslash, exactly like KaTeX's
// `group.label.slice(1)`.  `Align` is only used for the single-path images;
// the two- and three-path images use the fixed alignment lists below.
//
// The paths are stored in a fixed size array instead of a `std::vector` so
// that the whole table is constant initialized.  A table with a runtime
// initializer would stay empty in the WebAssembly build until the module's
// constructors run, which the standalone loader does not do.
struct StretchyImage {
    std::u16string_view Label;
    std::array<std::u16string_view, 3> Paths{};
    double MinWidth{0.0};
    int ViewBoxHeight{0};
    std::string_view Align;

    size_t PathCount() const {
        size_t count = 0;
        for (const std::u16string_view path : Paths) {
            if (path.empty()) break;
            ++count;
        }
        return count;
    }
};

// The alignments of the two- and three-path images (`src/stretchy.ts`).
constexpr std::string_view kAlignsTwo[2] = {"xMinYMin", "xMaxYMin"};
constexpr std::string_view kAlignsThree[3] = {"xMinYMin", "xMidYMin", "xMaxYMin"};

const StretchyImage kStretchyImages[] = {
    {u"overrightarrow", {u"rightarrow"}, 0.888, 522, "xMaxYMin"},
    {u"overleftarrow", {u"leftarrow"}, 0.888, 522, "xMinYMin"},
    {u"underrightarrow", {u"rightarrow"}, 0.888, 522, "xMaxYMin"},
    {u"underleftarrow", {u"leftarrow"}, 0.888, 522, "xMinYMin"},
    {u"xrightarrow", {u"rightarrow"}, 1.469, 522, "xMaxYMin"},
    {u"\\cdrightarrow", {u"rightarrow"}, 3.0, 522, "xMaxYMin"},
    {u"xleftarrow", {u"leftarrow"}, 1.469, 522, "xMinYMin"},
    {u"\\cdleftarrow", {u"leftarrow"}, 3.0, 522, "xMinYMin"},
    {u"Overrightarrow", {u"doublerightarrow"}, 0.888, 560, "xMaxYMin"},
    {u"xRightarrow", {u"doublerightarrow"}, 1.526, 560, "xMaxYMin"},
    {u"xLeftarrow", {u"doubleleftarrow"}, 1.526, 560, "xMinYMin"},
    {u"overleftharpoon", {u"leftharpoon"}, 0.888, 522, "xMinYMin"},
    {u"xleftharpoonup", {u"leftharpoon"}, 0.888, 522, "xMinYMin"},
    {u"xleftharpoondown", {u"leftharpoondown"}, 0.888, 522, "xMinYMin"},
    {u"overrightharpoon", {u"rightharpoon"}, 0.888, 522, "xMaxYMin"},
    {u"xrightharpoonup", {u"rightharpoon"}, 0.888, 522, "xMaxYMin"},
    {u"xrightharpoondown", {u"rightharpoondown"}, 0.888, 522, "xMaxYMin"},
    {u"xlongequal", {u"longequal"}, 0.888, 334, "xMinYMin"},
    {u"\\cdlongequal", {u"longequal"}, 3.0, 334, "xMinYMin"},
    {u"xtwoheadleftarrow", {u"twoheadleftarrow"}, 0.888, 334, "xMinYMin"},
    {u"xtwoheadrightarrow", {u"twoheadrightarrow"}, 0.888, 334, "xMaxYMin"},
    {u"overleftrightarrow", {u"leftarrow", u"rightarrow"}, 0.888, 522, ""},
    {u"overbrace", {u"leftbrace", u"midbrace", u"rightbrace"}, 1.6, 548, ""},
    {u"underbrace", {u"leftbraceunder", u"midbraceunder", u"rightbraceunder"}, 1.6, 548, ""},
    {u"underleftrightarrow", {u"leftarrow", u"rightarrow"}, 0.888, 522, ""},
    {u"xleftrightarrow", {u"leftarrow", u"rightarrow"}, 1.75, 522, ""},
    {u"xLeftrightarrow", {u"doubleleftarrow", u"doublerightarrow"}, 1.75, 560, ""},
    {u"xrightleftharpoons", {u"leftharpoondownplus", u"rightharpoonplus"}, 1.75, 716, ""},
    {u"xleftrightharpoons", {u"leftharpoonplus", u"rightharpoondownplus"}, 1.75, 716, ""},
    {u"xhookleftarrow", {u"leftarrow", u"righthook"}, 1.08, 522, ""},
    {u"xhookrightarrow", {u"lefthook", u"rightarrow"}, 1.08, 522, ""},
    {u"overlinesegment", {u"leftlinesegment", u"rightlinesegment"}, 0.888, 522, ""},
    {u"underlinesegment", {u"leftlinesegment", u"rightlinesegment"}, 0.888, 522, ""},
    {u"overbracket", {u"leftbracketover", u"rightbracketover"}, 1.6, 440, ""},
    {u"underbracket", {u"leftbracketunder", u"rightbracketunder"}, 1.6, 410, ""},
    {u"overgroup", {u"leftgroup", u"rightgroup"}, 0.888, 342, ""},
    {u"undergroup", {u"leftgroupunder", u"rightgroupunder"}, 0.888, 342, ""},
    {u"xmapsto", {u"leftmapsto", u"rightarrow"}, 1.5, 522, ""},
    {u"xtofrom", {u"leftToFrom", u"rightToFrom"}, 1.75, 528, ""},
    {u"xrightleftarrows", {u"baraboveleftarrow", u"rightarrowabovebar"}, 1.75, 901, ""},
    {u"xrightequilibrium", {u"baraboveshortleftharpoon", u"rightharpoonaboveshortbar"}, 1.75, 716, ""},
    {u"xleftequilibrium", {u"shortbaraboveleftharpoon", u"shortrightharpoonabovebar"}, 1.75, 716, ""},
};

const StretchyImage* FindStretchyImage(std::u16string_view label) {
    for (const StretchyImage& image : kStretchyImages) {
        if (image.Label == label) {
            return &image;
        }
    }
    return nullptr;
}

// KaTeX's `wideAccentLabels` (`src/stretchy.ts`).
bool IsWideAccentLabel(std::u16string_view label) {
    return label == u"widehat"_u16 || label == u"widecheck"_u16 || label == u"widetilde"_u16 ||
           label == u"utilde"_u16;
}

// KaTeX's SVG path data, generated from `src/svgGeometry.ts`.
std::string PathData(std::u16string_view name) {
    for (int i = 0; i < generated::HtmlPathData::PathCount; ++i) {
        const generated::HtmlPathData::Path& path = generated::HtmlPathData::Paths[i];
        if (path.Name == name) {
            return std::string(path.Data);
        }
    }
    return {};
}

// The result of KaTeX's `stretchySvg`.
struct StretchySpan {
    NodePtr<HtmlSpan> Span;
    double MinWidth{0.0};
    double Height{0.0};
};

// KaTeX's `stretchySvg` (`src/stretchy.ts`).
//
// `base_index` is the semantic index of the group's base, or -1 when the
// group has none (the extensible arrows).
StretchySpan MakeStretchySvg(const ModuleContext& context, int base_index, std::u16string_view label,
                             const HtmlOptions& options) {
    StretchySpan result;

    if (IsWideAccentLabel(label)) {
        // There are four SVG images available for each wide accent.  Choose a
        // taller image when there are more characters.
        int num_chars = 1;
        if (base_index >= 0 && context.Node(base_index).Kind == NodeKind::Row) {
            num_chars = static_cast<int>(context.Children(base_index).size());
        }
        int view_box_width = 0;
        int view_box_height = 0;
        double height = 0.0;
        std::u16string path_name;
        const bool is_hat = label == u"widehat"_u16 || label == u"widecheck"_u16;
        if (num_chars > 5) {
            if (is_hat) {
                view_box_height = 420;
                view_box_width = 2364;
                height = 0.42;
                path_name = std::u16string(label) + u"4";
            } else {
                view_box_height = 312;
                view_box_width = 2340;
                height = 0.34;
                path_name = u"tilde4";
            }
        } else {
            static constexpr int kImageIndex[6] = {1, 1, 2, 2, 3, 3};
            static constexpr int kHatWidth[4] = {0, 1062, 2364, 2364};
            static constexpr int kHatHeight[4] = {0, 239, 300, 360};
            static constexpr double kHatEm[4] = {0.0, 0.24, 0.3, 0.3};
            static constexpr int kTildeWidth[4] = {0, 600, 1033, 2339};
            static constexpr int kTildeHeight[4] = {0, 260, 286, 306};
            static constexpr double kTildeEm[4] = {0.0, 0.26, 0.286, 0.3};
            const int image_index = kImageIndex[num_chars];
            if (is_hat) {
                view_box_width = kHatWidth[image_index];
                view_box_height = kHatHeight[image_index];
                height = kHatEm[image_index];
                path_name = std::u16string(label) + static_cast<char16_t>(u'0' + image_index);
            } else {
                view_box_width = kTildeWidth[image_index];
                view_box_height = kTildeHeight[image_index];
                height = kTildeEm[image_index];
                path_name = std::u16string(u"tilde"_u16) + static_cast<char16_t>(u'0' + image_index);
            }
        }
        auto svg = MakeNode<HtmlSvg>();
        svg->SetAttribute("width", "100%");
        svg->SetAttributeEm("height", height);
        svg->SetAttribute("viewBox",
                          "0 0 " + std::to_string(view_box_width) + " " + std::to_string(view_box_height));
        svg->SetAttribute("preserveAspectRatio", "none");
        svg->Children().push_back(MakeNode<HtmlPath>(PathData(path_name)));
        NodeVector children;
        children.push_back(std::move(svg));
        result.Span = MakeSpan({}, std::move(children), options);
        result.Height = height;
    } else {
        const StretchyImage* image = FindStretchyImage(label);
        if (image == nullptr) {
            // KaTeX throws here; an empty span keeps the renderer going.
            result.Span = MakeSpan({}, {}, options);
            return result;
        }
        const double height = image->ViewBoxHeight / 1000.0;
        const int view_box_width = 400000;
        const size_t num_children = image->PathCount();
        // The classes of the pieces of the image.  KaTeX's `svgSpan` gives
        // every piece of the image its own class; the names are literals, so
        // that a class list can hold them as views.
        static constexpr const char* kSingleWidthClasses[] = {"hide-tail"};
        static constexpr const char* kArrowWidthClasses[] = {"halfarrow-left", "halfarrow-right"};
        static constexpr const char* kBraceWidthClasses[] = {"brace-left", "brace-center", "brace-right"};
        const char* const* width_classes = kBraceWidthClasses;
        std::vector<std::string_view> aligns;
        if (num_children == 1) {
            width_classes = kSingleWidthClasses;
            aligns = {image->Align};
        } else if (num_children == 2) {
            width_classes = kArrowWidthClasses;
            aligns = {kAlignsTwo[0], kAlignsTwo[1]};
        } else {
            aligns = {kAlignsThree[0], kAlignsThree[1], kAlignsThree[2]};
        }
        NodeVector spans;
        for (size_t i = 0; i < num_children; ++i) {
            auto svg = MakeNode<HtmlSvg>();
            svg->SetAttribute("width", "400em");
            svg->SetAttributeEm("height", height);
            svg->SetAttribute("viewBox", "0 0 " + std::to_string(view_box_width) + " " +
                                             std::to_string(image->ViewBoxHeight));
            svg->SetAttribute("preserveAspectRatio", std::string(aligns[i]) + " slice");
            svg->Children().push_back(MakeNode<HtmlPath>(PathData(image->Paths[i])));
            NodeVector children;
            children.push_back(std::move(svg));
            auto span = MakeSpan({width_classes[i]}, std::move(children), options);
            if (num_children == 1) {
                result.Span = std::move(span);
                result.MinWidth = image->MinWidth;
                result.Height = height;
                break;
            }
            span->Style.SetEm("height", height);
            spans.push_back(std::move(span));
        }
        if (result.Span == nullptr) {
            result.Span = MakeSpan({"katex-stretchy"}, std::move(spans), options);
        }
        result.MinWidth = image->MinWidth;
        result.Height = height;
    }

    // Note that we are returning span.depth = 0.  Any adjustments relative to
    // the baseline must be done by the caller.
    result.Span->Height = result.Height;
    result.Span->Style.SetEm("height", result.Height);
    if (result.MinWidth > 0.0) {
        result.Span->Style.SetEm("min-width", result.MinWidth);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Accent classification
// ---------------------------------------------------------------------------

// KaTeX's `NON_STRETCHY_ACCENT_REGEX` (`src/functions/accent.ts`).  The
// regular expression is not anchored, so it matches the exact command names
// below and nothing else.
bool IsNonStretchyAccent(std::u16string_view label) {
    return label == u"\\acute"_u16 || label == u"\\grave"_u16 || label == u"\\ddot"_u16 || label == u"\\tilde"_u16 ||
           label == u"\\bar"_u16 || label == u"\\breve"_u16 || label == u"\\check"_u16 || label == u"\\hat"_u16 ||
           label == u"\\vec"_u16 || label == u"\\dot"_u16 || label == u"\\mathring"_u16;
}

// The text-mode accents of `src/functions/accent.ts`.  They are never
// stretchy and they always shift with the base.
bool IsTextAccent(std::u16string_view label) {
    return label == u"\\'"_u16 || label == u"\\`"_u16 || label == u"\\^"_u16 || label == u"\\~"_u16 || label == u"\\="_u16 ||
           label == u"\\u"_u16 || label == u"\\."_u16 || label == u"\\\""_u16 || label == u"\\c"_u16 || label == u"\\r"_u16 ||
           label == u"\\H"_u16 || label == u"\\v"_u16 || label == u"\\textcircled"_u16;
}

// The extensible arrows of `src/functions/arrow.ts`.
bool IsExtensibleArrow(std::u16string_view label) {
    return label == u"\\xleftarrow"_u16 || label == u"\\xrightarrow"_u16 || label == u"\\xLeftarrow"_u16 ||
           label == u"\\xRightarrow"_u16 || label == u"\\xleftrightarrow"_u16 || label == u"\\xLeftrightarrow"_u16 ||
           label == u"\\xhookleftarrow"_u16 || label == u"\\xhookrightarrow"_u16 || label == u"\\xmapsto"_u16 ||
           label == u"\\xleftharpoonup"_u16 || label == u"\\xleftharpoondown"_u16 || label == u"\\xrightharpoonup"_u16 ||
           label == u"\\xrightharpoondown"_u16 || label == u"\\xrightleftharpoons"_u16 ||
           label == u"\\xleftrightharpoons"_u16 || label == u"\\xtwoheadleftarrow"_u16 ||
           label == u"\\xtwoheadrightarrow"_u16 || label == u"\\xlongequal"_u16 || label == u"\\xtofrom"_u16 ||
           label == u"\\xrightleftarrows"_u16 || label == u"\\xrightequilibrium"_u16 ||
           label == u"\\xleftequilibrium"_u16 || label == u"\\\\cdrightarrow"_u16 || label == u"\\\\cdleftarrow"_u16 ||
           label == u"\\\\cdlongequal"_u16;
}

// The horizontal braces of `src/functions/horizBrace.ts`.
bool IsHorizBrace(std::u16string_view label) {
    return label == u"\\overbrace"_u16 || label == u"\\underbrace"_u16 || label == u"\\overbracket"_u16 ||
           label == u"\\underbracket"_u16;
}

// The under accents of `src/functions/accentunder.ts`.
bool IsAccentUnder(std::u16string_view label) {
    return label == u"\\underleftarrow"_u16 || label == u"\\underrightarrow"_u16 ||
           label == u"\\underleftrightarrow"_u16 || label == u"\\undergroup"_u16 ||
           label == u"\\underlinesegment"_u16 || label == u"\\utilde"_u16;
}

// KaTeX's `isCharacterBox` (`src/utils.ts`): a group that boils down to a
// single atom.  Only such a base shifts with the accent.
bool IsCharacterBox(const ModuleContext& context, int index) {
    if (index < 0) {
        return false;
    }
    const SemanticNode& node = context.Node(index);
    switch (node.Kind) {
    case NodeKind::Symbol:
        return true;
    case NodeKind::Row:
    case NodeKind::Color: {
        const IndexVector children = context.Children(index);
        if (children.size() != 1) {
            return false;
        }
        return IsCharacterBox(context, children.front());
    }
    default:
        return false;
    }
}

// KaTeX's `getBaseSymbol` (`src/functions/accent.ts`): the symbol of a
// single-character base, used to read the skew of the base.
const HtmlSymbol* GetBaseSymbol(const HtmlNode& node) {
    if (node.Kind() == HtmlNodeKind::Symbol) {
        return &static_cast<const HtmlSymbol&>(node);
    }
    const auto& children = node.Children();
    if (children.size() == 1) {
        return GetBaseSymbol(*children.front());
    }
    return nullptr;
}

// KaTeX's `normalizeArgument` (`src/defineFunction.ts`): the math accents
// unwrap a group that holds a single element.  The text accents keep the
// group, so the distinction matters for the rendered markup.
int NormalizeBase(const ModuleContext& context, int index) {
    if (index < 0) {
        return index;
    }
    const SemanticNode& node = context.Node(index);
    if (node.Kind != NodeKind::Row) {
        return index;
    }
    const IndexVector children = context.Children(index);
    if (children.size() != 1) {
        return index;
    }
    return children.front();
}

// ---------------------------------------------------------------------------
// KaTeX's `accent` module
// ---------------------------------------------------------------------------

// KaTeX's accent builder (`src/functions/accent.ts`).
HtmlNodePtr BuildAccent(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const std::u16string_view label = text(node.Text);
    const bool is_stretchy = !IsNonStretchyAccent(label) && !IsTextAccent(label);
    const bool is_shifty = !is_stretchy || label == u"\\widehat"_u16 || label == u"\\widetilde"_u16 ||
                           label == u"\\widecheck"_u16;
    const bool is_text_accent = IsTextAccent(label);

    // The math accents unwrap a single-element group; the text accents do
    // not.
    const int base_index = is_text_accent ? node.FirstChild : NormalizeBase(context, node.FirstChild);

    // Build the base group.
    HtmlNodePtr body = context.build_group(base_index, options.HavingCrampedStyle());

    // Does the accent need to shift for the skew of a character?
    const bool must_shift = is_shifty && IsCharacterBox(context, base_index);

    // Calculate the skew of the accent.
    double skew = 0.0;
    if (must_shift) {
        const HtmlSymbol* symbol = GetBaseSymbol(*body);
        if (symbol != nullptr) {
            skew = symbol->SkewValue();
        }
    }

    const bool accent_below = label == u"\\c"_u16;

    // Calculate the amount of space between the body and the accent.
    double clearance = accent_below ? body->Height + body->Depth
                                     : std::min(body->Height, options.Metrics().XHeight);

    HtmlNodePtr accent_body;
    if (!is_stretchy) {
        HtmlNodePtr accent;
        double width = 0.0;
        if (label == u"\\vec"_u16) {
            // Before version 0.9, `\vec` used the combining font glyph
            // U+20D7.  Browsers are not consistent in how they render
            // combining characters, so KaTeX uses an SVG instead.
            accent = MakeStaticSvg(u"vec", 0.471, 0.714, options);
            width = 0.471;
        } else {
            OrdSpec spec;
            // KaTeX resolves the accent command through the symbol table
            // before the font is chosen (`lookupSymbol` in
            // `src/buildCommon.ts`), so that the accent is rendered in the
            // font of the surrounding group.
            std::u16string_view text = label;
            char16_t buffer[1];
            if (const SymbolInfo* info = LookupSymbol(label, node.TextMode ? Mode::Text : Mode::Math)) {
                if (info->CodePoint >= 0) {
                    buffer[0] = static_cast<char16_t>(info->CodePoint);
                    text = std::u16string_view(buffer, 1);
                }
            }
            spec.Text = text;
            spec.TextModeValue = node.TextMode ? Mode::Text : Mode::Math;
            spec.IsMathOrd = false;
            accent = MakeOrd(spec, options);
            // Remove the italic correction of the accent, because it only
            // serves to shift the accent over to a place we don't want.
            if (accent->Kind() == HtmlNodeKind::Symbol) {
                static_cast<HtmlSymbol&>(*accent).SetItalic(0.0);
                width = static_cast<HtmlSymbol&>(*accent).WidthValue();
            }
            if (accent_below) {
                clearance += accent->Depth;
            }
        }

        NodeVector accent_children;
        accent_children.push_back(std::move(accent));
        auto accent_span = MakeSpan({"accent-body"}, std::move(accent_children));

        // "Full" accents expand the width of the resulting symbol to be at
        // least the width of the accent, and overlap directly onto the
        // character without any vertical offset.
        const bool accent_full = label == u"\\textcircled"_u16;
        if (accent_full) {
            accent_span->AddClass("accent-full");
            clearance = body->Height;
        }

        // Shift the accent over by the skew.
        double left = skew;
        // CSS defines `.katex .katex-accent .accent-body:not(.accent-full)
        // { width: 0 }` so that the accent doesn't contribute to the bounding
        // box.  We need to shift the character by its width to compensate.
        if (!accent_full) {
            left -= width / 2.0;
        }
        accent_span->Style.SetEm("left", left);

        // `\textcircled` uses the `\bigcirc` glyph, so it needs some vertical
        // adjustment to match LaTeX.
        if (accent_full) {
            accent_span->Style.Set("top", ".2em");
        }

        VListParams params;
        params.PositionType = VListPositionType::FirstBaseline;
        VListElement body_element;
        body_element.Elem = std::move(body);
        params.Elements.push_back(std::move(body_element));
        VListElement kern;
        kern.IsKern = true;
        kern.KernSize = -clearance;
        params.Elements.push_back(std::move(kern));
        VListElement accent_element;
        accent_element.Elem = std::move(accent_span);
        params.Elements.push_back(std::move(accent_element));
        accent_body = MakeVList(std::move(params), options);
    } else {
        StretchySpan stretchy = MakeStretchySvg(context, base_index, label.substr(1), options);

        NodePtr<CssStyle> wrapper_style;
        if (skew > 0.0) {
            wrapper_style = MakeNode<CssStyle>();
            wrapper_style->Set("width", "calc(100% - " + MakeEm(2.0 * skew) + ")");
            wrapper_style->SetEm("margin-left", 2.0 * skew);
        }

        VListParams params;
        params.PositionType = VListPositionType::FirstBaseline;
        VListElement body_element;
        body_element.Elem = std::move(body);
        params.Elements.push_back(std::move(body_element));
        VListElement accent_element;
        accent_element.Elem = std::move(stretchy.Span);
        accent_element.WrapperClasses = {"svg-align"};
        accent_element.WrapperStyle = std::move(wrapper_style);
        params.Elements.push_back(std::move(accent_element));
        accent_body = MakeVList(std::move(params), options);
    }

    NodeVector children;
    children.push_back(std::move(accent_body));
    return MakeSpan({"mord", "katex-accent"}, std::move(children), options);
}

// ---------------------------------------------------------------------------
// KaTeX's `accentUnder` module
// ---------------------------------------------------------------------------

// KaTeX's under accent builder (`src/functions/accentunder.ts`).
HtmlNodePtr BuildAccentUnder(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const std::u16string_view label = text(node.Text);

    // Treat under accents much like underlines.
    HtmlNodePtr inner = context.build_group(node.FirstChild, options);

    StretchySpan accent = MakeStretchySvg(context, node.FirstChild, label.substr(1), options);
    const double kern = label == u"\\utilde"_u16 ? 0.12 : 0.0;

    VListParams params;
    params.PositionType = VListPositionType::Top;
    params.PositionData = inner->Height;
    VListElement accent_element;
    accent_element.Elem = std::move(accent.Span);
    accent_element.WrapperClasses = {"svg-align"};
    params.Elements.push_back(std::move(accent_element));
    VListElement kern_element;
    kern_element.IsKern = true;
    kern_element.KernSize = kern;
    params.Elements.push_back(std::move(kern_element));
    VListElement inner_element;
    inner_element.Elem = std::move(inner);
    params.Elements.push_back(std::move(inner_element));
    auto vlist = MakeVList(std::move(params), options);

    NodeVector children;
    children.push_back(std::move(vlist));
    return MakeSpan({"mord", "accentunder"}, std::move(children), options);
}

// ---------------------------------------------------------------------------
// KaTeX's `horizBrace` module
// ---------------------------------------------------------------------------

// KaTeX's horizontal brace builder (`src/functions/horizBrace.ts`).
//
// When a `supsub` group delegates to the brace (`htmlBuilderDelegate` in
// `src/functions/supsub.ts`), the script is passed in and wrapped in a second
// vlist, so that the equation controls the width of the brace.
HtmlNodePtr BuildHorizBrace(const ModuleContext& context, int index, const HtmlOptions& options,
                            HtmlNodePtr script) {
    const SemanticNode& node = context.Node(index);
    const std::u16string_view label = text(node.Text);
    const bool is_over = label.find(u"\\over"_u16) != std::u16string_view::npos;

    // The script is built by the caller, which passes the base options to the
    // group builder just like KaTeX does.
    HtmlNodePtr sup_sub_group = std::move(script);

    // Build the base group.
    HtmlNodePtr body = context.build_group(node.FirstChild, options.HavingBaseStyle(Style(0, 0, false)));

    // Create the stretchy element.
    StretchySpan brace = MakeStretchySvg(context, node.FirstChild, label.substr(1), options);

    // Generate the vlist, with the appropriate kerns.
    VListParams params;
    if (is_over) {
        params.PositionType = VListPositionType::FirstBaseline;
        VListElement body_element;
        body_element.Elem = std::move(body);
        params.Elements.push_back(std::move(body_element));
        VListElement kern;
        kern.IsKern = true;
        kern.KernSize = 0.1;
        params.Elements.push_back(std::move(kern));
        VListElement brace_element;
        brace_element.Elem = std::move(brace.Span);
        brace_element.WrapperClasses = {"svg-align"};
        params.Elements.push_back(std::move(brace_element));
    } else {
        params.PositionType = VListPositionType::Bottom;
        params.PositionData = body->Depth + 0.1 + brace.Span->Height;
        VListElement brace_element;
        brace_element.Elem = std::move(brace.Span);
        brace_element.WrapperClasses = {"svg-align"};
        params.Elements.push_back(std::move(brace_element));
        VListElement kern;
        kern.IsKern = true;
        kern.KernSize = 0.1;
        params.Elements.push_back(std::move(kern));
        VListElement body_element;
        body_element.Elem = std::move(body);
        params.Elements.push_back(std::move(body_element));
    }
    auto vlist = MakeVList(std::move(params), options);

    if (sup_sub_group) {
        // To write the supsub, wrap the first vlist in another vlist: they
        // cannot all go in the same vlist, because the note might be wider
        // than the equation.
        NodeVector vspan_children;
        vspan_children.push_back(std::move(vlist));
        HtmlNodePtr v_span = MakeSpan({"minner", is_over ? "mover" : "munder"}, std::move(vspan_children), options);

        VListParams outer;
        if (is_over) {
            outer.PositionType = VListPositionType::FirstBaseline;
            VListElement vspan_element;
            vspan_element.Elem = std::move(v_span);
            outer.Elements.push_back(std::move(vspan_element));
            VListElement kern;
            kern.IsKern = true;
            kern.KernSize = 0.2;
            outer.Elements.push_back(std::move(kern));
            VListElement script_element;
            script_element.Elem = std::move(sup_sub_group);
            outer.Elements.push_back(std::move(script_element));
        } else {
            outer.PositionType = VListPositionType::Bottom;
            outer.PositionData = v_span->Depth + 0.2 + sup_sub_group->Height + sup_sub_group->Depth;
            VListElement script_element;
            script_element.Elem = std::move(sup_sub_group);
            outer.Elements.push_back(std::move(script_element));
            VListElement kern;
            kern.IsKern = true;
            kern.KernSize = 0.2;
            outer.Elements.push_back(std::move(kern));
            VListElement vspan_element;
            vspan_element.Elem = std::move(v_span);
            outer.Elements.push_back(std::move(vspan_element));
        }
        vlist = MakeVList(std::move(outer), options);
    }

    NodeVector children;
    children.push_back(std::move(vlist));
    return MakeSpan({"minner", is_over ? "mover" : "munder"}, std::move(children), options);
}

// ---------------------------------------------------------------------------
// KaTeX's `overline` and `underline` modules
// ---------------------------------------------------------------------------

// KaTeX's overline builder (`src/functions/overline.ts`).
// KaTeX's function arguments are always groups (`Parser.parseGroup` returns
// an ordgroup), so an argument that holds a single element renders as a
// "mord" span.  The C++ tree stores the group as a Row node; an argument
// that was a single node has no Row, so the group is recreated here.
HtmlNodePtr BuildArgumentGroup(const ModuleContext& context, int index, const HtmlOptions& options) {
    if (index < 0) {
        return MakeSpan({}, {});
    }
    if (context.Node(index).Kind == NodeKind::Row) {
        return context.build_group(index, options);
    }
    NodeVector children;
    children.push_back(context.build_group(index, options));
    return MakeSpan({"mord"}, std::move(children), options);
}

HtmlNodePtr BuildOverline(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);

    // Build the inner group in the cramped style.
    HtmlNodePtr inner = BuildArgumentGroup(context, node.FirstChild, options.HavingCrampedStyle());

    // Create the line above the body.
    auto line = MakeLineSpan("overline-line", options);
    const double rule_thickness = options.Metrics().DefaultRuleThickness;

    VListParams params;
    params.PositionType = VListPositionType::FirstBaseline;
    VListElement inner_element;
    inner_element.Elem = std::move(inner);
    params.Elements.push_back(std::move(inner_element));
    VListElement kern_one;
    kern_one.IsKern = true;
    kern_one.KernSize = 3.0 * rule_thickness;
    params.Elements.push_back(std::move(kern_one));
    VListElement line_element;
    line_element.Elem = std::move(line);
    params.Elements.push_back(std::move(line_element));
    VListElement kern_two;
    kern_two.IsKern = true;
    kern_two.KernSize = rule_thickness;
    params.Elements.push_back(std::move(kern_two));
    auto vlist = MakeVList(std::move(params), options);

    NodeVector children;
    children.push_back(std::move(vlist));
    return MakeSpan({"mord", "katex-overline"}, std::move(children), options);
}

// KaTeX's underline builder (`src/functions/underline.ts`).
HtmlNodePtr BuildUnderline(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);

    // Build the inner group.
    HtmlNodePtr inner = BuildArgumentGroup(context, node.FirstChild, options);

    // Create the line to go below the body.
    auto line = MakeLineSpan("underline-line", options);
    const double rule_thickness = options.Metrics().DefaultRuleThickness;

    VListParams params;
    params.PositionType = VListPositionType::Top;
    params.PositionData = inner->Height;
    VListElement kern_one;
    kern_one.IsKern = true;
    kern_one.KernSize = rule_thickness;
    params.Elements.push_back(std::move(kern_one));
    VListElement line_element;
    line_element.Elem = std::move(line);
    params.Elements.push_back(std::move(line_element));
    VListElement kern_two;
    kern_two.IsKern = true;
    kern_two.KernSize = 3.0 * rule_thickness;
    params.Elements.push_back(std::move(kern_two));
    VListElement inner_element;
    inner_element.Elem = std::move(inner);
    params.Elements.push_back(std::move(inner_element));
    auto vlist = MakeVList(std::move(params), options);

    NodeVector children;
    children.push_back(std::move(vlist));
    return MakeSpan({"mord", "katex-underline"}, std::move(children), options);
}

// KaTeX's `buildGroup` with a base options object (`src/buildHTML.ts`).  When
// the size of a group differs from the size of its parent, KaTeX wraps the
// group in a sizing span.  The module API's `build_group` callback does not
// take a base options object, so the wrapper is applied here.
HtmlNodePtr BuildGroupWithBase(const ModuleContext& context, int index, const HtmlOptions& options,
                               const HtmlOptions& base_options) {
    HtmlNodePtr result = context.build_group(index, options);
    // KaTeX's `buildGroup` returns an empty span for a missing group and skips
    // the size wrapper in that case (`src/buildHTML.ts`).
    if (index < 0 || options.Size == base_options.Size) {
        return result;
    }
    NodeVector children;
    children.push_back(std::move(result));
    ClassList sizing_classes;
    options.AddSizingClasses(sizing_classes, base_options);
    auto wrapper = MakeSpan(std::move(sizing_classes), std::move(children), options);
    const double multiplier = options.SizeMultiplier / base_options.SizeMultiplier;
    wrapper->Height *= multiplier;
    wrapper->Depth *= multiplier;
    return wrapper;
}

// ---------------------------------------------------------------------------
// KaTeX's `xArrow` module
// ---------------------------------------------------------------------------

// KaTeX's extensible arrow builder (`src/functions/arrow.ts`).
HtmlNodePtr BuildArrow(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const std::u16string_view label = text(node.Text);

    // Build the argument groups in the appropriate style.
    HtmlNodePtr upper = BuildGroupWithBase(context, node.FirstChild, options.HavingStyle(options.CurrentStyle.Sup()),
                                           options);
    upper = WrapFragment(std::move(upper), options);
    // KaTeX's `xArrow` gives the two ends of the arrow the class
    // "x-arrow-pad" or "cd-arrow-pad".  The names are literals, so that a
    // class list can hold them as views.
    const std::string_view arrow_pad_class =
        label.substr(0, 2) == u"\\x"_u16 ? "x-arrow-pad" : "cd-arrow-pad";
    upper->AddClass(arrow_pad_class);

    HtmlNodePtr lower;
    if (node.SecondChild >= 0) {
        lower = BuildGroupWithBase(context, node.SecondChild, options.HavingStyle(options.CurrentStyle.Sub()),
                                   options);
        lower = WrapFragment(std::move(lower), options);
        lower->AddClass(arrow_pad_class);
    }

    StretchySpan arrow = MakeStretchySvg(context, -1, label.substr(1), options);

    // Re shift: the point we want on the math axis is at 0.5 * arrow.height.
    const double axis_height = options.Metrics().AxisHeight;
    const double arrow_shift = -axis_height + 0.5 * arrow.Height;
    // 2 mu kern.  Ref: amsmath.dtx: #7\if0#2\else\mkern#2mu\fi
    double upper_shift = -axis_height - 0.5 * arrow.Height - 0.111;
    if (upper->Depth > 0.25 || label == u"\\xleftequilibrium"_u16) {
        // Shift up if the depth encroaches.
        upper_shift -= upper->Depth;
    }

    VListParams params;
    params.PositionType = VListPositionType::IndividualShift;
    VListElement upper_element;
    upper_element.Elem = std::move(upper);
    upper_element.Shift = upper_shift;
    params.ShiftedElements.push_back(std::move(upper_element));
    VListElement arrow_element;
    arrow_element.Elem = std::move(arrow.Span);
    arrow_element.Shift = arrow_shift;
    arrow_element.WrapperClasses = {"svg-align"};
    params.ShiftedElements.push_back(std::move(arrow_element));
    if (lower != nullptr) {
        const double lower_shift = -axis_height + lower->Height + 0.5 * arrow.Height + 0.111;
        VListElement lower_element;
        lower_element.Elem = std::move(lower);
        lower_element.Shift = lower_shift;
        params.ShiftedElements.push_back(std::move(lower_element));
    }
    auto vlist = MakeVList(std::move(params), options);

    NodeVector children;
    children.push_back(std::move(vlist));
    return MakeSpan({"mrel", "x-arrow"}, std::move(children), options);
}

}  // namespace

// KaTeX's `staticSvg` (`src/buildCommon.ts`), used for `\vec` and for the
// oval that `\oiint` and `\oiiint` overlay on top of the integral.
NodePtr<HtmlSpan> MakeStaticSvg(std::u16string_view path_name, double width, double height,
                                        const HtmlOptions& options) {
    auto svg = MakeNode<HtmlSvg>();
    svg->SetAttributeEm("width", width);
    svg->SetAttributeEm("height", height);
    // Override the CSS rule `.katex svg { width: 100% }`.
    svg->SetAttribute("style", "width:" + MakeEm(width));
    svg->SetAttribute("viewBox", "0 0 " + JsNumberToString(1000.0 * width) + " " +
                                     JsNumberToString(1000.0 * height));
    svg->SetAttribute("preserveAspectRatio", "xMinYMin");
    svg->Children().push_back(MakeNode<HtmlPath>(PathData(path_name)));

    NodeVector children;
    children.push_back(std::move(svg));
    auto span = MakeSpan({"katex-overlay"}, std::move(children), options);
    span->Height = height;
    span->Style.SetEm("height", height);
    span->Style.SetEm("width", width);
    return span;
}

int NormalizeAccentBase(const ModuleContext& context, int index) {
    return NormalizeBase(context, index);
}

HtmlNodePtr BuildAccentNode(const ModuleContext& context, int index, const HtmlOptions& options) {
    const SemanticNode& node = context.Node(index);
    const std::u16string_view label = text(node.Text);
    if (IsExtensibleArrow(label)) {
        return BuildArrow(context, index, options);
    }
    if (IsHorizBrace(label)) {
        return BuildHorizBrace(context, index, options, nullptr);
    }
    if (label == u"\\overline"_u16) {
        return BuildOverline(context, index, options);
    }
    if (label == u"\\underline"_u16) {
        return BuildUnderline(context, index, options);
    }
    if (IsAccentUnder(label)) {
        return BuildAccentUnder(context, index, options);
    }
    return BuildAccent(context, index, options);
}

bool IsHorizBraceLabel(std::u16string_view label) {
    return IsHorizBrace(label);
}

HtmlNodePtr BuildHorizBraceNode(const ModuleContext& context, int index, const HtmlOptions& options,
                                HtmlNodePtr script) {
    return BuildHorizBrace(context, index, options, std::move(script));
}

}  // namespace velocitex::html
