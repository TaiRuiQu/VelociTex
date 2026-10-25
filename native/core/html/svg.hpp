// KaTeX-compatible SVG images for the HTML renderer.
//
// Port of the square root image construction from KaTeX 0.19.0
// `src/delimiter.ts` (`makeSqrtImage`).
#pragma once

#include <memory>
#include <string>

#include "domtree.hpp"
#include "options.hpp"

namespace velocitex::html {

struct SqrtImage {
    NodePtr<HtmlSpan> Span;
    double RuleWidth{0.0};
    double AdvanceWidth{0.0};
};

// KaTeX's `makeSqrtImage`.
SqrtImage MakeSqrtImage(double height, const HtmlOptions& options);

}  // namespace velocitex::html
