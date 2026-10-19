"""Review all P1 raster pairs at their recorded formula origin and baseline.

Visual alignment uses only the immutable document baseline / SVG viewBox and
the independent browser marker coordinates. No fitted translation or scale is
estimated. The automatic 95% edge criterion is copied into labels, never changed.
Only review sheets use bicubic subpixel translation; checks use original pixels.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from collections import defaultdict
from pathlib import Path
from xml.etree import ElementTree

from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]


def read(path: Path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def mask(image: Image.Image, background: tuple[int, int, int] | None, foreground: int):
    pixels = image.convert("RGBA").getdata()
    values = []
    for pixel in pixels:
        if background is None:
            values.append(255 if pixel[3] > 32 else 0)
        else:
            visible = pixel[3] > 32 and any(abs(pixel[c] - background[c]) / abs(foreground - background[c]) > 32 / 255 for c in range(3))
            values.append(255 if visible else 0)
    result = Image.new("L", image.size)
    result.putdata(values)
    return result


def normalized_bbox(mask_image: Image.Image, origin: tuple[float, float]):
    bounds = mask_image.getbbox()
    if bounds is None:
        return None
    return [bounds[0] - origin[0], bounds[1] - origin[1], bounds[2] - origin[0], bounds[3] - origin[1]]


def bbox_delta(a, b):
    if a is None or b is None:
        return None
    return max(abs(x - y) for x, y in zip(a, b))


def aligned(image, origin, common_origin, canvas_size, background):
    # output pixel p reads original pixel p + originalOrigin - commonOrigin.
    return image.convert("RGB").transform(canvas_size, Image.Transform.AFFINE,
        (1, 0, origin[0] - common_origin[0], 0, 1, origin[1] - common_origin[1]),
        resample=Image.Resampling.BICUBIC, fillcolor=background)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("folder", nargs="?", default="artifacts/p1/jit")
    parser.add_argument("--phase", choices=("p1", "p2", "p4"), default="p1")
    parser.add_argument("--compact", action="store_true", help="Show sixty actual raster pairs per sheet.")
    parser.add_argument("--deduplicate", action="store_true", help="Review identical raster/origin pairs once while retaining all identities in the audit.")
    args = parser.parse_args()
    directory = ROOT / args.folder
    report = read(directory / "report.json")
    reference_root = ROOT / "artifacts" / args.phase / "reference"
    reference = read(reference_root / "reference.json")
    checks = read(directory / "image-check.json")
    expected = {(x["id"], x["scenario"]): x for x in reference["results"]}
    measured = {(x["id"], x["scenario"]): x for x in checks["checks"]}
    out = ROOT / "artifacts" / args.phase / "review" / directory.name
    out.mkdir(parents=True, exist_ok=True)
    try:
        font = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 16)
        small = ImageFont.truetype("C:/Windows/Fonts/consola.ttf", 13)
    except OSError:
        font = small = ImageFont.load_default()
    cell_width, cell_height, columns, rows = (330, 190, 6, 10) if args.compact else (620, 390, 4, 6)
    sheet = None
    audit = []
    pair_paths = []
    seen_pairs = {}
    displayed = 0
    for index, actual in enumerate(x for x in report["results"] if x["success"]):
        key = (actual["id"], actual["scenario"])
        ref, check = expected[key], measured[key]
        scale = ref["dpiScale"]
        dark = ref["theme"] == "dark"
        background = (22, 22, 22) if dark else (255, 255, 255)
        foreground = 238 if dark else 17
        location = directory / actual["id"] / actual["scenario"]
        native_path = location / "formula.png"
        reference_path = reference_root / actual["id"] / actual["scenario"] / "reference.png"
        native_digest = hashlib.sha256(native_path.read_bytes()).hexdigest()
        svg_digest = hashlib.sha256((location / "formula.svg").read_bytes()).hexdigest()
        if native_digest != check["pngSha256"] or svg_digest != check["svgSha256"]:
            raise RuntimeError(f"Stale raster-check labels for {actual['id']}/{actual['scenario']}; run p1-images.mjs again first.")
        native = Image.open(native_path).convert("RGBA")
        browser = Image.open(reference_path).convert("RGBA")
        svg = ElementTree.parse(location / "formula.svg").getroot()
        view_box = list(map(float, svg.attrib["viewBox"].split()))
        native_origin = (-view_box[0] * scale, (actual["baseline"] - view_box[1]) * scale)
        reference_origin = (ref["geometry"]["origin"]["x"] * scale, ref["geometry"]["origin"]["y"] * scale)
        native_mask = mask(native, None, foreground)
        reference_mask = mask(browser, background, foreground)
        native_bbox = normalized_bbox(native_mask, native_origin)
        reference_bbox = normalized_bbox(reference_mask, reference_origin)
        ink = actual["inkBounds"]
        logical_ink = None if ink["width"] == 0 or ink["height"] == 0 else [
            ink["x"] * scale, (ink["y"] - actual["baseline"]) * scale,
            (ink["x"] + ink["width"]) * scale, (ink["y"] + ink["height"] - actual["baseline"]) * scale]
        row = {"id": actual["id"], "scenario": actual["scenario"], "category": actual["category"],
            "nativeOriginDevicePixels": native_origin, "referenceOriginDevicePixels": reference_origin,
            "svgViewBox": view_box, "documentBaselineDip": actual["baseline"],
            "ttfAndPathInkBoundsRelativeToBaselineDevicePixels": logical_ink,
            "nativeMaskBoundsRelativeToBaselineDevicePixels": native_bbox,
            "browserMaskBoundsRelativeToBaselineDevicePixels": reference_bbox,
            "nativeVersusInkMaximumBoundDeltaDevicePixels": bbox_delta(native_bbox, logical_ink),
            "browserVersusInkMaximumBoundDeltaDevicePixels": bbox_delta(reference_bbox, logical_ink),
            "nativeVersusBrowserMaximumBoundDeltaDevicePixels": bbox_delta(native_bbox, reference_bbox),
            "thresholdPass": all(check[k] >= 0.95 for k in ("nativeToReference", "referenceToNative", "nativeToSvg", "svgToNative")) and not check["edgeClipping"],
            "nativeToReference": check["nativeToReference"], "referenceToNative": check["referenceToNative"],
            "nativeToSvg": check["nativeToSvg"], "svgToNative": check["svgToNative"],
            "toleranceDevicePixels": check["toleranceDevicePixels"],
            "nativePngSha256": native_digest,
            "browserPngSha256": hashlib.sha256(reference_path.read_bytes()).hexdigest()}
        audit.append(row)
        identity_key = (native_digest, row["browserPngSha256"], tuple(native_origin), tuple(reference_origin), actual["baseline"])
        if args.deduplicate and identity_key in seen_pairs:
            row["reviewedIdenticalPair"] = seen_pairs[identity_key]
            continue
        seen_pairs[identity_key] = {"id": actual["id"], "scenario": actual["scenario"]}

        # Preserve all ink and use the original device-pixel scale. Padding and
        # common origin follow declared geometry, never the observed ink edges.
        left = math.ceil(max(native_origin[0], reference_origin[0])) + 5
        baseline = math.ceil(max(native_origin[1], reference_origin[1])) + 4
        right = math.ceil(max(native.width - native_origin[0], browser.width - reference_origin[0])) + 5
        bottom = math.ceil(max(native.height - native_origin[1], browser.height - reference_origin[1])) + 4
        common_origin = (left, baseline)
        size = (left + right, baseline + bottom)
        opaque_native = Image.new("RGBA", native.size, background + (255,))
        opaque_native.alpha_composite(native)
        n = aligned(opaque_native, native_origin, common_origin, size, background)
        b = aligned(browser, reference_origin, common_origin, size, background)
        pair = Image.new("RGB", (max(620, size[0] + 36), size[1] * 2 + 78), "#f4f4f4")
        draw = ImageDraw.Draw(pair)
        flag = "PASS" if row["thresholdPass"] else "FAIL"
        draw.text((8, 3), f"{actual['id']} | {actual['scenario']} | {flag}", fill="#185520" if flag == "PASS" else "#b01818", font=font)
        draw.text((8, 24), f"edge N->K {check['nativeToReference']:.3f} K->N {check['referenceToNative']:.3f} 95%; tol {check['toleranceDevicePixels']:.2f}px", fill="#222222", font=small)
        draw.text((8, 42), f"native/SVG {check['nativeToSvg']:.3f}/{check['svgToNative']:.3f}; recorded origin only", fill="#333333", font=small)
        for label, image, y in (("N", n, 72), ("K", b, 72 + size[1])):
            draw.text((3, y + 2), label, fill="#202020", font=font)
            pair.paste(image, (28, y))
            dd = ImageDraw.Draw(pair)
            dd.line((28, y + baseline, 28 + size[0], y + baseline), fill="#008d88", width=1)
            dd.line((28 + left, y, 28 + left, y + size[1]), fill="#c63b94", width=1)
        pair_path = out / f"pair-{index + 1:03d}-{actual['id']}-{actual['scenario']}.png"
        pair.save(pair_path)
        pair_paths.append(pair_path)

        page_index, slot = divmod(displayed, columns * rows)
        displayed += 1
        if slot == 0:
            sheet = Image.new("RGB", (columns * cell_width, rows * cell_height + 42), "#d3d8dc")
            ImageDraw.Draw(sheet).text((10, 8), f"{args.phase.upper()} {directory.name}: all pairs page {page_index+1}; N native / K KaTeX; turquoise baseline / magenta x=0", fill="#121212", font=font)
        cell = pair.copy()
        cell.thumbnail((cell_width - 10, cell_height - 10), Image.Resampling.LANCZOS)
        sheet.paste(cell, ((slot % columns) * cell_width + 5, (slot // columns) * cell_height + 42))
        if slot == columns * rows - 1:
            sheet.save(out / f"review-page-{page_index+1:02d}.png")

    if sheet is not None and displayed % (columns * rows):
        sheet.save(out / f"review-page-{page_index+1:02d}.png")

    summary = {"pairs": len(audit), "displayedUniquePairs": displayed, "deduplication": args.deduplicate, "thresholdPass": sum(x["thresholdPass"] for x in audit),
        "thresholdFail": sum(not x["thresholdPass"] for x in audit),
        "maximumNativeVersusInkBoundDeltaDevicePixels": max((x["nativeVersusInkMaximumBoundDeltaDevicePixels"] or 0) for x in audit),
        "maximumBrowserVersusInkBoundDeltaDevicePixels": max((x["browserVersusInkMaximumBoundDeltaDevicePixels"] or 0) for x in audit),
        "maximumNativeVersusBrowserBoundDeltaDevicePixels": max((x["nativeVersusBrowserMaximumBoundDeltaDevicePixels"] or 0) for x in audit)}
    groups = defaultdict(list)
    for row in audit:
        groups[row["scenario"].split("-")[-1]].append(row)
    summary["themes"] = {key: {"pairs": len(values), "failures": sum(not x["thresholdPass"] for x in values),
        "minimumNativeToReference": min(x["nativeToReference"] for x in values),
        "minimumReferenceToNative": min(x["referenceToNative"] for x in values)} for key, values in groups.items()}
    manifest = {"method": "Original raster masks at alpha coverage > 32/255. Native origin from SVG viewBox and document baseline; reference origin from independent DOM marker. No fitted offsets or scaling. Contact sheets alone use bicubic translation by recorded origins, then fit cells for review; original pixel checks unchanged.",
        "sourceReport": str(directory / "report.json"),
        "sourceReportSha256": hashlib.sha256((directory / "report.json").read_bytes()).hexdigest(),
        "rasterCheckSha256": hashlib.sha256((directory / "image-check.json").read_bytes()).hexdigest(),
        "referenceReportSha256": hashlib.sha256((reference_root / "reference.json").read_bytes()).hexdigest(),
        "summary": summary, "pairs": audit}
    (out / "review-audit.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2, allow_nan=False) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(out), **summary}))


if __name__ == "__main__":
    main()
