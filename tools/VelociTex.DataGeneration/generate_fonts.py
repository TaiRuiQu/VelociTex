#!/usr/bin/env python3
"""Verify locked KaTeX TTFs and emit deterministic, browser-free font tables.

Development tool only. Uses Python's standard library; no font library, TeX,
JavaScript runtime, network access, or installed/system font is used.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
GENERATOR_VERSION = 1


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


class StaticMetricsParser:
    """Accept only the static object/array/string/number grammar of this file.

    This is deliberately not a JavaScript evaluator or a command registry
    extractor. Unexpected executable syntax fails instead of being guessed.
    """

    token = re.compile(r'\s+|//[^\n]*|/\*[\s\S]*?\*/|"(?:[^"\\]|\\.)*"|-?(?:\d+\.?\d*|\.\d+)(?:[eE][+-]?\d+)?|[{}\[\]:,;]|[A-Za-z_$][\w$]*')

    def __init__(self, text: str):
        self.tokens: list[str] = []
        end = 0
        for match in self.token.finditer(text):
            if match.start() != end:
                raise ValueError(f"Unsupported font metrics syntax at {end}")
            end = match.end()
            value = match.group()
            if not value.isspace() and not value.startswith(("//", "/*")):
                self.tokens.append(value)
        if end != len(text):
            raise ValueError(f"Unsupported font metrics syntax at {end}")
        self.index = 0

    def take(self, expected: str | None = None) -> str:
        result = self.tokens[self.index]
        self.index += 1
        if expected is not None and expected != result:
            raise ValueError(f"Expected {expected}, got {result}")
        return result

    def value(self):
        current = self.take()
        if current == "{":
            result = {}
            while self.tokens[self.index] != "}":
                key = json.loads(self.take())
                self.take(":")
                if key in result:
                    raise ValueError(f"Duplicate metrics key {key}")
                result[key] = self.value()
                if self.tokens[self.index] != ",":
                    break
                self.take(",")
            self.take("}")
            return result
        if current == "[":
            result = []
            while self.tokens[self.index] != "]":
                result.append(self.value())
                if self.tokens[self.index] != ",":
                    break
                self.take(",")
            self.take("]")
            return result
        if current.startswith('"'):
            return json.loads(current)
        return float(current)

    def parse(self) -> dict:
        self.take("export")
        self.take("default")
        result = self.value()
        if self.index < len(self.tokens):
            self.take(";")
        if self.index != len(self.tokens):
            raise ValueError("Unexpected trailing font metrics syntax")
        return result


class Sfnt:
    def __init__(self, data: bytes):
        self.data = data
        if data[:4] != b"\x00\x01\x00\x00":
            raise ValueError("Expected a TrueType sfnt")
        self.tables = {}
        for index in range(self.u16(4)):
            offset = 12 + 16 * index
            tag = data[offset:offset + 4].decode("ascii")
            checksum, start, length = struct.unpack_from(">III", data, offset + 4)
            if start + length > len(data):
                raise ValueError(f"Truncated table {tag}")
            self.tables[tag] = (start, length, checksum)

    def u16(self, offset: int) -> int:
        return struct.unpack_from(">H", self.data, offset)[0]

    def i16(self, offset: int) -> int:
        return struct.unpack_from(">h", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        return struct.unpack_from(">I", self.data, offset)[0]

    def table(self, tag: str) -> int:
        return self.tables[tag][0]

    def names(self) -> list[dict]:
        start = self.table("name")
        storage = start + self.u16(start + 4)
        names = []
        for index in range(self.u16(start + 2)):
            platform, encoding, language, name_id, length, offset = struct.unpack_from(">6H", self.data, start + 6 + 12 * index)
            raw = self.data[storage + offset:storage + offset + length]
            codec = "utf-16-be" if platform in (0, 3) else "mac_roman" if platform == 1 else "latin-1"
            names.append({"nameId": name_id, "platformId": platform, "encodingId": encoding, "languageId": language, "text": raw.decode(codec)})
        return names

    def cmap(self) -> tuple[dict[int, int], list[dict]]:
        start = self.table("cmap")
        candidates = []
        for index in range(self.u16(start + 2)):
            platform, encoding, relative = struct.unpack_from(">HHI", self.data, start + 4 + 8 * index)
            subtable = start + relative
            fmt = self.u16(subtable)
            if platform == 0 or (platform == 3 and encoding in (1, 10)):
                if fmt not in (4, 12):
                    raise ValueError(f"Unsupported Unicode cmap format {fmt}")
                candidates.append((fmt, platform, encoding, subtable))
        if not candidates:
            raise ValueError("No Unicode cmap")
        result = {}
        for fmt, platform, encoding, subtable in sorted(candidates):
            values = {}
            if fmt == 12:
                for group in range(self.u32(subtable + 12)):
                    first, last, first_glyph = struct.unpack_from(">III", self.data, subtable + 16 + group * 12)
                    for cp in range(first, last + 1):
                        values[cp] = first_glyph + cp - first
            else:
                count = self.u16(subtable + 6) // 2
                ends = subtable + 14
                starts = ends + 2 * count + 2
                deltas = starts + 2 * count
                ranges = deltas + 2 * count
                for segment in range(count):
                    first, last = self.u16(starts + 2 * segment), self.u16(ends + 2 * segment)
                    delta = self.i16(deltas + 2 * segment)
                    range_offset = self.u16(ranges + 2 * segment)
                    for cp in range(first, min(last, 0xFFFE) + 1):
                        if range_offset:
                            glyph = self.u16(ranges + 2 * segment + range_offset + 2 * (cp - first))
                            if glyph:
                                glyph = (glyph + delta) & 0xFFFF
                        else:
                            glyph = (cp + delta) & 0xFFFF
                        if glyph:
                            values[cp] = glyph
            for cp, glyph in values.items():
                if cp in result and result[cp] != glyph:
                    raise ValueError(f"Unicode cmaps disagree for U+{cp:04X}")
                result[cp] = glyph
        return result, [{"format": f, "platformId": p, "encodingId": e} for f, p, e, _ in candidates]

    def extract(self) -> dict:
        head, hhea, maxp = self.table("head"), self.table("hhea"), self.table("maxp")
        upem = self.u16(head + 18)
        glyph_count, horizontal_count = self.u16(maxp + 4), self.u16(hhea + 34)
        cmap, subtables = self.cmap()
        loca, glyf, hmtx = self.table("loca"), self.table("glyf"), self.table("hmtx")
        long_loca = self.i16(head + 50) == 1
        offsets = [self.u32(loca + 4 * i) if long_loca else 2 * self.u16(loca + 2 * i) for i in range(glyph_count + 1)]
        glyphs = []
        for glyph_id in range(glyph_count):
            metric = hmtx + 4 * min(glyph_id, horizontal_count - 1)
            advance = self.u16(metric)
            lsb = self.i16(metric + 2) if glyph_id < horizontal_count else self.i16(hmtx + 4 * horizontal_count + 2 * (glyph_id - horizontal_count))
            bounds = [0, 0, 0, 0] if offsets[glyph_id] == offsets[glyph_id + 1] else list(struct.unpack_from(">4h", self.data, glyf + offsets[glyph_id] + 2))
            glyphs.append({"glyphId": glyph_id, "advanceUnits": advance, "leftSideBearingUnits": lsb, "inkBoundsUnits": bounds})
        if any(glyph >= glyph_count for glyph in cmap.values()):
            raise ValueError("cmap points beyond maxp glyph count")
        result = {"unitsPerEm": upem, "glyphCount": glyph_count, "ascenderUnits": self.i16(hhea + 4), "descenderUnits": self.i16(hhea + 6), "lineGapUnits": self.i16(hhea + 8), "unicodeCmaps": subtables, "names": self.names(), "cmap": [{"codePoint": cp, "glyphId": glyph} for cp, glyph in sorted(cmap.items())], "glyphs": glyphs}
        if "OS/2" in self.tables:
            os2 = self.table("OS/2")
            result["os2"] = {"weightClass": self.u16(os2 + 4), "selection": self.u16(os2 + 62), "typoAscenderUnits": self.i16(os2 + 68), "typoDescenderUnits": self.i16(os2 + 70), "typoLineGapUnits": self.i16(os2 + 72), "winAscentUnits": self.u16(os2 + 74), "winDescentUnits": self.u16(os2 + 76)}
        return result


def csharp_number(value: float | int) -> str:
    return format(value, ".12g") + "d"


def generate_csharp(fonts: list[dict], source_digest: str) -> str:
    lines = ["// <auto-generated />", "// KaTeX 0.19.0. Regenerate with tools/VelociTex.DataGeneration/generate_fonts.py.", f"// font-data.json SHA-256: {source_digest}", "namespace VelociTex;", "", "internal readonly record struct FontDescriptor(string Name, string ResourceFileName, ushort UnitsPerEm, int GlyphStart, int GlyphCount, int MetricStart, int MetricCount);", "internal readonly record struct FontGlyph(ushort CodePoint, ushort GlyphId, ushort AdvanceUnits, short LeftSideBearingUnits);", "internal readonly record struct TexGlyphMetric(ushort CodePoint, double Depth, double Height, double ItalicCorrection, double Skew, double Width);", "", "internal static class FontData", "{", "    internal static readonly FontDescriptor[] Fonts =", "    ["]
    glyph_offset = metric_offset = 0
    for font in fonts:
        count = len(font["cmap"])
        metric_count = len(font["texMetrics"])
        lines.append(f'        new("{font["name"]}", "{font["file"]}", {font["unitsPerEm"]}, {glyph_offset}, {count}, {metric_offset}, {metric_count}),')
        glyph_offset += count
        metric_offset += metric_count
    lines.extend(["    ];", "", "    internal static readonly FontGlyph[] Glyphs =", "    ["])
    for font in fonts:
        lines.append(f'        // {font["name"]}')
        for mapping in font["cmap"]:
            glyph = font["glyphs"][mapping["glyphId"]]
            if mapping["codePoint"] > 65535:
                raise ValueError("Update generated codepoint type for supplementary font glyph")
            lines.append(f'        new({mapping["codePoint"]}, {mapping["glyphId"]}, {glyph["advanceUnits"]}, {glyph["leftSideBearingUnits"]}),')
    lines.extend(["    ];", "", "    internal static readonly TexGlyphMetric[] TexMetrics =", "    ["])
    for font in fonts:
        lines.append(f'        // {font["name"]}: depth, height, italic correction, skew, width (em).')
        for metric in font["texMetrics"]:
            values = ", ".join(csharp_number(value) for value in metric["values"])
            lines.append(f'        new({metric["codePoint"]}, {values}),')
    lines.extend(["    ];", "}", ""])
    return "\n".join(lines)


def calibration_html(fonts: list[dict]) -> str:
    samples = []
    for index, font in enumerate(fonts):
        cp = 40 if font["name"].startswith("Size") else 65
        if font["name"] == "Math-Italic":
            cp = 102
        glyph_id = next(item["glyphId"] for item in font["cmap"] if item["codePoint"] == cp)
        tex = next((item["values"] for item in font["texMetrics"] if item["codePoint"] == cp), None)
        samples.append({"index": index, "name": font["name"], "file": font["file"], "codePoint": cp, "glyphId": glyph_id, "ttfAdvanceEm": font["glyphs"][glyph_id]["advanceUnits"] / font["unitsPerEm"], "texMetrics": tex})
    face_rules = "\n".join(f'@font-face{{font-family:Probe{i};src:url("../../src/VelociTex.Core/Resources/Fonts/{font["file"]}") format("truetype");font-weight:400;font-style:normal;font-display:block}}' for i, font in enumerate(fonts))
    return """<!doctype html>
<meta charset="utf-8"><title>KaTeX 0.19.0 glyph / baseline calibration</title>
<link rel="stylesheet" href="../upstream/npm/package/dist/katex.css">
<style>
""" + face_rules + """
body{font:16px Arial;margin:24px;color:#17202c;background:white}h1{font-size:22px}h2{font-size:18px;margin-top:28px}table{border-collapse:collapse}td,th{border:1px solid #d7dee7;padding:5px 12px;text-align:left}.sample{display:inline-block;position:relative;min-width:120px;white-space:nowrap;font-size:40px}.baseline{display:inline-block;width:0;height:0;position:relative}.baseline:after{content:"";position:absolute;top:0;left:-75px;width:140px;border-top:1px solid #0085cf}.baseline:before{content:"";position:absolute;top:-10px;left:-75px;width:140px;border-top:1px dashed #d34657}.glyph{white-space:pre}.formula{display:inline-block;padding:18px 10px;min-width:180px}.host{font:20px Arial}.probe-box{background:#f0f4f8;padding:10px;margin:8px 0}.note{color:#556274;font-size:13px}#status{white-space:pre-wrap}
</style>
<h1>KaTeX 0.19.0 private TTF calibration</h1>
<p class="note">40px single glyphs. Blue = baseline; dashed red = TeX axis (+0.25em). Browser measurements use exact locked TTF bytes, and the CSS comparison uses the exact npm distribution.</p>
<div id="status">Loading fonts…</div><table><thead><tr><th>Font</th><th>Code point / glyph ID</th><th>Glyph</th><th>TTF advance / TeX width (em)</th></tr></thead><tbody id="glyphs"></tbody></table>
<h2>CSS scaling, formula baseline and italic correction</h2><div class="probe-box host" id="css-host"><span id="css-raw" style="font-family:KaTeX_Main">x</span><span class="baseline" id="raw-base"></span> <span class="katex" id="css-katex"><span style="font-family:KaTeX_Main">x</span></span><span class="baseline" id="katex-base"></span></div>
<div class="probe-box host" id="formulas"></div>
<p class="note">Font advances and TeX metric widths are recorded separately. Raw text width is not a substitute for KaTeX layout. Samples are calibration probes and do not count toward engine compatibility.</p>
<script src="../upstream/npm/package/dist/katex.js"></script><script>
const samples = """ + json.dumps(samples, ensure_ascii=False) + """;
const formulas = [String.raw`x`,String.raw`f`,String.raw`fx`,String.raw`f^2`,String.raw`x_1^2`,String.raw`\\sum_{i=1}^n i`,String.raw`\\frac{x}{y}`,String.raw`\\int_0^1 f(x)\\,dx`];
for(const item of samples){const tr=document.createElement('tr');tr.innerHTML=`<td>${item.name}</td><td>U+${item.codePoint.toString(16).toUpperCase()} / ${item.glyphId}</td><td><span class="sample" style="font-family:Probe${item.index}"><span class="glyph" id="g${item.index}">${String.fromCodePoint(item.codePoint)}</span><span class="baseline" id="b${item.index}"></span></span></td><td>${item.ttfAdvanceEm} / ${item.texMetrics ? item.texMetrics[4] : 'no upstream metric table'}</td>`;document.querySelector('#glyphs').append(tr)}
for(let index=0;index<formulas.length;index++){const span=document.createElement('span');span.className='formula';span.innerHTML=`<span id="f${index}"></span><span class="baseline" id="fb${index}"></span>`;document.querySelector('#formulas').append(span);katex.render(formulas[index],document.querySelector(`#f${index}`),{throwOnError:true,output:'htmlAndMathml'})}
const rect = element => {const r=element.getBoundingClientRect();return {x:r.x,y:r.y,width:r.width,height:r.height}};
window.runFontCalibration=async function(){
  await Promise.all(samples.map(s=>document.fonts.load(`40px Probe${s.index}`,String.fromCodePoint(s.codePoint))));
  await document.fonts.ready;
  const context=document.createElement('canvas').getContext('2d');
  const glyphs=samples.map(item=>{context.font=`40px Probe${item.index}`;const m=context.measureText(String.fromCodePoint(item.codePoint));return {...item,fontLoaded:document.fonts.check(`40px Probe${item.index}`),glyphRect:rect(document.querySelector(`#g${item.index}`)),baselineY:rect(document.querySelector(`#b${item.index}`)).y,canvas:{advancePx:m.width,inkAscentPx:m.actualBoundingBoxAscent,inkDescentPx:m.actualBoundingBoxDescent,fontAscentPx:m.fontBoundingBoxAscent,fontDescentPx:m.fontBoundingBoxDescent}}});
  const result={schemaVersion:1,upstreamVersion:katex.version,userAgent:navigator.userAgent,devicePixelRatio:window.devicePixelRatio,glyphs,css:{hostFontSize:getComputedStyle(document.querySelector('#css-host')).fontSize,rawFontSize:getComputedStyle(document.querySelector('#css-raw')).fontSize,katexFontSize:getComputedStyle(document.querySelector('#css-katex')).fontSize,raw:rect(document.querySelector('#css-raw')),katex:rect(document.querySelector('#css-katex')),rawBaselineY:rect(document.querySelector('#raw-base')).y,katexBaselineY:rect(document.querySelector('#katex-base')).y},formulas:formulas.map((latex,index)=>({latex,rect:rect(document.querySelector(`#f${index}`)),baselineY:rect(document.querySelector(`#fb${index}`)).y,html:document.querySelector(`#f${index}`).innerHTML}))};
  document.querySelector('#status').textContent=`Loaded ${glyphs.filter(g=>g.fontLoaded).length}/20 private fonts. CSS x width ${result.css.raw.width.toFixed(3)}px → ${result.css.katex.width.toFixed(3)}px; font size ${result.css.rawFontSize} → ${result.css.katexFontSize}.`;
  window.fontCalibration=result;return result;
};
window.fontCalibrationReady=window.runFontCalibration();
</script>
"""


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", type=Path, required=True, help="Verified npm package directory containing dist/fonts and src/fontMetricsData.js")
    parser.add_argument("--check", action="store_true", help="Fail if committed generated files differ; do not update them")
    args = parser.parse_args()
    lock_path = ROOT / "tools/reference/data/katex-0.19.0.lock.json"
    lock_bytes = lock_path.read_bytes()
    lock = json.loads(lock_bytes)
    metrics_path = args.upstream / "src/fontMetricsData.js"
    metrics_bytes = metrics_path.read_bytes()
    files_manifest = json.loads((ROOT / "tools/reference/data/katex-0.19.0.files.json").read_bytes())
    expected_metrics = next(item for item in files_manifest["sourceFiles"] if item["path"] == "src/fontMetricsData.js")
    if files_manifest["sourceCommit"] != lock["source"]["commit"] or sha256(metrics_bytes) != expected_metrics["sha256"]:
        raise ValueError("Locked source/fontMetricsData.js checksum mismatch")
    metrics = StaticMetricsParser(metrics_bytes.decode("utf-8")).parse()
    fonts = []
    comparisons = []
    all_copyright = set()
    all_license = set()
    reserved_names = set()
    targets: dict[Path, bytes] = {}
    for expected in lock["ttfFonts"]:
        name = expected["file"]
        path = args.upstream / "dist/fonts" / name
        data = path.read_bytes()
        if len(data) != expected["bytes"] or sha256(data) != expected["sha256"]:
            raise ValueError(f"Locked TTF checksum/size mismatch: {name}")
        font = Sfnt(data).extract()
        font.update({"name": name.removeprefix("KaTeX_").removesuffix(".ttf"), "file": name, "bytes": len(data), "sha256": sha256(data)})
        names = font["names"]
        copyrights = sorted({item["text"] for item in names if item["nameId"] == 0})
        licenses = sorted({item["text"] for item in names if item["nameId"] == 13})
        license_urls = sorted({item["text"] for item in names if item["nameId"] == 14})
        if not licenses or not all("SIL OPEN FONT LICENSE" in text.upper() for text in licenses):
            raise ValueError(f"Missing OFL font name declaration: {name}")
        if not copyrights or not all("Reserved Font Name" in text for text in licenses):
            raise ValueError(f"Missing RFN license declaration: {name}")
        font["license"] = {"spdx": "OFL-1.1", "copyright": copyrights, "licenseUrls": license_urls}
        all_copyright.update(copyrights)
        all_license.update(licenses)
        for text in licenses:
            for rfn in lock["licenses"]["reservedFontNames"]:
                if rfn in text:
                    reserved_names.add(rfn)
        cmap = {item["codePoint"]: item["glyphId"] for item in font["cmap"]}
        font["texMetrics"] = []
        for cp_text, values in sorted(metrics.get(font["name"], {}).items(), key=lambda pair: int(pair[0])):
            cp = int(cp_text)
            if len(values) != 5:
                raise ValueError(f"Expected five TeX metrics: {name} U+{cp:04X}")
            font["texMetrics"].append({"codePoint": cp, "values": values})
            glyph_id = cmap.get(cp)
            advance = None if glyph_id is None else font["glyphs"][glyph_id]["advanceUnits"] / font["unitsPerEm"]
            comparisons.append({"font": font["name"], "codePoint": cp, "glyphId": glyph_id, "texWidthEm": values[4], "ttfAdvanceEm": advance, "deltaEm": None if advance is None else round(advance - values[4], 12)})
        targets[ROOT / "src/VelociTex.Core/Resources/Fonts" / name] = data
        fonts.append(font)
    if reserved_names != set(lock["licenses"]["reservedFontNames"]):
        raise ValueError("The font RFNs differ from the lock")
    ofl_body = (Path(__file__).parent / "OFL-1.1.txt").read_bytes()
    source = {"npmArchiveSha256": lock["npm"]["sha256"], "lockSha256": sha256(lock_bytes), "fontMetricsDataSha256": sha256(metrics_bytes), "oflBodySha256": sha256(ofl_body), "generatorSha256": sha256(Path(__file__).read_bytes())}
    generated = {"schemaVersion": 1, "generatorVersion": GENERATOR_VERSION, "upstreamVersion": "0.19.0", "source": source, "metricsOrder": ["depth", "height", "italicCorrection", "skew", "width"], "fonts": fonts}
    generated_bytes = (json.dumps(generated, ensure_ascii=False, indent=2) + "\n").encode()
    targets[ROOT / "src/VelociTex.Core/Generated/font-data.json"] = generated_bytes
    targets[ROOT / "src/VelociTex.Core/Generated/FontData.g.cs"] = generate_csharp(fonts, sha256(generated_bytes)).encode()
    font_license = "KaTeX font license — font bytes are unchanged.\n\n" + "\n\n".join(sorted(all_copyright)) + "\n\n" + "\n\n".join(sorted(all_license)) + "\n\n" + ofl_body.decode("utf-8")
    targets[ROOT / "licenses/KaTeX-FONTS-OFL.txt"] = font_license.encode()
    artifact_dir = ROOT / "artifacts/fonts"
    artifact_dir.mkdir(parents=True, exist_ok=True)
    write_json(artifact_dir / "advance-comparison.json", {"source": source, "comparisons": comparisons})
    (artifact_dir / "font-calibration.html").write_text(calibration_html(fonts), encoding="utf-8", newline="\n")
    missing = [row for row in comparisons if row["glyphId"] is None]
    differences = [row for row in comparisons if row["deltaEm"] is not None and abs(row["deltaEm"]) > 0.00001]
    summary = {"schemaVersion": 1, "generatorVersion": GENERATOR_VERSION, "upstreamVersion": "0.19.0", "source": source, "ttfCount": len(fonts), "ttfBytes": sum(font["bytes"] for font in fonts), "cmapEntryCount": sum(len(font["cmap"]) for font in fonts), "glyphCount": sum(font["glyphCount"] for font in fonts), "texMetricCount": len(comparisons), "fontsWithoutTexMetrics": [font["name"] for font in fonts if not font["texMetrics"]], "metricsWithoutDirectCmap": missing, "advancesDifferingOver1eMinus5Em": len(differences), "maxAdvanceDifferenceEm": max(abs(row["deltaEm"]) for row in comparisons if row["deltaEm"] is not None), "reservedFontNames": sorted(reserved_names), "files": [{"path": path.relative_to(ROOT).as_posix(), "bytes": len(data), "sha256": sha256(data)} for path, data in sorted(targets.items())]}
    targets[ROOT / "tools/reference/data/font-generation-0.19.0.json"] = (json.dumps(summary, ensure_ascii=False, indent=2) + "\n").encode()
    for path, data in targets.items():
        if args.check:
            if not path.exists() or path.read_bytes() != data:
                raise ValueError(f"Generated file differs: {path.relative_to(ROOT)}")
        else:
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
    write_json(artifact_dir / "font-verification.json", summary)
    print(json.dumps({key: summary[key] for key in ("ttfCount", "ttfBytes", "cmapEntryCount", "glyphCount", "texMetricCount", "advancesDifferingOver1eMinus5Em", "maxAdvanceDifferenceEm")}))
    print(f"Metrics without direct cmap: {len(missing)}; generated bytes {'checked' if args.check else 'written'}.")


if __name__ == "__main__":
    main()
