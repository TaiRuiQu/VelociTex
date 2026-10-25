#pragma once
#include "parser.hpp"
#include "html/markup.hpp"

#include <atomic>
#include <mutex>

namespace velocitex {
struct MathWorkspace {
    std::atomic<bool> Busy{false};
    ParserWorkspace Parser;
    bool Enter();
    void Leave();
    void Clear();
};
struct MathDocument {
    // Results can be shared by the native document cache and ABI callers.
    // The initial reference belongs to the caller that receives the result.
    std::atomic<uint32_t> References{1};
    std::u16string Source;
    MathOptions Options;
    std::vector<SemanticNode> Nodes;
    int Root{};
    LayoutOutput Layout;
    std::vector<MathWarning> Warnings;
    bool HasLayout{};
    // The HTML and the two MathML variants are built on demand and then
    // cached.  The cache is guarded by a mutex so that a document can be
    // shared between threads.  A document that is reused for another formula
    // resets the flags together with the markup.
    std::mutex MarkupMutex;
    bool MathMlReady{false}, UnannotatedReady{false}, HtmlReady{false};
    html::MarkupBuffer AnnotatedMathMl, UnannotatedMathMl;
    // The HTML markup is written into a buffer that is reused between renders.
    html::MarkupBuffer Html;
};
void retain_document(MathDocument* document) noexcept;
void release_document(MathDocument* document) noexcept;
bool valid_options(const MathOptions& options);
bool try_compile(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                 std::unique_ptr<MathDocument>& document, MathDiagnostic& diagnostic, bool layout = true);
bool try_compile_presentation(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                              bool render_errors, MathColor error_color, std::unique_ptr<MathDocument>& document,
                              MathDiagnostic& diagnostic, bool& error_fallback);
std::string_view document_mathml(MathDocument& document, bool include_annotation = true);
std::string_view document_html(MathDocument& document);
// Renders the HTML markup of the document into `out` instead of into the
// cached buffer of the document.  A caller that collects the parts of one
// response in a single buffer does not need the copy of the cached markup.
void document_html_into(MathDocument& document, html::MarkupBuffer& out);
// Renders the MathML markup of the document into `out` instead of into the
// cached buffer of the document.
void document_mathml_into(MathDocument& document, html::MarkupBuffer& out, bool include_annotation);
} // namespace velocitex
