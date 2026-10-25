#include "api.h"

#include "core.hpp"

#include <atomic>
#include <type_traits>

using namespace velocitex;
namespace {
thread_local MathDiagnostic last_diagnostic;
struct CallbackFailure : std::exception {};
std::u16string_view view(vt_string16 s) {
    return s.Length == 0 ? std::u16string_view{}
                         : std::u16string_view(reinterpret_cast<const char16_t*>(s.Data), size_t(s.Length));
}
vt_string16 str(std::u16string_view s) {
    return s.empty() ? vt_string16{nullptr, 0}
                     : vt_string16{reinterpret_cast<const uint16_t*>(s.data()), int32_t(s.size())};
}
vt_string16 str(const std::optional<std::u16string>& s) {
    return s ? str(std::u16string_view(*s)) : vt_string16{nullptr, 0};
}
vt_bounds bounds(const MathBounds& b) {
    return {b.X, b.Y, b.Width, b.Height};
}
MathBounds bounds(vt_bounds b) {
    return {b.X, b.Y, b.Width, b.Height};
}
vt_transform transform(const MathTransform& t) {
    return {t.M11, t.M12, t.M21, t.M22, t.Dx, t.Dy};
}
void diagnostic(const MathDiagnostic& d, vt_diagnostic* output) {
    last_diagnostic = d;
    if (output)
        *output = {int32_t(d.Code), {d.Range.Start, d.Range.Length}, str(std::u16string_view(last_diagnostic.Message))};
}
// The host stores the cancellation flag through its own atomic write; an
// acquire load here observes everything the host published before setting it.
int32_t acquire_load(const int32_t* flag) {
#if defined(__EMSCRIPTEN__)
    // Emscripten's libc++ has no std::atomic_ref, and wasm32 has no separate
    // 64-bit atomic ABI for a plain int32_t slot. The browser runs the compile
    // callback on one thread, and a volatile load re-reads shared memory on
    // every check, so it observes a cancellation written between checks.
    return *static_cast<const volatile int32_t*>(flag);
#else
    return std::atomic_ref<const int32_t>(*flag).load(std::memory_order_acquire);
#endif
}
bool valid_string(vt_string16 s) {
    return s.Length >= 0 && (s.Data || s.Length == 0);
}
template <class F>
int32_t guard(F&& action) noexcept {
    try {
        return action();
    } catch (const CallbackFailure&) {
        return 3;
    } catch (const std::bad_alloc&) {
        return 4;
    } catch (...) {
        return 5;
    }
}
struct HostShaper final : IMathTextShaper {
    const vt_options& Options;
    explicit HostShaper(const vt_options& options) : Options(options) {}
    bool TryShape(int cp, MathFont font, MathTextShape& shape) override {
        if (!Options.ShapeHandler)
            return false;
        vt_shape result{};
        const auto status = Options.ShapeHandler(Options.CallbackContext, cp, int(font), &result);
        if (status < 0)
            throw CallbackFailure{};
        if (!status)
            return false;
        if (result.SegmentCount < 0 || (!result.Segments && result.SegmentCount))
            throw CallbackFailure{};
        if (!valid_string(result.FontFamily))
            throw CallbackFailure{};
        shape.Advance = result.Advance;
        shape.InkBounds = bounds(result.InkBounds);
        shape.FontFamily = view(result.FontFamily);
        shape.Segments.clear();
        if (result.SegmentCount) {
            const auto* first = reinterpret_cast<const MathPathSegment*>(result.Segments);
            shape.Segments.assign(first, first + result.SegmentCount);
        }
        return true;
    }
    bool SupportsRuns() const override { return Options.RunHandler != nullptr; }
    bool TryShapeRun(std::u16string_view source, MathFont font, MathTextRunShape& shape) override {
        if (!Options.RunHandler)
            return false;
        vt_run_shape result{};
        const auto status = Options.RunHandler(Options.CallbackContext, str(source), int(font), &result);
        if (status < 0)
            throw CallbackFailure{};
        if (!status)
            return false;
        if (result.PathCount < 0 || (!result.Paths && result.PathCount))
            throw CallbackFailure{};
        shape.Advance = result.Advance;
        shape.InkBounds = bounds(result.InkBounds);
        shape.Paths.clear();
        shape.Paths.reserve(result.PathCount);
        for (int i = 0; i < result.PathCount; i++) {
            const auto& path = result.Paths[i];
            if (path.SegmentCount < 0 || (!path.Segments && path.SegmentCount))
                throw CallbackFailure{};
            MathPath item;
            item.X = path.X;
            item.Y = path.Y;
            item.ScaleX = path.ScaleX;
            item.ScaleY = path.ScaleY;
            item.ClipBounds = bounds(path.ClipBounds);
            if (path.SegmentCount) {
                const auto* first = reinterpret_cast<const MathPathSegment*>(path.Segments);
                item.Segments.assign(first, first + path.SegmentCount);
            }
            shape.Paths.push_back(std::move(item));
        }
        return true;
    }
};
MathOptions convert(const vt_options& o, HostShaper& shaper, int source_length) {
    MathOptions result;
    result.FontSize = o.FontSize;
    result.DisplayMode = o.DisplayMode != 0;
    result.MaxInputLength = o.MaxInputLength;
    result.MaxNodes = o.MaxNodes;
    result.MaxNesting = o.MaxNesting;
    result.MaxExpansion = o.MaxExpansion;
    result.MaxTokens = o.MaxTokens;
    result.MaxSize = o.MaxSize;
    result.MinRuleThickness = o.MinRuleThickness;
    result.ColorIsTextColor = o.ColorIsTextColor != 0;
    result.GlobalGroup = o.GlobalGroup != 0;
    result.Leqno = o.Leqno != 0;
    result.Fleqn = o.Fleqn != 0;
    result.DisplayWidth = o.DisplayWidth;
    result.EquationNumber = o.EquationNumber;
    result.StrictMode = MathStrictMode(o.StrictMode);
    result.ThrowOnError = o.ThrowOnError != 0;
    result.Session = static_cast<MacroSession*>(o.Session);
    if (o.ShapeHandler || o.RunHandler)
        result.TextShaper = &shaper;
    if (o.StrictHandler)
        result.StrictHandler = [&o](const MathWarning& warning) {
            vt_warning value{str(std::u16string_view(warning.Code)),
                             {warning.Range.Start, warning.Range.Length},
                             str(std::u16string_view(warning.Message))};
            int32_t mode = o.StrictHandler(o.CallbackContext, &value);
            if (mode < 0)
                throw CallbackFailure{};
            return MathStrictMode(mode);
        };
    if (o.CancellationFlag)
        result.CheckCancellation = [flag = o.CancellationFlag, source_length] {
            // Emscripten's libc++ has no std::atomic_ref; see acquire_load.
            if (acquire_load(flag))
                throw CompileFailure(
                    {MathDiagnosticCode::Cancelled, {0, source_length}, u"Compilation was cancelled."});
        };
    for (int i = 0; i < o.MacroCount; i++) {
        const auto& m = o.Macros[i];
        if (!valid_string(m.Name) || !valid_string(m.Body))
            throw std::invalid_argument("Invalid macro buffer");
        result.Macros.emplace(view(m.Name), view(m.Body));
    }
    return result;
}
template <class T>
bool index(void* owner, int32_t i, const std::vector<T>& data) {
    return owner && i >= 0 && size_t(i) < data.size();
}
static_assert(sizeof(vt_segment) == sizeof(MathPathSegment) &&
              offsetof(vt_segment, X1) == offsetof(MathPathSegment, X1));
static_assert(sizeof(vt_glyph) == sizeof(MathGlyph) && offsetof(vt_glyph, X) == offsetof(MathGlyph, X));
static_assert(sizeof(vt_rule) == sizeof(MathRule));
static_assert(sizeof(vt_paint) == sizeof(MathPaintCommand));
} // namespace
uint32_t vt_core_abi_version() {
    return 1;
}
void vt_core_default_options(vt_options* o) {
    if (!o)
        return;
    *o = {};
    o->StructSize = sizeof(vt_options);
    o->FontSize = 24;
    o->MaxInputLength = 32768;
    o->MaxNodes = 65536;
    o->MaxNesting = 128;
    o->MaxExpansion = 1000;
    o->MaxTokens = 65536;
    o->MaxSize = std::numeric_limits<double>::infinity();
    o->EquationNumber = 1;
    o->StrictMode = 1;
    o->ThrowOnError = 1;
}
void* vt_core_workspace_create() {
    try {
        return new MathWorkspace;
    } catch (...) {
        return nullptr;
    }
}
int32_t vt_core_workspace_clear(void* p) {
    if (!p)
        return 2;
    return guard([&] {
        static_cast<MathWorkspace*>(p)->Clear();
        return 0;
    });
}
void vt_core_workspace_destroy(void* p) {
    delete static_cast<MathWorkspace*>(p);
}
void* vt_core_session_create() {
    try {
        return new MacroSession;
    } catch (...) {
        return nullptr;
    }
}
void vt_core_session_destroy(void* p) {
    delete static_cast<MacroSession*>(p);
}
int32_t vt_core_compile(void* workspace, vt_string16 source, const vt_options* options, int32_t with_layout,
                        void** result, vt_diagnostic* error) {
    if (result)
        *result = nullptr;
    if (!workspace || !result || !error || !valid_string(source) || !options ||
        options->StructSize != sizeof(vt_options) || options->MacroCount < 0 || options->MacroCount > 1048576 ||
        (!options->Macros && options->MacroCount))
        return 2;
    return guard([&] {
        HostShaper shaper(*options);
        auto settings = convert(*options, shaper, source.Length);
        std::unique_ptr<MathDocument> document;
        MathDiagnostic d;
        bool success =
            try_compile(view(source), settings, *static_cast<MathWorkspace*>(workspace), document, d, with_layout != 0);
        diagnostic(d, error);
        if (!success)
            return 1;
        *result = document.release();
        return 0;
    });
}
int32_t vt_core_present(void* workspace, vt_string16 source, const vt_options* options, int32_t render_errors,
                        uint32_t rgba, void** result, vt_diagnostic* error, int32_t* error_fallback) {
    if (result)
        *result = nullptr;
    if (error_fallback)
        *error_fallback = 0;
    if (!workspace || !result || !error || !error_fallback || !valid_string(source) || !options ||
        options->StructSize != sizeof(vt_options) || options->MacroCount < 0 || options->MacroCount > 1048576 ||
        (!options->Macros && options->MacroCount))
        return 2;
    return guard([&] {
        HostShaper shaper(*options);
        auto settings = convert(*options, shaper, source.Length);
        std::unique_ptr<MathDocument> document;
        MathDiagnostic d;
        bool fallback = false;
        MathColor color{uint8_t(rgba >> 24), uint8_t(rgba >> 16), uint8_t(rgba >> 8), uint8_t(rgba)};
        bool success = try_compile_presentation(view(source), settings, *static_cast<MathWorkspace*>(workspace),
                                                render_errors != 0, color, document, d, fallback);
        diagnostic(d, error);
        *error_fallback = fallback ? 1 : 0;
        if (!success)
            return 1;
        *result = document.release();
        return 0;
    });
}
void vt_core_result_destroy(void* p) {
    release_document(static_cast<MathDocument*>(p));
}
int32_t vt_core_result_view(void* p, vt_document_view* v) {
    if (!p || !v)
        return 2;
    const auto& d = *static_cast<MathDocument*>(p);
    const auto& l = d.Layout;
    *v = {l.LogicalSize.Width,
          l.LogicalSize.Height,
          l.Baseline,
          bounds(l.InkBounds),
          reinterpret_cast<const vt_glyph*>(l.Glyphs.data()),
          int32_t(l.Glyphs.size()),
          reinterpret_cast<const vt_rule*>(l.Rules.data()),
          int32_t(l.Rules.size()),
          reinterpret_cast<const vt_paint*>(l.PaintCommands.data()),
          int32_t(l.PaintCommands.size()),
          int32_t(l.Paths.size()),
          int32_t(l.PaintStates.size()),
          int32_t(l.TextGlyphs.size()),
          int32_t(l.TextRuns.size()),
          int32_t(d.Warnings.size()),
          int32_t(d.Nodes.size()),
          d.Root};
    return 0;
}
int32_t vt_core_result_path(void* p, int32_t i, vt_path* path) {
    if (!p || !path)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Layout.Paths;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    *path = {reinterpret_cast<const vt_segment*>(v.Segments.data()),
             int32_t(v.Segments.size()),
             v.X,
             v.Y,
             v.ScaleX,
             v.ScaleY,
             bounds(v.ClipBounds)};
    return 0;
}
int32_t vt_core_result_state(void* p, int32_t i, vt_state* state) {
    if (!p || !state)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Layout.PaintStates;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    auto c = v.Color.value_or(MathColor{});
    *state = {v.Color ? 1 : 0, c.R, c.G, c.B, c.A, transform(v.Transform)};
    return 0;
}
int32_t vt_core_result_text_glyph(void* p, int32_t i, vt_text_glyph* glyph) {
    if (!p || !glyph)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Layout.TextGlyphs;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    *glyph = {v.CodePoint, v.X, v.Y, v.FontSize, str(std::u16string_view(v.FontFamily))};
    return 0;
}
int32_t vt_core_result_text_run(void* p, int32_t i, vt_text_run* run) {
    if (!p || !run)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Layout.TextRuns;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    *run = {str(std::u16string_view(v.Text)), v.X, v.Y, v.FontSize, v.FirstPath, v.PathCount, v.Advance};
    return 0;
}
int32_t vt_core_result_warning(void* p, int32_t i, vt_warning* warning) {
    if (!p || !warning)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Warnings;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    *warning = {str(std::u16string_view(v.Code)), {v.Range.Start, v.Range.Length}, str(std::u16string_view(v.Message))};
    return 0;
}
int32_t vt_core_result_node(void* p, int32_t i, vt_node* node) {
    if (!p || !node)
        return 2;
    const auto& data = static_cast<MathDocument*>(p)->Nodes;
    if (!index(p, i, data))
        return 2;
    const auto& v = data[i];
    auto length = [](TexLength l) {
        return vt_length{l.Number, int32_t(l.Unit)};
    };
    *node = {int32_t(v.Kind),
             {v.Range.Start, v.Range.Length},
             v.FirstChild,
             v.NextSibling,
             v.SecondChild,
             v.ThirdChild,
             v.FourthChild,
             v.CodePoint,
             v.OriginalCodePoint,
             v.FontIdOverride,
             int32_t(v.Atom),
             int32_t(v.Font),
             int32_t(v.SymbolFont),
             int32_t(v.Style),
             v.TextMode ? 1 : 0,
             v.Value,
             v.Value2,
             v.Value3,
             str(v.Text),
             str(v.Alignment),
             str(v.Text2),
             str(v.Text3),
             length(v.Length1),
             length(v.Length2),
             length(v.Length3),
             v.LeftDelimiter,
             v.RightDelimiter,
             v.ColumnCount,
             v.RuleAbove,
             v.RuleBelow,
             v.HasBar ? 1 : 0,
             v.Limits ? 1 : 0,
             v.AlwaysLimits ? 1 : 0,
             v.NeverLimits ? 1 : 0,
             v.IsMathOrd ? 1 : 0};
    return 0;
}
int32_t vt_core_result_mathml(void* p, int32_t annotation, vt_string8* output) {
    if (!p || !output)
        return 2;
    return guard([&] {
        const auto& value = document_mathml(*static_cast<MathDocument*>(p), annotation != 0);
        *output = {value.data(), int32_t(value.size())};
        return 0;
    });
}
int32_t vt_core_result_html(void* p, vt_string8* output) {
    if (!p || !output)
        return 2;
    return guard([&] {
        const auto& value = document_html(*static_cast<MathDocument*>(p));
        *output = {value.data(), int32_t(value.size())};
        return 0;
    });
}
int32_t vt_core_parse_color(vt_string16 value, uint32_t* rgba) {
    if (!rgba || !valid_string(value))
        return 0;
    MathColor color;
    if (!try_parse_color(view(value), color))
        return 0;
    *rgba = (uint32_t(color.R) << 24) | (uint32_t(color.G) << 16) | (uint32_t(color.B) << 8) | color.A;
    return 1;
}
