#pragma once

#include <string_view>

namespace velocitex::generated {

// KaTeX's SVG path data, used by the HTML renderer for stretchy glyphs.
struct HtmlPathData {
    struct Path {
        std::u16string_view Name;
        std::string_view Data;
    };
    static const Path Paths[];
    static const int PathCount;

    // Looks up a path by name; returns nullptr when the name is unknown.
    static const std::string_view* Find(std::u16string_view name);
};

}  // namespace velocitex::generated
