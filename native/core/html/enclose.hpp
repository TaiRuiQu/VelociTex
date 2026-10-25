// KaTeX-compatible rendering of enclosures, rules and equation tags.
//
// Port of KaTeX 0.19.0 `src/functions/enclose.ts`, `src/functions/rule.ts`
// and `src/functions/tag.ts`.
#pragma once

#include "module_api.hpp"

namespace velocitex::html {

// Renders a `NodeKind::Enclose`, `NodeKind::Rule` or `NodeKind::Tag` node.
HtmlNodePtr BuildEncloseNode(const ModuleContext& context, int index, const HtmlOptions& options);

}  // namespace velocitex::html
