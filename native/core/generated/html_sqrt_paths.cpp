// VelociTex square root path templates.
//
// Generated from KaTeX 0.19.0 `src/svgGeometry.ts` by
// tools/reference/generate-html-sqrt-paths.mjs.
// svgGeometry.ts SHA-256: ef252fb6007a382c47bcd01852c168ad96b577d046872e5b4bc0b18f4a75718f

#include "html_sqrt_paths.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace velocitex::generated {
namespace {

// The JavaScript number to string conversion used by the template
// literals (shortest round-trip representation).
std::string Number(double value) {
    if (value == 0.0) {
        return "0";
    }
    if (value == static_cast<double>(static_cast<long long>(value)) &&
        value < 1e21 && value > -1e21) {
        return std::to_string(static_cast<long long>(value));
    }
    char buffer[64];
    const int written = std::snprintf(buffer, sizeof(buffer), "%.17g", value);
    for (int precision = 1; precision < 17; ++precision) {
        char candidate[64];
        const int count = std::snprintf(candidate, sizeof(candidate), "%.*g", precision, value);
        if (std::strtod(candidate, nullptr) == value) {
            return std::string(candidate, static_cast<size_t>(count));
        }
    }
    return std::string(buffer, static_cast<size_t>(written));
}

}  // namespace

std::string sqrtMain(double extra_viniculum, double h_line_pad) {
    std::string out;
    out += "M95,";
    out += Number(622 + extra_viniculum + h_line_pad);
    out += "\nc-2.7,0,-7.17,-2.7,-13.5,-8c-5.8,-5.3,-9.5,-10,-9.5,-14\nc0,-2,0.3,-3.3,1,-4c1.3,-2.7,23.83,-20.7,67.5,-54\nc44.2,-33.3,65.8,-50.3,66.5,-51c1.3,-1.3,3,-2,5,-2c4.7,0,8.7,3.3,12,10\ns173,378,173,378c0.7,0,35.3,-71,104,-213c68.7,-142,137.5,-285,206.5,-429\nc69,-144,104.5,-217.7,106.5,-221\nl";
    out += Number(extra_viniculum / 2.075);
    out += " -";
    out += Number(extra_viniculum);
    out += "\nc5.3,-9.3,12,-14,20,-14\nH400000v";
    out += Number(40 + extra_viniculum);
    out += "H845.2724\ns-225.272,467,-225.272,467s-235,486,-235,486c-2.7,4.7,-9,7,-19,7\nc-6,0,-10,-1,-12,-3s-194,-422,-194,-422s-65,47,-65,47z\nM";
    out += Number(834 + extra_viniculum);
    out += " ";
    out += Number(h_line_pad);
    out += "h400000v";
    out += Number(40 + extra_viniculum);
    out += "h-400000z";
    return out;
}

std::string sqrtSize1(double extra_viniculum, double h_line_pad) {
    std::string out;
    out += "M263,";
    out += Number(601 + extra_viniculum + h_line_pad);
    out += "c0.7,0,18,39.7,52,119\nc34,79.3,68.167,158.7,102.5,238c34.3,79.3,51.8,119.3,52.5,120\nc340,-704.7,510.7,-1060.3,512,-1067\nl";
    out += Number(extra_viniculum / 2.084);
    out += " -";
    out += Number(extra_viniculum);
    out += "\nc4.7,-7.3,11,-11,19,-11\nH40000v";
    out += Number(40 + extra_viniculum);
    out += "H1012.3\ns-271.3,567,-271.3,567c-38.7,80.7,-84,175,-136,283c-52,108,-89.167,185.3,-111.5,232\nc-22.3,46.7,-33.8,70.3,-34.5,71c-4.7,4.7,-12.3,7,-23,7s-12,-1,-12,-1\ns-109,-253,-109,-253c-72.7,-168,-109.3,-252,-110,-252c-10.7,8,-22,16.7,-34,26\nc-22,17.3,-33.3,26,-34,26s-26,-26,-26,-26s76,-59,76,-59s76,-60,76,-60z\nM";
    out += Number(1001 + extra_viniculum);
    out += " ";
    out += Number(h_line_pad);
    out += "h400000v";
    out += Number(40 + extra_viniculum);
    out += "h-400000z";
    return out;
}

std::string sqrtSize2(double extra_viniculum, double h_line_pad) {
    std::string out;
    out += "M983 ";
    out += Number(10 + extra_viniculum + h_line_pad);
    out += "\nl";
    out += Number(extra_viniculum / 3.13);
    out += " -";
    out += Number(extra_viniculum);
    out += "\nc4,-6.7,10,-10,18,-10 H400000v";
    out += Number(40 + extra_viniculum);
    out += "\nH1013.1s-83.4,268,-264.1,840c-180.7,572,-277,876.3,-289,913c-4.7,4.7,-12.7,7,-24,7\ns-12,0,-12,0c-1.3,-3.3,-3.7,-11.7,-7,-25c-35.3,-125.3,-106.7,-373.3,-214,-744\nc-10,12,-21,25,-33,39s-32,39,-32,39c-6,-5.3,-15,-14,-27,-26s25,-30,25,-30\nc26.7,-32.7,52,-63,76,-91s52,-60,52,-60s208,722,208,722\nc56,-175.3,126.3,-397.3,211,-666c84.7,-268.7,153.8,-488.2,207.5,-658.5\nc53.7,-170.3,84.5,-266.8,92.5,-289.5z\nM";
    out += Number(1001 + extra_viniculum);
    out += " ";
    out += Number(h_line_pad);
    out += "h400000v";
    out += Number(40 + extra_viniculum);
    out += "h-400000z";
    return out;
}

std::string sqrtSize3(double extra_viniculum, double h_line_pad) {
    std::string out;
    out += "M424,";
    out += Number(2398 + extra_viniculum + h_line_pad);
    out += "\nc-1.3,-0.7,-38.5,-172,-111.5,-514c-73,-342,-109.8,-513.3,-110.5,-514\nc0,-2,-10.7,14.3,-32,49c-4.7,7.3,-9.8,15.7,-15.5,25c-5.7,9.3,-9.8,16,-12.5,20\ns-5,7,-5,7c-4,-3.3,-8.3,-7.7,-13,-13s-13,-13,-13,-13s76,-122,76,-122s77,-121,77,-121\ns209,968,209,968c0,-2,84.7,-361.7,254,-1079c169.3,-717.3,254.7,-1077.7,256,-1081\nl";
    out += Number(extra_viniculum / 4.223);
    out += " -";
    out += Number(extra_viniculum);
    out += "c4,-6.7,10,-10,18,-10 H400000\nv";
    out += Number(40 + extra_viniculum);
    out += "H1014.6\ns-87.3,378.7,-272.6,1166c-185.3,787.3,-279.3,1182.3,-282,1185\nc-2,6,-10,9,-24,9\nc-8,0,-12,-0.7,-12,-2z M";
    out += Number(1001 + extra_viniculum);
    out += " ";
    out += Number(h_line_pad);
    out += "\nh400000v";
    out += Number(40 + extra_viniculum);
    out += "h-400000z";
    return out;
}

std::string sqrtSize4(double extra_viniculum, double h_line_pad) {
    std::string out;
    out += "M473,";
    out += Number(2713 + extra_viniculum + h_line_pad);
    out += "\nc339.3,-1799.3,509.3,-2700,510,-2702 l";
    out += Number(extra_viniculum / 5.298);
    out += " -";
    out += Number(extra_viniculum);
    out += "\nc3.3,-7.3,9.3,-11,18,-11 H400000v";
    out += Number(40 + extra_viniculum);
    out += "H1017.7\ns-90.5,478,-276.2,1466c-185.7,988,-279.5,1483,-281.5,1485c-2,6,-10,9,-24,9\nc-8,0,-12,-0.7,-12,-2c0,-1.3,-5.3,-32,-16,-92c-50.7,-293.3,-119.7,-693.3,-207,-1200\nc0,-1.3,-5.3,8.7,-16,30c-10.7,21.3,-21.3,42.7,-32,64s-16,33,-16,33s-26,-26,-26,-26\ns76,-153,76,-153s77,-151,77,-151c0.7,0.7,35.7,202,105,604c67.3,400.7,102,602.7,104,\n606zM";
    out += Number(1001 + extra_viniculum);
    out += " ";
    out += Number(h_line_pad);
    out += "h400000v";
    out += Number(40 + extra_viniculum);
    out += "H1017.7z";
    return out;
}

std::string sqrtTall(double extra_viniculum, double h_line_pad, double view_box_height) {
    // One path edge has a variable length: it runs vertically from the
    // vinculum to a point near the bottom of the surd.
    const double vert_segment = view_box_height - 54 - h_line_pad - extra_viniculum;
    std::string out;
    out += "M702 ";
    out += Number(extra_viniculum + h_line_pad);
    out += "H400000";
    out += Number(40 + extra_viniculum);
    out += "\nH742v";
    out += Number(vert_segment);
    out += "l-4 4-4 4c-.667.7 -2 1.5-4 2.5s-4.167 1.833-6.5 2.5-5.5 1-9.5 1\nh-12l-28-84c-16.667-52-96.667 -294.333-240-727l-212 -643 -85 170\nc-4-3.333-8.333-7.667-13 -13l-13-13l77-155 77-156c66 199.333 139 419.667\n219 661 l218 661zM702 ";
    out += Number(h_line_pad);
    out += "H400000v";
    out += Number(40 + extra_viniculum);
    out += "H742z";
    return out;
}

}  // namespace velocitex::generated
