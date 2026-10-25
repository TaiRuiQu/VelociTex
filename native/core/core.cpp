#include "core.hpp"

#include "layout.hpp"
#include "mathml.hpp"
#include "html/build_html.hpp"

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
    // The macro table, the session and the callbacks are not part of the
    // detached copy.  Building the copy field by field keeps the macro table
    // out of it, and a copy of the table would allocate for every formula.
    MathOptions result;
    result.FontSize = original.FontSize;
    result.DisplayMode = original.DisplayMode;
    result.MaxInputLength = original.MaxInputLength;
    result.MaxNodes = original.MaxNodes;
    result.MaxNesting = original.MaxNesting;
    result.MaxExpansion = original.MaxExpansion;
    result.MaxTokens = original.MaxTokens;
    result.MaxSize = original.MaxSize;
    result.MinRuleThickness = original.MinRuleThickness;
    result.ColorIsTextColor = original.ColorIsTextColor;
    result.GlobalGroup = original.GlobalGroup;
    result.Leqno = original.Leqno;
    result.Fleqn = original.Fleqn;
    result.DisplayWidth = original.DisplayWidth;
    result.EquationNumber = original.EquationNumber;
    result.ThrowOnError = original.ThrowOnError;
    result.ErrorColor = original.ErrorColor;
    result.StrictMode = original.StrictMode;
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
    // The document of the previous formula is reused when the caller hands one
    // in.  Reusing it keeps the node arena, the source buffer and the cached
    // markup alive, so a steady stream of formulas does not allocate and free
    // them over and over again.
    std::unique_ptr<MathDocument> reused = std::move(document);
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
        auto result = reused ? std::move(reused) : std::make_unique<MathDocument>();
        result->Source.assign(source);
        result->Warnings.clear();
        {
            // The cached markup belongs to the previous formula.
            std::lock_guard<std::mutex> lock(result->MarkupMutex);
            result->MathMlReady = false;
            result->UnannotatedReady = false;
            result->HtmlReady = false;
        }
        const CancellationToken cancellation(options.CheckCancellation);
        Expand(result->Source, options, workspace.Parser, cancellation);
        // The parser writes into the arena of the workspace.  The two arenas
        // are swapped so that the parser fills the buffer the document already
        // owns while the workspace keeps a buffer of its own, and neither side
        // has to allocate a new arena for every formula.
        result->Nodes.swap(workspace.Parser.Nodes);
        workspace.Parser.Nodes.clear();
        // The semantic node arena grows with every node the parser creates.
        // Reserving it up front avoids the repeated reallocation of a vector
        // whose elements are several hundred bytes large.
        workspace.Parser.Nodes.reserve(
            std::min<size_t>(size_t(options.MaxNodes), workspace.Parser.Expanded.size() * 2 + 8));
        int root = 0;
        try {
            root = Parse(result->Source, options, workspace.Parser, cancellation);
        } catch (...) {
            result->Nodes.swap(workspace.Parser.Nodes);
            throw;
        }
        result->Nodes.swap(workspace.Parser.Nodes);
        result->Root = root;
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
std::string_view document_html(MathDocument& document) {
    std::lock_guard<std::mutex> lock(document.MarkupMutex);
    if (!document.HtmlReady) {
        document.Html.Clear();
        html::BuildHtmlMarkup(document.Nodes, document.Root, document.Options, document.Html);
        document.HtmlReady = true;
    }
    return document.Html.view();
}
std::string_view document_mathml(MathDocument& document, bool include_annotation) {
    std::lock_guard<std::mutex> lock(document.MarkupMutex);
    bool& ready = include_annotation ? document.MathMlReady : document.UnannotatedReady;
    html::MarkupBuffer& output = include_annotation ? document.AnnotatedMathMl : document.UnannotatedMathMl;
    if (!ready) {
        output.Clear();
        write_mathml(document.Source, document.Options, document.Nodes, document.Root, include_annotation, output);
        ready = true;
    }
    return output.view();
}
void document_html_into(MathDocument& document, html::MarkupBuffer& out) {
    std::lock_guard<std::mutex> lock(document.MarkupMutex);
    html::BuildHtmlMarkup(document.Nodes, document.Root, document.Options, out);
}
void document_mathml_into(MathDocument& document, html::MarkupBuffer& out, bool include_annotation) {
    std::lock_guard<std::mutex> lock(document.MarkupMutex);
    write_mathml(document.Source, document.Options, document.Nodes, document.Root, include_annotation, out);
}
} // namespace velocitex
