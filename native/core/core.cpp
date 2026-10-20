#include "core.hpp"

#include "layout.hpp"
#include "mathml.hpp"

#include <charconv>

namespace velocitex {
namespace {
template <class T>
void trim(std::vector<T>& values, size_t limit) {
    values.clear();
    if (values.capacity() > limit)
        std::vector<T>().swap(values);
}
MathOptions detached_options(const MathOptions& original) {
    MathOptions result = original;
    result.Macros.clear();
    result.Session = nullptr;
    result.TextShaper = nullptr;
    result.StrictHandler = {};
    result.CheckCancellation = {};
    return result;
}
struct WorkspaceScope {
    MathWorkspace& Workspace;
    MacroSession* Session;
    ~WorkspaceScope() {
        if (Session)
            Session->Leave();
        Workspace.Leave();
    }
};
} // namespace
void retain_document(MathDocument* document) noexcept {
    if (document)
        document->References.fetch_add(1, std::memory_order_relaxed);
}
void release_document(MathDocument* document) noexcept {
    if (document && document->References.fetch_sub(1, std::memory_order_acq_rel) == 1)
        delete document;
}
bool MathWorkspace::Enter() {
    bool expected = false;
    return Busy.compare_exchange_strong(expected, true);
}
void MathWorkspace::Leave() {
    auto& p = Parser;
    trim(p.Pending, 4096);
    trim(p.Expanded, 4096);
    trim(p.Arguments, 4096);
    trim(p.Nodes, 4096);
    trim(p.Tasks, 1024);
    trim(p.Undo, 256);
    trim(p.Scopes, 128);
    trim(p.ScopeKinds, 128);
    trim(p.ExpansionModes, 128);
    trim(p.Warnings, 128);
    p.Definitions.clear();
    if (p.Definitions.bucket_count() > 2048)
        p.Definitions.rehash(0);
    Busy.store(false);
}
void MathWorkspace::Clear() {
    if (!Enter())
        throw std::logic_error("Cannot dispose a workspace during compilation.");
    Parser = ParserWorkspace{};
    Busy.store(false);
}
bool valid_options(const MathOptions& o) {
    return std::isfinite(o.FontSize) && o.FontSize > 0 && o.FontSize <= 1024 && o.MaxInputLength > 0 &&
           o.MaxInputLength <= 1048576 && o.MaxNodes > 0 && o.MaxNodes <= 262144 && o.MaxNesting > 0 &&
           o.MaxNesting <= 1024 && o.MaxExpansion > 0 && o.MaxExpansion <= 1000000 && o.MaxTokens > 0 &&
           o.MaxTokens <= 1048576 && !std::isnan(o.MaxSize) && o.MaxSize >= 0 && std::isfinite(o.MinRuleThickness) &&
           o.MinRuleThickness >= 0 && std::isfinite(o.DisplayWidth) && o.DisplayWidth >= 0 && o.DisplayWidth <= 32768 &&
           o.EquationNumber > 0 && o.EquationNumber <= 1000000 && int(o.StrictMode) >= 0 && int(o.StrictMode) <= 2;
}
bool try_compile(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                 std::unique_ptr<MathDocument>& document, MathDiagnostic& diagnostic, bool layout) {
    document.reset();
    diagnostic = {};
    if (!valid_options(options)) {
        diagnostic = {
            MathDiagnosticCode::InvalidOptions, {0, 0}, u"Options exceed supported resource or font-size bounds."};
        return false;
    }
    if (source.size() > size_t(options.MaxInputLength)) {
        diagnostic = {MathDiagnosticCode::InputLimit,
                      {options.MaxInputLength, int(source.size()) - options.MaxInputLength},
                      u"Input length budget exceeded."};
        return false;
    }
    if (!workspace.Enter()) {
        diagnostic = {MathDiagnosticCode::WorkspaceInUse, {0, 0}, u"Workspace is already in use."};
        return false;
    }
    WorkspaceScope scope{workspace, nullptr};
    try {
        check_cancel(options);
        if (options.Session) {
            if (!options.Session->Enter()) {
                diagnostic = {MathDiagnosticCode::SessionInUse, {0, 0}, u"Macro session is already in use."};
                return false;
            }
            scope.Session = options.Session;
        }
        auto result = std::make_unique<MathDocument>();
        result->Source = source;
        const CancellationToken cancellation(options.CheckCancellation);
        Expand(result->Source, options, workspace.Parser, cancellation);
        result->Root = Parse(result->Source, options, workspace.Parser, cancellation);
        result->Nodes = workspace.Parser.Nodes;
        if (layout) {
            result->Layout = build_layout(result->Nodes, result->Root, options);
            result->HasLayout = true;
        }
        check_cancel(options);
        result->Warnings = workspace.Parser.Warnings;
        result->Options = detached_options(options);
        if (options.Session)
            options.Session->Commit(workspace.Parser.Definitions);
        document = std::move(result);
        return true;
    } catch (const CompileFailure& failure) {
        diagnostic = failure.Diagnostic;
        return false;
    }
}
bool try_compile_presentation(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                              bool render_errors, MathColor error_color, std::unique_ptr<MathDocument>& document,
                              MathDiagnostic& diagnostic, bool& error_fallback) {
    error_fallback = false;
    if (try_compile(source, options, workspace, document, diagnostic))
        return true;
    switch (diagnostic.Code) {
    case MathDiagnosticCode::InvalidOptions:
    case MathDiagnosticCode::InputLimit:
    case MathDiagnosticCode::NodeLimit:
    case MathDiagnosticCode::TokenLimit:
    case MathDiagnosticCode::NestingLimit:
    case MathDiagnosticCode::ExpansionLimit:
    case MathDiagnosticCode::Cancelled:
    case MathDiagnosticCode::WorkspaceInUse:
    case MathDiagnosticCode::SessionInUse:
    case MathDiagnosticCode::UnknownSymbol:
        return false;
    default:
        break;
    }
    if (!render_errors || !workspace.Enter())
        return false;
    WorkspaceScope scope{workspace, nullptr};
    try {
        auto result = std::make_unique<MathDocument>();
        result->Source = source;
        result->Options = detached_options(options);
        result->Root = 0;
        auto& nodes = result->Nodes;
        const SourceRange range{0, int(source.size())};
        nodes.reserve(std::min(size_t(options.MaxNodes), source.size() + 4));
        nodes.emplace_back(NodeKind::Row, range);
        nodes.back().FirstChild = 1;
        nodes.emplace_back(NodeKind::Color, range);
        nodes.back().FirstChild = 2;
        const char16_t* hex = u"0123456789abcdef";
        std::u16string color = u"#";
        for (auto value : {error_color.R, error_color.G, error_color.B}) {
            color.push_back(hex[value >> 4]);
            color.push_back(hex[value & 15]);
        }
        if (error_color.A != 255) {
            color.push_back(hex[error_color.A >> 4]);
            color.push_back(hex[error_color.A & 15]);
        }
        nodes.back().Text2 = std::move(color);
        nodes.emplace_back(NodeKind::Text, range);
        nodes.back().FirstChild = 3;
        nodes.back().TextMode = true;
        nodes.back().Font = MathFont::MathRoman;
        nodes.back().Text = u"\\text";
        nodes.emplace_back(NodeKind::Row, range);
        nodes.back().TextMode = true;
        nodes.back().Font = MathFont::MathRoman;
        int previous = -1;
        for (size_t position = 0; position < source.size();) {
            check_cancel(options);
            if (nodes.size() >= size_t(options.MaxNodes))
                return false;
            int length;
            int cp = scalar_at(source, position, length);
            if (cp >= 0xd800 && cp <= 0xdfff)
                cp = 0xfffd;
            if (cp == u'\r' || cp == u'\n' || cp == u'\t')
                cp = u' ';
            const int id = int(nodes.size());
            nodes.emplace_back(NodeKind::Symbol, SourceRange{int(position), length});
            nodes.back().CodePoint = cp;
            nodes.back().TextMode = true;
            nodes.back().Font = MathFont::MathRoman;
            if (previous < 0)
                nodes[3].FirstChild = id;
            else
                nodes[previous].NextSibling = id;
            previous = id;
            position += length;
        }
        result->Layout = build_layout(nodes, 0, options);
        result->HasLayout = true;
        document = std::move(result);
        error_fallback = true;
        return true;
    } catch (const CompileFailure& failure) {
        if (failure.Diagnostic.Code == MathDiagnosticCode::Cancelled)
            diagnostic = failure.Diagnostic;
        return false;
    }
}
const std::string& document_mathml(MathDocument& document, bool include_annotation) {
    auto& once = include_annotation ? document.MathMlOnce : document.UnannotatedOnce;
    auto& output = include_annotation ? document.AnnotatedMathMl : document.UnannotatedMathMl;
    std::call_once(once, [&] {
        output = write_mathml(document.Source, document.Options, document.Nodes, document.Root, include_annotation);
    });
    return output;
}
} // namespace velocitex
