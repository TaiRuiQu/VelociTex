#pragma once
#include "types.hpp"
#include <span>
#include <string>
#include <string_view>

namespace velocitex {
std::string write_mathml(std::u16string_view source, const MathOptions& options,
                        std::span<const SemanticNode> semantic, int root,
                        bool include_annotation = true);
}
