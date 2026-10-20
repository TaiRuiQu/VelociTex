#pragma once
#include "types.hpp"
namespace velocitex {
LayoutOutput build_layout(std::span<const SemanticNode> nodes, int root, const MathOptions& options);
double mathml_length(TexLength value, MathStyle style, uint8_t textSize, const MathOptions& options);
double mathml_size_scale(int size);
} // namespace velocitex
