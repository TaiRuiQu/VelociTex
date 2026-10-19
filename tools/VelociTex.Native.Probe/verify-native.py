"""Development-only independent SVG rendering and native-output inspection.

Requires Pillow and Playwright in the development Python environment. No runtime dependency.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import xml.etree.ElementTree as ET
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageStat
from playwright.sync_api import sync_playwright

parser = argparse.ArgumentParser()
parser.add_argument("root", type=Path)
parser.add_argument("--browser", default=r"C:\Program Files\Google\Chrome\Application\chrome.exe")
args = parser.parse_args()
root = args.root.resolve()
expected_packages = {"Pillow": "11.1.0", "playwright": "1.55.0"}
for name, expected in expected_packages.items():
    assert importlib.metadata.version(name) == expected, f"Audit dependency drift: {name}"
browser_lock = json.loads((Path(__file__).resolve().parents[2] / "docs/reference/development-toolchain.lock.json").read_text(encoding="utf-8"))
assert hashlib.sha256(Path(args.browser).read_bytes()).hexdigest() == browser_lock["browserSha256"], "Audit browser binary drift"
result = {"kind": "P0 hard-coded native probe image audit", "native": [], "svg": []}
result["auditDependencies"] = expected_packages
result["browserSha256"] = browser_lock["browserSha256"]
jit_report = json.loads((root / "jit" / "report.json").read_text(encoding="utf-8"))
aot_report = json.loads((root / "aot" / "report.json").read_text(encoding="utf-8"))
assert jit_report["dynamicCodeSupported"] is True
assert aot_report["dynamicCodeSupported"] is False
for theme in ("light", "dark"):
    svg_path = root / "aot" / f"probe-{theme}.svg"
    assert svg_path.read_bytes() == (root / "jit" / svg_path.name).read_bytes()
    xml = ET.parse(svg_path)
    for node in xml.iter():
        assert node.tag.split("}")[-1] not in ("script", "text", "image", "foreignObject")
        for key, value in node.attrib.items():
            if key.split("}")[-1] == "href":
                assert value.startswith("#")
for path in sorted((root / "aot").glob("probe-*.png")):
    image = Image.open(path).convert("RGBA")
    percent = int(path.stem.rsplit("-", 1)[1])
    assert image.size == (640 * percent // 100, 180 * percent // 100)
    alpha = image.getchannel("A")
    bounds = alpha.getbbox()
    assert bounds and bounds[0] > 0 and bounds[1] > 0
    assert bounds[2] < image.width and bounds[3] < image.height
    assert alpha.getextrema() == (0, 255)
    assert sum(1 for n in alpha.getdata() if 0 < n < 255) > 100
    jit_path = root / "jit" / path.name
    assert path.read_bytes() == jit_path.read_bytes(), f"JIT/AOT output differs: {path.name}"
    peer = path.with_name(path.name.replace("-warp-", "-wic-"))
    assert path.read_bytes() == peer.read_bytes(), f"WIC/WARP output differs: {path.name}"
    result["native"].append({"file": path.name, "size": image.size, "inkBounds": bounds,
                             "transparent": True, "jitAotEqual": True, "wicWarpEqual": True,
                             "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})

with sync_playwright() as p:
    browser = p.chromium.launch(executable_path=args.browser, headless=True)
    result["svgBrowserVersion"] = browser.version
    for theme in ("light", "dark"):
        for percent in (100, 125, 150, 200):
            context = browser.new_context(viewport={"width": 640, "height": 180},
                                          device_scale_factor=percent / 100)
            page = context.new_page()
            page.goto((root / "aot" / f"probe-{theme}.svg").as_uri())
            page.locator("svg").wait_for()
            screenshot = root / "aot" / f"svg-preview-{theme}-{percent}.png"
            page.screenshot(path=str(screenshot), omit_background=True)
            svg = Image.open(screenshot).convert("RGBA")
            native = Image.open(root / "aot" / f"probe-warp-{theme}-{percent}.png").convert("RGBA")
            bounds = svg.getchannel("A").getbbox()
            native_bounds = native.getchannel("A").getbbox()
            max_delta = max(abs(a-b) for a,b in zip(bounds, native_bounds))
            assert max_delta <= 2, f"SVG/native ink bounds differ: {theme}/{percent}: {bounds}/{native_bounds}"
            diff = ImageChops.difference(svg.getchannel("A"), native.getchannel("A"))
            rmse = ImageStat.Stat(diff).rms[0] / 255
            result["svg"].append({"theme": theme, "scalePercent": percent, "size": svg.size,
                                   "svgInkBounds": bounds, "nativeInkBounds": native_bounds,
                                   "maxBoundDeltaPixels": max_delta, "alphaRmseWholeCanvas": rmse})
            context.close()
    browser.close()

sheet = Image.new("RGB", (1280, 440), "#ffffff")
draw = ImageDraw.Draw(sheet)
for row, theme in enumerate(("light", "dark")):
    background = "#ffffff" if theme == "light" else "#141821"
    for col, (kind, filename) in enumerate((("Native WARP", f"probe-warp-{theme}-100.png"),
                                             ("Independent SVG", f"svg-preview-{theme}-100.png"))):
        top = row * 220
        draw.rectangle((col * 640, top, col * 640 + 639, top + 219), fill=background)
        draw.text((col * 640 + 12, top + 8), f"{kind}; {theme}; P0 hard-coded probe", fill="#263046" if theme == "light" else "#e7eaf0")
        image = Image.open(root / "aot" / filename).convert("RGBA")
        sheet.paste(image, (col * 640, top + 30), image)
sheet.save(root / "visual-comparison.png")
(root / "image-audit.json").write_text(json.dumps(result, indent=2), encoding="utf-8")
print(f"PASS: {len(result['native'])} native PNGs, {len(result['svg'])} independently rendered SVGs; JIT/AOT and WIC/WARP byte equality")
