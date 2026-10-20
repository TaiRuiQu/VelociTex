"""Refresh all public performance claims from the validated benchmark summary."""

import json
import re
import shutil
from pathlib import Path

BASE = "https://github.com/TaiRuiQu/VelociTex/blob/v1.0.0-preview6"
RAW = "https://raw.githubusercontent.com/TaiRuiQu/VelociTex/v1.0.0-preview6/docs/benchmarks"
CDN = "https://cdn.jsdelivr.net/npm/velocitex@1.0.0-preview6/images"
DATA = Path("benchmarks/results/2026-10-20-preview6")
S = json.loads((DATA / "benchmark-summary.json").read_text(encoding="utf-8"))
NAMES = {"single": "Common formulas", "long": "Complex formulas", "page": "Mixed pages"}


def value(engine, workload, metric):
    return S["engines"][engine][workload]["metrics"][metric]["msPerFormula"]


def performance(image_base, guide, chinese=False):
    ratios = [value("katex", w, "dom") / value("velocitex", w, "dom") for w in NAMES]
    native_us = value("cpp", "single", "mathml") * 1000
    native_rate = 1000 / value("cpp", "single", "mathml")
    complex_ratio = value("velocitex", "long", "mathml") / value("katex", "long", "mathml")
    headline = f"C++ WASM + fallback delivers **{min(ratios):.1f}–{max(ratios):.1f}× KaTeX browser render-work throughput** across these three workloads. The native C++ Core generates common-formula MathML in **{native_us:.1f} µs/formula (~{native_rate:,.0f} formulas/s)** on this machine, without a window or refresh-rate limit."
    if chinese:
        headline = f"本机三类完整负载中，C++ WASM（含自动回退）的浏览器渲染工作吞吐为 KaTeX 的 **{min(ratios):.1f}–{max(ratios):.1f} 倍**；原生 C++ Core 的普通公式 MathML 生成为 **{native_us:.1f} µs/公式（约 {native_rate:,.0f} 公式/秒）**，无需窗口，不受屏幕刷新率限制。"
    lines = ["## 性能" if chinese else "## Performance", "", headline, "", "2026-10-20 · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).", "", "| MathML generation | Native C++ ABI | C++ WASM + fallback | KaTeX | WASM relative throughput |", "| --- | ---: | ---: | ---: | ---: |"]
    for w, name in NAMES.items():
        a, b, c = [value(e, w, "mathml") * 1000 for e in ("cpp", "velocitex", "katex")]
        lines.append(f"| {name} | {a:.1f} | {b:.1f} | {c:.1f} | {c/b:.2f}× |")
    lines += ["", "| Render work | Native WinUI / Direct2D | WASM DOM + layout | KaTeX DOM + layout |", "| --- | ---: | ---: | ---: |"]
    for w, name in NAMES.items():
        a, b, c = [value(e, w, "dom") * 1000 for e in ("winui", "velocitex", "katex")]
        lines.append(f"| {name} | {a:.1f} | {b:.1f} | {c:.1f} |")
    lines += ["", f"The complex-formula MathML workload takes {complex_ratio:.2f}× KaTeX time in WASM because fallback remains included. Native WinUI is slower than KaTeX on single/complex render work; its window/surface diagnostics can include graphics-queue backpressure and are separate from pure C++ engine performance. There is no claim of a universal speedup.", "", f"![Render-work throughput benchmark]({image_base}/benchmark-render-work.png)", "", "All 36 formula occurrences are included, including automatic fallback. Three fresh processes × 15 calibrated batches per case; medians of batch averages, with actual page formula counts. No explicit frame waits or document cache. Startup/download excluded. Render work measures native surface submission or browser DOM + forced layout, with different output backends; it excludes a GPU completion fence, browser rasterization, and screen presentation. Results are specific to this hardware and corpus.", "", f"[Methodology, all cases, raw batches and reproduction commands]({guide}).", ""]
    return "\n".join(lines)


def rendering(image_base, chinese=False):
    lines = ["## 渲染效果对比" if chinese else "## Rendering comparison", "", "Actual output at 32 px math em, captured at 2× resolution. Each row uses equal-scale canvases; table display scales them together. WASM uses the default C++ MathML route with automatic full-Core fallback. Native and KaTeX use their own font rendering; identical pixels are not claimed.", "", "| Formula | Windows native / Direct2D | VelociTex WASM | KaTeX 0.19.0 |", "| --- | --- | --- | --- |"]
    for item, label in [("algebra", "Algebra"), ("calculus", "Calculus"), ("structures", "Piecewise & matrices"), ("color", "Color, accents & fonts")]:
        cells = [f'<img src="{image_base}/rendering/{item}-{e}.png" width="280" alt="{label}: {e}">' for e in ("native", "wasm", "katex")]
        lines.append(f"| {label} | " + " | ".join(cells) + " |")
    return "\n".join(lines) + "\n"


def insert(path, section):
    content = path.read_text(encoding="utf-8")
    content = re.sub(r"<!-- benchmark:start -->.*?<!-- benchmark:end -->\s*", "", content, flags=re.S)
    index = content.index("\n## ")
    content = content[:index].rstrip() + "\n\n<!-- benchmark:start -->\n" + section.rstrip() + "\n<!-- benchmark:end -->\n\n" + content[index:].lstrip()
    path.write_text(content, encoding="utf-8")


for filename, chinese in [("README.md", False), ("README.zh-CN.md", True)]:
    insert(Path(filename), performance("docs/benchmarks", "benchmarks/README.md", chinese) + "\n" + rendering("docs/benchmarks", chinese))
insert(Path("PACKAGE-README.md"), performance(RAW, BASE + "/benchmarks/README.md") + "\n" + rendering(RAW))
npm_readme = Path("npm/velocitex/README.md")
content = npm_readme.read_text(encoding="utf-8")
content = re.sub(r"<!-- benchmark:start -->.*?<!-- benchmark:end -->\s*", "", content, flags=re.S)
content = re.sub(r"## Rendering comparison.*?(?=## APIs)", "", content, flags=re.S)
npm_readme.write_text(content, encoding="utf-8")
insert(npm_readme, performance(CDN, BASE + "/benchmarks/README.md") + "\n" + rendering(CDN))

routes = S["routes"]
fast = sum(v == "cpp-fast" for v in routes.values())
docs = "# 性能基准：1.0.0-preview6\n\n" + performance("benchmarks", "../benchmarks/README.md", True)
docs += "\n![无帧等待的渲染工作](benchmarks/benchmark-render-work.png)\n\n![图文页面工作吞吐](benchmarks/benchmark-page-throughput.png)\n\n"
docs += rendering("benchmarks", True)
docs += f"\nC++ WASM 快速路径命中 **{fast}/{len(routes)}** 条公式出现位置；其余全部自动回退并计入。逐项路由见 [原始汇总](benchmarks/benchmark-summary.json)。完整方法、软硬件、统计口径、局限和复现步骤见 [benchmark guide](../benchmarks/README.md)。旧版含帧等待的数据保留在旧结果目录，不再用于本版性能结论。\n"
browser = json.loads((DATA / "run-1/katex/katex-report.json").read_text(encoding="utf-8"))
native = json.loads((DATA / "run-1/native/native-report.json").read_text(encoding="utf-8"))
docs += f"\n测试环境：{S['manifest']['cpu'].strip()}，{S['manifest']['memoryBytes'] / 1024**3:.1f} GiB 内存；{browser['os']}；{native['runtime']}。浏览器为 `{browser['chrome']['userAgent']}`，DPR {browser['chrome']['devicePixelRatio']}；原生宿主为 Release x64 JIT，浏览器回退为 Release WASM AOT。页面正文和宿主创建在计时之外，原生用图文行、浏览器用可换行段落，因此整页数据衡量其中的公式工作，不代表完整文档构建或正文排版。\n"
docs += "\n## 逐项结果\n\n单位为 µs/完整公式或完整页；本表的页值尚未除以公式数。每项先取进程内批次均值的中位数，再取三个进程的中位数。\n\n| Input | Formulas | C++ ABI MathML | .NET MathML | WASM MathML | KaTeX MathML | Native UI work | WASM render work | KaTeX render work |\n| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n"
for workload in NAMES:
    cases = S["engines"]["native"][workload]["metrics"]["mathml"]["cases"]
    for key, case in cases.items():
        numbers = [S["engines"][engine][workload]["metrics"][metric]["cases"][key]["medianMsPerUnit"] * 1000 for engine, metric in [("cpp", "mathml"), ("native", "mathml"), ("velocitex", "mathml"), ("katex", "mathml"), ("winui", "dom"), ("velocitex", "dom"), ("katex", "dom")]]
        docs += f"| {key} | {case['formulaCount']} | " + " | ".join(f"{n:.1f}" for n in numbers) + " |\n"
Path("docs/benchmarks.md").write_text(docs, encoding="utf-8")
notes = "# 1.0.0-preview6\n\n" + performance(RAW, BASE + "/benchmarks/README.md")
notes += "\n- Replaced refresh-limited headline benchmarks with calibrated batch measurements and three fresh-process replicates.\n- Fixed benchmark options that accidentally disabled the C++ WASM fast path. All fallback inputs remain included.\n- Added actual rendering comparison tables to the repository, npm and NuGet READMEs.\n- Fixed npm publishing to resolve the verified archive to an absolute filesystem path, preventing GitHub shorthand parsing.\n\nInstall: `npm install velocitex@1.0.0-preview6` or add `VelociTex.Core`, `VelociTex.Rendering.Direct2D`, and `VelociTex.WinUI` version `1.0.0-preview6`.\n"
Path("docs/releases").mkdir(exist_ok=True)
Path("docs/releases/1.0.0-preview6.md").write_text(notes, encoding="utf-8")
images = Path("npm/velocitex/images")
shutil.copytree("docs/benchmarks/rendering", images / "rendering", dirs_exist_ok=True)
for name in ["benchmark-comparison.png", "benchmark-render-work.png", "benchmark-page-throughput.png", "benchmark-summary.json"]:
    shutil.copyfile(Path("docs/benchmarks") / name, images / name)
