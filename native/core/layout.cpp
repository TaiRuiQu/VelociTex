#include "layout.hpp"

#include "generated/data.hpp"
#include "geometry.hpp"
#include "layout_support.hpp"
namespace velocitex {
static thread_local int _;
namespace RadicalPathData {
extern const std::vector<MathPathSegment> sqrtMain, sqrtSize1, sqrtSize2, sqrtSize3, sqrtSize4;
}
struct Box {
    double Width{}, Height{}, Depth{}, Italic{};
    MathAtom Atom{};
    bool Character{}, Symbol{}, NoAtom{};
    int Font{}, GlyphIndex{}, FirstDecoration{};
    double GlyphScale{}, GlyphY{}, Skew{};
    bool TextShaped{};
    MathTextShape TextShape;
    bool TextRunShaped{};
    MathTextRunShape TextRunShape;
    std::u16string RunText;
    std::vector<MathPathSegment> Path;
    double PathX{}, PathY{}, PathScale{}, PathHeight{};
    bool DecorationsAfter{};
};
enum class DecorationKind { Glyph, Path, Rule };
struct Decoration {
    DecorationKind Kind{};
    int Next{}, Font{}, GlyphIndex{}, CodePoint{};
    double X{}, Y{}, Width{}, Height{}, ScaleX{}, ScaleY{};
    std::vector<MathPathSegment> Path;
    MathBounds Clip;
    bool Vertical{}, Exact{}, HasColor{};
    MathColor Color;
};
struct Frame {
    int Node{};
    LayoutContext Context;
    bool Complete{};
};
struct Placement {
    int Node{};
    double X{}, Y{};
    int State{};
    bool Decorations{};
};
struct StretchyInfo {
    std::u16string Left;
    std::optional<std::u16string> Right;
    double MinWidth{}, Height{};
    bool AlignRight{};
    std::optional<std::u16string> Middle;
};
constexpr uint8_t SupStyles[] = {4, 5, 4, 5, 6, 7, 6, 7}, SubStyles[] = {5, 5, 5, 5, 7, 7, 7, 7},
                  NumeratorStyles[] = {2, 3, 4, 5, 6, 7, 6, 7}, DenominatorStyles[] = {3, 3, 5, 5, 7, 7, 7, 7};
constexpr auto& SizeMultipliers = LayoutContext::Multipliers;
struct LayoutScratch {
    std::vector<Box> Boxes;
    std::vector<LayoutContext> Styles;
    std::vector<uint8_t> ScriptBases, Hidden;
    std::vector<double> X, Y;
    std::vector<Frame> Frames;
    std::vector<int> Sequence, CollectStack, ColorStack;
    std::vector<MathAtom> Atoms;
    std::vector<std::optional<MathColor>> Colors;
    std::vector<Placement> Placements;
    std::vector<Decoration> Decorations;
    std::vector<MathGlyph> Glyphs;
    std::vector<MathRule> Rules;
    std::vector<MathPath> Paths;
    std::vector<MathTextGlyph> TextGlyphs;
    std::vector<MathTextRun> TextRuns;
    std::vector<MathPaintCommand> PaintCommands;
    std::vector<MathPaintState> PaintStates;
};
struct ScratchPool {
    std::vector<std::unique_ptr<LayoutScratch>> Buffers;
    size_t Depth{};
};
static thread_local ScratchPool scratchPool;
struct ScratchLease {
    size_t Index;
    ScratchLease() : Index(scratchPool.Depth++) {
        try {
            if (Index == scratchPool.Buffers.size())
                scratchPool.Buffers.push_back(std::make_unique<LayoutScratch>());
        } catch (...) {
            --scratchPool.Depth;
            throw;
        }
    }
    ~ScratchLease() { --scratchPool.Depth; }
    LayoutScratch& Get() { return *scratchPool.Buffers[Index]; }
};
template <class T>
std::vector<T>& Ensure(std::vector<T>& b, int n) {
    if ((int)b.size() < n)
        b.resize(n);
    return b;
}
struct Layout {
    static bool Find(int font, int cp, int& index, TexGlyphMetric& metric) {
        const auto& f = FontData::Fonts[font];
        auto begin = FontData::Glyphs.begin() + f.GlyphStart, end = begin + f.GlyphCount;
        auto g = std::lower_bound(begin, end, cp, [](const auto& a, int b) { return a.CodePoint < b; });
        if (g == end || g->CodePoint != cp) {
            index = -1;
            metric = {};
            return false;
        }
        index = int(g - FontData::Glyphs.begin());
        auto mb = FontData::TexMetrics.begin() + f.MetricStart, me = mb + f.MetricCount;
        auto m = std::lower_bound(mb, me, cp, [](const auto& a, int b) { return a.CodePoint < b; });
        if (m != me && m->CodePoint == cp) {
            metric = *m;
            return true;
        }
        metric = {};
        int ignored;
        if (font == 3 && Find(4, cp, ignored, metric))
            return true;
        if (font == 1 && Find(2, cp, ignored, metric))
            return true;
        return false;
    }
#include "layout_declarations.inc"
};
#include "layout_methods.inc"
LayoutOutput build_layout(std::span<const SemanticNode> nodes, int root, const MathOptions& options) {
    return Layout::Build(nodes, root, options);
}
double mathml_length(TexLength v, MathStyle s, uint8_t t, const MathOptions& o) {
    return Layout::MathMlLength(v, s, t, o);
}
double mathml_size_scale(int s) {
    return Layout::MathMlSizeScale(s);
}
} // namespace velocitex
