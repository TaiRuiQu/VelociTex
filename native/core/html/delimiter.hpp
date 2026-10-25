// KaTeX-compatible stretchy delimiters.
//
// Port of KaTeX 0.19.0 `src/delimiter.ts` (`makeCustomSizedDelim`,
// `makeSizedDelim`, `makeStackedDelim`, `makeInner`, `makeLeftRightDelim`,
// `makeSmallDelim`, `makeLargeDelim`, `makeGlyphSpan`, `styleWrap`,
// `centerSpan`, `traverseSequence` and `delimTypeToFont`) together with the
// SVG geometry helpers `innerPath` and `tallDelim` from `src/svgGeometry.ts`.
#pragma once

#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "domtree.hpp"
#include "options.hpp"

namespace velocitex::html {

// KaTeX's `makeCustomSizedDelim`: a delimiter whose total height is at least
// `height` em.
NodePtr<HtmlSpan> MakeCustomSizedDelim(std::u16string_view delim, double height, bool center,
                                               const HtmlOptions& options, Mode mode,
                                               ClassList&& classes);

// KaTeX's `makeSizedDelim`: a delimiter of a fixed size (1 to 4), as used by
// `\bigl` and friends.
NodePtr<HtmlSpan> MakeSizedDelim(std::u16string_view delim, int size, const HtmlOptions& options,
                                          Mode mode, ClassList&& classes);

// KaTeX's `makeLeftRightDelim`: the delimiter used by `\left` and `\right`.
NodePtr<HtmlSpan> MakeLeftRightDelim(std::u16string_view delim, double height, double depth,
                                              const HtmlOptions& options, Mode mode,
                                              ClassList&& classes);

// KaTeX's `innerPath` (`src/svgGeometry.ts`): the inner part of a stretchy
// tall delimiter.
std::string InnerPath(char16_t name, int height);

// KaTeX's `tallDelim` (`src/svgGeometry.ts`): the outline of a stretchy tall
// delimiter.
std::string TallDelim(std::string_view label, int mid_height);

// Maps the delimiter code points stored in the semantic tree to the
// delimiter names KaTeX uses in `src/delimiter.ts` (for example 123 to
// "\\{").  Returns an empty string for unknown delimiters.
std::u16string DelimiterName(int code_point);

}  // namespace velocitex::html
