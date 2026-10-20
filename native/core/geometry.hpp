#pragma once
#include "types.hpp"
namespace velocitex::MathPathCache {
std::vector<MathPathSegment> Get(std::u16string_view name);
std::vector<MathPathSegment> TallPath(std::u16string_view label, int64_t height);
std::vector<MathPathSegment> InnerPath(int cp, int64_t height);
std::vector<MathPathSegment> Radical(double height);
std::vector<MathPathSegment> RadicalWithRule(int size, double extra, double height);
} // namespace velocitex::MathPathCache
