#pragma once

#include <string>

namespace velocitex::generated {

// KaTeX's square root path templates (`src/svgGeometry.ts`).
std::string sqrtMain(double extra_viniculum, double h_line_pad);
std::string sqrtSize1(double extra_viniculum, double h_line_pad);
std::string sqrtSize2(double extra_viniculum, double h_line_pad);
std::string sqrtSize3(double extra_viniculum, double h_line_pad);
std::string sqrtSize4(double extra_viniculum, double h_line_pad);
std::string sqrtTall(double extra_viniculum, double h_line_pad, double view_box_height);

}  // namespace velocitex::generated
