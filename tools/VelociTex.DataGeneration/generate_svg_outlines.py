"""Generate portable SVG outlines from checksum-locked, unchanged KaTeX fonts.

Build-time only: pip install -r requirements-wasm.txt. Generated contours retain
font-unit coordinates with downward Y. No system font or Windows API is used.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path

from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.ttLib import TTFont

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    lock = json.loads((ROOT / "docs/reference/katex-0.19.0.lock.json").read_bytes())
    metrics = json.loads((ROOT / "src/VelociTex.Core/Generated/font-data.json").read_bytes())
    expected = {f["file"]: f for f in lock["ttfFonts"]}
    fonts = []
    for entry in metrics["fonts"]:
        data = (ROOT / "src/VelociTex.Core/Resources/Fonts" / entry["file"]).read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if digest != expected[entry["file"]]["sha256"]:
            raise ValueError(f"Locked font checksum mismatch: {entry['file']}")
        with TTFont(io.BytesIO(data)) as font:
            glyphs = font.getGlyphSet()
            outlines = []
            for name in font.getGlyphOrder():
                pen = SVGPathPen(glyphs)
                glyphs[name].draw(TransformPen(pen, (1, 0, 0, -1, 0, 0)))
                outlines.append(pen.getCommands())
            fonts.append({"name": entry["name"], "sha256": digest,
                          "unitsPerEm": font["head"].unitsPerEm, "paths": outlines})
    result = {"schemaVersion": 1, "upstreamVersion": "0.19.0", "fonts": fonts}
    data = (json.dumps(result, ensure_ascii=True, separators=(",", ":")) + "\n").encode()
    target = ROOT / "src/VelociTex.Rendering.Svg/Generated/glyph-outlines.json"
    if args.check:
        if target.read_bytes() != data:
            raise ValueError("Generated SVG outlines differ; regenerate with pinned FontTools")
    else:
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    print(f"SVG outlines: {len(fonts)} fonts, {sum(len(f['paths']) for f in fonts)} glyphs, {len(data)} bytes")


if __name__ == "__main__":
    main()
