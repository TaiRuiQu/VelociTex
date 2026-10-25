#pragma once
#include "types.hpp"
#include "html/markup.hpp"

#include <span>
#include <string>
#include <string_view>

namespace velocitex {
// Writes the MathML markup into `out`.  The caller can hand in a buffer that
// still holds the capacity of the previous render, so the markup does not
// have to be built in a fresh string.
void write_mathml(std::u16string_view source, const MathOptions& options, std::span<const SemanticNode> semantic,
                  int root, bool include_annotation, html::MarkupBuffer& out);
}
