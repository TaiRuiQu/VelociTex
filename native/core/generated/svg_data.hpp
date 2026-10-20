#pragma once
#include <span>
#include <string_view>
namespace velocitex::generated {
struct SvgFont { int UnitsPerEm; std::span<const std::string_view> Paths; };
extern const std::span<const SvgFont> SvgFonts;
}
