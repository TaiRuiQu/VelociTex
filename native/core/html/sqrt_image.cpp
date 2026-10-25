// KaTeX-compatible square root image for the HTML renderer.
//
// Port of the square root image construction from KaTeX 0.19.0
// `src/delimiter.ts` (`makeSqrtImage`).
//
// The module is named after the image it builds rather than after SVG: the
// legacy `native/core/svg.cpp` writes the SVG path of the .NET endpoint, and
// MSVC derives the object file name from the source file name, so two
// translation units called `svg.cpp` would overwrite each other in the
// object directory of the Windows build.
#include "sqrt_image.hpp"

#include <algorithm>
#include <cmath>

#include "../generated/html_sqrt_paths.hpp"
#include "build_common.hpp"
#include "jsnum.hpp"

namespace velocitex::html {
namespace {

// KaTeX's `vbPad`: padding above the surd, measured inside the viewBox.
constexpr double kVbPad = 80.0;
// KaTeX's `emPad`: padding, in ems, measured in the document.
constexpr double kEmPad = 0.08;
// KaTeX's `hLinePad`: padding above a sqrt vinculum.
constexpr double kHLinePad = 80.0;
// KaTeX's `sizeToMaxHeight`.
constexpr double kSizeToMaxHeight[5] = {0.0, 1.2, 1.8, 2.4, 3.0};

enum class DelimiterType { Small, Large, Tall };

struct Delimiter {
    DelimiterType Type{DelimiterType::Small};
    int Size{0};
    Style DelimiterStyle{};
};

const Delimiter kStackLargeDelimiterSequence[] = {
    {DelimiterType::Small, 0, Style(6, 3, true)},   // SCRIPTSCRIPT
    {DelimiterType::Small, 0, Style(4, 2, false)},  // SCRIPT
    {DelimiterType::Small, 0, Style(2, 1, false)},  // TEXT
    {DelimiterType::Large, 1, Style(0, 0, false)},
    {DelimiterType::Large, 2, Style(0, 0, false)},
    {DelimiterType::Large, 3, Style(0, 0, false)},
    {DelimiterType::Large, 4, Style(0, 0, false)},
    {DelimiterType::Tall, 0, Style(0, 0, false)},
};

FontId DelimiterFont(const Delimiter& delimiter) {
    if (delimiter.Type == DelimiterType::Large) {
        // The name of the font of a large delimiter used to be built with a
        // `std::to_string`, which is a heap allocation for every delimiter of
        // every formula.
        switch (delimiter.Size) {
            case 1: return FontId::Size1Regular;
            case 2: return FontId::Size2Regular;
            case 3: return FontId::Size3Regular;
            case 4: return FontId::Size4Regular;
            default: return FontId::Unknown;
        }
    }
    return FontId::MainRegular;
}

// KaTeX's `traverseSequence`.
Delimiter TraverseSequence(double height, const HtmlOptions& options) {
    const int start = std::min(2, 3 - options.CurrentStyle.Size);
    const int count = static_cast<int>(sizeof(kStackLargeDelimiterSequence) / sizeof(Delimiter));
    for (int i = start; i < count; ++i) {
        const Delimiter& delimiter = kStackLargeDelimiterSequence[i];
        if (delimiter.Type == DelimiterType::Tall) {
            break;
        }
        const CharMetrics* metrics =
            GetCharacterMetrics(u"√", DelimiterFont(delimiter), Mode::Math);
        if (metrics == nullptr) {
            continue;
        }
        double height_depth = metrics->Height + metrics->Depth;
        if (delimiter.Type == DelimiterType::Small) {
            const HtmlOptions new_options = options.HavingBaseStyle(delimiter.DelimiterStyle);
            height_depth *= new_options.SizeMultiplier;
        }
        if (height_depth > height) {
            return delimiter;
        }
    }
    return kStackLargeDelimiterSequence[count - 1];
}

// KaTeX's `sqrtPath`.
std::string SqrtPath(const std::string& size, double extra_viniculum, double view_box_height) {
    const double scaled = 1000.0 * extra_viniculum;
    if (same_text(size, "sqrtMain")) {
        return generated::sqrtMain(scaled, kHLinePad);
    }
    if (same_text(size, "sqrtSize1")) {
        return generated::sqrtSize1(scaled, kHLinePad);
    }
    if (same_text(size, "sqrtSize2")) {
        return generated::sqrtSize2(scaled, kHLinePad);
    }
    if (same_text(size, "sqrtSize3")) {
        return generated::sqrtSize3(scaled, kHLinePad);
    }
    if (same_text(size, "sqrtSize4")) {
        return generated::sqrtSize4(scaled, kHLinePad);
    }
    return generated::sqrtTall(scaled, kHLinePad, view_box_height);
}

// KaTeX's `sqrtSvg`.
NodePtr<HtmlSpan> MakeSqrtSvg(const std::string& sqrt_name, double height, double view_box_height,
                                      double extra_viniculum, const HtmlOptions& options) {
    auto svg = MakeNode<HtmlSvg>();
    svg->SetAttribute("width", "400em");
    svg->SetAttributeEm("height", height);
    svg->SetAttribute("viewBox", "0 0 400000 " + JsNumberToString(view_box_height));
    svg->SetAttribute("preserveAspectRatio", "xMinYMin slice");
    svg->Children().push_back(MakeNode<HtmlPath>(SqrtPath(sqrt_name, extra_viniculum, view_box_height)));

    NodeVector children;
    children.push_back(std::move(svg));
    return MakeSpan({"hide-tail"}, std::move(children), options);
}

}  // namespace

SqrtImage MakeSqrtImage(double height, const HtmlOptions& options) {
    // Remove the effect of size changes such as \Huge.
    const HtmlOptions new_options = options.HavingBaseSizing();

    const Delimiter delimiter = TraverseSequence(height * new_options.SizeMultiplier, new_options);

    double size_multiplier = new_options.SizeMultiplier;
    // The standard sqrt SVGs have a 0.04em thick vinculum.
    const double extra_viniculum =
        std::max(0.0, options.MinRuleThickness - options.Metrics().SqrtRuleThickness);

    NodePtr<HtmlSpan> span;
    double span_height = 0.0;
    double tex_height = 0.0;
    double view_box_height = 0.0;
    double advance_width = 0.0;

    if (delimiter.Type == DelimiterType::Small) {
        view_box_height = 1000.0 + 1000.0 * extra_viniculum + kVbPad;
        if (height < 1.0) {
            size_multiplier = 1.0;
        } else if (height < 1.4) {
            size_multiplier = 0.7;
        }
        span_height = (1.0 + extra_viniculum + kEmPad) / size_multiplier;
        tex_height = (1.00 + extra_viniculum) / size_multiplier;
        span = MakeSqrtSvg("sqrtMain", span_height, view_box_height, extra_viniculum, options);
        span->Style.Set("min-width", "0.853em");
        advance_width = 0.833 / size_multiplier;
    } else if (delimiter.Type == DelimiterType::Large) {
        view_box_height = (1000.0 + kVbPad) * kSizeToMaxHeight[delimiter.Size];
        tex_height = (kSizeToMaxHeight[delimiter.Size] + extra_viniculum) / size_multiplier;
        span_height = (kSizeToMaxHeight[delimiter.Size] + extra_viniculum + kEmPad) / size_multiplier;
        span = MakeSqrtSvg("sqrtSize" + std::to_string(delimiter.Size), span_height, view_box_height,
                           extra_viniculum, options);
        span->Style.Set("min-width", "1.02em");
        advance_width = 1.0 / size_multiplier;
    } else {
        span_height = height + extra_viniculum + kEmPad;
        tex_height = height + extra_viniculum;
        view_box_height = std::floor(1000.0 * height + extra_viniculum) + kVbPad;
        span = MakeSqrtSvg("sqrtTall", span_height, view_box_height, extra_viniculum, options);
        span->Style.Set("min-width", "0.742em");
        advance_width = 1.056;
    }

    span->Height = tex_height;
    span->Style.SetEm("height", span_height);

    SqrtImage image;
    image.Span = std::move(span);
    image.AdvanceWidth = advance_width;
    image.RuleWidth = (options.Metrics().SqrtRuleThickness + extra_viniculum) * size_multiplier;
    return image;
}

}  // namespace velocitex::html
