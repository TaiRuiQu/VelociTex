#!/usr/bin/env python3
"""Check the browser calibration against the independently decoded TTFs."""
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
path = ROOT / "artifacts/fonts/browser-calibration.json"
data = json.loads(path.read_bytes())
font_data = json.loads((ROOT / "src/VelociTex.Core/Generated/font-data.json").read_bytes())
assert data["upstreamVersion"] == "0.19.0"
assert len(data["glyphs"]) == 20 and len(data["formulas"]) == 8
assert all(glyph["fontLoaded"] for glyph in data["glyphs"])
measurements = []
for glyph in data["glyphs"]:
    font = next(font for font in font_data["fonts"] if font["name"] == glyph["name"])
    glyph_id = next(item["glyphId"] for item in font["cmap"] if item["codePoint"] == glyph["codePoint"])
    assert glyph_id == glyph["glyphId"]
    expected = font["glyphs"][glyph_id]["advanceUnits"] / font["unitsPerEm"] * 40
    error = abs(glyph["canvas"]["advancePx"] - expected)
    assert error < 1 / 64, (glyph["name"], error)
    measurements.append({"font": glyph["name"], "codePoint": glyph["codePoint"], "glyphId": glyph_id, "ttfAdvance40px": expected, "canvasAdvancePx": glyph["canvas"]["advancePx"], "absoluteErrorPx": error})
css = data["css"]
size = lambda text: float(text.removesuffix("px"))
assert abs(size(css["katexFontSize"]) / size(css["rawFontSize"]) - 1.21) < 1e-12
baseline_delta = abs(css["rawBaselineY"] - css["katexBaselineY"])
assert baseline_delta < 1 / 64
assert 'margin-right: 0.1076em' in data["formulas"][1]["html"]
summary = {"schemaVersion": 1, "upstreamVersion": data["upstreamVersion"], "browserVersion": data["browser"]["browserVersion"], "browserSha256": data["browser"]["browserSha256"], "devicePixelRatio": data["devicePixelRatio"], "privateFontsLoaded": len(measurements), "formulaSamples": len(data["formulas"]), "maxCanvasAdvanceErrorPx": max(row["absoluteErrorPx"] for row in measurements), "cssFontSizeScale": size(css["katexFontSize"]) / size(css["rawFontSize"]), "cssRawWidthPx": css["raw"]["width"], "cssKaTeXWidthPx": css["katex"]["width"], "cssBaselineDeltaPx": baseline_delta, "fItalicCorrectionCssEm": 0.1076, "measurements": measurements, "artifacts": []}
for name in ("browser-calibration.json", "font-calibration.png"):
    content = (path.parent / name).read_bytes()
    summary["artifacts"].append({"path": "artifacts/fonts/" + name, "sha256": hashlib.sha256(content).hexdigest(), "bytes": len(content)})
result = json.dumps(summary, ensure_ascii=False, indent=2) + "\n"
(ROOT / "docs/reference/font-browser-calibration-0.19.0.json").write_text(result, encoding="utf-8", newline="\n")
print(json.dumps({key: summary[key] for key in ("privateFontsLoaded", "formulaSamples", "maxCanvasAdvanceErrorPx", "cssFontSizeScale", "cssBaselineDeltaPx")}))
