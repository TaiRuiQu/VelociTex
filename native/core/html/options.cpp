// KaTeX-compatible rendering options implementation.
//
// Port of KaTeX 0.19.0 `src/Options.ts`, `src/Style.ts` and the metric
// lookups from `src/fontMetrics.ts`.
#include "options.hpp"

#include "domtree.hpp"
#include "../state.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>
#include <vector>

#include "../generated/data.hpp"

namespace velocitex::html {
namespace {

// KaTeX's `sizeStyleMap`: [textsize, scriptsize, scriptscriptsize].
constexpr int kSizeStyleMap[11][3] = {
    {1, 1, 1},  {2, 1, 1},  {3, 1, 1},  {4, 2, 1},   {5, 2, 1},   {6, 3, 1},
    {7, 4, 2},  {8, 6, 3},  {9, 7, 6},  {10, 8, 7},  {11, 10, 9},
};

constexpr double kSizeMultipliers[11] = {0.5,   0.6,   0.7,   0.8,   0.9,   1.0,
                                          1.2,   1.44,  1.728, 2.074, 2.488};

const std::array<FontMapEntry, 12> kFontMap{{
    {"mathbf", FontId::MainBold, false},
    {"mathrm", FontId::MainRegular, false},
    {"textit", FontId::MainItalic, false},
    {"mathit", FontId::MainItalic, false},
    {"mathnormal", FontId::MathItalic, false},
    {"mathsfit", FontId::SansSerifItalic, false},
    {"mathbb", FontId::AmsRegular, false},
    {"mathcal", FontId::CaligraphicRegular, false},
    {"mathfrak", FontId::FrakturRegular, false},
    {"mathscr", FontId::ScriptRegular, false},
    {"mathsf", FontId::SansSerifRegular, false},
    {"mathtt", FontId::TypewriterRegular, true},
}};

// KaTeX's `extraCharacterMap`: rough metric approximations for characters that
// have no metrics of their own.
struct ExtraCharacter {
    char16_t Character;
    char16_t Replacement;
};

constexpr ExtraCharacter kExtraCharacters[] = {
    {0x00C5, 0x0041}, {0x00D0, 0x0044}, {0x00DE, 0x006F}, {0x00E5, 0x0061},
    {0x00F0, 0x0064}, {0x00FE, 0x006F},
    {0x0410, 0x0041}, {0x0411, 0x0042}, {0x0412, 0x0042}, {0x0413, 0x0046},
    {0x0414, 0x0041}, {0x0415, 0x0045}, {0x0416, 0x004B}, {0x0417, 0x0033},
    {0x0418, 0x004E}, {0x0419, 0x004E}, {0x041A, 0x004B}, {0x041B, 0x004E},
    {0x041C, 0x004D}, {0x041D, 0x0048}, {0x041E, 0x004F}, {0x041F, 0x004E},
    {0x0420, 0x0050}, {0x0421, 0x0043}, {0x0422, 0x0054}, {0x0423, 0x0079},
    {0x0424, 0x004F}, {0x0425, 0x0058}, {0x0426, 0x0055}, {0x0427, 0x0068},
    {0x0428, 0x0057}, {0x0429, 0x0057}, {0x042A, 0x0042}, {0x042B, 0x0058},
    {0x042C, 0x0042}, {0x042D, 0x0033}, {0x042E, 0x0058}, {0x042F, 0x0052},
    {0x0430, 0x0061}, {0x0431, 0x0062}, {0x0432, 0x0061}, {0x0433, 0x0072},
    {0x0434, 0x0079}, {0x0435, 0x0065}, {0x0436, 0x006D}, {0x0437, 0x0065},
    {0x0438, 0x006E}, {0x0439, 0x006E}, {0x043A, 0x006E}, {0x043B, 0x006E},
    {0x043C, 0x006D}, {0x043D, 0x006E}, {0x043E, 0x006F}, {0x043F, 0x006E},
    {0x0440, 0x0070}, {0x0441, 0x0063}, {0x0442, 0x006F}, {0x0443, 0x0079},
    {0x0444, 0x0062}, {0x0445, 0x0078}, {0x0446, 0x006E}, {0x0447, 0x006E},
    {0x0448, 0x0077}, {0x0449, 0x0077}, {0x044A, 0x0061}, {0x044B, 0x006D},
    {0x044C, 0x0061}, {0x044D, 0x0065}, {0x044E, 0x006D}, {0x044F, 0x0072},
};

// The extra characters are looked up for every character that a font has no
// metrics for, so the table is indexed by codepoint.  All of them are below
// 0x0450.
constexpr int kExtraCharacterLimit = 0x0450;

const std::array<char16_t, kExtraCharacterLimit>& ExtraCharacters() {
    static const std::array<char16_t, kExtraCharacterLimit> table = [] {
        std::array<char16_t, kExtraCharacterLimit> table{};
        for (const auto& entry : kExtraCharacters) {
            table[entry.Character] = entry.Replacement;
        }
        return table;
    }();
    return table;
}

// KaTeX's `supportedCodepoint` block table.
constexpr int kSupportedBlocks[][2] = {
    {0x0100, 0x024F}, {0x0300, 0x036F}, {0x0400, 0x04FF}, {0x0530, 0x058F},
    {0x0900, 0x109F}, {0x10A0, 0x10FF}, {0x3000, 0x30FF}, {0x4E00, 0x9FAF},
    {0xFF00, 0xFF60}, {0xAC00, 0xD7AF},
};

bool SupportedCodepoint(int codepoint) {
    for (const auto& block : kSupportedBlocks) {
        if (codepoint >= block[0] && codepoint <= block[1]) {
            return true;
        }
    }
    return false;
}

// The 32 bit finalizer of MurmurHash3.  It spreads the codepoints over the
// whole word, so the low bits of the result are usable as a hash table slot.
uint32_t MixHash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

// The metrics of one font.  Codepoints below 256 are resolved through a
// direct table, the remaining ones through an open-addressing hash table.
// The offsets are relative to the font's range in `FontData::TexMetrics`.
struct FontMetricIndex {
    int MetricStart{0};
    int MetricCount{0};
    std::array<int32_t, 256> Direct{};  // metric offset, -1 when absent
    uint32_t HashMask{0};
    std::vector<int32_t> HashCodes;    // codepoint, -1 when the slot is free
    std::vector<int32_t> HashOffsets;  // metric offset
};

// The generated tables are immutable, so the lookup indices are built once
// and then shared.  A metric is addressed by (font, codepoint).
class MetricIndex {
public:
    MetricIndex() {
        const size_t font_count = FontData::Fonts.size();
        fonts_.resize(font_count);
        for (size_t f = 0; f < font_count; ++f) {
            const FontDescriptor& font = FontData::Fonts[f];
            FontMetricIndex& index = fonts_[f];
            index.MetricStart = font.MetricStart;
            index.MetricCount = font.MetricCount;
            index.Direct.fill(-1);
            size_t hash_entries = 0;
            for (int i = 0; i < font.MetricCount; ++i) {
                if (FontData::TexMetrics[font.MetricStart + i].CodePoint >= 256) {
                    ++hash_entries;
                }
            }
            size_t capacity = 8;
            while (capacity < hash_entries * 2) {
                capacity <<= 1;
            }
            index.HashMask = static_cast<uint32_t>(capacity - 1);
            index.HashCodes.assign(capacity, -1);
            index.HashOffsets.assign(capacity, -1);
            for (int i = 0; i < font.MetricCount; ++i) {
                const TexGlyphMetric& metric = FontData::TexMetrics[font.MetricStart + i];
                if (metric.CodePoint >= 0 && metric.CodePoint < 256) {
                    index.Direct[metric.CodePoint] = i;
                    continue;
                }
                uint32_t slot = MixHash(static_cast<uint32_t>(metric.CodePoint)) & index.HashMask;
                while (index.HashCodes[slot] >= 0) {
                    slot = (slot + 1) & index.HashMask;
                }
                index.HashCodes[slot] = metric.CodePoint;
                index.HashOffsets[slot] = i;
            }
        }
    }

    const TexGlyphMetric* Find(int font_index, int codepoint) const {
        if (font_index < 0 || font_index >= static_cast<int>(fonts_.size()) || codepoint < 0) {
            return nullptr;
        }
        const FontMetricIndex& index = fonts_[font_index];
        if (codepoint < 256) {
            const int32_t offset = index.Direct[codepoint];
            if (offset < 0) {
                return nullptr;
            }
            return &FontData::TexMetrics[index.MetricStart + offset];
        }
        uint32_t slot = MixHash(static_cast<uint32_t>(codepoint)) & index.HashMask;
        for (;;) {
            const int32_t code = index.HashCodes[slot];
            if (code < 0) {
                return nullptr;
            }
            if (code == codepoint) {
                return &FontData::TexMetrics[index.MetricStart + index.HashOffsets[slot]];
            }
            slot = (slot + 1) & index.HashMask;
        }
    }

private:
    std::vector<FontMetricIndex> fonts_;
};

const TexGlyphMetric* RawMetric(int fontIndex, int codepoint) {
    static const MetricIndex index;
    return index.Find(fontIndex, codepoint);
}

const TexGlyphMetric* FallbackMetric(int fontIndex, int codepoint, bool text) {
    // The metric is handed out as a pointer, so it has to outlive the call.
    static thread_local TexGlyphMetric metric{};
    if (FallbackMetricData::Find(fontIndex, codepoint, text, metric)) {
        return &metric;
    }
    return nullptr;
}

}  // namespace

// KaTeX's `scriptFromCodepoint` (`src/unicodeScripts.ts`).  The name is used
// to mark the symbol with a `<script>_fallback` class.  The table lives in the
// core, because the parser needs it for KaTeX's `supportedCodepoint` as well.
const char* ScriptFromCodepoint(int codepoint) {
    return UnicodeScriptFamily(codepoint);
}

const char* ScriptFallbackClass(int codepoint) {
    return UnicodeScriptFallbackClass(codepoint);
}

Style Style::Sup() const {
    static const int kSup[8] = {4, 5, 4, 5, 6, 7, 6, 7};
    const int id = kSup[Id];
    return Style(id, id / 2, id % 2 == 1);
}

Style Style::Sub() const {
    static const int kSub[8] = {5, 5, 5, 5, 7, 7, 7, 7};
    const int id = kSub[Id];
    return Style(id, id / 2, id % 2 == 1);
}

Style Style::FracNum() const {
    static const int kFracNum[8] = {2, 3, 4, 5, 6, 7, 6, 7};
    const int id = kFracNum[Id];
    return Style(id, id / 2, id % 2 == 1);
}

Style Style::FracDen() const {
    static const int kFracDen[8] = {3, 3, 5, 5, 7, 7, 7, 7};
    const int id = kFracDen[Id];
    return Style(id, id / 2, id % 2 == 1);
}

Style Style::Cramp() const {
    static const int kCramp[8] = {1, 1, 3, 3, 5, 5, 7, 7};
    const int id = kCramp[Id];
    return Style(id, id / 2, id % 2 == 1);
}

Style Style::Text() const {
    static const int kText[8] = {0, 1, 2, 3, 2, 3, 2, 3};
    const int id = kText[Id];
    return Style(id, id / 2, id % 2 == 1);
}

namespace {

// KaTeX's global metrics only depend on the size of the style, and there are
// just three variants.  The table is built once and then shared, so copying
// the options does not recompute the thirty seven constants.
GlobalMetrics BuildGlobalMetrics(int index) {
    GlobalMetrics metrics;
    metrics.Slant = MathConstants::slant(index);
    metrics.Space = MathConstants::space(index);
    metrics.Stretch = MathConstants::stretch(index);
    metrics.Shrink = MathConstants::shrink(index);
    metrics.XHeight = MathConstants::xHeight(index);
    metrics.Quad = MathConstants::quad(index);
    metrics.ExtraSpace = MathConstants::extraSpace(index);
    metrics.Num1 = MathConstants::num1(index);
    metrics.Num2 = MathConstants::num2(index);
    metrics.Num3 = MathConstants::num3(index);
    metrics.Denom1 = MathConstants::denom1(index);
    metrics.Denom2 = MathConstants::denom2(index);
    metrics.Sup1 = MathConstants::sup1(index);
    metrics.Sup2 = MathConstants::sup2(index);
    metrics.Sup3 = MathConstants::sup3(index);
    metrics.Sub1 = MathConstants::sub1(index);
    metrics.Sub2 = MathConstants::sub2(index);
    metrics.SupDrop = MathConstants::supDrop(index);
    metrics.SubDrop = MathConstants::subDrop(index);
    metrics.Delim1 = MathConstants::delim1(index);
    metrics.Delim2 = MathConstants::delim2(index);
    metrics.AxisHeight = MathConstants::axisHeight(index);
    metrics.DefaultRuleThickness = MathConstants::defaultRuleThickness(index);
    metrics.BigOpSpacing1 = MathConstants::bigOpSpacing1(index);
    metrics.BigOpSpacing2 = MathConstants::bigOpSpacing2(index);
    metrics.BigOpSpacing3 = MathConstants::bigOpSpacing3(index);
    metrics.BigOpSpacing4 = MathConstants::bigOpSpacing4(index);
    metrics.BigOpSpacing5 = MathConstants::bigOpSpacing5(index);
    metrics.SqrtRuleThickness = MathConstants::sqrtRuleThickness(index);
    metrics.PtPerEm = MathConstants::ptPerEm(index);
    metrics.DoubleRuleSep = MathConstants::doubleRuleSep(index);
    metrics.ArrayRuleWidth = MathConstants::arrayRuleWidth(index);
    metrics.Fboxsep = MathConstants::fboxsep(index);
    metrics.Fboxrule = MathConstants::fboxrule(index);
    metrics.CssEmPerMu = metrics.Quad / 18.0;
    return metrics;
}

}  // namespace

const GlobalMetrics& GetGlobalMetrics(int size) {
    static const GlobalMetrics table[3] = {BuildGlobalMetrics(0), BuildGlobalMetrics(1),
                                            BuildGlobalMetrics(2)};
    return table[size >= 5 ? 0 : (size >= 3 ? 1 : 2)];
}


// The font name is looked up for every symbol.  There are only twenty fonts,
// so an open-addressing hash table resolves the name in constant time.  The
// names are kept as byte strings, so that a candidate is confirmed with a
// single memcmp instead of a loop over the UTF-16 code units.
class FontNameIndex {
public:
    FontNameIndex() {
        const size_t count = FontData::Fonts.size();
        names_.reserve(count * 24);
        offsets_.reserve(count);
        lengths_.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            const std::u16string_view name = FontData::Fonts[i].Name;
            offsets_.push_back(names_.size());
            lengths_.push_back(name.size());
            for (char16_t c : name) {
                names_.push_back(static_cast<char>(c));
            }
        }
        size_t capacity = 32;
        while (capacity < count * 4) {
            capacity <<= 1;
        }
        mask_ = static_cast<uint32_t>(capacity - 1);
        indices_.assign(capacity, -1);
        for (size_t i = 0; i < count; ++i) {
            const std::string_view name(names_.data() + offsets_[i], lengths_[i]);
            uint32_t slot = Hash(name) & mask_;
            while (indices_[slot] >= 0) {
                if (SameName(indices_[slot], name)) {
                    // A duplicate name keeps the first font, like the scan
                    // over the font table did.
                    break;
                }
                slot = (slot + 1) & mask_;
            }
            if (indices_[slot] < 0) {
                indices_[slot] = static_cast<int>(i);
            }
        }
    }

    int Find(std::string_view font_name) const {
        uint32_t slot = Hash(font_name) & mask_;
        for (;;) {
            const int index = indices_[slot];
            if (index < 0) {
                return -1;
            }
            if (lengths_[index] == font_name.size() &&
                SameBytes(names_.data() + offsets_[index], font_name.data(), font_name.size())) {
                return index;
            }
            slot = (slot + 1) & mask_;
        }
    }

private:
    // Compares two names of the same length without the call into the string
    // library.  The name is looked up for every symbol of a formula, and in
    // WebAssembly the call costs more than the comparison of a dozen bytes.
    static bool SameBytes(const char* a, const char* b, size_t length) {
        size_t i = 0;
        for (; i + 8 <= length; i += 8) {
            uint64_t x;
            uint64_t y;
            std::memcpy(&x, a + i, 8);
            std::memcpy(&y, b + i, 8);
            if (x != y) {
                return false;
            }
        }
        for (; i < length; ++i) {
            if (a[i] != b[i]) {
                return false;
            }
        }
        return true;
    }
    bool SameName(int index, std::string_view name) const {
        return lengths_[index] == name.size() &&
               SameBytes(names_.data() + offsets_[index], name.data(), name.size());
    }
    // The hash looks at the length and at the first few bytes.  The twenty
    // font names differ within their first eight characters, so the hash
    // spreads them over the table.
    static uint32_t Hash(std::string_view value) {
        uint32_t hash = static_cast<uint32_t>(value.size());
        const size_t count = value.size() < 8 ? value.size() : 8;
        for (size_t i = 0; i < count; ++i) {
            hash = hash * 131u + static_cast<uint8_t>(value[i]);
        }
        return hash;
    }

    uint32_t mask_{0};
    std::string names_;
    std::vector<size_t> offsets_;
    std::vector<size_t> lengths_;
    std::vector<int32_t> indices_;
};

int FontIndexByName(std::string_view fontName) {
    static const FontNameIndex index;
    return index.Find(fontName);
}

// The index of the metrics of every font, in the order of `FontId`.
//
// The table is built once, when the module starts: the names of the fonts are
// in the generated data, and the lookup by name is the slow part.  It lives
// in a namespace scope object, so that an access to it does not pay for the
// initialization guard of a `thread_local`.
struct FontIndexTable {
    FontIndexTable() {
        for (auto& entry : indices) {
            entry = -1;
        }
        // The names of the fonts, in the order of `FontId`.
        static constexpr const char* kNames[] = {
            "",                 "AMS-Regular",       "Caligraphic-Bold", "Caligraphic-Regular",
            "Fraktur-Bold",     "Fraktur-Regular",   "Main-Bold",        "Main-BoldItalic",
            "Main-Italic",      "Main-Regular",      "Math-BoldItalic",  "Math-Italic",
            "SansSerif-Bold",   "SansSerif-Italic",  "SansSerif-Regular", "Script-Regular",
            "Size1-Regular",    "Size2-Regular",     "Size3-Regular",    "Size4-Regular",
            "Typewriter-Regular"};
        for (size_t i = 1; i < sizeof(kNames) / sizeof(kNames[0]); ++i) {
            indices[i] = FontIndexByName(kNames[i]);
        }
    }
    int indices[static_cast<size_t>(FontId::TypewriterRegular) + 1];
};
VELOCITEX_REUSED_STATE(FontIndexTable, g_font_index_table);

int FontMetricsIndex(FontId font) {
    return g_font_index_table.indices[static_cast<size_t>(font)];
}

const CharMetrics* GetCharacterMetrics(std::u16string_view character, FontId font, Mode mode) {
    if (character.empty()) {
        return nullptr;
    }
    const int fontIndex = FontMetricsIndex(font);
    if (fontIndex < 0) {
        return nullptr;
    }
    int codepoint = character[0];
    const TexGlyphMetric* metric = RawMetric(fontIndex, codepoint);
    if (metric == nullptr) {
        if (codepoint < kExtraCharacterLimit) {
            const char16_t replacement = ExtraCharacters()[codepoint];
            if (replacement != 0) {
                codepoint = replacement;
            }
        }
        metric = RawMetric(fontIndex, codepoint);
    }
    if (metric == nullptr && mode == Mode::Text && SupportedCodepoint(codepoint)) {
        metric = RawMetric(fontIndex, 'M');
    }
    if (metric == nullptr) {
        // Fall back to the precomputed KaTeX fallback table, which also covers
        // the text-mode 'M' approximation for scripts without metrics.
        metric = FallbackMetric(fontIndex, codepoint, mode == Mode::Text);
    }
    if (metric == nullptr) {
        return nullptr;
    }
    static thread_local CharMetrics result;
    result.Depth = metric->Depth;
    result.Height = metric->Height;
    result.Italic = metric->ItalicCorrection;
    result.Skew = metric->Skew;
    result.Width = metric->Width;
    return &result;
}

const FontMapEntry* LookupFontMap(std::string_view fontClass) {
    for (const auto& entry : kFontMap) {
        if (entry.Variant == fontClass) {
            return &entry;
        }
    }
    return nullptr;
}

// The font of a text font: the family selects the base font, the weight and
// the shape select the style.
//
// KaTeX's `retrieveTextFontName` builds the name of the font.  The names of
// the four known families and the four styles are a table, and only a family
// of its own needs a name that is built at run time.  KaTeX's table of the
// metrics has no bold italic of a text font and no bold or italic of the
// typewriter, so those names stand for a font without metrics.
FontId RetrieveTextFontId(std::string_view family, std::string_view weight, std::string_view shape,
                          bool* is_typewriter) {
    // The font of every family and style, in the order of the four families
    // (amsrm, textrm, textsf, texttt) and the four styles (regular, bold,
    // italic, bold italic).  `Unknown` is a font that has no metrics.
    static constexpr FontId kFonts[4][4] = {
        {FontId::AmsRegular, FontId::Unknown, FontId::Unknown, FontId::Unknown},
        {FontId::MainRegular, FontId::MainBold, FontId::MainItalic, FontId::MainBoldItalic},
        {FontId::SansSerifRegular, FontId::SansSerifBold, FontId::SansSerifItalic, FontId::Unknown},
        {FontId::TypewriterRegular, FontId::Unknown, FontId::Unknown, FontId::Unknown}};
    int base = -1;
    if (same_text(family, "amsrm")) {
        base = 0;
    } else if (same_text(family, "textrm")) {
        base = 1;
    } else if (same_text(family, "textsf")) {
        base = 2;
    } else if (same_text(family, "texttt")) {
        base = 3;
    }
    int style = 0;
    if (same_text(weight, "textbf") && same_text(shape, "textit")) {
        style = 3;
    } else if (same_text(weight, "textbf")) {
        style = 1;
    } else if (same_text(shape, "textit")) {
        style = 2;
    }
    if (base >= 0) {
        *is_typewriter = base == 3;
        return kFonts[base][style];
    }
    // A family of its own: KaTeX builds the name of the font, and the table
    // of the metrics has no such font, so the lookup finds nothing.  KaTeX
    // still deconstructs a ligature in it when the name of the font starts
    // with "Typewriter".
    *is_typewriter = family.rfind("Typewriter", 0) == 0;
    return FontId::Unknown;
}

double HtmlOptions::SizeMultiplierFor(int size) {
    if (size < 1) {
        size = 1;
    }
    if (size > 11) {
        size = 11;
    }
    return kSizeMultipliers[size - 1];
}

int HtmlOptions::SizeAtStyle(int size, const Style& style) {
    if (style.Size < 2) {
        return size;
    }
    return kSizeStyleMap[size - 1][style.Size - 1];
}

void HtmlOptions::Refresh() {
    SizeMultiplier = SizeMultiplierFor(Size);
    metrics_ = &GetGlobalMetrics(Size);
}

HtmlOptions HtmlOptions::HavingStyle(const Style& style) const {
    if (style.Id == CurrentStyle.Id) {
        return *this;
    }
    HtmlOptions result = *this;
    result.CurrentStyle = style;
    result.Size = SizeAtStyle(TextSize, style);
    result.Refresh();
    return result;
}

HtmlOptions HtmlOptions::HavingCrampedStyle() const { return HavingStyle(CurrentStyle.Cramp()); }

HtmlOptions HtmlOptions::HavingSize(int size) const {
    if (Size == size && TextSize == size) {
        return *this;
    }
    HtmlOptions result = *this;
    result.CurrentStyle = CurrentStyle.Text();
    result.Size = size;
    result.TextSize = size;
    result.Refresh();
    return result;
}

HtmlOptions HtmlOptions::HavingBaseStyle(const Style& style) const {
    const Style target = style.Id == 0 && style.Size == 0 && !style.Cramped ? CurrentStyle.Text() : style;
    const int want_size = SizeAtStyle(BaseSize, target);
    if (Size == want_size && TextSize == BaseSize && target.Id == CurrentStyle.Id) {
        return *this;
    }
    HtmlOptions result = *this;
    result.CurrentStyle = target;
    result.Size = want_size;
    result.Refresh();
    return result;
}

HtmlOptions HtmlOptions::HavingBaseSizing() const {
    int size = 6;
    switch (CurrentStyle.Id) {
        case 4:
        case 5:
            size = 3;
            break;
        case 6:
        case 7:
            size = 1;
            break;
        default:
            size = 6;
            break;
    }
    HtmlOptions result = *this;
    result.CurrentStyle = CurrentStyle.Text();
    result.Size = size;
    result.Refresh();
    return result;
}

HtmlOptions HtmlOptions::WithColor(std::string_view color) const {
    HtmlOptions result = *this;
    result.Color = color;
    result.HasColor = true;
    return result;
}

HtmlOptions HtmlOptions::WithPhantom() const {
    HtmlOptions result = *this;
    result.Phantom = true;
    return result;
}

HtmlOptions HtmlOptions::WithFont(std::string_view font) const {
    HtmlOptions result = *this;
    result.Font = font;
    return result;
}

HtmlOptions HtmlOptions::WithTextFontFamily(std::string_view family) const {
    HtmlOptions result = *this;
    result.FontFamily = family;
    result.Font = {};
    return result;
}

HtmlOptions HtmlOptions::WithTextFontWeight(std::string_view weight) const {
    HtmlOptions result = *this;
    result.FontWeight = weight;
    result.Font = {};
    return result;
}

HtmlOptions HtmlOptions::WithTextFontShape(std::string_view shape) const {
    HtmlOptions result = *this;
    result.FontShape = shape;
    result.Font = {};
    return result;
}

namespace {
// The names of the sizing classes.  KaTeX's sizes run from one to eleven
// (`\tiny` is one and `\Huge` is eleven), so the table holds every size of the
// renderer plus the zero of the table.
constexpr std::string_view kSizeClassNames[] = {
    "size0",  "size1",  "size2",  "size3",  "size4",  "size5",  "size6",
    "size7",  "size8",  "size9",  "size10", "size11",
};
constexpr std::string_view kResetSizeClassNames[] = {
    "reset-size0",  "reset-size1",  "reset-size2",  "reset-size3",  "reset-size4",
    "reset-size5",  "reset-size6",  "reset-size7",  "reset-size8",  "reset-size9",
    "reset-size10", "reset-size11",
};
}  // namespace

std::string_view HtmlOptions::SizeClassName(int size) {
    if (size >= 0 && size < static_cast<int>(std::size(kSizeClassNames))) {
        return kSizeClassNames[size];
    }
    // A size outside the table is rare; the name is copied into the arena of
    // the nodes, so that it lives as long as the render and a second size
    // cannot overwrite it.
    return CopyToNodeArena("size" + std::to_string(size));
}

std::string_view HtmlOptions::ResetSizeClassName(int size) {
    if (size >= 0 && size < static_cast<int>(std::size(kResetSizeClassNames))) {
        return kResetSizeClassNames[size];
    }
    return CopyToNodeArena("reset-size" + std::to_string(size));
}

void HtmlOptions::AddSizingClasses(ClassList& classes, const HtmlOptions& old_options) const {
    if (old_options.Size == Size) {
        return;
    }
    classes.Add("katex-sizing");
    classes.Add(ResetSizeClassName(old_options.Size));
    classes.Add(SizeClassName(Size));
}

void HtmlOptions::AddBaseSizingClasses(ClassList& classes) const {
    if (Size == BaseSize) {
        return;
    }
    classes.Add("katex-sizing");
    classes.Add(ResetSizeClassName(Size));
    classes.Add(SizeClassName(BaseSize));
}

std::optional<std::string_view> HtmlOptions::GetColor() const {
    if (Phantom) {
        return std::string_view{"transparent"};
    }
    if (!HasColor) {
        return std::nullopt;
    }
    return Color;
}

}  // namespace velocitex::html
