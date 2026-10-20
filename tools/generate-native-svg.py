"""Generate native immutable SVG contours from the locked glyph outline catalog."""
import json
from pathlib import Path

root = Path(__file__).resolve().parent.parent
data = json.loads((root / "src/VelociTex.Rendering.Svg/Generated/glyph-outlines.json").read_text(encoding="utf-8"))
assert data["upstreamVersion"] == "0.19.0"
fonts = data["fonts"]
lines = ['#include "svg_data.hpp"', 'namespace velocitex::generated {', 'namespace {']
for index, font in enumerate(fonts):
    lines.append(f"const std::string_view Paths{index}[] = {{")
    lines.extend(json.dumps(path, ensure_ascii=True) + "," for path in font["paths"])
    lines.append("};")
lines.append("const SvgFont Fonts[] = {")
lines.extend(f"{{{font['unitsPerEm']},Paths{index}}}," for index, font in enumerate(fonts))
lines.extend(["};", "}", "const std::span<const SvgFont> SvgFonts = Fonts;", "}", ""])
(root / "native/core/generated/svg_data.cpp").write_text("\n".join(lines), encoding="utf-8")
print(f"Generated SVG contours for {len(fonts)} fonts / {sum(len(font['paths']) for font in fonts)} glyphs")
