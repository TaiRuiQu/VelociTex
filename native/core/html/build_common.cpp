// KaTeX-compatible HTML building helpers.
//
// Port of KaTeX 0.19.0 `src/buildCommon.ts`.
#include "build_common.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "../generated/data.hpp"
#include "jsnum.hpp"

namespace velocitex::html {
namespace {

// KaTeX's `ligatures` table (only used for typewriter fonts).
bool IsTypewriterLigature(std::u16string_view text) {
    static constexpr std::u16string_view kLigatures[] = {
        u"<<"_u16, u">>"_u16, u"\\["_u16, u"\\]"_u16, u"\\{"_u16, u"\\}"_u16, u"\\|"_u16,
        u"\\~"_u16, u"\\^"_u16, u"\\`"_u16,
    };
    for (std::u16string_view ligature : kLigatures) {
        if (text == ligature) {
            return true;
        }
    }
    return false;
}

// KaTeX's `wideCharacterFont` (surrogate pairs for mathematical alphanumerics).
struct WideCharacterFont {
    FontId Font;
    const char* MathClass;
    const char* TextClass;
};

// KaTeX's `wideCharacterFont` (`src/wide-character.ts`).  The block of
// mathematical alphanumeric symbols is laid out in rows of 26 letters (and
// rows of ten numerals); every row shares one font and one CSS class.  The
// function returns `nullptr` for characters outside the supported range, and
// the `noFont` entry for the rows that KaTeX does not render with a font.
const WideCharacterFont* WideCharacterFontOf(std::u16string_view text) {
    if (text.size() < 2 || text[0] < 0xD800 || text[0] > 0xDBFF || text[1] < 0xDC00 || text[1] > 0xDFFF) {
        return nullptr;
    }
    const int code = 0x10000 + ((text[0] - 0xD800) << 10) + (text[1] - 0xDC00);
    static const WideCharacterFont kBoldUpright{FontId::MainBold, "mathbf", "textbf"};
    static const WideCharacterFont kItalic{FontId::MathItalic, "mathnormal", "textit"};
    static const WideCharacterFont kBoldItalic{FontId::MainBoldItalic, "boldsymbol", "boldsymbol"};
    static const WideCharacterFont kScript{FontId::ScriptRegular, "mathscr", "textscr"};
    static const WideCharacterFont kNoFont{FontId::Unknown, "", ""};
    static const WideCharacterFont kFraktur{FontId::FrakturRegular, "mathfrak", "textfrak"};
    static const WideCharacterFont kDoubleStruck{FontId::AmsRegular, "mathbb", "textbb"};
    static const WideCharacterFont kBoldFraktur{FontId::FrakturRegular, "mathboldfrak", "textboldfrak"};
    static const WideCharacterFont kSansSerif{FontId::SansSerifRegular, "mathsf", "textsf"};
    static const WideCharacterFont kBoldSansSerif{FontId::SansSerifBold, "mathboldsf", "textboldsf"};
    static const WideCharacterFont kItalicSansSerif{FontId::SansSerifItalic, "mathitsf", "textitsf"};
    static const WideCharacterFont kMonospace{FontId::TypewriterRegular, "mathtt", "texttt"};
    static const WideCharacterFont kLatin[26] = {
        kBoldUpright,      kBoldUpright,        // A-Z, a-z
        kItalic,           kItalic,             // A-Z, a-z
        kBoldItalic,       kBoldItalic,         // A-Z, a-z
        kScript,           kNoFont,             // A-Z script, a-z (no font)
        kNoFont,           kNoFont,             // A-Z bold script (no font)
        kFraktur,          kFraktur,            // A-Z, a-z
        kDoubleStruck,     kDoubleStruck,       // A-Z double struck, k double struck
        kBoldFraktur,      kBoldFraktur,        // A-Z, a-z
        kSansSerif,        kSansSerif,          // A-Z, a-z
        kBoldSansSerif,    kBoldSansSerif,      // A-Z, a-z
        kItalicSansSerif,  kItalicSansSerif,   // A-Z, a-z
        kNoFont,           kNoFont,             // A-Z bold italic sans (no font)
        kMonospace,        kMonospace,          // A-Z, a-z
    };
    static const WideCharacterFont kNumerals[5] = {
        kBoldUpright,   // 0-9
        kNoFont,        // 0-9 double struck (no font)
        kSansSerif,     // 0-9
        kBoldSansSerif, // 0-9
        kMonospace,     // 0-9
    };
    if (code >= 0x1D400 && code < 0x1D6A4) {
        return &kLatin[(code - 0x1D400) / 26];
    }
    if (code >= 0x1D7CE && code <= 0x1D7FF) {
        return &kNumerals[(code - 0x1D7CE) / 10];
    }
    if (code == 0x1D6A5 || code == 0x1D6A6) {
        // Dotless i and j.
        return &kBoldUpright;
    }
    if (code > 0x1D6A6 && code < 0x1D7CE) {
        // Greek letters. Not supported, yet.
        return &kNoFont;
    }
    return nullptr;
}

// Writes the UTF-8 of a text into a buffer and returns its length.
//
// A symbol is a single code point, so the buffer of the caller is enough for
// it and the text never needs a string of its own.
size_t ToUtf8Into(char* buffer, std::u16string_view text) {
    size_t length = 0;
    for (size_t i = 0; i < text.size(); ++i) {
        int cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            buffer[length++] = static_cast<char>(cp);
        } else if (cp < 0x800) {
            buffer[length++] = static_cast<char>(0xC0 | (cp >> 6));
            buffer[length++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            buffer[length++] = static_cast<char>(0xE0 | (cp >> 12));
            buffer[length++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            buffer[length++] = static_cast<char>(0xF0 | (cp >> 18));
            buffer[length++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            buffer[length++] = static_cast<char>(0x80 | (cp & 0x3F));
        }
    }
    return length;
}

std::string ToUtf8(std::u16string_view text) {
    std::string out;
    for (size_t i = 0; i < text.size(); ++i) {
        int cp = text[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 1 < text.size() && text[i + 1] >= 0xDC00 &&
            text[i + 1] <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (text[i + 1] - 0xDC00);
            ++i;
        }
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }
    return out;
}

// KaTeX compares the joined class names of two symbols.  The names of a list
// are compared one by one, which is the same as comparing the joined forms.
bool SameClasses(const ClassList& left, const ClassList& right) {
    if (left.Size() != right.Size()) {
        return false;
    }
    for (size_t i = 0; i < left.Size(); ++i) {
        if (left.At(i) != right.At(i)) {
            return false;
        }
    }
    return true;
}

bool CanCombine(const HtmlSymbol& previous, const HtmlSymbol& next) {
    if (!SameClasses(previous.Classes, next.Classes) ||
        previous.SkewValue() != next.SkewValue() ||
        previous.MaxFontSize != next.MaxFontSize ||
        (previous.ItalicValue() != 0.0 && previous.HasClass("mathnormal"))) {
        return false;
    }
    if (previous.Classes.Size() == 1) {
        const std::string_view cls = previous.Classes.Front();
        if (same_text(cls, "mbin") || same_text(cls, "mord")) {
            return false;
        }
    }
    const std::optional<std::string_view> previous_style = previous.Style.Find("color");
    const std::optional<std::string_view> next_style = next.Style.Find("color");
    if (previous_style.has_value() != next_style.has_value()) {
        return false;
    }
    if (previous_style.has_value() && *previous_style != *next_style) {
        return false;
    }
    return true;
}

}  // namespace

void SizeElementFromChildren(HtmlNode& element) {
    double height = 0.0;
    double depth = 0.0;
    double max_font_size = 0.0;
    for (const auto& child : element.Children()) {
        height = std::max(height, child->Height);
        depth = std::max(depth, child->Depth);
        max_font_size = std::max(max_font_size, child->MaxFontSize);
    }
    element.Height = height;
    element.Depth = depth;
    element.MaxFontSize = max_font_size;
}

// The symbol table holds more than two thousand entries, so the lookup uses a
// hash index instead of scanning it.  The keys are views into the generated
// table, which outlives the index.
struct SymbolHash {
    using is_transparent = void;
    size_t operator()(std::u16string_view value) const noexcept {
        return std::hash<std::u16string_view>{}(value);
    }
};
struct SymbolEqual {
    using is_transparent = void;
    bool operator()(std::u16string_view a, std::u16string_view b) const noexcept { return a == b; }
};

using SymbolIndex = std::unordered_map<std::u16string_view, const SymbolDefinition*, SymbolHash, SymbolEqual>;

// The symbol table of one mode, plus a direct table for the single ASCII
// characters.  Those are by far the most common symbol names, so they are
// resolved without hashing the name.
struct SymbolTable {
    SymbolIndex Math;
    SymbolIndex Text;
    std::array<const SymbolDefinition*, 128> AsciiMath{};
    std::array<const SymbolDefinition*, 128> AsciiText{};
};

const SymbolTable& GetSymbolTable() {
    static const SymbolTable table = [] {
        SymbolTable table;
        table.Math.reserve(SymbolData::Symbols.size() * 2);
        table.Text.reserve(SymbolData::Symbols.size() * 2);
        for (const auto& symbol : SymbolData::Symbols) {
            if (symbol.TextMode) {
                table.Text.try_emplace(symbol.Name, &symbol);
            } else {
                table.Math.try_emplace(symbol.Name, &symbol);
            }
            // The direct table follows the same rule as the hash indices: the
            // first entry with a given name wins.
            if (symbol.Name.size() == 1 && symbol.Name[0] < 128) {
                const size_t slot = static_cast<size_t>(symbol.Name[0]);
                std::array<const SymbolDefinition*, 128>& direct =
                    symbol.TextMode ? table.AsciiText : table.AsciiMath;
                if (direct[slot] == nullptr) {
                    direct[slot] = &symbol;
                }
            }
        }
        return table;
    }();
    return table;
}

const SymbolInfo* LookupSymbol(std::u16string_view name, Mode mode) {
    const SymbolTable& table = GetSymbolTable();
    const SymbolDefinition* symbol = nullptr;
    if (name.size() == 1 && name[0] < 128) {
        const std::array<const SymbolDefinition*, 128>& direct =
            mode == Mode::Text ? table.AsciiText : table.AsciiMath;
        symbol = direct[static_cast<size_t>(name[0])];
        if (symbol == nullptr) {
            return nullptr;
        }
    } else {
        const SymbolIndex& index = mode == Mode::Text ? table.Text : table.Math;
        auto it = index.find(name);
        if (it == index.end()) {
            return nullptr;
        }
        symbol = it->second;
    }
    static thread_local SymbolInfo info;
    info.CodePoint = symbol->CodePoint;
    info.IsMain = symbol->Font != MathFont::AMS;
    info.Group = symbol->Group;
    info.IsMathOrd = symbol->IsMathOrd;
    return &info;
}

NodePtr<HtmlSymbol> MakeSymbol(std::u16string_view value, FontId font, Mode mode,
                                       const HtmlOptions& options, ClassList&& classes) {
    // Apply the symbol replacement from KaTeX's symbol table.  KaTeX takes the
    // metrics from the symbol's own font ("main" or "ams"), not from the
    // requested font.  The replacement is a single character, so the value is
    // kept as a view and no copy of the symbol text is needed.
    char16_t replacement = 0;
    std::u16string_view replaced = value;
    if (const SymbolInfo* info = LookupSymbol(value, mode)) {
        if (info->CodePoint >= 0) {
            replacement = static_cast<char16_t>(info->CodePoint);
            replaced = std::u16string_view(&replacement, 1);
        }
    }
    const CharMetrics* metrics = GetCharacterMetrics(replaced, font, mode);
    // The text of a symbol is a single code point, so it is written into a
    // buffer of the caller instead of a string of its own.
    NodePtr<HtmlSymbol> symbol;
    if (replaced.size() <= 1) {
        char buffer[4];
        const size_t length = ToUtf8Into(buffer, replaced);
        symbol = MakeNode<HtmlSymbol>(std::string_view(buffer, length));
    } else {
        symbol = MakeNode<HtmlSymbol>(ToUtf8(replaced));
    }
    if (metrics != nullptr) {
        double italic = metrics->Italic;
        if (mode == Mode::Text || same_text(options.Font, "mathit")) {
            italic = 0.0;
        }
        symbol->SetItalic(italic);
        symbol->SetSkew(metrics->Skew);
        symbol->SetWidth(metrics->Width);
        symbol->Height = metrics->Height;
        symbol->Depth = metrics->Depth;
    } else {
        symbol->SetItalic(0.0);
        symbol->SetSkew(0.0);
        symbol->SetWidth(0.0);
    }
    symbol->Classes = std::move(classes);
    // KaTeX marks symbols from non-Latin scripts with a `<script>_fallback`
    // class so that the browser picks a matching font.
    if (!replaced.empty()) {
        if (const char* script_class = ScriptFallbackClass(static_cast<int>(replaced[0]))) {
            symbol->AddClass(script_class);
        }
    }
    symbol->MaxFontSize = options.SizeMultiplier;
    if (options.CurrentStyle.IsTight()) {
        symbol->AddClass("mtight");
    }
    if (const std::optional<std::string_view> color = options.GetColor()) {
        symbol->Style.Set("color", *color);
    }
    return symbol;
}

NodePtr<HtmlSymbol> MathSym(std::u16string_view value, Mode mode, const HtmlOptions& options,
                                     ClassList&& classes) {
    if (same_text(options.Font, "boldsymbol")) {
        const CharMetrics* metrics = GetCharacterMetrics(value, FontId::MainBold, mode);
        if (metrics != nullptr) {
            classes.Add("mathbf");
            return MakeSymbol(value, FontId::MainBold, mode, options, std::move(classes));
        }
    }
    const SymbolInfo* info = LookupSymbol(value, mode);
    if (value == u"\\"_u16 || (info != nullptr && info->IsMain)) {
        return MakeSymbol(value, FontId::MainRegular, mode, options, std::move(classes));
    }
    classes.Add("amsrm");
    return MakeSymbol(value, FontId::AmsRegular, mode, options, std::move(classes));
}

NodePtr<HtmlSymbol> MathSym(std::u16string_view value, Mode mode, const HtmlOptions& options,
                                     ClassList&& classes, MathFont symbol_font) {
    if (same_text(options.Font, "boldsymbol")) {
        const CharMetrics* metrics = GetCharacterMetrics(value, FontId::MainBold, mode);
        if (metrics != nullptr) {
            classes.Add("mathbf");
            return MakeSymbol(value, FontId::MainBold, mode, options, std::move(classes));
        }
    }
    if (symbol_font != MathFont::AMS) {
        return MakeSymbol(value, FontId::MainRegular, mode, options, std::move(classes));
    }
    classes.Add("amsrm");
    return MakeSymbol(value, FontId::AmsRegular, mode, options, std::move(classes));
}

HtmlNodePtr MakeOrd(const OrdSpec& spec, const HtmlOptions& options) {
    ClassList classes{"mord"};
    const Mode mode = spec.TextModeValue;
    const std::u16string_view text = spec.Text;
    const bool use_font = mode == Mode::Math || !options.Font.empty();
    const std::string_view font_or_family = use_font ? options.Font : options.FontFamily;

    if (!text.empty() && text[0] == 0xD835) {
        // KaTeX's `makeOrd` gives the mathematical alphanumeric symbols their
        // own font and CSS class.  The rows without a font fall through to
        // the regular code path.
        const WideCharacterFont* wide = WideCharacterFontOf(text);
        if (wide != nullptr && wide->Font != FontId::Unknown) {
            classes.Add(mode == Mode::Math ? wide->MathClass : wide->TextClass);
            return MakeSymbol(text, wide->Font, mode, options, std::move(classes));
        }
    }
    if (!font_or_family.empty()) {
        // The font of the symbol.  KaTeX looks the metrics of a symbol up by
        // the name of its font, and the name is resolved once here instead of
        // once per symbol.
        FontId font = FontId::Unknown;
        bool has_font = false;
        bool is_typewriter = false;
        ClassList font_classes;
        if (same_text(font_or_family, "boldsymbol")) {
            const CharMetrics* bold_italic = GetCharacterMetrics(text, FontId::MathBoldItalic, mode);
            // KaTeX uses the bold italic font for mathords and the bold font
            // for textords.
            if (spec.IsMathOrd && bold_italic != nullptr) {
                font = FontId::MathBoldItalic;
                font_classes = ClassList{"boldsymbol"};
            } else {
                font = FontId::MainBold;
                font_classes = ClassList{"mathbf"};
            }
            has_font = true;
        } else if (use_font) {
            const FontMapEntry* entry = LookupFontMap(font_or_family);
            if (entry != nullptr) {
                font = entry->Font;
                is_typewriter = entry->IsTypewriter;
                font_classes = ClassList{font_or_family};
                has_font = true;
            }
        } else {
            font = RetrieveTextFontId(font_or_family, options.FontWeight, options.FontShape,
                                      &is_typewriter);
            font_classes = ClassList{font_or_family, options.FontWeight, options.FontShape};
            has_font = true;
        }
        if (has_font) {
            const CharMetrics* metrics = GetCharacterMetrics(text, font, mode);
            if (metrics != nullptr) {
                for (size_t i = 0; i < font_classes.Size(); ++i) {
                    classes.Add(font_classes.At(i));
                }
                return MakeSymbol(text, font, mode, options, std::move(classes));
            }
            if (IsTypewriterLigature(text) && is_typewriter) {
                NodeVector parts;
                for (const char16_t c : text) {
                    ClassList part_classes = classes;
                    for (size_t i = 0; i < font_classes.Size(); ++i) {
                        part_classes.Add(font_classes.At(i));
                    }
                    parts.push_back(MakeSymbol(std::u16string_view(&c, 1), font, mode, options,
                                               std::move(part_classes)));
                }
                return MakeFragment(std::move(parts));
            }
        }
    }
    if (spec.IsMathOrd) {
        classes.Add("mathnormal");
        return MakeSymbol(text, FontId::MathItalic, mode, options, std::move(classes));
    }
    // KaTeX picks the font of a "textord" from its symbol table.  The lookup
    // uses the character that was parsed, not its replacement, so the black
    // board bold letters keep their "amsrm" font even though they are drawn
    // with the narrow letter.
    if (spec.SymbolFont == MathFont::AMS) {
        bool is_typewriter = false;
        const FontId font =
            RetrieveTextFontId("amsrm", options.FontWeight, options.FontShape, &is_typewriter);
        classes.Add("amsrm");
        classes.Add(options.FontWeight);
        classes.Add(options.FontShape);
        return MakeSymbol(text, font, mode, options, std::move(classes));
    }
    bool is_typewriter = false;
    const FontId font =
        RetrieveTextFontId("textrm", options.FontWeight, options.FontShape, &is_typewriter);
    classes.Add(options.FontWeight);
    classes.Add(options.FontShape);
    return MakeSymbol(text, font, mode, options, std::move(classes));
}

NodeVector TryCombineChars(NodeVector chars) {
    // KaTeX merges the next symbol into the previous one and then retries the
    // same position, so a run of glyphs collapses into a single symbol node.
    for (long i = 0; i + 1 < static_cast<long>(chars.size()); ++i) {
        if (chars[i]->Kind() != HtmlNodeKind::Symbol || chars[i + 1]->Kind() != HtmlNodeKind::Symbol) {
            continue;
        }
        auto& previous = static_cast<HtmlSymbol&>(*chars[i]);
        auto& next = static_cast<HtmlSymbol&>(*chars[i + 1]);
        if (!CanCombine(previous, next)) {
            continue;
        }
        // Combine the two symbols into one.
        previous.AppendText(next.Text());
        previous.Height = std::max(previous.Height, next.Height);
        previous.Depth = std::max(previous.Depth, next.Depth);
        // Use the last character's italic correction since we use it to add
        // padding to the right of the span created from the combined glyphs.
        previous.SetItalic(next.ItalicValue());
        chars.erase(chars.begin() + static_cast<long>(i) + 1);
        --i;
    }
    return chars;
}

NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children) {
    auto span = MakeNode<HtmlSpan>(std::move(classes));
    span->Children() = std::move(children);
    SizeElementFromChildren(*span);
    return span;
}

NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children,
                                   const HtmlOptions& options) {
    auto span = MakeNode<HtmlSpan>(std::move(classes));
    span->Children() = std::move(children);
    if (options.CurrentStyle.IsTight()) {
        span->AddClass("mtight");
    }
    if (const std::optional<std::string_view> color = options.GetColor()) {
        span->Style.Set("color", *color);
    }
    SizeElementFromChildren(*span);
    return span;
}

NodePtr<HtmlSpan> MakeSpan(ClassList&& classes, NodeVector children,
                                   NodePtr<CssStyle> style) {
    auto span = MakeNode<HtmlSpan>(std::move(classes));
    span->Children() = std::move(children);
    if (style != nullptr) {
        // The declarations are moved, so that the list of them is not copied.
        span->Style = std::move(*style);
    }
    SizeElementFromChildren(*span);
    return span;
}

NodePtr<HtmlSpan> MakeLineSpan(std::string_view class_name, const HtmlOptions& options, double thickness) {
    // KaTeX creates the line with `makeSpan([className], [], options)`, so the
    // line inherits the "mtight" class of a tight style.
    auto line = MakeSpan({class_name}, {}, options);
    const double height = std::max(thickness > 0.0 ? thickness : options.Metrics().DefaultRuleThickness,
                                   options.MinRuleThickness);
    line->Height = height;
    line->Style.SetEm("border-bottom-width", height);
    line->MaxFontSize = 1.0;
    return line;
}

NodePtr<HtmlAnchor> MakeAnchor(std::string href, ClassList&& classes,
                                        NodeVector children) {
    auto anchor = MakeNode<HtmlAnchor>(std::move(href), std::move(classes));
    anchor->Children() = std::move(children);
    SizeElementFromChildren(*anchor);
    return anchor;
}

NodePtr<HtmlFragment> MakeFragment(NodeVector children) {
    auto fragment = MakeNode<HtmlFragment>();
    fragment->Children() = std::move(children);
    SizeElementFromChildren(*fragment);
    return fragment;
}

HtmlNodePtr WrapFragment(HtmlNodePtr group, const HtmlOptions& options) {
    if (group->Kind() == HtmlNodeKind::Fragment) {
        NodeVector children;
        children.push_back(std::move(group));
        return MakeSpan({}, std::move(children));
    }
    return group;
}

double CalculateSize(double number, TexUnit unit, const HtmlOptions& options) {
    double scale = 0.0;
    const GlobalMetrics& metrics = options.Metrics();
    switch (unit) {
        case TexUnit::Pt:
            scale = 1.0 / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Mm:
            scale = (7227.0 / 2540.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Cm:
            scale = (7227.0 / 254.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::In:
            scale = 72.27 / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Bp:
            scale = (803.0 / 800.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Pc:
            scale = 12.0 / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Dd:
            scale = (1238.0 / 1157.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Cc:
            scale = (14856.0 / 1157.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Nd:
            scale = (685.0 / 642.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Nc:
            scale = (1370.0 / 107.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Sp:
            scale = (1.0 / 65536.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Px:
            scale = (803.0 / 800.0) / metrics.PtPerEm / options.SizeMultiplier;
            break;
        case TexUnit::Mu:
            scale = metrics.CssEmPerMu;
            break;
        case TexUnit::Ex:
        case TexUnit::Em: {
            HtmlOptions unit_options = options;
            if (options.CurrentStyle.IsTight()) {
                unit_options = options.HavingStyle(options.CurrentStyle.Text());
            }
            const GlobalMetrics& unit_metrics = unit_options.Metrics();
            scale = unit == TexUnit::Ex ? unit_metrics.XHeight : unit_metrics.Quad;
            if (unit_options.Size != options.Size) {
                scale *= unit_options.SizeMultiplier / options.SizeMultiplier;
            }
            break;
        }
    }
    return std::min(number * scale, options.MaxSize);
}

NodePtr<HtmlSpan> MakeGlue(double size, const HtmlOptions& options) {
    // KaTeX's `makeGlue` builds the space with the current options so that it
    // inherits the `mtight` class and the current colour.
    auto rule = MakeSpan({"mspace"}, {}, options);
    rule->Style.SetEm("margin-right", size);
    return rule;
}

NodePtr<HtmlSpan> MakeVList(VListParams params, const HtmlOptions& options) {
    struct Child {
        // The constructor is written out, so that the vector does not zero the
        // whole child before the fields are filled: a value initialization
        // zeroes every byte of the child, and in WebAssembly that is a call
        // into the library for a child of this size.
        Child() {}
        bool IsKern{false};
        HtmlNodePtr Elem;
        double KernSize{0.0};
        double Shift{0.0};
        std::optional<double> MarginLeft;
        std::optional<double> MarginRight;
        ClassList WrapperClasses;
        NodePtr<CssStyle> WrapperStyle;
    };
    std::vector<Child, ArenaAllocator<Child>> children;
    double depth = 0.0;

    if (params.PositionType == VListPositionType::IndividualShift) {
        auto& old = params.ShiftedElements;
        if (old.empty()) {
            return MakeSpan({"vlist-t"}, {});
        }
        // The number of the children is known: the first element, and a kern
        // and an element for every element that follows.  The list is
        // reserved once, so that the vector does not grow (and move every
        // child) while it is filled.
        children.reserve(old.size() * 2 - 1);
        const double first_height = old[0].Elem->Height;
        const double first_depth = old[0].Elem->Depth;
        const double first_shift = old[0].Shift;
        // The child is built in the vector itself.  A local copy would be
        // moved into the vector, and in WebAssembly the move of a struct of
        // this size is a call into libc++ instead of a few instructions.
        children.emplace_back();
        Child& first = children.back();
        first.Elem = std::move(old[0].Elem);
        first.Shift = first_shift;
        first.MarginLeft = old[0].MarginLeft;
        first.MarginRight = old[0].MarginRight;
        // The parameters belong to this call, so the classes and the style of
        // the wrapper are taken over instead of copied.  A copy of a style
        // copies every declaration of it into the arena again.
        first.WrapperClasses = std::move(old[0].WrapperClasses);
        first.WrapperStyle = std::move(old[0].WrapperStyle);
        depth = -first_shift - first_depth;
        double curr_pos = depth;
        double previous_height = first_height;
        double previous_depth = first_depth;
        for (size_t i = 1; i < old.size(); ++i) {
            const double diff = -old[i].Shift - curr_pos - old[i].Elem->Depth;
            const double size = diff - (previous_height + previous_depth);
            curr_pos += diff;
            previous_height = old[i].Elem->Height;
            previous_depth = old[i].Elem->Depth;
            children.emplace_back();
            Child& kern = children.back();
            kern.IsKern = true;
            kern.KernSize = size;
            children.emplace_back();
            Child& element = children.back();
            element.Elem = std::move(old[i].Elem);
            element.Shift = old[i].Shift;
            element.MarginLeft = old[i].MarginLeft;
            element.MarginRight = old[i].MarginRight;
            element.WrapperClasses = std::move(old[i].WrapperClasses);
            element.WrapperStyle = std::move(old[i].WrapperStyle);
        }
    } else {
        // Every element of the list becomes one child of the list.
        children.reserve(params.Elements.size());
        for (auto& element : params.Elements) {
            children.emplace_back();
            Child& child = children.back();
            if (element.IsKern) {
                child.IsKern = true;
                child.KernSize = element.KernSize;
                continue;
            }
            child.Elem = std::move(element.Elem);
            child.MarginLeft = element.MarginLeft;
            child.MarginRight = element.MarginRight;
            child.WrapperClasses = std::move(element.WrapperClasses);
            child.WrapperStyle = std::move(element.WrapperStyle);
        }
        if (params.PositionType == VListPositionType::Top) {
            double bottom = params.PositionData;
            for (const auto& child : children) {
                bottom -= child.IsKern ? child.KernSize : child.Elem->Height + child.Elem->Depth;
            }
            depth = bottom;
        } else if (params.PositionType == VListPositionType::Bottom) {
            depth = -params.PositionData;
        } else {
            if (children.empty() || children.front().IsKern) {
                return MakeSpan({"vlist-t"}, {});
            }
            if (params.PositionType == VListPositionType::Shift) {
                depth = -children.front().Elem->Depth - params.PositionData;
            } else {
                depth = -children.front().Elem->Depth;
            }
        }
    }

    // The strut is taller than any list item.
    double pstrut_size = 0.0;
    for (const auto& child : children) {
        if (!child.IsKern) {
            pstrut_size = std::max(pstrut_size, child.Elem->MaxFontSize);
            pstrut_size = std::max(pstrut_size, child.Elem->Height);
        }
    }
    pstrut_size += 2;

    NodeVector real_children;
    real_children.reserve(children.size());
    double min_pos = depth;
    double max_pos = depth;
    double curr_pos = depth;
    for (auto& child : children) {
        if (child.IsKern) {
            curr_pos += child.KernSize;
        } else {
            // The style of the wrapper belongs to this child, so it is taken
            // over instead of copied.  A copy would copy every declaration of
            // the style into the arena again.
            NodePtr<CssStyle> wrapper_style = std::move(child.WrapperStyle);
            const double elem_height = child.Elem->Height;
            const double elem_depth = child.Elem->Depth;
            NodeVector wrapper_children;
            wrapper_children.reserve(2);
            wrapper_children.push_back(MakeSpan({"pstrut"}, {}));
            wrapper_children[0]->Style.SetEm("height", pstrut_size);
            wrapper_children.push_back(std::move(child.Elem));
            auto child_wrap = MakeSpan(std::move(child.WrapperClasses), std::move(wrapper_children), std::move(wrapper_style));
            child_wrap->Style.SetEm("top", -pstrut_size - curr_pos - elem_depth);
            if (child.MarginLeft.has_value()) {
                child_wrap->Style.SetEm("margin-left", *child.MarginLeft);
            }
            if (child.MarginRight.has_value()) {
                child_wrap->Style.SetEm("margin-right", *child.MarginRight);
            }
            real_children.push_back(std::move(child_wrap));
            curr_pos += elem_height + elem_depth;
        }
        min_pos = std::min(min_pos, curr_pos);
        max_pos = std::max(max_pos, curr_pos);
    }

    auto vlist = MakeSpan({"vlist"}, std::move(real_children));
    vlist->Style.SetEm("height", max_pos);

    NodeVector rows;
    rows.reserve(2);
    if (min_pos < 0) {
        auto empty_span = MakeSpan({}, {});
        NodeVector depth_children;
        depth_children.push_back(std::move(empty_span));
        auto depth_strut = MakeSpan({"vlist"}, std::move(depth_children));
        depth_strut->Style.SetEm("height", -min_pos);

        NodeVector top_children;
        top_children.push_back(MakeNode<HtmlSymbol>("\xE2\x80\x8B"));
        auto top_strut = MakeSpan({"vlist-s"}, std::move(top_children));

        NodeVector row_one;
        row_one.push_back(std::move(vlist));
        row_one.push_back(std::move(top_strut));
        NodeVector row_two;
        row_two.push_back(std::move(depth_strut));
        rows.push_back(MakeSpan({"vlist-r"}, std::move(row_one)));
        rows.push_back(MakeSpan({"vlist-r"}, std::move(row_two)));
    } else {
        NodeVector row;
        row.push_back(std::move(vlist));
        rows.push_back(MakeSpan({"vlist-r"}, std::move(row)));
    }

    const bool two_rows = rows.size() == 2;
    auto vtable = MakeSpan({"vlist-t"}, std::move(rows));
    if (two_rows) {
        vtable->AddClass("vlist-t2");
    }
    vtable->Height = max_pos;
    vtable->Depth = -min_pos;
    return vtable;
}

}  // namespace velocitex::html
