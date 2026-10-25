// KaTeX-compatible HTML renderer.
//
// Port of KaTeX 0.19.0 `src/buildHTML.ts` plus the HTML builders of the
// `src/functions/*` modules, operating on VelociTex's semantic tree.
#pragma once
#include "html/markup.hpp"

#include <span>
#include <string>

#include "../types.hpp"

namespace velocitex::html {

// Renders the semantic tree into KaTeX-compatible HTML markup.  The result is
// the markup of the inner `katex-html` span, matching KaTeX byte for byte.
//
// The markup is appended to `out` so that the caller can reuse the buffer of
// the previous render; the buffer keeps its capacity and the markup does not
// have to be copied into a fresh string.
void BuildHtmlMarkup(std::span<const SemanticNode> semantic, int root, const MathOptions& options, MarkupBuffer& out);

}  // namespace velocitex::html
