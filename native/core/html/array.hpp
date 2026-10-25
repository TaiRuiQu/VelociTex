// KaTeX-compatible rendering of matrix and array environments.
//
// Port of KaTeX 0.19.0 `src/environments/array.ts`.
#pragma once

#include "module_api.hpp"

namespace velocitex::html {

// Renders a `NodeKind::Matrix` node (and its rows and cells).
HtmlNodePtr BuildArrayNode(const ModuleContext& context, int index, const HtmlOptions& options);

}  // namespace velocitex::html
