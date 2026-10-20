#include "types.hpp"

#include <array>

namespace velocitex {
std::string utf8(std::u16string_view source) {
    std::string result;
    result.reserve(source.size());
    for (size_t i = 0; i < source.size();) {
        int length = 1;
        int cp = scalar_at(source, i, length);
        i += length;
        if (cp >= 0xd800 && cp <= 0xdfff)
            cp = 0xfffd;
        if (cp < 0x80)
            result.push_back(char(cp));
        else if (cp < 0x800) {
            result.push_back(char(0xc0 | (cp >> 6)));
            result.push_back(char(0x80 | (cp & 63)));
        } else if (cp < 0x10000) {
            result.push_back(char(0xe0 | (cp >> 12)));
            result.push_back(char(0x80 | ((cp >> 6) & 63)));
            result.push_back(char(0x80 | (cp & 63)));
        } else {
            result.push_back(char(0xf0 | (cp >> 18)));
            result.push_back(char(0x80 | ((cp >> 12) & 63)));
            result.push_back(char(0x80 | ((cp >> 6) & 63)));
            result.push_back(char(0x80 | (cp & 63)));
        }
    }
    return result;
}
std::u16string utf16(std::string_view source) {
    std::u16string result;
    result.reserve(source.size());
    for (size_t i = 0; i < source.size();) {
        auto first = uint8_t(source[i++]);
        int cp = first, count = 0;
        if (first < 0x80) {
        } else if (first >= 0xc2 && first < 0xe0) {
            cp = first & 31;
            count = 1;
        } else if (first >= 0xe0 && first < 0xf0) {
            cp = first & 15;
            count = 2;
        } else if (first >= 0xf0 && first < 0xf5) {
            cp = first & 7;
            count = 3;
        } else {
            append_scalar(result, 0xfffd);
            continue;
        }
        const int minimum = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
        bool valid = i + count <= source.size();
        for (int j = 0; valid && j < count; j++) {
            auto next = uint8_t(source[i + j]);
            if ((next & 0xc0) != 0x80)
                valid = false;
            else
                cp = (cp << 6) | (next & 63);
        }
        if (!valid) {
            append_scalar(result, 0xfffd);
            continue;
        }
        i += count;
        if (count && (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)))
            cp = 0xfffd;
        append_scalar(result, cp);
    }
    return result;
}
bool try_parse_color(std::u16string_view source, MathColor& result) {
    result = {0, 0, 0, 0};
    if (source.empty())
        return false;
    if (source[0] == u'#') {
        auto hex = source.substr(1);
        if (hex.size() != 3 && hex.size() != 4 && hex.size() != 6 && hex.size() != 8)
            return false;
        uint32_t rgba = 0;
        for (char16_t c : hex) {
            int digit = c >= u'0' && c <= u'9'   ? c - u'0'
                        : c >= u'a' && c <= u'f' ? c - u'a' + 10
                        : c >= u'A' && c <= u'F' ? c - u'A' + 10
                                                 : -1;
            if (digit < 0)
                return false;
            rgba = (rgba << 4) | uint32_t(digit);
        }
        if (hex.size() <= 4) {
            uint8_t alpha = hex.size() == 4 ? uint8_t((rgba & 15) * 17) : 255;
            if (hex.size() == 4)
                rgba >>= 4;
            result = {uint8_t(((rgba >> 8) & 15) * 17), uint8_t(((rgba >> 4) & 15) * 17), uint8_t((rgba & 15) * 17),
                      alpha};
        } else {
            uint8_t alpha = hex.size() == 8 ? uint8_t(rgba) : 255;
            if (hex.size() == 8)
                rgba >>= 8;
            result = {uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba), alpha};
        }
        return true;
    }
    std::u16string normalized(source);
    for (auto& c : normalized)
        if (c >= u'A' && c <= u'Z')
            c += u'a' - u'A';
    for (size_t pos = 0; (pos = normalized.find(u"grey", pos)) != std::u16string::npos; pos += 4)
        normalized.replace(pos, 4, u"gray");
    struct Named {
        std::u16string_view Name;
        uint32_t Rgb;
    };
    static constexpr Named colors[] = {{u"aliceblue", 0xf0f8ff},
                                       {u"antiquewhite", 0xfaebd7},
                                       {u"aqua", 0x00ffff},
                                       {u"aquamarine", 0x7fffd4},
                                       {u"azure", 0xf0ffff},
                                       {u"beige", 0xf5f5dc},
                                       {u"bisque", 0xffe4c4},
                                       {u"black", 0},
                                       {u"blanchedalmond", 0xffebcd},
                                       {u"blue", 0x0000ff},
                                       {u"blueviolet", 0x8a2be2},
                                       {u"brown", 0xa52a2a},
                                       {u"burlywood", 0xdeb887},
                                       {u"cadetblue", 0x5f9ea0},
                                       {u"chartreuse", 0x7fff00},
                                       {u"chocolate", 0xd2691e},
                                       {u"coral", 0xff7f50},
                                       {u"cornflowerblue", 0x6495ed},
                                       {u"cornsilk", 0xfff8dc},
                                       {u"crimson", 0xdc143c},
                                       {u"cyan", 0x00ffff},
                                       {u"darkblue", 0x00008b},
                                       {u"darkcyan", 0x008b8b},
                                       {u"darkgoldenrod", 0xb8860b},
                                       {u"darkgray", 0xa9a9a9},
                                       {u"darkgreen", 0x006400},
                                       {u"darkkhaki", 0xbdb76b},
                                       {u"darkmagenta", 0x8b008b},
                                       {u"darkolivegreen", 0x556b2f},
                                       {u"darkorange", 0xff8c00},
                                       {u"darkorchid", 0x9932cc},
                                       {u"darkred", 0x8b0000},
                                       {u"darksalmon", 0xe9967a},
                                       {u"darkseagreen", 0x8fbc8f},
                                       {u"darkslateblue", 0x483d8b},
                                       {u"darkslategray", 0x2f4f4f},
                                       {u"darkturquoise", 0x00ced1},
                                       {u"darkviolet", 0x9400d3},
                                       {u"deeppink", 0xff1493},
                                       {u"deepskyblue", 0x00bfff},
                                       {u"dimgray", 0x696969},
                                       {u"dodgerblue", 0x1e90ff},
                                       {u"firebrick", 0xb22222},
                                       {u"floralwhite", 0xfffaf0},
                                       {u"forestgreen", 0x228b22},
                                       {u"fuchsia", 0xff00ff},
                                       {u"gainsboro", 0xdcdcdc},
                                       {u"ghostwhite", 0xf8f8ff},
                                       {u"gold", 0xffd700},
                                       {u"goldenrod", 0xdaa520},
                                       {u"gray", 0x808080},
                                       {u"green", 0x008000},
                                       {u"greenyellow", 0xadff2f},
                                       {u"honeydew", 0xf0fff0},
                                       {u"hotpink", 0xff69b4},
                                       {u"indianred", 0xcd5c5c},
                                       {u"indigo", 0x4b0082},
                                       {u"ivory", 0xfffff0},
                                       {u"khaki", 0xf0e68c},
                                       {u"lavender", 0xe6e6fa},
                                       {u"lavenderblush", 0xfff0f5},
                                       {u"lawngreen", 0x7cfc00},
                                       {u"lemonchiffon", 0xfffacd},
                                       {u"lightblue", 0xadd8e6},
                                       {u"lightcoral", 0xf08080},
                                       {u"lightcyan", 0xe0ffff},
                                       {u"lightgoldenrodyellow", 0xfafad2},
                                       {u"lightgray", 0xd3d3d3},
                                       {u"lightgreen", 0x90ee90},
                                       {u"lightpink", 0xffb6c1},
                                       {u"lightsalmon", 0xffa07a},
                                       {u"lightseagreen", 0x20b2aa},
                                       {u"lightskyblue", 0x87cefa},
                                       {u"lightslategray", 0x778899},
                                       {u"lightsteelblue", 0xb0c4de},
                                       {u"lightyellow", 0xffffe0},
                                       {u"lime", 0x00ff00},
                                       {u"limegreen", 0x32cd32},
                                       {u"linen", 0xfaf0e6},
                                       {u"magenta", 0xff00ff},
                                       {u"maroon", 0x800000},
                                       {u"mediumaquamarine", 0x66cdaa},
                                       {u"mediumblue", 0x0000cd},
                                       {u"mediumorchid", 0xba55d3},
                                       {u"mediumpurple", 0x9370db},
                                       {u"mediumseagreen", 0x3cb371},
                                       {u"mediumslateblue", 0x7b68ee},
                                       {u"mediumspringgreen", 0x00fa9a},
                                       {u"mediumturquoise", 0x48d1cc},
                                       {u"mediumvioletred", 0xc71585},
                                       {u"midnightblue", 0x191970},
                                       {u"mintcream", 0xf5fffa},
                                       {u"mistyrose", 0xffe4e1},
                                       {u"moccasin", 0xffe4b5},
                                       {u"navajowhite", 0xffdead},
                                       {u"navy", 0x000080},
                                       {u"oldlace", 0xfdf5e6},
                                       {u"olive", 0x808000},
                                       {u"olivedrab", 0x6b8e23},
                                       {u"orange", 0xffa500},
                                       {u"orangered", 0xff4500},
                                       {u"orchid", 0xda70d6},
                                       {u"palegoldenrod", 0xeee8aa},
                                       {u"palegreen", 0x98fb98},
                                       {u"paleturquoise", 0xafeeee},
                                       {u"palevioletred", 0xdb7093},
                                       {u"papayawhip", 0xffefd5},
                                       {u"peachpuff", 0xffdab9},
                                       {u"peru", 0xcd853f},
                                       {u"pink", 0xffc0cb},
                                       {u"plum", 0xdda0dd},
                                       {u"powderblue", 0xb0e0e6},
                                       {u"purple", 0x800080},
                                       {u"rebeccapurple", 0x663399},
                                       {u"red", 0xff0000},
                                       {u"rosybrown", 0xbc8f8f},
                                       {u"royalblue", 0x4169e1},
                                       {u"saddlebrown", 0x8b4513},
                                       {u"salmon", 0xfa8072},
                                       {u"sandybrown", 0xf4a460},
                                       {u"seagreen", 0x2e8b57},
                                       {u"seashell", 0xfff5ee},
                                       {u"sienna", 0xa0522d},
                                       {u"silver", 0xc0c0c0},
                                       {u"skyblue", 0x87ceeb},
                                       {u"slateblue", 0x6a5acd},
                                       {u"slategray", 0x708090},
                                       {u"snow", 0xfffafa},
                                       {u"springgreen", 0x00ff7f},
                                       {u"steelblue", 0x4682b4},
                                       {u"tan", 0xd2b48c},
                                       {u"teal", 0x008080},
                                       {u"thistle", 0xd8bfd8},
                                       {u"tomato", 0xff6347},
                                       {u"turquoise", 0x40e0d0},
                                       {u"violet", 0xee82ee},
                                       {u"wheat", 0xf5deb3},
                                       {u"white", 0xffffff},
                                       {u"whitesmoke", 0xf5f5f5},
                                       {u"yellow", 0xffff00},
                                       {u"yellowgreen", 0x9acd32}};
    if (normalized == u"transparent") {
        result = {255, 255, 255, 0};
        return true;
    }
    for (const auto& named : colors)
        if (normalized == named.Name) {
            result = {uint8_t(named.Rgb >> 16), uint8_t(named.Rgb >> 8), uint8_t(named.Rgb), 255};
            return true;
        }
    return false;
}
} // namespace velocitex
