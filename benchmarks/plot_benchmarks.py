"""Render the professional benchmark charts from the raw JSON reports.

Usage:
  python benchmarks/plot_benchmarks.py --data artifacts/benchmarks --out docs/benchmarks
"""

import argparse
import json
import pathlib

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

NATIVE = "#1f4e79"
WASM = "#2e8b57"
KATEX = "#c55a11"
GRID = "#d8dee4"
TEXT = "#18212b"
MUTED = "#5b6672"

plt.rcParams.update(
    {
        "font.sans-serif": ["Microsoft YaHei", "Segoe UI", "DejaVu Sans"],
        "font.family": "sans-serif",
        "axes.unicode_minus": False,
        "figure.facecolor": "white",
        "axes.facecolor": "white",
        "savefig.facecolor": "white",
        "axes.edgecolor": GRID,
        "axes.linewidth": 0.8,
        "text.color": TEXT,
        "axes.labelcolor": TEXT,
        "xtick.color": TEXT,
        "ytick.color": MUTED,
    }
)


def load(path):
    with open(path, encoding="utf-8") as file:
        return json.load(file)


def p50(report, *keys):
    value = report
    for key in keys:
        value = value[key]
    return float(value["p50Ms"])


def style_axis(ax, log=False):
    ax.set_axisbelow(True)
    ax.yaxis.grid(True, color=GRID, linewidth=0.7)
    ax.xaxis.grid(False)
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.spines["left"].set_color("#b8c2cc")
    ax.spines["bottom"].set_color("#b8c2cc")
    if log:
        ax.set_yscale("log")


def annotate(ax, bars, values, fmt="{:.3g}"):
    for bar, value in zip(bars, values):
        if value is None:
            continue
        ax.annotate(
            fmt.format(value),
            xy=(bar.get_x() + bar.get_width() / 2, bar.get_height()),
            xytext=(0, 3),
            textcoords="offset points",
            ha="center",
            va="bottom",
            fontsize=8,
            color=TEXT,
        )


def grouped_bars(ax, workloads, series, log=True, value_fmt="{:.3g}"):
    x = np.arange(len(workloads))
    count = len(series)
    width = 0.8 / count
    style_axis(ax, log)
    for index, (label, color, values) in enumerate(series):
        offsets = x - 0.4 + width * (index + 0.5)
        bars = ax.bar(offsets, values, width, label=label, color=color, edgecolor="white", linewidth=0.6, zorder=3)
        annotate(ax, bars, values, value_fmt)
    ax.set_xticks(x, workloads)
    ax.set_xlim(-0.55, len(workloads) - 0.45)
    finite = [value for _, _, values in series for value in values if value is not None and value > 0]
    if finite:
        if log:
            ax.set_ylim(min(finite) * 0.55, max(finite) * 2.6)
        else:
            ax.set_ylim(0, max(finite) * 1.22)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="artifacts/benchmarks")
    parser.add_argument("--out", default="docs/benchmarks")
    parser.add_argument("--workloads", default="benchmarks/workloads.json")
    args = parser.parse_args()
    data = pathlib.Path(args.data)
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)

    native = load(data / "native" / "native-report.json")
    wasm = load(data / "wasm" / "velocitex-report.json")
    katex = load(data / "katex" / "katex-report.json")
    winui = load(data / "winui" / "winui-report.json")
    workloads = load(args.workloads)["workloads"]

    page_formulas = [
        sum(1 for paragraph in page["paragraphs"] for segment in paragraph if "latex" in segment)
        for page in workloads["page"]["cases"]
    ]
    page_average = sum(page_formulas) / len(page_formulas)
    labels = ["单个公式", "超长公式", "整页均摊"]

    native_mathml = [p50(native, "workloads", name, "mathMl") for name in ("single", "long")]
    native_mathml.append(p50(native, "workloads", "page", "mathMl") / page_average)
    wasm_mathml = [p50(wasm, "result", "parse", name, "mathml", "summary") for name in ("single", "long", "page")]
    wasm_mathml[2] /= page_average
    katex_mathml = [p50(katex, "result", "parse", name, "mathml", "summary") for name in ("single", "long", "page")]
    katex_mathml[2] /= page_average

    native_svg = [p50(native, "workloads", name, "svg") for name in ("single", "long")]
    native_svg.append(p50(native, "workloads", "page", "svg") / page_average)
    wasm_svg = [p50(wasm, "result", "parse", name, "svg", "summary") for name in ("single", "long", "page")]
    wasm_svg[2] /= page_average

    native_e2e = [p50(winui, "single", "summary", "secondFrame"), p50(winui, "longFormulas", "summary", "secondFrame")]
    native_e2e.append(p50(winui, "page", "summary", "secondFrame"))
    wasm_e2e = [p50(wasm, "result", "endToEnd", name, "summary") for name in ("single", "long", "page")]
    katex_e2e = [p50(katex, "result", "endToEnd", name, "summary") for name in ("single", "long", "page")]
    native_e2e_per = native_e2e[:2] + [native_e2e[2] / page_average]
    wasm_e2e_per = wasm_e2e[:2] + [wasm_e2e[2] / page_average]
    katex_e2e_per = katex_e2e[:2] + [katex_e2e[2] / page_average]

    native_sync = [p50(winui, "single", "summary", "sync"), p50(winui, "longFormulas", "summary", "sync")]
    native_sync.append(p50(winui, "page", "summary", "sync") / page_average)
    wasm_sync = [p50(wasm, "result", "endToEnd", name, "syncSummary") for name in ("single", "long", "page")]
    wasm_sync[2] /= page_average
    katex_sync = [p50(katex, "result", "endToEnd", name, "syncSummary") for name in ("single", "long", "page")]
    katex_sync[2] /= page_average

    fig, axes = plt.subplots(2, 2, figsize=(14.2, 11.2))
    fig.subplots_adjust(left=0.075, right=0.98, top=0.855, bottom=0.135, wspace=0.18, hspace=0.34)
    fig.suptitle("VelociTex 与 KaTeX 0.19.0 性能对比", fontsize=20, fontweight="bold", color="#0b1f33", y=0.975)
    fig.text(0.5, 0.938, "Windows x64 · 同一冻结语料 · 中位数（P50）· 2026-10-20", ha="center", fontsize=10.5, color=MUTED)

    ax = axes[0, 0]
    grouped_bars(ax, labels, [("Windows 原生", NATIVE, native_mathml), ("WASM Binding", WASM, wasm_mathml), ("KaTeX 0.19.0", KATEX, katex_mathml)])
    ax.set_title("A · LaTeX → MathML 解析", loc="left", fontsize=13, fontweight="bold", pad=10)
    ax.set_ylabel("每公式毫秒（对数坐标）")
    ax.legend(frameon=False, ncol=3, loc="upper left", fontsize=9)

    ax = axes[0, 1]
    grouped_bars(ax, labels, [("Windows 原生", NATIVE, native_svg), ("WASM Binding", WASM, wasm_svg)])
    ax.set_title("B · LaTeX → SVG 解析（KaTeX 无原生 SVG 输出）", loc="left", fontsize=12.5, fontweight="bold", pad=10)
    ax.set_ylabel("每公式毫秒（对数坐标）")
    ax.legend(frameon=False, ncol=2, loc="upper left", fontsize=9)

    ax = axes[1, 0]
    grouped_bars(
        ax,
        labels,
        [("Windows 原生", NATIVE, native_e2e_per), ("WASM Binding", WASM, wasm_e2e_per), ("KaTeX 0.19.0", KATEX, katex_e2e_per)],
        value_fmt="{:.2f}",
    )
    ax.set_title("C · 调用到屏幕呈现（每公式摊销）", loc="left", fontsize=13, fontweight="bold", pad=10)
    ax.set_ylabel("毫秒（对数坐标）")
    ax.legend(frameon=False, ncol=3, loc="upper left", fontsize=9)

    ax = axes[1, 1]
    values_native = native_e2e[2]
    values_wasm = wasm_e2e[2]
    values_katex = katex_e2e[2]
    grouped_bars(
        ax,
        ["整页渲染"],
        [("Windows 原生", NATIVE, [values_native]), ("WASM Binding", WASM, [values_wasm]), ("KaTeX 0.19.0", KATEX, [values_katex])],
        log=False,
        value_fmt="{:.1f}",
    )
    ax.set_title(f"D · 图文混排整页呈现（约 {page_average:.0f} 条公式）", loc="left", fontsize=13, fontweight="bold", pad=10)
    ax.set_ylabel("整页毫秒")
    ax.set_xlim(-0.6, 0.6)
    ax.text(
        0.99,
        0.04,
        f"吞吐：原生 {page_average / (values_native / 1000):.0f} / WASM {page_average / (values_wasm / 1000):.0f} / KaTeX {page_average / (values_katex / 1000):.0f} 公式每秒",
        transform=ax.transAxes,
        ha="right",
        va="bottom",
        fontsize=8.2,
        color=MUTED,
        bbox={"facecolor": "white", "edgecolor": "#d8dee4", "boxstyle": "round,pad=0.3"},
    )
    ax.legend(frameon=False, ncol=3, loc="upper left", fontsize=9)

    for ax in axes.flat:
        ax.tick_params(axis="both", labelsize=9)

    fig.text(
        0.5,
        0.035,
        "解析：LaTeX 到 MathML / SVG，不含 DOM 与合成帧。呈现：从渲染调用开始，到第二次合成帧，已包含解析、布局与帧节拍。\n"
        "Windows 原生为 Release x64（.NET 10、C++ Core、DirectWrite/Direct2D），浏览器为 Chrome headless；本机原生会话合成节拍为 31.5 ms（约 30 Hz），浏览器为 16.7 ms（60 Hz），帧等待会放大原生的呈现延迟。",
        ha="center",
        fontsize=8.3,
        color=MUTED,
        linespacing=1.5,
    )
    fig.savefig(out / "benchmark-comparison.png", dpi=200, bbox_inches="tight")
    print(f"wrote {out / 'benchmark-comparison.png'}")

    fig2, ax2 = plt.subplots(figsize=(9.6, 5.4), constrained_layout=True)
    throughput = [page_average / (value / 1000) for value in (values_native, values_wasm, values_katex)]
    names = ["Windows 原生", "WASM Binding", "KaTeX 0.19.0"]
    bars = ax2.bar(names, throughput, color=[NATIVE, WASM, KATEX], width=0.55, edgecolor="white", zorder=3)
    style_axis(ax2, log=False)
    ax2.set_title("图文混排页面吞吐量", loc="left", fontsize=15, fontweight="bold", pad=12)
    ax2.set_ylabel("公式 / 秒（整页调用到第二次合成帧）")
    ax2.set_ylim(0, max(throughput) * 1.22)
    for bar, value in zip(bars, throughput):
        ax2.annotate(f"{value:.0f}", xy=(bar.get_x() + bar.get_width() / 2, value), xytext=(0, 4), textcoords="offset points", ha="center", fontsize=11, fontweight="bold")
    fig2.text(0.5, -0.02, f"每页约 {page_average:.0f} 条公式，含正文与公式混排；吞吐量按整页呈现时间折算，包含帧节拍。", ha="center", fontsize=8.5, color=MUTED)
    fig2.savefig(out / "benchmark-page-throughput.png", dpi=200, bbox_inches="tight")
    print(f"wrote {out / 'benchmark-page-throughput.png'}")

    # Frame-independent CPU work: useful because the native window and headless
    # Chrome were composited at different refresh rates.
    fig3, ax3 = plt.subplots(figsize=(14.2, 5.8), constrained_layout=True)
    grouped_bars(
        ax3,
        labels,
        [("Windows 原生", NATIVE, native_sync), ("WASM Binding", WASM, wasm_sync), ("KaTeX 0.19.0", KATEX, katex_sync)],
        value_fmt="{:.2f}",
    )
    ax3.set_title("渲染调用同步耗时（不含帧等待，每公式）", loc="left", fontsize=15, fontweight="bold", pad=12)
    ax3.set_ylabel("毫秒（对数坐标）")
    ax3.legend(frameon=False, ncol=3, loc="upper left", fontsize=9)
    fig3.text(
        0.5,
        -0.02,
        "同步耗时 = 解析、布局、绘制提交返回到调用方的时间；原生包含 MathEngine.TryCompile 与 MathView.Result 赋值，浏览器包含 render 的 Promise 完成。",
        ha="center",
        fontsize=8.5,
        color=MUTED,
    )
    fig3.savefig(out / "benchmark-render-work.png", dpi=200, bbox_inches="tight")
    print(f"wrote {out / 'benchmark-render-work.png'}")

    summary = {
        "generatedAt": "2026-10-20",
        "pageFormulas": page_formulas,
        "pageAverageFormulas": page_average,
        "parseMathmlPerFormulaMs": {"native": native_mathml, "wasm": wasm_mathml, "katex": katex_mathml},
        "parseSvgPerFormulaMs": {"native": native_svg, "wasm": wasm_svg},
        "endToEndPerFormulaMs": {"native": native_e2e_per, "wasm": wasm_e2e_per, "katex": katex_e2e_per},
        "syncPerFormulaMs": {"native": native_sync, "wasm": wasm_sync, "katex": katex_sync},
        "pageTotalMs": {"native": values_native, "wasm": values_wasm, "katex": values_katex},
        "pageThroughputFormulasPerSecond": {"native": throughput[0], "wasm": throughput[1], "katex": throughput[2]},
        "frameIntervalMs": {
            "native": p50(winui, "frameInterval"),
            "wasm": p50(wasm, "result", "frameInterval"),
            "katex": p50(katex, "result", "frameInterval"),
        },
    }
    (out / "benchmark-summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
