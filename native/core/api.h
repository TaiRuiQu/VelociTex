#pragma once
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#define VT_CORE_API __declspec(dllexport)
#else
#define VT_CORE_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct vt_string16 {
    const uint16_t* Data;
    int32_t Length;
} vt_string16;
typedef struct vt_string8 {
    const char* Data;
    int32_t Length;
} vt_string8;
typedef struct vt_range {
    int32_t Start, Length;
} vt_range;
typedef struct vt_bounds {
    double X, Y, Width, Height;
} vt_bounds;
typedef struct vt_diagnostic {
    int32_t Code;
    vt_range Range;
    vt_string16 Message;
} vt_diagnostic;
typedef struct vt_warning {
    vt_string16 Code;
    vt_range Range;
    vt_string16 Message;
} vt_warning;
typedef struct vt_macro {
    vt_string16 Name, Body;
} vt_macro;
typedef struct vt_segment {
    int32_t Operation;
    double X1, Y1, X2, Y2, X3, Y3;
} vt_segment;
typedef struct vt_path {
    const vt_segment* Segments;
    int32_t SegmentCount;
    double X, Y, ScaleX, ScaleY;
    vt_bounds ClipBounds;
} vt_path;
typedef struct vt_shape {
    double Advance;
    const vt_segment* Segments;
    int32_t SegmentCount;
    vt_bounds InkBounds;
    vt_string16 FontFamily;
} vt_shape;
typedef struct vt_run_shape {
    double Advance;
    const vt_path* Paths;
    int32_t PathCount;
    vt_bounds InkBounds;
} vt_run_shape;
typedef int32_t (*vt_strict_handler)(void* context, const vt_warning* warning);
typedef int32_t (*vt_shape_handler)(void* context, int32_t code_point, int32_t font, vt_shape* shape);
typedef int32_t (*vt_run_handler)(void* context, vt_string16 text, int32_t font, vt_run_shape* shape);
typedef struct vt_options {
    uint32_t StructSize;
    double FontSize;
    int32_t DisplayMode, MaxInputLength, MaxNodes, MaxNesting, MaxExpansion, MaxTokens;
    double MaxSize, MinRuleThickness;
    int32_t ColorIsTextColor, GlobalGroup, Leqno, Fleqn;
    double DisplayWidth;
    int32_t EquationNumber, StrictMode, ThrowOnError;
    const vt_macro* Macros;
    int32_t MacroCount;
    void* Session;
    void* CallbackContext;
    vt_strict_handler StrictHandler;
    vt_shape_handler ShapeHandler;
    vt_run_handler RunHandler;
    const int32_t* CancellationFlag;
} vt_options;
typedef struct vt_glyph {
    int32_t FontId;
    uint16_t GlyphId;
    int32_t CodePoint;
    double X, Y, FontSize;
} vt_glyph;
typedef struct vt_rule {
    double X, Y, Width, Height;
} vt_rule;
typedef struct vt_transform {
    double M11, M12, M21, M22, Dx, Dy;
} vt_transform;
typedef struct vt_paint {
    int32_t Kind, Index, StateIndex;
} vt_paint;
typedef struct vt_state {
    int32_t HasColor;
    uint8_t R, G, B, A;
    vt_transform Transform;
} vt_state;
typedef struct vt_text_glyph {
    int32_t CodePoint;
    double X, Y, FontSize;
    vt_string16 FontFamily;
} vt_text_glyph;
typedef struct vt_text_run {
    vt_string16 Text;
    double X, Y, FontSize;
    int32_t FirstPath, PathCount;
    double Advance;
} vt_text_run;
typedef struct vt_length {
    double Number;
    int32_t Unit;
} vt_length;
typedef struct vt_node {
    int32_t Kind;
    vt_range Range;
    int32_t FirstChild, NextSibling, SecondChild, ThirdChild, FourthChild, CodePoint, OriginalCodePoint, FontIdOverride;
    int32_t Atom, Font, SymbolFont, Style, TextMode;
    double Value, Value2, Value3;
    vt_string16 Text, Alignment, Text2, Text3;
    vt_length Length1, Length2, Length3;
    int32_t LeftDelimiter, RightDelimiter, ColumnCount, RuleAbove, RuleBelow;
    int32_t HasBar, Limits, AlwaysLimits, NeverLimits, IsMathOrd;
} vt_node;
typedef struct vt_document_view {
    double Width, Height, Baseline;
    vt_bounds InkBounds;
    const vt_glyph* Glyphs;
    int32_t GlyphCount;
    const vt_rule* Rules;
    int32_t RuleCount;
    const vt_paint* PaintCommands;
    int32_t PaintCount;
    int32_t PathCount, StateCount, TextGlyphCount, TextRunCount, WarningCount, NodeCount, Root;
} vt_document_view;

/* Status: 0 success, 1 formula diagnostic, 2 invalid ABI argument,
   3 host callback failure, 4 allocation failure, 5 internal failure.
   Strings and views borrow their owner and never require caller deallocation. */
VT_CORE_API uint32_t vt_core_abi_version(void);
VT_CORE_API void vt_core_default_options(vt_options* options);
VT_CORE_API void* vt_core_workspace_create(void);
VT_CORE_API int32_t vt_core_workspace_clear(void* workspace);
VT_CORE_API void vt_core_workspace_destroy(void* workspace);
VT_CORE_API void* vt_core_session_create(void);
VT_CORE_API void vt_core_session_destroy(void* session);
VT_CORE_API int32_t vt_core_compile(void* workspace, vt_string16 source, const vt_options* options, int32_t with_layout,
                                    void** result, vt_diagnostic* diagnostic);
VT_CORE_API int32_t vt_core_present(void* workspace, vt_string16 source, const vt_options* options,
                                    int32_t render_errors, uint32_t error_rgba, void** result,
                                    vt_diagnostic* diagnostic, int32_t* error_fallback);
VT_CORE_API void vt_core_result_destroy(void* result);
VT_CORE_API int32_t vt_core_result_view(void* result, vt_document_view* view);
VT_CORE_API int32_t vt_core_result_path(void* result, int32_t index, vt_path* path);
VT_CORE_API int32_t vt_core_result_state(void* result, int32_t index, vt_state* state);
VT_CORE_API int32_t vt_core_result_text_glyph(void* result, int32_t index, vt_text_glyph* glyph);
VT_CORE_API int32_t vt_core_result_text_run(void* result, int32_t index, vt_text_run* run);
VT_CORE_API int32_t vt_core_result_warning(void* result, int32_t index, vt_warning* warning);
VT_CORE_API int32_t vt_core_result_node(void* result, int32_t index, vt_node* node);
VT_CORE_API int32_t vt_core_result_mathml(void* result, int32_t include_annotation, vt_string8* output);
VT_CORE_API int32_t vt_core_result_html(void* result, vt_string8* output);
VT_CORE_API int32_t vt_core_parse_color(vt_string16 text, uint32_t* rgba);
#ifdef __cplusplus
}
#endif
