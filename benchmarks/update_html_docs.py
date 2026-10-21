"""Refresh HTML/CSS campaign documentation; leave historical release notes intact."""

import argparse
import json
import re
import shutil
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("--data", default="benchmarks/results/2026-10-21-html-css")
parser.add_argument("--release-version", help="Pin package documentation links to this release tag")
args = parser.parse_args()
data = Path(args.data)
summary = json.loads((data / "benchmark-summary.json").read_text(encoding="utf-8"))
if summary["schemaVersion"] != 3:
    raise ValueError("Current documentation requires the HTML/CSS campaign (schema 3)")
names = {"single": "Common formulas", "long": "Complex formulas", "page": "Mixed pages"}
date = summary["manifest"]["utc"][:10]


def value(engine, workload, metric):
    return summary["engines"][engine][workload]["metrics"][metric]["msPerFormula"]


def performance(images, guide):
    lines = [
        "## Performance", "",
        f"{date} · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).",
        "", "Both browser engines generate default HTML with accessibility MathML, using the same locked KaTeX CSS and fonts. Every benchmark formula is checked for byte-identical markup before timing.",
        "", "Correctness validation passed for 3,659 valid core HTML/MathML fixtures and 46 browser integration formulas, including bundled stylesheet and font loading.",
        "", "| Workload | WASM HTML generation | KaTeX HTML generation | WASM DOM + layout | KaTeX DOM + layout |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for w, label in names.items():
        numbers = [value(e, w, m) * 1000 for e, m in [("velocitex", "html"), ("katex", "html"), ("velocitex", "dom"), ("katex", "dom")]]
        lines.append(f"| {label} | " + " | ".join(f"{n:.1f}" for n in numbers) + " |")
    ratios = [value("katex", w, "html") / value("velocitex", w, "html") for w in names]
    dom_ratios = [value("katex", w, "dom") / value("velocitex", w, "dom") for w in names]
    lines += ["", f"WASM/KaTeX relative throughput is **{min(ratios):.2f}–{max(ratios):.2f}×** for HTML generation and **{min(dom_ratios):.2f}–{max(dom_ratios):.2f}×** for DOM render work on this machine. Ratios below 1 mean WASM is slower.",
              "", f"![HTML generation]({images}/benchmark-comparison.png)", "", f"![Browser render work]({images}/benchmark-render-work.png)",
              "", "All 36 formula occurrences are retained: 6 common, 3 complex, and 27 across three mixed pages. Three fresh processes × 15 calibrated batches per case; each case warms for at least 200 ms. Public async WASM API overhead is included. DOM timing forces layout after every formula. Startup, download, browser rasterization, GPU completion and screen presentation are excluded. Native C++/.NET/WinUI results and MathML serialization are separate diagnostics; the C++ Core does not expose this HTML generator.",
              "", f"[Methodology and reproduction]({guide}).", ""]
    return "\n".join(lines)


def rendering(images):
    lines = ["## Rendering comparison", "", "Actual output at 32 px math em and 2× capture resolution. The WASM column uses the current default HTML/CSS renderer and bundled fonts. All four previews have matching computed styles and dimensions in their visible HTML trees. Three have byte-identical markup; the combined color example differs in CSS declaration order and a redundant inherited color declaration. Captures are not asserted pixel-identical. Cells in each row share a canvas and scale. Native uses DirectWrite/Direct2D.", "", "| Formula | Windows native / Direct2D | VelociTex HTML/CSS WASM | KaTeX 0.19.0 |", "| --- | --- | --- | --- |"]
    for item, label in [("algebra", "Algebra"), ("calculus", "Calculus"), ("structures", "Piecewise & matrices"), ("color", "Color, accents & fonts")]:
        cells = [f'<img src="{images}/rendering/{item}-{e}.png" width="280" alt="{label}: {e}">' for e in ("native", "wasm", "katex")]
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def insert(path, section):
    content = path.read_text(encoding="utf-8")
    block = "<!-- benchmark:start -->\n\n" + section.rstrip() + "\n\n<!-- benchmark:end -->"
    if "<!-- benchmark:start -->" in content:
        content = re.sub(r"<!-- benchmark:start -->.*?<!-- benchmark:end -->", lambda _: block, content, flags=re.S)
    else:
        index = content.index("\n## ")
        content = content[:index].rstrip() + "\n\n" + block + "\n" + content[index:]
    path.write_text(content, encoding="utf-8")


for filename in ["README.md", "README.zh-CN.md"]:
    insert(Path(filename), performance("docs/benchmarks", "benchmarks/README.md") + "\n" + rendering("docs/benchmarks"))
# Preserve the measured source revision; release links identify the packaged docs.
ref = "v" + args.release_version if args.release_version else "main"
raw = f"https://raw.githubusercontent.com/TaiRuiQu/VelociTex/{ref}/docs/benchmarks"
guide = f"https://github.com/TaiRuiQu/VelociTex/blob/{ref}/benchmarks/README.md"
insert(Path("PACKAGE-README.md"), performance(raw, guide) + "\n" + rendering(raw))
insert(Path("npm/velocitex/README.md"), performance("images", guide) + "\n" + rendering("images"))

docs = f"# HTML/CSS benchmarks · {date}\n\n" + performance("benchmarks", "../benchmarks/README.md")
docs += "\n![Mixed-page throughput](benchmarks/benchmark-page-throughput.png)\n\n" + rendering("benchmarks")
docs += f"\nSource: `{summary['manifest']['sourceRevision']}` plus the hashed harness changes in the [campaign manifest](../{data.as_posix()}/manifest.json). [All raw batches and summary](../{data.as_posix()}/benchmark-summary.json). Historical preview6 MathML-path results remain in `benchmarks/results/2026-10-20-preview6`.\n"
docs += f"\n[Rendering validation, including the color serialization difference](../{data.as_posix()}/rendering-validation.json). [Combined rendering preview](benchmarks/rendering/comparison-preview.png).\n"
browser = json.loads((data / "run-1/katex/katex-report.json").read_text(encoding="utf-8"))
docs += f"\nEnvironment: {summary['manifest']['cpu'].strip()}, {summary['manifest']['memoryBytes'] / 1024**3:.1f} GiB RAM; {browser['os']}; `{browser['chrome']['userAgent']}`.\n"
docs += "\n## Native and MathML diagnostics\n\nNative output contracts remain MathML, laid-out documents and SVG/Direct2D. These are not HTML-generation comparisons. All values are µs/formula.\n\n| Workload | C++ ABI MathML | .NET MathML | WASM MathML | KaTeX MathML | Native WinUI work |\n| --- | ---: | ---: | ---: | ---: | ---: |\n"
for w, label in names.items():
    numbers = [value(e, w, m) * 1000 for e, m in [("cpp", "mathml"), ("native", "mathml"), ("velocitex", "mathml"), ("katex", "mathml"), ("winui", "dom")]]
    docs += f"| {label} | " + " | ".join(f"{n:.1f}" for n in numbers) + " |\n"
docs += "\n## Per-case browser results\n\nUnits: µs/complete formula or complete page (page values are not divided by formula count).\n\n| Input | Formulas | WASM HTML | KaTeX HTML | WASM DOM + layout | KaTeX DOM + layout |\n| --- | ---: | ---: | ---: | ---: | ---: |\n"
for w in names:
    for key, case in summary["engines"]["velocitex"][w]["metrics"]["html"]["cases"].items():
        numbers = [summary["engines"][e][w]["metrics"][m]["cases"][key]["medianMsPerUnit"] * 1000 for e, m in [("velocitex", "html"), ("katex", "html"), ("velocitex", "dom"), ("katex", "dom")]]
        docs += f"| {key} | {case['formulaCount']} | " + " | ".join(f"{n:.1f}" for n in numbers) + " |\n"
Path("docs/benchmarks.md").write_text(docs, encoding="utf-8")
images = Path("npm/velocitex/images")
shutil.copytree("docs/benchmarks/rendering", images / "rendering", dirs_exist_ok=True)
for name in ["benchmark-comparison.png", "benchmark-render-work.png", "benchmark-page-throughput.png", "benchmark-mathml.png", "benchmark-summary.json"]:
    shutil.copyfile(Path("docs/benchmarks") / name, images / name)
