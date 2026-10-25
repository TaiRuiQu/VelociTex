#include "mathml.hpp"
#include "state.hpp"

#include "html/domtree.hpp"

#include "generated/data.hpp"
#include "layout.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <deque>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace velocitex {
namespace {
using Opt = std::optional<std::string>;
// A value that is one of the literals of the renderer.  Such a value does
// not need a string of its own, so it stays a view.
using OptView = std::optional<std::string_view>;
// Writes the UTF-8 encoding of a code point into the buffer and returns the
// number of bytes it used.
//
// The encoding is written into the caller's buffer instead of being returned
// as a string: a formula has hundreds of characters, and a string per
// character costs an allocation each time.
size_t scalar(char* out, int cp) {
    if (cp < 0 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
        throw std::invalid_argument("Invalid Unicode scalar.");
    if (cp < 0x80) {
        out[0] = char(cp);
        return 1;
    }
    if (cp < 0x800) {
        out[0] = char(0xc0 | (cp >> 6));
        out[1] = char(0x80 | (cp & 63));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = char(0xe0 | (cp >> 12));
        out[1] = char(0x80 | ((cp >> 6) & 63));
        out[2] = char(0x80 | (cp & 63));
        return 3;
    }
    out[0] = char(0xf0 | (cp >> 18));
    out[1] = char(0x80 | ((cp >> 12) & 63));
    out[2] = char(0x80 | ((cp >> 6) & 63));
    out[3] = char(0x80 | (cp & 63));
    return 4;
}
// The UTF-8 encoding of a text, for the few places that need one.
std::string mml_utf8(std::u16string_view source) {
    std::string s;
    // A UTF-16 code unit needs at most three UTF-8 bytes.
    s.reserve(source.size() * 3);
    char buffer[4];
    for (size_t i = 0; i < source.size(); ++i) {
        int cp = source[i];
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < source.size() && source[i + 1] >= 0xdc00 && source[i + 1] <= 0xdfff)
            cp = 0x10000 + ((cp - 0xd800) << 10) + (source[++i] - 0xdc00);
        else if (cp >= 0xd800 && cp <= 0xdfff)
            cp = 0xfffd;
        const size_t length = scalar(buffer, cp);
        // The bytes are copied one by one.  `append` would ask the library
        // for a copy of a handful of bytes, and the call costs more than the
        // copy itself.
        for (size_t k = 0; k < length; ++k) {
            s.push_back(buffer[k]);
        }
    }
    return s;
}
Opt mml_text(const std::optional<std::u16string>& s) {
    return s ? Opt(mml_utf8(*s)) : std::nullopt;
}
// The length in bytes of the first UTF-8 character of `s`.
size_t utf8_head(std::string_view s) {
    if (s.empty())
        return 0;
    unsigned char c = static_cast<unsigned char>(s[0]);
    size_t length = 1;
    if ((c & 0xe0) == 0xc0)
        length = 2;
    else if ((c & 0xf0) == 0xe0)
        length = 3;
    else if ((c & 0xf8) == 0xf0)
        length = 4;
    return std::min(length, s.size());
}
bool eq(const std::optional<std::u16string>& s, std::u16string_view expected) {
    return s && *s == expected;
}
// The literals are taken as separate arguments, so that the length of each of
// them is known at compile time.  A comparison against a literal of a known
// length is a load and a compare, while a comparison against a view is a call
// into the C library: the WebAssembly target does not expand a `memcmp` of a
// known length into loads, so every one of those calls costs more in the
// browser than the comparison itself.
template <class... Literals>
bool oneof(std::string_view value, const Literals&... literals) {
    return (same_text(value, literals) || ...);
}
// Compares two views of the same length.  There is no literal to compare
// against, so the comparison of the bytes is a call into the C library.
bool views_equal(std::string_view a, std::string_view b) {
    return a.size() == b.size() && (a.size() == 0 || std::memcmp(a.data(), b.data(), a.size()) == 0);
}
// The same for the UTF-16 side: the literals are bare, without the `_u16`
// suffix, because the suffix builds a view and the view hides the length of
// the literal from the compiler.
template <class... Literals>
bool oneof(const std::optional<std::u16string>& value, const Literals&... literals) {
    if (!value)
        return false;
    return (same_text(*value, literals) || ...);
}
// Formats a number the way .NET's custom numeric format does: fifteen
// significant decimal digits first, then the requested decimal positions are
// rounded away from zero.
//
// The text goes into a buffer of the caller, so that a length of the MathML
// output does not need a string of its own.  The buffer has to hold the
// sign, the digits, the point and the zeros of a very small number, so it
// is 192 bytes long.
size_t FormatNumber(double value, int decimals, char* out) {
    char raw[64];
    auto result = std::to_chars(raw, raw + sizeof(raw), std::abs(value), std::chars_format::scientific, 14);
    if (result.ec != std::errc())
        throw std::runtime_error("Unable to format MathML dimension.");
    const char* const raw_end = result.ptr;
    // The exponent of the number.
    const char* exponent_at = raw;
    while (exponent_at < raw_end && *exponent_at != 'e') ++exponent_at;
    if (exponent_at == raw_end) {
        // The scientific form always carries an exponent; this is a safety
        // net for a format that does not.
        const size_t length = size_t(raw_end - raw);
        std::memcpy(out, raw, length);
        if (std::signbit(value)) {
            std::memmove(out + 1, out, length);
            out[0] = '-';
            return length + 1;
        }
        return length;
    }
    int exponent = 0;
    bool negative_exponent = false;
    {
        const char* p = exponent_at + 1;
        if (p < raw_end && (*p == '+' || *p == '-')) {
            negative_exponent = (*p == '-');
            ++p;
        }
        for (; p < raw_end; ++p)
            exponent = exponent * 10 + (*p - '0');
        if (negative_exponent)
            exponent = -exponent;
    }
    // The digits of the number, without the point.
    char digits[32];
    size_t digit_count = 0;
    for (const char* p = raw; p < exponent_at; ++p)
        if (*p != '.')
            digits[digit_count++] = *p;

    int point = exponent + 1;
    const int keep = point + decimals;
    if (keep < 0) {
        digits[0] = '0';
        digit_count = 1;
    } else if (keep < int(digit_count)) {
        const bool up = digits[size_t(keep)] >= '5';
        digit_count = size_t(keep);
        if (up) {
            int i = int(digit_count) - 1;
            while (i >= 0 && digits[size_t(i)] == '9')
                digits[size_t(i--)] = '0';
            if (i >= 0) {
                ++digits[size_t(i)];
            } else {
                // The carry adds a digit in front.
                for (int k = int(digit_count); k > 0; --k)
                    digits[size_t(k)] = digits[size_t(k - 1)];
                digits[0] = '1';
                ++digit_count;
                ++point;
            }
        }
        if (digit_count == 0) {
            digits[0] = '0';
            digit_count = 1;
        }
    }

    // The text of the number.
    size_t count = 0;
    if (std::signbit(value))
        out[count++] = '-';
    bool has_point = false;
    if (point <= 0) {
        out[count++] = '0';
        out[count++] = '.';
        has_point = true;
        for (int i = 0; i < -point; ++i)
            out[count++] = '0';
        // The copies use wide moves instead of `memcpy`: their length is
        // only known at run time, and a call into the C library costs more
        // in the WebAssembly build than the copy itself.  The digits of a
        // number fit into the short copy.
        html::detail::CopyShort(out + count, digits, digit_count);
        count += digit_count;
    } else {
        if (int(digit_count) < point) {
            const size_t padding = size_t(point - int(digit_count));
            html::detail::CopyShort(out + count, digits, digit_count);
            count += digit_count;
            // The padding is a run of zeros, which is written with a loop
            // instead of a `memset` of a length that is only known at run
            // time.
            for (size_t i = 0; i < padding; ++i) {
                out[count + i] = '0';
            }
            count += padding;
            digit_count += padding;
        } else {
            html::detail::CopyShort(out + count, digits, size_t(point));
            count += size_t(point);
        }
        if (int(digit_count) > point) {
            out[count++] = '.';
            has_point = true;
            const size_t rest = size_t(int(digit_count) - point);
            html::detail::CopyShort(out + count, digits + point, rest);
            count += rest;
        }
    }
    // The trailing zeros of the fraction are dropped.
    if (has_point) {
        while (count > 0 && out[count - 1] == '0')
            --count;
        if (count > 0 && out[count - 1] == '.')
            --count;
    }
    return count;
}

// The length in `em` of a number, written into a buffer of the caller.
size_t FormatEm(double value, char* out) {
    const size_t count = FormatNumber(value, 4, out);
    out[count] = 'e';
    out[count + 1] = 'm';
    return count + 2;
}
// KaTeX escapes text with utils.escape (src/utils.ts); the same function is
// used for element text and for attribute values.
void escaped(html::MarkupBuffer& output, std::string_view s) {
    // KaTeX escapes the same characters in the MathML as in the HTML, so the
    // table of the HTML renderer is used here as well.  It answers the
    // question for a character with one lookup instead of five comparisons.
    html::detail::AppendEscaped(output, s);
}

// Writes a text, encoded as UTF-8 and escaped, straight into the buffer.
//
// The annotation of a formula is the whole source, so it is the longest text
// the MathML writer produces.  Encoding it into a string first asks the
// library for the storage of the whole source; writing it into the buffer
// needs no string at all.
void escaped_utf8(html::MarkupBuffer& output, std::u16string_view source) {
    // The characters that KaTeX escapes are all ASCII, and the UTF-8 encoding
    // of a code point above ASCII never holds a byte below 0x80.  So the
    // ASCII part of the text can be copied in runs, and only the code points
    // above ASCII have to be encoded one by one.
    char run[64];
    size_t used = 0;
    for (size_t i = 0; i < source.size(); ++i) {
        int cp = source[i];
        if (cp >= 0xd800 && cp <= 0xdbff && i + 1 < source.size() && source[i + 1] >= 0xdc00 &&
            source[i + 1] <= 0xdfff) {
            cp = 0x10000 + ((cp - 0xd800) << 10) + (source[++i] - 0xdc00);
        } else if (cp >= 0xd800 && cp <= 0xdfff) {
            cp = 0xfffd;
        }
        if (cp >= 0x80) {
            if (used > 0) {
                output.append(run, used);
                used = 0;
            }
            char encoded[4];
            const size_t length = scalar(encoded, cp);
            output.append(encoded, length);
            continue;
        }
        const char raw = static_cast<char>(cp);
        if (!html::detail::kEscapeTable.value[static_cast<unsigned char>(raw)]) {
            if (used == sizeof(run)) {
                output.append(run, used);
                used = 0;
            }
            run[used++] = raw;
            continue;
        }
        if (used > 0) {
            output.append(run, used);
            used = 0;
        }
        switch (raw) {
            case '&': output.append("&amp;", 5); break;
            case '>': output.append("&gt;", 4); break;
            case '<': output.append("&lt;", 4); break;
            case '"': output.append("&quot;", 6); break;
            default: output.append("&#x27;", 6); break;
        }
    }
    if (used > 0) {
        output.append(run, used);
    }
}
void attr(html::MarkupBuffer& output, std::string_view name, std::string_view value) {
    output += ' ';
    output += name;
    output.append("=\"", 2);
    escaped(output, value);
    output += '"';
}
int fence_cp(int cp) {
    return cp == '|' ? 0x2223 : cp == 0x2016 ? 0x2225 : cp;
}
int accent_cp(std::string_view label, int fallback = 0) {
    if (oneof(label, "\\overline", "\\underline"))
        return 0x203e;
    if (same_text(label, "\\widehat"))
        return '^';
    if (same_text(label, "\\widecheck"))
        return 0x2c7;
    if (oneof(label, "\\widetilde", "\\utilde", "\\tilde"))
        return '~';
    if (same_text(label, "\\^"))
        return 0x2c6;
    if (oneof(label, "\\overrightarrow", "\\underrightarrow", "\\xrightarrow", "\\\\cdrightarrow"))
        return 0x2192;
    if (oneof(label, "\\overleftarrow", "\\underleftarrow", "\\xleftarrow", "\\\\cdleftarrow"))
        return 0x2190;
    if (oneof(label, "\\overleftrightarrow", "\\underleftrightarrow", "\\xleftrightarrow"))
        return 0x2194;
    if (oneof(label, "\\Overrightarrow", "\\xRightarrow"))
        return 0x21d2;
    if (same_text(label, "\\xLeftarrow"))
        return 0x21d0;
    if (same_text(label, "\\xLeftrightarrow"))
        return 0x21d4;
    if (oneof(label, "\\overleftharpoon", "\\xleftharpoonup"))
        return 0x21bc;
    if (oneof(label, "\\overrightharpoon", "\\xrightharpoonup"))
        return 0x21c0;
    if (same_text(label, "\\xleftharpoondown"))
        return 0x21bd;
    if (same_text(label, "\\xrightharpoondown"))
        return 0x21c1;
    if (oneof(label, "\\xrightleftharpoons", "\\xrightequilibrium"))
        return 0x21cc;
    if (oneof(label, "\\xleftrightharpoons", "\\xleftequilibrium"))
        return 0x21cb;
    if (oneof(label, "\\\\cdlongequal", "\\xlongequal"))
        return '=';
    if (same_text(label, "\\xhookleftarrow"))
        return 0x21a9;
    if (same_text(label, "\\xhookrightarrow"))
        return 0x21aa;
    if (same_text(label, "\\xmapsto"))
        return 0x21a6;
    if (same_text(label, "\\xtwoheadleftarrow"))
        return 0x219e;
    if (same_text(label, "\\xtwoheadrightarrow"))
        return 0x21a0;
    if (oneof(label, "\\xtofrom", "\\xrightleftarrows"))
        return 0x21c4;
    if (same_text(label, "\\overbrace"))
        return 0x23de;
    if (same_text(label, "\\underbrace"))
        return 0x23df;
    if (same_text(label, "\\overbracket"))
        return 0x23b4;
    if (same_text(label, "\\underbracket"))
        return 0x23b5;
    if (same_text(label, "\\overgroup"))
        return 0x23e0;
    if (same_text(label, "\\undergroup"))
        return 0x23e1;
    if (oneof(label, "\\overlinesegment", "\\underlinesegment"))
        return 0x23af;
    return fallback == 0 ? '^' : fallback;
}
OptView variant(const SemanticNode& n) {
    if (n.OriginalCodePoint > 0)
        return n.OriginalCodePoint >= 0x1d7ce || (n.IsMathOrd && !n.TextMode) || n.TextMode ? OptView{} : OptView("normal");
    // The variant is always one of the literals below, so it stays a view
    // instead of a string of its own.
    std::string_view value;
    switch (n.Font) {
    case MathFont::MathRoman:
        value = generated::has_main_metric(n.CodePoint) ? "normal" : n.IsMathOrd && !n.TextMode ? "italic" : "normal";
        break;
    case MathFont::MathItalic:
        value = "italic";
        break;
    case MathFont::MathBold:
        value = "bold";
        break;
    case MathFont::MathBoldItalic:
        value = n.IsMathOrd ? "bold-italic" : "bold";
        break;
    case MathFont::MainBoldItalic:
        value = "bold-italic";
        break;
    case MathFont::MathSansBoldItalic:
        value = "sans-serif-bold-italic";
        break;
    case MathFont::MathSans:
        value = "sans-serif";
        break;
    case MathFont::MathSansBold:
        value = "bold-sans-serif";
        break;
    case MathFont::MathSansItalic:
        value = "sans-serif-italic";
        break;
    case MathFont::MathTypewriter:
        value = "monospace";
        break;
    case MathFont::AMS:
        value = n.SymbolFont == MathFont::AMS ? n.IsMathOrd && !n.TextMode ? "italic" : "normal" : "double-struck";
        break;
    case MathFont::Caligraphic:
    case MathFont::Script:
        value = "script";
        break;
    case MathFont::Fraktur:
        value = "fraktur";
        break;
    default:
        value = n.IsMathOrd ? "italic" : "normal";
        break;
    }
    const std::string_view default_value =
        !n.IsMathOrd && (n.TextMode || (n.CodePoint >= '0' && n.CodePoint <= '9')) ? "normal" : "italic";
    if (n.Atom != MathAtom::Ord)
        return n.Atom == MathAtom::Bin && n.Font == MathFont::MathBoldItalic ? OptView("bold-italic") : OptView{};
    return value == default_value ? OptView{} : OptView(value);
}
// The attributes a MathML element can carry.  The order is the order in
// which KaTeX writes them (src/buildMathML.ts).
enum class Attr : uint8_t {
    None = 0,
    MathVariant,
    Width,
    LineThickness,
    Fence,
    Stretchy,
    MinSize,
    MaxSize,
    LSpace,
    RSpace,
    Accent,
    AccentUnder,
    RowSpacing,
    ColumnAlign,
    ColumnLines,
    ColumnSpacing,
    RowLines,
    Notation,
    MathColor,
    MathBackground,
    MathSize,
    Height,
    Depth,
    VOffset,
    Style,
    LineBreak,
    ScriptLevel,
    DisplayStyle,
    Class,
};
std::string_view attr_name(Attr id) {
    // The names are views, so that the length of a name is known at compile
    // time: a `const char*` would ask `strlen` for it at every attribute of
    // every node.
    static constexpr std::string_view names[] = {
        "",           "mathvariant", "width",  "linethickness", "fence",          "stretchy",     "minsize",
        "maxsize",    "lspace",      "rspace", "accent",       "accentunder",    "rowspacing",   "columnalign",
        "columnlines", "columnspacing", "rowlines", "notation", "mathcolor",     "mathbackground", "mathsize",
        "height",     "depth",       "voffset", "style",       "linebreak",      "scriptlevel",  "displaystyle",
        "class"};
    return names[size_t(id)];
}
// The flags of a node.
enum NodeFlag : uint16_t {
    // KaTeX's isNumberPunctuation (src/buildMathML.ts) looks for this.
    kSeparator = 1 << 0,
    // The node is an <mo> that must not stretch.
    kNoStretch = 1 << 1,
    // The spacing around the node is suppressed.
    kSuppressSpacing = 1 << 2,
    // The node is a large operator that must not grow.
    kLargeOpFalse = 1 << 3,
    // The node is an <mstyle> that only sets the script level.
    kScriptLevelOnly = 1 << 4,
    // KaTeX's SpaceNode is not a MathNode, so the space characters it emits
    // are never merged with their neighbours.
    kSpaceText = 1 << 5,
    // KaTeX's SpaceNode (src/mathMLTree.ts) serialises as <mspace .../> while a
    // regular MathNode always has a closing tag.
    kSelfClosing = 1 << 6,
    // True for an <mrow> that this file created for a row of nodes, so that
    // nested <mrow>s are not mistaken for it.
    kOwnRow = 1 << 7,
    // The attributes of the node are written in the order they were set.
    kOrdered = 1 << 8,
};
// The strings the nodes refer to.  The values live in the pool instead of in
// the nodes, so that the nodes themselves stay small; the indices stay valid
// as long as the pool lives, and the pool lives as long as the writer that
// owns it.  A deque keeps the strings at a stable address, so a view stays
// valid while the writer works.
// The values of the attributes and the texts of the elements.
//
// The values live in one buffer with the offsets into it, so that a render
// does not ask the heap for a string per value and so that the values of a
// node are close to each other in memory.
class ValuePool {
public:
    uint32_t intern(std::string_view value) { return intern(value.data(), value.size()); }
    uint32_t intern(const char* data, size_t length) {
        // The pool is reused between renders, so a value does not ask the heap
        // for a fresh string every time.  The bytes go into the markup buffer,
        // which copies a short fragment inline; the call into the string
        // library that a `std::string` needs costs more than the copy.
        const uint32_t id = count_++;
        if (offsets.size() <= id + 1) {
            offsets.resize(id + 2, 0);
        }
        offsets[id] = static_cast<uint32_t>(buffer_.size());
        buffer_.Append(data, length);
        offsets[id + 1] = static_cast<uint32_t>(buffer_.size());
        return id;
    }
    std::string_view get(uint32_t id) const {
        return std::string_view(buffer_.data() + offsets[id], offsets[id + 1] - offsets[id]);
    }
    // The values of the previous render are dropped, but their bytes are
    // kept, so that the next render does not have to allocate them again.
    void clear() {
        count_ = 0;
        buffer_.Clear();
    }
    // Reserves room for the values of a render, so that the buffer does not
    // have to grow while the values are written.
    void reserve(size_t bytes) { buffer_.Reserve(bytes); }
    // Interns a number.  The number is formatted into a buffer of the stack
    // and goes into the pool from there, so it does not need a string of its
    // own: the lengths of the MathML output are written a few times per
    // formula, and a string of its own is a heap allocation each time.
    uint32_t intern_number(double value, int decimals) {
        char buffer[224];
        const size_t length = FormatNumber(value, decimals, buffer);
        return intern(buffer, length);
    }
    uint32_t intern_em(double value) {
        char buffer[224];
        const size_t length = FormatEm(value, buffer);
        return intern(buffer, length);
    }
    // Interns a number with a unit behind it.
    uint32_t intern_number_with_unit(double value, int decimals, std::string_view unit) {
        char buffer[256];
        size_t length = FormatNumber(value, decimals, buffer);
        // The unit is short ("em", "mu", "ex"), so the copy uses wide moves.
        html::detail::CopyShort(buffer + length, unit.data(), unit.size());
        length += unit.size();
        return intern(buffer, length);
    }

private:
    html::MarkupBuffer buffer_;
    std::vector<uint32_t> offsets;
    uint32_t count_ = 0;
};
// A node of the MathML tree.  The text of the element and the values of its
// attributes live in the writer's value pool, so a node stays small enough to
// be moved around cheaply.
struct MmlNode {
    // The number of attributes a node holds without spilling into the
    // overflow table.  The busiest node, a colour box, carries six.
    static constexpr int kAttrCapacity = 6;
    // The bits of a slot: the attribute id and the index of the value in the
    // pool.  A slot of zero means "no attribute".
    static constexpr uint32_t kValueMask = 0xffffff;

    // The tag is always one of the literals below, so a view is enough.
    std::string_view Tag;
    uint32_t slots[kAttrCapacity] = {};
    // The text of the element: the index into the pool plus one, so that zero
    // means "no text".
    uint32_t content = 0;
    int First = -1, Next = -1;
    // The index of the attributes that did not fit into the slots; zero means
    // that the node has none.
    uint32_t overflow_id = 0;
    uint16_t flags = 0;
    MathStyle Style{};
    // The attributes that did not fit into the slots live in a table of their
    // own, so that a node stays inside one cache line.  No node of the corpus
    // needs the table, but a formula that carries more attributes must not
    // lose one.
    VELOCITEX_REUSED_MEMBER std::vector<std::vector<uint32_t>> overflows;
    VELOCITEX_REUSED_MEMBER uint32_t overflow_count = 0;

    MmlNode(ValuePool& pool, std::string_view tag, Opt value = {}, OptView variant = {})
        : Tag(tag) {
        if (value)
            content = pool.intern(*value) + 1;
        if (variant)
            set(pool, Attr::MathVariant, *variant);
    }
    // A node whose text is a single code point.  The UTF-8 encoding goes
    // straight into the pool, so the most common node of a formula does not
    // need a string of its own.
    MmlNode(ValuePool& pool, std::string_view tag, int code_point, OptView variant = {})
        : Tag(tag) {
        char buffer[4];
        const size_t length = scalar(buffer, code_point);
        content = pool.intern(buffer, length) + 1;
        if (variant)
            set(pool, Attr::MathVariant, *variant);
    }
    // Sets the text of the element.
    void set_content(ValuePool& pool, std::string_view text) { content = pool.intern(text) + 1; }
    // Sets the text of the element from a code point.  The UTF-8 encoding
    // goes straight into the pool, so the most common node of a formula does
    // not need a string of its own.
    void set_content_code_point(ValuePool& pool, int code_point) {
        char buffer[4];
        const size_t length = scalar(buffer, code_point);
        content = pool.intern(buffer, length) + 1;
    }
    // Drops the attributes of the previous render.  The tables keep their
    // capacity, so the next render does not have to allocate them again.
    static void reset_overflows() { overflow_count = 0; }
    // The attributes of the node that did not fit into the slots.
    const std::vector<uint32_t>& overflow() const {
        static const std::vector<uint32_t> empty;
        if (overflow_id == 0)
            return empty;
        return overflows[overflow_id - 1];
    }
    // Adds an attribute that did not fit into the slots.
    void push_overflow(uint32_t packed) {
        if (overflow_id == 0) {
            overflow_id = ++overflow_count;
            if (overflows.size() < overflow_count)
                overflows.resize(overflow_count);
            overflows[overflow_id - 1].clear();
        }
        overflows[overflow_id - 1].push_back(packed);
    }
    // Sets an attribute; a node keeps at most one value per attribute.
    void set(ValuePool& pool, Attr id, std::string_view value) {
        set_value(pool, id, pool.intern(value));
    }
    // Sets an attribute whose value is a length in `em`.  The number is
    // formatted straight into the pool, so it does not need a string.
    void set_em(ValuePool& pool, Attr id, double value) {
        set_value(pool, id, pool.intern_em(value));
    }
    // Sets an attribute from a value that is already in the pool.
    void set_value(ValuePool& pool, Attr id, uint32_t value_id) {
        uint32_t packed = (uint32_t(id) << 24) | value_id;
        for (int i = 0; i < kAttrCapacity; ++i)
            if (slots[i] != 0 && (slots[i] >> 24) == uint32_t(id)) {
                slots[i] = packed;
                return;
            }
        for (int i = 0; i < kAttrCapacity; ++i)
            if (slots[i] == 0) {
                slots[i] = packed;
                return;
            }
        push_overflow(packed);
    }
    // Sets an attribute from an optional value; an empty optional removes the
    // attribute.
    void set_optional(ValuePool& pool, Attr id, const Opt& value) {
        if (value)
            set(pool, id, std::string_view(*value));
        else
            remove(id);
    }
    // Adds an attribute that KaTeX writes in its own order; such an
    // attribute replaces the default order of the node.
    void set_ordered(ValuePool& pool, Attr id, std::string_view value) {
        set_ordered_value(pool, id, pool.intern(value));
    }
    // Adds an attribute in KaTeX's own order from a value that is already in
    // the pool.
    void set_ordered_value(ValuePool& pool, Attr id, uint32_t value_id) {
        flags |= kOrdered;
        uint32_t packed = (uint32_t(id) << 24) | value_id;
        for (int i = 0; i < kAttrCapacity; ++i)
            if (slots[i] == 0) {
                slots[i] = packed;
                return;
            }
        push_overflow(packed);
    }
    // Removes an attribute.
    void remove(Attr id) {
        for (int i = 0; i < kAttrCapacity; ++i)
            if (slots[i] != 0 && (slots[i] >> 24) == uint32_t(id)) {
                for (int j = i; j + 1 < kAttrCapacity; ++j)
                    slots[j] = slots[j + 1];
                slots[kAttrCapacity - 1] = 0;
                return;
            }
        if (overflow_id == 0)
            return;
        auto& list = overflows[overflow_id - 1];
        for (size_t i = 0; i < list.size(); ++i)
            if ((list[i] >> 24) == uint32_t(id)) {
                list.erase(list.begin() + i);
                return;
            }
    }
    // The slot of an attribute, or zero when the node does not carry it.
    uint32_t slot(Attr id) const {
        for (int i = 0; i < kAttrCapacity; ++i)
            if (slots[i] != 0 && (slots[i] >> 24) == uint32_t(id))
                return slots[i];
        for (uint32_t packed : overflow())
            if ((packed >> 24) == uint32_t(id))
                return packed;
        return 0;
    }
    // The attribute slots in the order they were set.
    template <typename F>
    void for_each_slot(F&& f) const {
        for (uint32_t packed : slots)
            if (packed != 0)
                f(packed);
        for (uint32_t packed : overflow())
            f(packed);
    }
    bool flag(NodeFlag f) const { return (flags & f) != 0; }
    void set_flag(NodeFlag f) { flags |= f; };
};
// Joins the parts with a space between them.
//
// The parts are views: a `std::string` per part would ask the C library for
// the length of every literal and would allocate a string for it.
std::string join(const std::vector<std::string_view>& parts) {
    std::string s;
    for (const auto& p : parts) {
        if (!s.empty())
            s += ' ';
        s += p;
    }
    return s;
}
// The stacks of the two walks of the writer.  They are reused between
// renders, so they do not have to be allocated for every formula.  They live
// in namespace scope objects, so that an access to them does not pay for the
// initialization guard of a `thread_local`: the guard is a call into the
// runtime, and the walk touches the stack for every node.
using MathMlStack = std::vector<std::pair<int, bool>>;
VELOCITEX_REUSED_STATE(MathMlStack, g_mathml_build_stack);
VELOCITEX_REUSED_STATE(MathMlStack, g_mathml_serialize_stack);

class Writer {
    const MathOptions& options;
    std::span<const SemanticNode> semantic;
    // The tree of the previous render is reused, so that the buffers do not
    // have to be allocated again for every formula.
    VELOCITEX_REUSED_MEMBER std::vector<MmlNode> nodes;
    VELOCITEX_REUSED_MEMBER std::vector<int> mapped, parents, sizes;
    VELOCITEX_REUSED_MEMBER std::vector<MathStyle> styles;
    // The colour of a semantic node: the index into the value pool, or zero
    // when the node has no colour of its own.
    VELOCITEX_REUSED_MEMBER std::vector<uint32_t> colors;
    // The strings the nodes and the colours refer to.
    VELOCITEX_REUSED_MEMBER ValuePool pool;
    // The node is moved into the vector, so the parameter is taken by
    // reference: a by-value parameter would copy it twice.
    int add(MmlNode&& n) {
        nodes.push_back(std::move(n));
        return int(nodes.size()) - 1;
    }
    MmlNode& ref(int id) { return nodes.at(id); }
    int unwrap(int id) { return id >= 0 && same_text(ref(id).Tag, "mrow") ? ref(id).First : id; }
    // The value of an attribute of a node; an empty optional means that the
    // node does not carry the attribute.
    std::optional<std::string_view> attribute_of(const MmlNode& node, Attr id) const {
        uint32_t slot = node.slot(id);
        if (!slot)
            return std::nullopt;
        return pool.get(slot & MmlNode::kValueMask);
    }
    // The text of a node.
    std::string_view content_of(const MmlNode& node) const {
        return node.content ? pool.get(node.content - 1) : std::string_view();
    }
    // The text a node contributes to its parent.
    std::string_view node_text(int id) { return content_of(ref(id)); }
    // A row that KaTeX would have built from an ordgroup.  KaTeX wraps
    // braced groups and array cells in an ordgroup (src/environments/array.ts);
    // the latter are stored as plain rows inside a styled matrix cell.
    bool is_ordgroup(int index) {
        if (semantic[index].Value == 1)
            return true;
        int parent = parents[index];
        if (parent < 0 || semantic[parent].Kind != NodeKind::Style)
            return false;
        int grand = parents[parent];
        return grand >= 0 && semantic[grand].Kind == NodeKind::MatrixCell;
    }
    // The body of a cell of the {CD} environment.  KaTeX builds these cells
    // as "styling" groups, whose body is a plain list of nodes, so a lone
    // operator inside them does not suppress its spacing
    // (`src/environments/cd.ts`).
    bool is_cd_cell_body(int index) {
        int style = parents[index];
        if (style < 0 || semantic[style].Kind != NodeKind::Style)
            return false;
        int cell = parents[style];
        if (cell < 0 || semantic[cell].Kind != NodeKind::MatrixCell)
            return false;
        int row = parents[cell];
        if (row < 0 || parents[row] < 0)
            return false;
        return eq(semantic[parents[row]].Text, u"CD"_u16);
    }
    // KaTeX's isNumberPunctuation (src/buildMathML.ts): <mi>.</mi> or a
    // braced comma rendered as <mo separator="true" ...>,</mo>.
    bool is_number_punctuation(int id) {
        if (id < 0)
            return false;
        const auto& item = ref(id);
        if (item.First >= 0)
            return false;
        if (same_text(item.Tag, "mi"))
            return same_text(node_text(id), ".");
        if (same_text(item.Tag, "mo")) {
            auto lspace = attribute_of(item, Attr::LSpace);
            auto rspace = attribute_of(item, Attr::RSpace);
            return item.flag(kSeparator) && lspace && same_text(*lspace, "0em") && rspace &&
                   same_text(*rspace, "0em") && same_text(node_text(id), ",");
        }
        return false;
    }
    // <mn>...</mn> or a number punctuation node.
    bool is_number(int id) { return (id >= 0 && same_text(ref(id).Tag, "mn")) || is_number_punctuation(id); }
    // Result of merging two neighbouring nodes, mirroring the rules in
    // KaTeX's buildExpression.
    enum class Merge { None, IntoLeft, IntoRight };
    Merge merge(int left, int right) {
        auto& a = ref(left);
        auto& b = ref(right);
        auto append = [&](int target, int source) {
            auto& t = ref(target);
            const auto& s = ref(source);
            std::string merged = std::string(content_of(t));
            merged += content_of(s);
            t.content = pool.intern(merged) + 1;
        };
        // Concatenate adjacent <mtext>s.
        // The two variants are compared as views: a comparison of two
        // `string_view`s against each other is a call into the C library,
        // because the length it compares is only known at run time.
        const auto variant_a = attribute_of(a, Attr::MathVariant);
        const auto variant_b = attribute_of(b, Attr::MathVariant);
        const bool same_variant = variant_a.has_value() == variant_b.has_value() &&
                                  (!variant_a || views_equal(*variant_a, *variant_b));
        if (same_text(a.Tag, "mtext") && same_text(b.Tag, "mtext") && same_variant &&
            !a.flag(kSpaceText) && !b.flag(kSpaceText)) {
            append(left, right);
            return Merge::IntoLeft;
        }
        // Concatenate adjacent <mn>s.
        if (same_text(a.Tag, "mn") && same_text(b.Tag, "mn")) {
            append(left, right);
            return Merge::IntoLeft;
        }
        // Concatenate <mn>...</mn> followed by <mi>.</mi>.
        if (same_text(a.Tag, "mn") && is_number_punctuation(right)) {
            append(left, right);
            return Merge::IntoLeft;
        }
        // Concatenate <mi>.</mi> followed by <mn>...</mn>; the <mn> wins
        // because it keeps its own attributes.
        if (same_text(b.Tag, "mn") && is_number_punctuation(left)) {
            auto& t = ref(right);
            std::string text(node_text(left));
            text += content_of(t);
            t.content = pool.intern(text) + 1;
            return Merge::IntoRight;
        }
        return Merge::None;
    }
    // KaTeX overlays the combining solidus of \not onto the following symbol
    // (src/buildMathML.ts).
    bool overlay_not(int last, int current) {
        if (last < 0 || current < 0)
            return false;
        if (!same_text(ref(last).Tag, "mi") || ref(last).First >= 0 ||
            !same_text(node_text(last), "\u0338"))
            return false;
        if (!oneof(ref(current).Tag, "mo", "mi", "mn"))
            return false;
        std::string_view text = node_text(current);
        if (text.empty())
            return false;
        // KaTeX inserts the solidus after the first character of the symbol.
        size_t head = utf8_head(text);
        std::string merged(text.substr(0, head));
        merged += "\u0338";
        merged.append(text.substr(head));
        ref(current).content = pool.intern(merged) + 1;
        return true;
    }
    int link(int first) {
        int result = -1, last = -1, previous = -1;
        for (int child = first; child >= 0; child = semantic[child].NextSibling) {
            int current = mapped[child];
            if (current < 0)
                throw std::logic_error("An unvisited semantic child reached MathML linking.");
            if (last >= 0) {
                auto merged = merge(last, current);
                if (merged == Merge::IntoLeft)
                    continue;
                if (merged == Merge::IntoRight) {
                    // `current` replaces `last` in the chain.
                    if (previous < 0)
                        result = current;
                    else
                        ref(previous).Next = current;
                    last = current;
                    continue;
                }
                if (overlay_not(last, current)) {
                    if (previous < 0)
                        result = current;
                    else
                        ref(previous).Next = current;
                    last = current;
                    continue;
                }
            }
            if (result < 0)
                result = current;
            else
                ref(last).Next = current;
            previous = last;
            last = current;
        }
        return result;
    }
    int fences(int content, int left, int right, bool unwrapping = false) {
        if ((left == 0 || left == '.') && (right == 0 || right == '.'))
            return content;
        int first = unwrapping && ref(content).flag(kOwnRow) ? ref(content).First : content, last = first;
        if (last >= 0)
            while (ref(last).Next >= 0)
                last = ref(last).Next;
        if (left != 0 && left != '.') {
            MmlNode n(pool, "mo", fence_cp(left));
            n.set(pool, Attr::Fence, "true");
            int fence = add(std::move(n));
            ref(fence).Next = first;
            first = fence;
            if (last < 0)
                last = fence;
        }
        if (right != 0 && right != '.') {
            MmlNode n(pool, "mo", fence_cp(right));
            n.set(pool, Attr::Fence, "true");
            int fence = add(std::move(n));
            if (last >= 0)
                ref(last).Next = fence;
            else
                first = fence;
        }
        MmlNode row(pool, "mrow");
        row.First = first;
        return add(std::move(row));
    }
    std::string content(int id) {
        std::string result;
        std::vector<int> stack{id};
        while (!stack.empty()) {
            int current = stack.back();
            stack.pop_back();
            auto& item = ref(current);
            if (item.Next >= 0)
                stack.push_back(item.Next);
            result += content_of(item);
            if (item.First >= 0)
                stack.push_back(item.First);
        }
        return result;
    }
    bool plain_name(int first) {
        if (first < 0)
            return true;
        if (semantic[first].Kind == NodeKind::Row)
            return plain_name(semantic[first].FirstChild);
        for (int child = first; child >= 0; child = semantic[child].NextSibling)
            if (semantic[child].Kind != NodeKind::Symbol && semantic[child].Kind != NodeKind::Space)
                return false;
        return true;
    }
    double length(TexLength value, int index) {
        return mathml_length(value, styles[index], uint8_t(sizes[index]), options);
    }
    // Mirrors KaTeX's SpaceNode (src/mathMLTree.ts): small widths become a
    // space character inside an <mtext>, everything else an <mspace/>.
    int space(double width) {
        Opt value;
        if (width >= .05555 && width <= .05556)
            value = "\u200a";
        else if (width >= .1666 && width <= .1667)
            value = "\u2009";
        else if (width >= .2222 && width <= .2223)
            value = "\u2005";
        else if (width >= .2777 && width <= .2778)
            value = "\u2005\u200a";
        else if (width >= -.05556 && width <= -.05555)
            value = "\u200a\u2063";
        else if (width >= -.1667 && width <= -.1666)
            value = "\u2009\u2063";
        else if (width >= -.2223 && width <= -.2222)
            value = "\u205f\u2063";
        else if (width >= -.2778 && width <= -.2777)
            value = "\u2005\u2063";
        MmlNode n(pool, value ? "mtext" : "mspace", value);
        n.set_flag(kSpaceText);
        if (!value) {
            n.set_em(pool, Attr::Width, width);
            n.set_flag(kSelfClosing);
        }
        return add(std::move(n));
    }
    int box(const SemanticNode& b, int) {
        int body = b.FirstChild >= 0 ? mapped[b.FirstChild] : add(MmlNode(pool, "mrow"));
        // The label stays a view: `value_or` would copy the string of the
        // optional, and it would build a string of its own for the fallback.
        const auto label_value = mml_text(b.Text);
        const std::string_view label = label_value ? std::string_view(*label_value) : std::string_view();
        MmlNode n(pool, "mpadded");
        n.First = body;
        if (same_text(label, "\\phantom")) {
            n.Tag = "mphantom";
            // KaTeX's `phantom` builder takes the body of the group, so the
            // children of the ordgroup become the children of the element.
            n.First = unwrap(link(b.FirstChild));
        } else if (same_text(label, "\\vphantom")) {
            MmlNode p(pool, "mphantom");
            p.First = unwrap(body);
            n.First = add(std::move(p));
            n.set(pool, Attr::Width, "0px");
        } else if (same_text(label, "\\smash")) {
            const auto selection_value = mml_text(b.Alignment);
            const std::string_view selection =
                selection_value ? std::string_view(*selection_value) : std::string_view("tb");
            // The set of the characters is a view, so that the search does
            // not ask the C library for the length of the literal.
            bool valid = selection.find_first_not_of(std::string_view("tb", 2)) == std::string_view::npos;
            if (valid && selection.find('t') != std::string_view::npos)
                n.set(pool, Attr::Height, "0px");
            if (valid && selection.find('b') != std::string_view::npos)
                n.set(pool, Attr::Depth, "0px");
        } else if (oneof(label, "\\mathllap", "\\mathclap", "\\mathrlap")) {
            // KaTeX inserts the attributes of a lap in this order
            // (src/functions/lap.ts).
            if (!same_text(label, "\\mathrlap"))
                n.set_ordered(pool, Attr::LSpace, same_text(label, "\\mathllap") ? "-1width" : "-0.5width");
            n.set_ordered(pool, Attr::Width, "0px");
        } else if (same_text(label, "\\raisebox")) {
            static constexpr const char* units[] = {"em", "ex", "mu", "pt", "bp", "pc", "in", "cm",
                                                    "mm", "dd", "cc", "nd", "nc", "sp", "px"};
            n.set_value(pool, Attr::VOffset,
                        pool.intern_number_with_unit(b.Length1.Number, 16, units[int(b.Length1.Unit)]));
        } else if (same_text(label, "\\vcenter")) {
            n.set(pool, Attr::Class, "vcenter");
            int padded = add(std::move(n));
            MmlNode row(pool, "mrow");
            row.First = padded;
            return add(std::move(row));
        } else if (same_text(label, "\\hbox")) {
            n.Tag = "mrow";
            n.First = link(b.FirstChild);
        } else if (same_text(label, "\\pmb")) {
            n.Tag = "mstyle";
            n.First = unwrap(link(b.FirstChild));
            n.set(pool, Attr::Style, "text-shadow: 0.02em 0.01em 0.04px");
        } else if (same_text(label, "cdparent"))
            n.Tag = "mrow";
        else if (same_text(label, "cdleft") || same_text(label, "cdright")) {
            MmlNode row(pool, "mrow");
            row.First = body;
            n.First = add(std::move(row));
            n.set(pool, Attr::Width, "0");
            if (same_text(label, "cdleft"))
                n.set(pool, Attr::LSpace, "-1width");
            n.set(pool, Attr::VOffset, "0.7em");
            int pad = add(std::move(n));
            MmlNode style(pool, "mstyle");
            style.First = pad;
            style.Style = MathStyle::Script;
            // KaTeX's `cdlabel` builder sets `displaystyle` before
            // `scriptlevel` (`src/environments/cd.ts`), which is the opposite
            // order of the `styling` builder.
            style.set_ordered(pool, Attr::DisplayStyle, "false");
            style.set_ordered(pool, Attr::ScriptLevel, "1");
            return add(std::move(style));
        } else
            return body;
        return add(std::move(n));
    }
    int enclose(const SemanticNode& e) {
        const auto label_value = mml_text(e.Text);
        const std::string_view label = label_value ? std::string_view(*label_value) : std::string_view();
        // The needle is a view, so that the search does not ask the C library
        // for the length of the literal.
        bool color_box = label.find(std::string_view("colorbox", 8)) != std::string_view::npos;
        MmlNode n(pool, color_box ? "mpadded" : "menclose");
        n.First = mapped[e.FirstChild];
        if (same_text(label, "\\cancel"))
            n.set(pool, Attr::Notation, "updiagonalstrike");
        else if (same_text(label, "\\bcancel"))
            n.set(pool, Attr::Notation, "downdiagonalstrike");
        else if (same_text(label, "\\xcancel"))
            n.set(pool, Attr::Notation, "updiagonalstrike downdiagonalstrike");
        else if (same_text(label, "\\phase"))
            n.set(pool, Attr::Notation, "phasorangle");
        else if (same_text(label, "\\sout"))
            n.set(pool, Attr::Notation, "horizontalstrike");
        else if (same_text(label, "\\fbox"))
            n.set(pool, Attr::Notation, "box");
        else if (same_text(label, "\\angl"))
            n.set(pool, Attr::Notation, "actuarial");
        if (color_box) {
            // KaTeX inserts the attributes of a colour box in this order
            // (src/functions/enclose.ts).
            n.set_ordered(pool, Attr::Width, "+6pt");
            n.set_ordered(pool, Attr::Height, "+6pt");
            n.set_ordered(pool, Attr::LSpace, "3pt");
            n.set_ordered(pool, Attr::VOffset, "3pt");
            if (same_text(label, "\\fcolorbox")) {
                // The border is assembled in a buffer of the stack, so that
                // the length inside it does not need a string of its own.
                char buffer[512];
                size_t count = 0;
                std::memcpy(buffer + count, "border: ", 8);
                count += 8;
                count += FormatEm(std::max(.04, options.MinRuleThickness), buffer + count);
                std::memcpy(buffer + count, " solid ", 7);
                count += 7;
                const std::string colour = mml_text(e.Text3).value_or("");
                std::memcpy(buffer + count, colour.data(), colour.size());
                count += colour.size();
                n.set_ordered_value(pool, Attr::Style, pool.intern(buffer, count));
            }
            if (const auto background = mml_text(e.Text2))
                n.set_ordered(pool, Attr::MathBackground, *background);
        } else if (const auto background = mml_text(e.Text2)) {
            n.set(pool, Attr::MathBackground, *background);
        }
        return add(std::move(n));
    }
    int table_attributes(int table, const SemanticNode& matrix) {
        // The two names stay views: `value_or` would copy the string of the
        // optional and would build a string of its own for the fallback.
        const auto env_value = mml_text(matrix.Text);
        const auto spec_value = mml_text(matrix.Alignment);
        const std::string_view env = env_value ? std::string_view(*env_value) : std::string_view("matrix");
        const std::string_view spec = spec_value ? std::string_view(*spec_value) : std::string_view();
        bool small = oneof(env, "smallmatrix", "subarray"),
             align = oneof(env, "aligned", "alignedat", "split", "align", "align*", "alignat", "alignat*"),
             gather = oneof(env, "gathered", "gather", "gather*"), cd = same_text(env, "CD"),
             cases = env.find(std::string_view("cases", 5)) != std::string_view::npos;
        ref(table).set_em(pool, Attr::RowSpacing,
                          small   ? .1
                          : cases ? .36
                                  : .16 + (matrix.Value2 > 0 ? matrix.Value2 - 1 : 0) + (align || gather || cd ? .09 : 0));
        std::vector<std::string_view> alignments, lines;
        bool previous = false;
        std::string notation;
        size_t start = 0, end = spec.size();
        if (start < end && (spec[start] == '|' || spec[start] == ':')) {
            notation += std::string_view("top ", 4);
            ++start;
        }
        if (start < end && (spec[end - 1] == '|' || spec[end - 1] == ':')) {
            notation += std::string_view("bottom ", 7);
            --end;
        }
        for (size_t i = start; i < end; ++i) {
            char c = spec[i];
            if (c == 'c' || c == 'l' || c == 'r') {
                alignments.push_back(c == 'c' ? "center" : c == 'r' ? "right" : "left");
                if (previous)
                    lines.push_back("none");
                previous = true;
            } else if ((c == '|' || c == ':') && previous) {
                lines.push_back(c == '|' ? "solid" : "dashed");
                previous = false;
            }
        }
        if (align || cases || cd || env.ends_with(std::string_view("matrix", 6)) ||
            env.ends_with(std::string_view("matrix*", 7))) {
            auto def = alignments.size() == 1 ? alignments[0] : "center";
            int columns = oneof(env, "alignedat", "alignat", "alignat*") ? int(matrix.Value) * 2 : matrix.ColumnCount;
            alignments.clear();
            for (int c = 0; c < (cases ? 2 : columns); ++c)
                alignments.push_back(cases ? "left" : align ? (c % 2 == 0 ? "right" : "left") : def);
        }
        if (alignments.empty())
            for (int c = 0; c < matrix.ColumnCount; ++c)
                alignments.push_back(align ? (c % 2 == 0 ? "right" : "left") : cases ? "left" : "center");
        if (!oneof(env, "smallmatrix", "equation", "equation*"))
            ref(table).set(pool, Attr::ColumnAlign, join(alignments));
        if (std::any_of(lines.begin(), lines.end(), [](auto& v) { return !same_text(v, "none"); }))
            ref(table).set(pool, Attr::ColumnLines, join(lines));
        if (oneof(env, "alignedat", "alignat", "alignat*") || gather)
            ref(table).set(pool, Attr::ColumnSpacing, "0em");
        else if (align) {
            std::vector<std::string_view> spacing;
            for (int c = 1; c < matrix.ColumnCount; ++c)
                spacing.push_back(c % 2 == 1 ? "0em" : "1em");
            ref(table).set(pool, Attr::ColumnSpacing, join(spacing));
        } else
            ref(table).set(pool, Attr::ColumnSpacing,
                           cd ? "0.5em" : same_text(env, "smallmatrix") ? "0.2778em" : "1em");
        lines.clear();
        int count = 0;
        for (int row = matrix.FirstChild; row >= 0; row = semantic[row].NextSibling) {
            if (count++ == 0) {
                if (semantic[row].RuleAbove != 0)
                    notation += std::string_view("left ", 5);
            } else
                lines.push_back(semantic[row].RuleAbove > 0   ? "solid"
                                : semantic[row].RuleAbove < 0 ? "dashed"
                                                              : "none");
        }
        if (std::any_of(lines.begin(), lines.end(), [](auto& v) { return !same_text(v, "none"); }))
            ref(table).set(pool, Attr::RowLines, join(lines));
        if (matrix.RuleBelow != 0)
            notation += std::string_view("right ", 6);
        if (!notation.empty()) {
            notation.pop_back();
            MmlNode n(pool, "menclose");
            n.First = table;
            n.set(pool, Attr::Notation, notation);
            table = add(std::move(n));
        }
        return table;
    }
    // Writes the script level of an <mstyle>.  KaTeX omits it when the
    // element carries a colour, a size or a style of its own.
    bool write_scriptlevel(html::MarkupBuffer& output, const MmlNode& node, bool mathcolor, bool mathsize, bool style) {
        if (!same_text(node.Tag, "mstyle") || mathcolor || mathsize || style)
            return false;
        attr(output, "scriptlevel",
             node.flag(kScriptLevelOnly)                                           ? "1"
             : node.Style == MathStyle::Display || node.Style == MathStyle::Text ? "0"
             : node.Style == MathStyle::Script                                ? "1"
                                                                                   : "2");
        if (!node.flag(kScriptLevelOnly))
            attr(output, "displaystyle", node.Style == MathStyle::Display ? "true" : "false");
        return true;
    }
    // Writes the attributes of a node in the order KaTeX uses them.  The node
    // keeps its attributes in the order they were set, so they are sorted by
    // their id first.
    void write_attributes(html::MarkupBuffer& output, const MmlNode& node, uint32_t* list, int count) {
        for (int i = 1; i < count; ++i) {
            uint32_t value = list[i];
            int j = i - 1;
            while (j >= 0 && (list[j] >> 24) > (value >> 24)) {
                list[j + 1] = list[j];
                --j;
            }
            list[j + 1] = value;
        }
        bool has_lspace = false, has_rspace = false, has_mathcolor = false, has_mathsize = false, has_style = false;
        bool separator_done = false, largeop_done = false, scriptlevel_done = false;
        for (int i = 0; i < count; ++i) {
            Attr id = Attr(list[i] >> 24);
            // The separator and the stretchy flag come right after the
            // variant.
            if (!separator_done && id > Attr::MathVariant) {
                separator_done = true;
                if (node.flag(kSeparator))
                    output.append(" separator=\"true\"", 17);
                if (node.flag(kNoStretch))
                    output.append(" stretchy=\"false\"", 17);
            }
            // The large operator flag and the suppressed spacing come after
            // the accent attributes (src/buildMathML.ts).
            if (!largeop_done && id > Attr::AccentUnder) {
                largeop_done = true;
                if (node.flag(kLargeOpFalse))
                    output.append(" largeop=\"false\"", 16);
                if (node.flag(kSuppressSpacing)) {
                    if (!has_lspace)
                        output.append(" lspace=\"0em\"", 13);
                    if (!has_rspace)
                        output.append(" rspace=\"0em\"", 13);
                }
            }
            std::string_view value = pool.get(list[i] & MmlNode::kValueMask);
            switch (id) {
            case Attr::LSpace:
                has_lspace = true;
                break;
            case Attr::RSpace:
                has_rspace = true;
                break;
            case Attr::MathColor:
                has_mathcolor = true;
                break;
            case Attr::MathSize:
                has_mathsize = true;
                break;
            case Attr::Style:
                has_style = true;
                break;
            default:
                break;
            }
            if (id == Attr::Class) {
                // KaTeX emits the class list after every attribute, with a
                // space before the equals sign (src/mathMLTree.ts).
                scriptlevel_done = write_scriptlevel(output, node, has_mathcolor, has_mathsize, has_style) ||
                                   scriptlevel_done;
                output.append(" class =\"", 9);
                escaped(output, value);
                output += '"';
            } else
                attr(output, attr_name(id), value);
        }
        if (!separator_done) {
            if (node.flag(kSeparator))
                output.append(" separator=\"true\"", 17);
            if (node.flag(kNoStretch))
                output.append(" stretchy=\"false\"", 17);
        }
        if (!largeop_done) {
            if (node.flag(kLargeOpFalse))
                output.append(" largeop=\"false\"", 16);
            if (node.flag(kSuppressSpacing)) {
                if (!has_lspace)
                    output.append(" lspace=\"0em\"", 13);
                if (!has_rspace)
                    output.append(" rspace=\"0em\"", 13);
            }
        }
        if (!scriptlevel_done)
            write_scriptlevel(output, node, has_mathcolor, has_mathsize, has_style);
    }
    void build(int index, int root);
    void serialize(html::MarkupBuffer& output, int root);

public:
    Writer(const MathOptions& o, std::span<const SemanticNode> s) : options(o), semantic(s) {}
    void write(std::u16string_view source, int root, bool annotation, html::MarkupBuffer& output) {
        // The buffers of the previous render are reused; their capacity stays
        // and only their content is dropped.
        nodes.clear();
        pool.clear();
        MmlNode::reset_overflows();
        // Every node carries a value or two, so the values of a render need
        // about this much room.
        pool.reserve(256 + semantic.size() * 24);
        mapped.assign(semantic.size(), -1);
        parents.assign(semantic.size(), -1);
        sizes.assign(semantic.size(), 0);
        styles.assign(semantic.size(), MathStyle::Text);
        colors.assign(semantic.size(), 0);
        nodes.reserve(semantic.size() * 2 + 8);
        sizes.at(root) = 6;
        styles[root] = options.DisplayMode ? MathStyle::Display : MathStyle::Text;
        // The stack of the walk is reused between renders, so it does not have
        // to be allocated for every formula.
        auto& stack = g_mathml_build_stack;
        stack.clear();
        stack.reserve(semantic.size() + 1);
        stack.emplace_back(root, false);
        while (!stack.empty()) {
            // The pair of the stack is unpacked by hand: a lambda cannot
            // capture a structured binding before clang 16, and the build that
            // collects the profile of the core (the script
            // tools/reference/generate-pgo-profile.mjs) uses a compiler that
            // predates it.  The binding of the source has to be the binding
            // that the profile was collected from, or the profile does not
            // match this function.
            const int index = stack.back().first;
            const bool visit = stack.back().second;
            stack.pop_back();
            const auto& n = semantic[index];
            if (visit) {
                build(index, root);
                continue;
            }
            stack.emplace_back(index, true);
            auto style = n.Kind == NodeKind::Style                                          ? n.Style
                         : n.Kind == NodeKind::Sizing && styles[index] >= MathStyle::Script ? MathStyle::Text
                                                                                            : styles[index];
            styles[index] = style;
            if (n.Kind == NodeKind::Sizing)
                sizes[index] = int(n.Value);
            if (n.Kind == NodeKind::Color)
                if (const auto color = mml_text(n.Text2))
                    colors[index] = pool.intern(*color);
            auto child = [&](int c) {
                if (c >= 0) {
                    parents[c] = index;
                    styles[c] = style;
                    sizes[c] = sizes[index];
                    colors[c] = colors[index];
                    stack.emplace_back(c, false);
                }
            };
            if (n.Kind == NodeKind::Row || n.Kind == NodeKind::Matrix || n.Kind == NodeKind::MatrixRow ||
                n.Kind == NodeKind::MatrixCell || n.Kind == NodeKind::Color || n.Kind == NodeKind::Sizing ||
                n.Kind == NodeKind::Class || n.Kind == NodeKind::Operator ||
                (n.Kind == NodeKind::Box && oneof(n.Text, u"\\phantom", u"\\hbox", u"\\pmb"))) {
                for (int c = n.FirstChild; c >= 0; c = semantic[c].NextSibling)
                    child(c);
                if (n.Kind == NodeKind::MatrixRow)
                    child(n.SecondChild);
            } else {
                child(n.FirstChild);
                child(n.SecondChild);
                child(n.ThirdChild);
                child(n.FourthChild);
            }
        }
        output.Reserve(256 + semantic.size() * 40 + source.size());
        output.append("<math xmlns=\"http://www.w3.org/1998/Math/MathML\"", 48);
        if (options.DisplayMode)
            output.append(" display=\"block\"", 16);
        output += '>';
        if (annotation)
            output.append("<semantics>", 11);
        serialize(output, mapped[root]);
        if (annotation) {
            output.append("<annotation encoding=\"application/x-tex\">", 41);
            escaped_utf8(output, source);
            output.append("</annotation></semantics>", 25);
        }
        output.append("</math>", 7);
    }
};
void Writer::build(int index, int root) {
    const auto& n = semantic[index];
    switch (n.Kind) {
    case NodeKind::Symbol: {
        if (!n.TextMode && !n.IsMathOrd && n.CodePoint == 32) {
            // KaTeX's `textord` builder uses an <mi> with the "normal"
            // variant for a space in math mode (`src/functions/symbolsOrd.ts`).
            mapped[index] = add(MmlNode(pool, "mi", " ", "normal"));
            break;
        }
        // The tag is a view into the literals below; the node outlives this
        // scope, so the name must not be a temporary string.
        const std::string_view tag = n.IsMathOrd ? "mi"
                          : ((n.TextMode && n.Atom == MathAtom::Ord) || n.CodePoint == 32 || n.CodePoint == 160)
                              ? "mtext"
                          : (n.Atom != MathAtom::Ord || n.CodePoint == 0x2032)                     ? "mo"
                          : (n.OriginalCodePoint == 0 && n.CodePoint >= '0' && n.CodePoint <= '9') ? "mn"
                                                                                                   : "mi";
        MmlNode item(pool, tag,
                     n.OriginalCodePoint > 0   ? n.OriginalCodePoint
                            : n.TextMode && n.CodePoint == 32 ? 160
                                                              : n.CodePoint,
                     n.CodePoint == 32 || n.CodePoint == 160 ? OptView{} : variant(n));
        if (n.Atom == MathAtom::Punct)
            item.set_flag(kSeparator);
        if (n.Atom == MathAtom::Open || n.Atom == MathAtom::Close)
            item.set_flag(kNoStretch);
        if (n.CodePoint == 0x2032 && parents[index] >= 0 && semantic[parents[index]].Kind == NodeKind::Row &&
            semantic[parents[index]].Value == 1 && semantic[parents[index]].FirstChild == index && n.NextSibling < 0)
            item.set_flag(kSuppressSpacing);
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Space: {
        MmlNode item(pool, "mspace");
        if (oneof(n.Text, u"\\nobreak", u"\\allowbreak")) {
            mapped[index] = add(std::move(item));
            break;
        }
        if (eq(n.Text, u"\\\\"_u16)) {
            if (!options.DisplayMode || options.StrictMode != MathStrictMode::Error)
                item.set(pool, Attr::LineBreak, "newline");
            if (n.Length1.Number != 0)
                item.set_em(pool, Attr::Height, length(n.Length1, index));
            mapped[index] = add(std::move(item));
            break;
        }
        if (oneof(n.Text, u"\\kern", u"\\mkern", u"\\hskip", u"\\mskip")) {
            mapped[index] = space(length(n.Length1, index));
            break;
        }
        // The remaining spacing commands (\, \: \; \! \quad \qquad and the
        // operator name spaces) carry their width in Value.
        mapped[index] = space(n.Value);
        break;
    }
    case NodeKind::Row: {
        int first = -1, last = -1, count = 0;
        for (int child = n.FirstChild; child >= 0; child = semantic[child].NextSibling) {
            int current = mapped[child];
            if (last >= 0) {
                auto merged = merge(last, current);
                if (merged == Merge::IntoLeft)
                    continue;
                if (merged == Merge::IntoRight) {
                    // `current` takes the place of `last` in the row.
                    if (first == last)
                        first = current;
                    else {
                        int before = first;
                        while (ref(before).Next != last)
                            before = ref(before).Next;
                        ref(before).Next = current;
                    }
                    last = current;
                    continue;
                }
            }
            if (last >= 0 && (same_text(ref(current).Tag, "msup") || same_text(ref(current).Tag, "msub")) && is_number(last) &&
                same_text(ref(ref(current).First).Tag, "mn")) {
                int base_id = ref(current).First;
                std::string prefix = std::string(content_of(ref(last)));
                prefix += content_of(ref(base_id));
                ref(base_id).content = pool.intern(prefix) + 1;
                if (first == last)
                    first = last = current;
                else {
                    int before = first;
                    while (ref(before).Next != last)
                        before = ref(before).Next;
                    ref(before).Next = current;
                    last = current;
                }
                continue;
            }
            if (last >= 0 && overlay_not(last, current)) {
                // The combining solidus disappears into `current`.
                if (first == last)
                    first = current;
                else {
                    int before = first;
                    while (ref(before).Next != last)
                        before = ref(before).Next;
                    ref(before).Next = current;
                }
                last = current;
                continue;
            }
            if (first < 0)
                first = current;
            else
                ref(last).Next = current;
            last = current;
            ++count;
        }
        bool literal = true;
        if (parents[index] >= 0) {
            const auto& owner = semantic[parents[index]];
            if (owner.Kind == NodeKind::Choice || owner.Kind == NodeKind::MathMlChoice ||
                (owner.Kind == NodeKind::Box && oneof(owner.Text, u"\\vphantom", u"\\phantom")) ||
                (owner.Kind == NodeKind::Class && !oneof(owner.Text, u"\\boldsymbol", u"\\bm")))
                literal = false;
        }
        // KaTeX suppresses the spacing around a lone operator inside an
        // ordgroup (src/buildMathML.ts).  The body of \mathop is unwrapped by
        // ordargument, so such a row is not an ordgroup for KaTeX, and the
        // body of a {CD} cell is a plain list of nodes for KaTeX as well.
        if (count == 1 && is_ordgroup(index) && literal && !n.TextMode && same_text(ref(first).Tag, "mo") &&
            (parents[index] < 0 || semantic[parents[index]].Kind != NodeKind::Operator) &&
            !is_cd_cell_body(index))
            ref(first).set_flag(kSuppressSpacing);
        // KaTeX wraps the top level expression in an <mrow> unless it already
        // is a single <mrow> or <mtable> (src/buildMathML.ts).
        if (count == 1 && (index != root || same_text(ref(first).Tag, "mrow") || same_text(ref(first).Tag, "mtable")))
            mapped[index] = first;
        else {
            MmlNode item(pool, "mrow");
            item.First = first;
            item.set_flag(kOwnRow);
            mapped[index] = add(std::move(item));
        }
        break;
    }
    case NodeKind::Font: {
        int child = mapped[n.FirstChild];
        if (n.Font == MathFont::MathBoldItalic) {
            int atom = n.FirstChild;
            if (semantic[atom].Kind == NodeKind::Row && semantic[atom].FirstChild >= 0)
                atom = semantic[atom].FirstChild;
            auto tag = semantic[atom].Atom == MathAtom::Bin || semantic[atom].Atom == MathAtom::Rel ? "mo" : "mi";
            bool character = oneof(ref(child).Tag, "mi", "mn", "mo") && ref(child).First < 0;
            if (character) {
                ref(child).Tag = tag;
                mapped[index] = child;
            } else {
                MmlNode item(pool, tag);
                item.First = child;
                mapped[index] = add(std::move(item));
            }
            if (semantic[atom].Atom == MathAtom::Bin) {
                ref(mapped[index]).set(pool, Attr::LSpace, "0.22em");
                ref(mapped[index]).set(pool, Attr::RSpace, "0.22em");
            }
        } else
            mapped[index] = child;
        break;
    }
    case NodeKind::Text:
        mapped[index] = mapped[n.FirstChild];
        break;
    case NodeKind::Style: {
        int child = mapped[n.FirstChild], parent = parents[index];
        bool cd_cell = parent >= 0 && semantic[parent].Kind == NodeKind::MatrixCell && parents[parent] >= 0 &&
                       parents[parents[parent]] >= 0 && eq(semantic[parents[parents[parent]]].Text, u"CD"_u16);
        if (cd_cell && semantic[n.FirstChild].Kind == NodeKind::Row && semantic[n.FirstChild].FirstChild < 0)
            child = -1;
        if (child >= 0 && semantic[n.FirstChild].Kind == NodeKind::Row && semantic[n.FirstChild].Value == 0 &&
            same_text(ref(child).Tag, "mrow") && (parent < 0 || semantic[parent].Kind != NodeKind::MatrixCell) &&
            (semantic[n.FirstChild].FirstChild < 0 || semantic[semantic[n.FirstChild].FirstChild].NextSibling >= 0))
            child = ref(child).First;
        MmlNode item(pool, "mstyle");
        item.First = child;
        item.Style = n.Style;
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Fraction: {
        int numerator = mapped[n.FirstChild], denominator = mapped[n.SecondChild];
        ref(numerator).Next = denominator;
        MmlNode item(pool, "mfrac");
        item.First = numerator;
        if (!n.HasBar)
            item.set(pool, Attr::LineThickness, "0px");
        else if (n.Value > 0)
            item.set_em(pool, Attr::LineThickness,
                        eq(n.Text, u"\\genfrac"_u16) ? length(n.Length1, index) : n.Value);
        int fraction = add(std::move(item));
        mapped[index] = fences(fraction, n.LeftDelimiter, n.RightDelimiter);
        break;
    }
    case NodeKind::Radical: {
        int body = mapped[n.FirstChild];
        if (n.SecondChild >= 0)
            ref(body).Next = mapped[n.SecondChild];
        MmlNode item(pool, n.SecondChild >= 0 ? "mroot" : "msqrt");
        item.First = body;
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Script: {
        int base = mapped[n.FirstChild], last = base;
        if (n.ThirdChild >= 0) {
            int sub = mapped[n.ThirdChild];
            ref(last).Next = sub;
            last = sub;
        }
        if (n.SecondChild >= 0)
            ref(last).Next = mapped[n.SecondChild];
        const auto& op = semantic[n.FirstChild];
        bool limits =
            op.Kind == NodeKind::Operator &&
            (eq(op.Text, u"\\operatorname"_u16)
                 ? (eq(op.Text2, u"star"_u16) &&
                    (styles[index] == MathStyle::Display || (op.AlwaysLimits && !op.NeverLimits)))
                 : (!op.NeverLimits && (((op.Limits || op.AlwaysLimits) && styles[index] == MathStyle::Display) ||
                                        (op.AlwaysLimits && (n.SecondChild < 0 || n.ThirdChild < 0)))));
        bool brace = op.Kind == NodeKind::Accent &&
                     ((oneof(op.Text, u"\\overbrace", u"\\overbracket") && n.ThirdChild < 0) ||
                      (oneof(op.Text, u"\\underbrace", u"\\underbracket") && n.SecondChild < 0));
        const std::string_view tag = limits || brace ? (n.SecondChild < 0  ? "munder"
                                             : n.ThirdChild < 0 ? "mover"
                                                                : "munderover")
                                          : (n.SecondChild < 0  ? "msub"
                                             : n.ThirdChild < 0 ? "msup"
                                                                : "msubsup");
        MmlNode item(pool, tag);
        item.First = base;
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Operator: {
        if (n.CodePoint > 0) {
            MmlNode item(pool, "mo", n.CodePoint);
            if (eq(n.Text, u"\\smallint"_u16))
                item.set_flag(kLargeOpFalse);
            mapped[index] = add(std::move(item));
        } else if (eq(n.Text, u"\\mathop"_u16)) {
            MmlNode item(pool, "mo");
            item.First = unwrap(link(n.FirstChild));
            mapped[index] = add(std::move(item));
        } else {
            bool name = oneof(n.Text, u"\\operatorname@", u"\\operatornamewithlimits", u"\\operatorname", u"\\operatorname*", u"\\liminf", u"\\limsup");
            Opt value;
            if (eq(n.Text, u"\\liminf"_u16))
                value = "lim\u2009inf";
            else if (eq(n.Text, u"\\limsup"_u16))
                value = "lim\u2009sup";
            else if (name) {
                value = content(mapped[n.FirstChild]);
                for (auto pair : {std::pair<std::string, std::string>{"\u2212", "-"}, {"\u2217", "*"}}) {
                    size_t p = 0;
                    while ((p = value->find(pair.first, p)) != std::string::npos) {
                        value->replace(p, pair.first.size(), pair.second);
                        p += pair.second.size();
                    }
                }
            } else {
                value = mml_text(n.Text);
                if (value) {
                    auto p = value->find_first_not_of('\\');
                    *value = p == std::string::npos ? "" : value->substr(p);
                }
            }
            bool plain = !name || plain_name(n.FirstChild);
            MmlNode identifier(pool, "mi", plain ? value : Opt{}, name ? OptView("normal") : OptView{});
            identifier.First = plain ? -1 : unwrap(mapped[n.FirstChild]);
            int id = add(std::move(identifier)), apply = add(MmlNode(pool, "mo", "\u2061"));
            ref(id).Next = apply;
            MmlNode item(pool, parents[index] >= 0 && semantic[parents[index]].Kind == NodeKind::Script ? "mrow"
                                                                                                       : "#fragment");
            item.First = id;
            mapped[index] = add(std::move(item));
        }
        break;
    }
    case NodeKind::Delimiter: {
        MmlNode item(pool, "mo");
        if (n.CodePoint == 0 || n.CodePoint == '.')
            item.set_content(pool, std::string_view());
        else
            item.set_content_code_point(pool, n.Value == 0 && n.CodePoint == '|' ? '|' : fence_cp(n.CodePoint));
        item.set(pool, Attr::Fence,
                 n.Value == 0 || n.Atom == MathAtom::Open || n.Atom == MathAtom::Close ? "true" : "false");
        if (n.Value > 0) {
            item.set(pool, Attr::Stretchy, "true");
            const double size = n.Value == 1 ? 1.2 : n.Value == 2 ? 1.8 : n.Value == 3 ? 2.4 : 3;
            item.set_em(pool, Attr::MinSize, size);
            item.set_em(pool, Attr::MaxSize, size);
        }
        if (n.Value == 0) {
            item.set(pool, Attr::LSpace, "0.05em");
            item.set(pool, Attr::RSpace, "0.05em");
        }
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Delimited:
        // KaTeX puts the fences next to the body elements, so the body's own
        // <mrow> is flattened (src/functions/delimsizing.ts).
        mapped[index] = fences(mapped[n.FirstChild], n.LeftDelimiter, n.RightDelimiter, true);
        break;
    case NodeKind::Accent: {
        auto label = mml_text(n.Text).value_or("");
        if (label.starts_with("\\x") || label.starts_with("\\\\cd")) {
            MmlNode arrow(pool, "mo", accent_cp(label));
            arrow.set(pool, Attr::Stretchy, "true");
            arrow.set(pool, Attr::MinSize, "3.0em");
            int a = add(std::move(arrow));
            MmlNode upper(pool, "mpadded");
            upper.First = n.FirstChild < 0 ? -1 : mapped[n.FirstChild];
            upper.set(pool, Attr::Width, "+0.6em");
            upper.set(pool, Attr::LSpace, "0.3em");
            int u = add(std::move(upper));
            if (n.SecondChild >= 0) {
                MmlNode lower(pool, "mpadded");
                lower.First = mapped[n.SecondChild];
                lower.set(pool, Attr::Width, "+0.6em");
                lower.set(pool, Attr::LSpace, "0.3em");
                int l = add(std::move(lower));
                ref(a).Next = l;
                ref(l).Next = u;
            } else
                ref(a).Next = u;
            MmlNode item(pool, n.SecondChild >= 0 ? "munderover" : "mover");
            item.First = a;
            mapped[index] = add(std::move(item));
        } else {
            bool under = label.starts_with("\\under") || same_text(label, "\\utilde"),
                 brace = oneof(label, "\\overbrace", "\\underbrace", "\\overbracket", "\\underbracket");
            bool stretchy = !oneof(label, "\\acute", "\\grave", "\\ddot", "\\tilde", "\\bar", "\\breve", "\\check", "\\hat", "\\vec", "\\dot", "\\mathring", "\\'", "\\`", "\\^", "\\~", "\\=", "\\u", "\\.", "\\\"", "\\c", "\\r", "\\H", "\\v", "\\textcircled");
            MmlNode accent(pool, "mo");
            if (oneof(label, "\\overlinesegment", "\\underlinesegment"))
                accent.set_content(pool, std::string_view("undefined"));
            else
                accent.set_content_code_point(pool, accent_cp(label, n.CodePoint));
            if (stretchy)
                accent.set(pool, Attr::Stretchy, "true");
            int a = add(std::move(accent)), body = mapped[n.FirstChild];
            ref(body).Next = a;
            MmlNode item(pool, under ? "munder" : "mover");
            item.First = body;
            if (!under && !brace)
                item.set(pool, Attr::Accent, "true");
            if (under && !brace)
                item.set(pool, Attr::AccentUnder, "true");
            mapped[index] = add(std::move(item));
        }
        break;
    }
    case NodeKind::MatrixCell: {
        MmlNode item(pool, "mtd");
        item.First = mapped[n.FirstChild];
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::MatrixRow: {
        int children = link(n.FirstChild);
        if (n.SecondChild >= 0 || (n.AlwaysLimits && !n.NeverLimits)) {
            MmlNode glue(pool, "mtd");
            glue.set(pool, Attr::Class, "mtr-glue");
            int left = add(MmlNode(glue)), right = add(std::move(glue));
            MmlNode tag(pool, "mtd");
            tag.set(pool, Attr::Class, "mml-eqn-num");
            int eq_tag = add(std::move(tag)), last = children;
            if (last >= 0)
                while (ref(last).Next >= 0)
                    last = ref(last).Next;
            ref(left).Next = children;
            if (last >= 0)
                ref(last).Next = right;
            else
                ref(left).Next = right;
            if (options.Leqno) {
                ref(eq_tag).Next = left;
                children = eq_tag;
            } else {
                ref(right).Next = eq_tag;
                children = left;
            }
        }
        MmlNode item(pool, "mtr");
        item.First = children;
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Matrix: {
        MmlNode item(pool, "mtable");
        item.First = link(n.FirstChild);
        int table = table_attributes(add(std::move(item)), n);
        if (oneof(n.Text, u"smallmatrix", u"subarray")) {
            MmlNode style(pool, "mstyle");
            style.First = table;
            style.set_flag(kScriptLevelOnly);
            table = add(std::move(style));
        }
        mapped[index] = fences(table, n.LeftDelimiter, n.RightDelimiter);
        break;
    }
    case NodeKind::Color: {
        // KaTeX's `color` builder takes the body of the group, so the
        // children of the ordgroup become the children of the <mstyle>
        // element instead of being wrapped in an <mrow>.  A body with a
        // single group keeps its own <mrow>.
        int first = link(n.FirstChild);
        if (n.FirstChild >= 0 && semantic[n.FirstChild].Kind == NodeKind::Row) {
            const SemanticNode& row = semantic[n.FirstChild];
            if (row.FirstChild < 0)
                first = -1;
            else if (semantic[row.FirstChild].NextSibling >= 0)
                first = unwrap(first);
        }
        MmlNode item(pool, "mstyle");
        item.First = first;
        item.set_optional(pool, Attr::MathColor, mml_text(n.Text2));
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Sizing: {
        MmlNode item(pool, "mstyle");
        item.First = unwrap(link(n.FirstChild));
        item.set_em(pool, Attr::MathSize, mathml_size_scale(int(n.Value)));
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Choice:
        mapped[index] = mapped[styles[index] == MathStyle::Display  ? n.FirstChild
                               : styles[index] == MathStyle::Text   ? n.SecondChild
                               : styles[index] == MathStyle::Script ? n.ThirdChild
                                                                    : n.FourthChild];
        break;
    case NodeKind::MathMlChoice:
        mapped[index] = mapped[n.SecondChild];
        break;
    case NodeKind::Box:
        mapped[index] = box(n, index);
        break;
    case NodeKind::Class: {
        int first = link(n.FirstChild), declared = n.FirstChild;
        if (declared >= 0 && semantic[declared].Kind == NodeKind::Row && semantic[declared].FirstChild >= 0 &&
            semantic[semantic[declared].FirstChild].NextSibling < 0)
            declared = semantic[declared].FirstChild;
        bool plain =
            declared >= 0 && (semantic[declared].Kind == NodeKind::Row || semantic[declared].Kind == NodeKind::Symbol);
        if (!oneof(n.Text, u"\\boldsymbol", u"\\bm") && plain)
            first = unwrap(first);
        bool character =
            first >= 0 && ref(first).Next < 0 && oneof(ref(first).Tag, "mi", "mo", "mn") && ref(first).First < 0;
        if (declared >= 0 && semantic[declared].Kind == NodeKind::MathMlChoice)
            character = false;
        auto tag = n.Atom == MathAtom::Inner ? "mpadded" : n.Atom == MathAtom::Ord ? "mi" : "mo";
        if (character && n.Atom != MathAtom::Inner) {
            ref(first).Tag = tag;
            mapped[index] = first;
        } else {
            MmlNode item(pool, tag);
            item.First = first;
            mapped[index] = add(std::move(item));
        }
        auto& item = ref(mapped[index]);
        if (n.Atom == MathAtom::Bin) {
            item.set(pool, Attr::LSpace, "0.22em");
            item.set(pool, Attr::RSpace, "0.22em");
        } else if (n.Atom == MathAtom::Punct) {
            item.set(pool, Attr::LSpace, "0em");
            item.set(pool, Attr::RSpace, "0.17em");
        } else if ((n.Atom == MathAtom::Open || n.Atom == MathAtom::Close) && !eq(n.Text, u"\\html@mathml"_u16)) {
            item.set(pool, Attr::LSpace, "0em");
            item.set(pool, Attr::RSpace, "0em");
        }
        break;
    }
    case NodeKind::Rule: {
        // KaTeX inserts mathbackground, width and height in that order
        // (src/functions/rule.ts).
        MmlNode rule(pool, "mspace");
        rule.set_ordered(pool, Attr::MathBackground,
                         colors[index] ? std::string(pool.get(colors[index])) : std::string("black"));
        rule.set_ordered_value(pool, Attr::Width, pool.intern_em(length(n.Length1, index)));
        rule.set_ordered_value(pool, Attr::Height, pool.intern_em(length(n.Length2, index)));
        int r = add(std::move(rule));
        double shift = length(n.Length3, index);
        MmlNode item(pool, "mpadded");
        item.First = r;
        item.set_em(pool, Attr::Height, shift);
        if (shift < 0)
            item.set_em(pool, Attr::Depth, -shift);
        item.set_em(pool, Attr::VOffset, shift);
        mapped[index] = add(std::move(item));
        break;
    }
    case NodeKind::Enclose:
        mapped[index] = enclose(n);
        break;
    case NodeKind::Verb: {
        auto value = mml_text(n.Text2 ? n.Text2 : n.Text).value_or("");
        std::string replaced;
        for (char c : value) {
            if (c == ' ')
                replaced += n.Value != 0 ? "\u2423" : "\u00a0";
            else
                replaced += c;
        }
        mapped[index] = add(MmlNode(pool, "mtext", replaced, "monospace"));
        break;
    }
    case NodeKind::Tag: {
        int body = mapped[n.FirstChild], label = n.SecondChild >= 0 ? mapped[n.SecondChild] : add(MmlNode(pool, "mrow"));
        MmlNode pad(pool, "mtd");
        pad.set(pool, Attr::Width, "50%");
        int left = add(MmlNode(pad)), right = add(std::move(pad));
        MmlNode cell(pool, "mtd");
        cell.First = body;
        int formula = add(std::move(cell));
        MmlNode tag(pool, "mtd");
        tag.First = label;
        int tag_cell = add(std::move(tag));
        ref(left).Next = formula;
        ref(formula).Next = right;
        ref(right).Next = tag_cell;
        MmlNode row(pool, "mtr");
        row.First = left;
        int r = add(std::move(row));
        MmlNode table(pool, "mtable");
        table.First = r;
        table.set(pool, Attr::Width, "100%");
        mapped[index] = add(std::move(table));
        break;
    }
    }
}
void Writer::serialize(html::MarkupBuffer& output, int root) {
    // The stack of the walk is reused between renders, so it does not have to
    // be allocated for every formula.
    auto& tasks = g_mathml_serialize_stack;
    tasks.clear();
    tasks.reserve(nodes.size() + 1);
    tasks.emplace_back(root, false);
    while (!tasks.empty()) {
        auto [index, close] = tasks.back();
        tasks.pop_back();
        const auto& n = ref(index);
        if (close) {
            output.append("</", 2);
            output += n.Tag;
            output += '>';
            continue;
        }
        if (n.Next >= 0)
            tasks.emplace_back(n.Next, false);
        if (same_text(n.Tag, "#text")) {
            if (n.content)
                escaped(output, content_of(n));
            continue;
        }
        if (same_text(n.Tag, "#fragment")) {
            if (n.First >= 0)
                tasks.emplace_back(n.First, false);
            continue;
        }
        output += '<';
        output += n.Tag;
        if (n.flag(kOrdered)) {
            // The attributes are written in the exact order KaTeX inserts them.
            uint32_t deferred = 0;
            n.for_each_slot([&](uint32_t slot) {
                if ((slot >> 24) == uint32_t(Attr::Class))
                    deferred = slot;
                else
                    attr(output, attr_name(Attr(slot >> 24)), pool.get(slot & MmlNode::kValueMask));
            });
            // KaTeX emits the class list after every attribute, with a space
            // before the equals sign (src/mathMLTree.ts).
            if (deferred) {
                output.append(" class =\"", 9);
                escaped(output, pool.get(deferred & MmlNode::kValueMask));
                output += '"';
            }
        } else {
            uint32_t list[MmlNode::kAttrCapacity];
            int count = 0;
            for (uint32_t slot : n.slots)
                if (slot != 0)
                    list[count++] = slot;
            if (n.overflow().empty())
                write_attributes(output, n, list, count);
            else {
                // A node with more attributes than the slots hold is rare; the
                // attributes are collected in a vector then.
                std::vector<uint32_t> all(list, list + count);
                const auto& extra = n.overflow();
                all.insert(all.end(), extra.begin(), extra.end());
                write_attributes(output, n, all.data(), int(all.size()));
            }
        }
        if (n.flag(kSelfClosing)) {
            output.append("/>", 2);
            continue;
        }
        output += '>';
        if (n.content)
            escaped(output, content_of(n));
        tasks.emplace_back(index, true);
        if (n.First >= 0)
            tasks.emplace_back(n.First, false);
    }
}
} // namespace
void write_mathml(std::u16string_view source, const MathOptions& options, std::span<const SemanticNode> semantic,
                  int root, bool include_annotation, html::MarkupBuffer& out) {
    Writer(options, semantic).write(source, root, include_annotation, out);
}
} // namespace velocitex
