#pragma once
#include "parser.hpp"

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
    std::once_flag MathMlOnce, UnannotatedOnce;
    std::string AnnotatedMathMl, UnannotatedMathMl;
};
void retain_document(MathDocument* document) noexcept;
void release_document(MathDocument* document) noexcept;
bool valid_options(const MathOptions& options);
bool try_compile(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                 std::unique_ptr<MathDocument>& document, MathDiagnostic& diagnostic, bool layout = true);
bool try_compile_presentation(std::u16string_view source, const MathOptions& options, MathWorkspace& workspace,
                              bool render_errors, MathColor error_color, std::unique_ptr<MathDocument>& document,
                              MathDiagnostic& diagnostic, bool& error_fallback);
const std::string& document_mathml(MathDocument& document, bool include_annotation = true);
} // namespace velocitex
