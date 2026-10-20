#pragma once
#include "types.hpp"
namespace velocitex {
struct SharedSvg { std::string Svg, Definitions; };
MathBounds svg_viewport(const LayoutOutput& layout);
std::string write_svg(std::u16string_view source,const LayoutOutput& layout,std::string_view id_prefix);
SharedSvg write_shared_svg(std::u16string_view source,const LayoutOutput& layout,std::string_view glyph_prefix,std::string_view clip_prefix);
}
