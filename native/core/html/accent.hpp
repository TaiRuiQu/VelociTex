// KaTeX-compatible rendering of accents, horizontal braces and arrows.
//
// Port of KaTeX 0.19.0 `src/functions/accent.ts`,
// `src/functions/horizBrace.ts` and `src/functions/arrow.ts`.
#pragma once

#include <string_view>

#include "module_api.hpp"

namespace velocitex::html {

// Renders a `NodeKind::Accent` node.
HtmlNodePtr BuildAccentNode(const ModuleContext& context, int index, const HtmlOptions& options);

// True when the accent label names one of KaTeX's horizontal braces
// (`\overbrace`, `\underbrace`, `\overbracket` and `\underbracket`).
bool IsHorizBraceLabel(std::u16string_view label);

// KaTeX's `normalizeArgument` (`src/defineFunction.ts`): the math accents
// unwrap a group that holds a single element.
int NormalizeAccentBase(const ModuleContext& context, int index);

// Renders a horizontal brace that carries a super/subscript.  KaTeX's
// `supsub` builder delegates to the brace in that case
// (`src/functions/supsub.ts`, `htmlBuilderDelegate`).  The script is built by
// the caller, which passes the base options to the group builder.
HtmlNodePtr BuildHorizBraceNode(const ModuleContext& context, int index, const HtmlOptions& options,
                                HtmlNodePtr script);

// KaTeX's `staticSvg` (`src/buildCommon.ts`): an inline SVG that is overlaid
// on top of another element.  KaTeX uses it for `\vec` and for the oval of
// `\oiint` and `\oiiint`.
NodePtr<HtmlSpan> MakeStaticSvg(std::u16string_view path_name, double width, double height,
                                        const HtmlOptions& options);

}  // namespace velocitex::html
