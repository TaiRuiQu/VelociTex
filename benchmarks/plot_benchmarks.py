"""Aggregate schema-v2 raw batches and plot refresh-independent work.

python benchmarks/plot_benchmarks.py --data benchmarks/results/2026-10-20-preview6
"""

import argparse
import hashlib
import json
from pathlib import Path
from statistics import median

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

COLORS = {"cpp": "#183d59", "native": "#183d59", "winui": "#183d59", "velocitex": "#007d6b", "katex": "#b25b33"}
LABELS = {"cpp": "VelociTex native C++ ABI", "native": "VelociTex native C++ / .NET", "winui": "VelociTex WinUI / Direct2D", "velocitex": "VelociTex C++ WASM + fallback", "katex": "KaTeX 0.19.0 / JavaScript"}
WORKLOADS = ["single", "long", "page"]
TITLES = ["Common formulas", "Complex formulas", "Mixed pages (8–10 formulas)"]


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def aggregate(data):
    manifest = load(data / "manifest.json")
    corpus_bytes = Path("benchmarks/workloads.json").read_bytes()
    digest = hashlib.sha256(corpus_bytes).hexdigest()
    corpus = json.loads(corpus_bytes)
    summary = {"schemaVersion": 2, "manifest": manifest, "workloadSha256": digest, "engines": {}}
    summary["cppManifest"] = load(data / "cpp-manifest.json")
    for engine in ["cpp", "native", "winui", "velocitex", "katex"]:
        reports = [load(data / f"run-{i}" / engine / f"{engine}-report.json") for i in range(1, manifest["runs"] + 1)]
        if any(r["schemaVersion"] != 2 or r["workloadSha256"] != digest for r in reports):
            raise ValueError("Mixed schemas or workloads")
        engine_data = {}
        metrics = ["mathml"] if engine == "cpp" else ["dom"] if engine == "winui" else ["mathml", "dom"] if engine in ("velocitex", "katex") else ["mathml", "compile", "svg"]
        for name in WORKLOADS:
            expected = corpus["workloads"][name]["cases"]
            expected_counts = {c["id"]: sum("latex" in s for p in c["paragraphs"] for s in p) if name == "page" else 1 for c in expected}
            for report in reports:
                actual = report["result"]["workloads"][name]
                if {c["id"]: c["formulaCount"] for c in actual} != expected_counts or len(actual) != len(expected):
                    raise ValueError(f"Skipped, duplicate or miscounted inputs: {engine}/{name}")
            workload_data = {"formulaCount": sum(expected_counts.values()), "caseCount": len(expected), "metrics": {}}
            for metric in metrics:
                runs = []
                per_case = {key: [] for key in expected_counts}
                for report in reports:
                    total = 0
                    for case in report["result"]["workloads"][name]:
                        measurement = case[metric]
                        batches = measurement["batches"]
                        if len(batches) != manifest["samples"] or any(b["operations"] <= 0 or b["totalMs"] <= 0 for b in batches):
                            raise ValueError("Invalid raw batch")
                        value = median(b["totalMs"] / b["operations"] for b in batches)
                        total += value
                        per_case[case["id"]].append(value)
                    runs.append(total / sum(expected_counts.values()))
                workload_data["metrics"][metric] = {
                    "msPerFormula": median(runs), "runMsPerFormula": runs,
                    "minimumRunMs": min(runs), "maximumRunMs": max(runs),
                    "formulasPerSecond": 1000 / median(runs),
                    "cases": {key: {"formulaCount": expected_counts[key], "medianMsPerUnit": median(values), "runMsPerUnit": values} for key, values in per_case.items()},
                }
            engine_data[name] = workload_data
        summary["engines"][engine] = engine_data
    routes = [load(data / f"run-{i}" / "velocitex" / "velocitex-report.json")["result"]["routes"] for i in range(1, manifest["runs"] + 1)]
    if any(r != routes[0] for r in routes):
        raise ValueError("Fast-path routing changed across replicates")
    summary["routes"] = routes[0]
    return summary


def chart(summary, out, metric, filename, title, subtitle, engines):
    fig, axes = plt.subplots(1, 3, figsize=(14, 4.8))
    fig.patch.set_facecolor("white")
    for ax, workload, name in zip(axes, WORKLOADS, TITLES):
        values = [summary["engines"][e][workload]["metrics"][metric] for e in engines]
        numbers = [v["msPerFormula"] * 1000 for v in values]
        ax.barh(np.arange(len(engines)), numbers, height=.55, color=[COLORS[e] for e in engines])
        for i, (number, value) in enumerate(zip(numbers, values)):
            lo, hi = value["minimumRunMs"] * 1000, value["maximumRunMs"] * 1000
            ax.errorbar(number, i, xerr=[[number - lo], [hi - number]], color="#263642", capsize=3, linewidth=1)
            ax.text(hi + max(numbers) * .035, i, f"{number:,.1f}", va="center", fontsize=10, fontweight="bold", color="#172d3b")
        ax.set_yticks(np.arange(len(engines)), ["Native C++" if e in ("cpp", "native", "winui") else "C++ WASM" if e == "velocitex" else "KaTeX" for e in engines])
        ax.invert_yaxis()
        ax.set_xlim(0, max(v["maximumRunMs"] for v in values) * 1000 * 1.28)
        ax.set_title(name, loc="left", fontsize=11, pad=18, fontweight="bold")
        ax.set_xlabel("µs / formula · lower is better", color="#586776", fontsize=9)
        ax.set_axisbelow(True)
        ax.xaxis.grid(True, color="#e3e9ed", linewidth=.7)
        ax.tick_params(length=0, labelsize=9)
        for spine in ax.spines.values():
            spine.set_visible(False)
    fig.suptitle(title, x=.055, y=.97, ha="left", fontsize=21, fontweight="bold", color="#163245")
    fig.text(.055, .865, subtitle, fontsize=10, color="#586776")
    fig.text(.055, .06, "  •  ".join(LABELS[e] for e in engines), fontsize=9, color="#354c5d")
    fig.text(.055, .015, "Full frozen corpus, including fallback • 3 fresh processes × 15 calibrated batches • whiskers: run min–max • Windows x64, 2026-10-20", fontsize=8, color="#586776")
    fig.subplots_adjust(left=.075, right=.97, top=.72, bottom=.24, wspace=.38)
    fig.savefig(out / filename, dpi=180)
    plt.close(fig)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--data", default="benchmarks/results/2026-10-20-preview6")
    parser.add_argument("--out", default="docs/benchmarks")
    args = parser.parse_args()
    data, out = Path(args.data), Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    summary = aggregate(data)
    encoded = json.dumps(summary, indent=2) + "\n"
    (out / "benchmark-summary.json").write_text(encoded, encoding="utf-8")
    (data / "benchmark-summary.json").write_text(encoded, encoding="utf-8")
    chart(summary, out, "mathml", "benchmark-comparison.png", "MathML generation · independent of display refresh", "LaTeX → complete owned presentation MathML. Public API overhead included; startup excluded.", ["cpp", "velocitex", "katex"])
    chart(summary, out, "dom", "benchmark-render-work.png", "Browser render work · no frame waits", "Fresh public render call + DOM replacement + forced layout after every formula. Excludes browser rasterization and screen presentation.", ["velocitex", "katex"])
    fig, ax = plt.subplots(figsize=(10, 4.6))
    engines = ["velocitex", "katex"]
    values = [summary["engines"][e]["page"]["metrics"]["dom"]["formulasPerSecond"] for e in engines]
    ax.barh([LABELS[e] for e in engines], values, color=[COLORS[e] for e in engines], height=.5)
    ax.invert_yaxis()
    for i, value in enumerate(values):
        ax.text(value + max(values) * .025, i, f"{value:,.0f}", va="center", fontweight="bold")
    ax.set_xlim(0, max(values) * 1.2)
    ax.set_xlabel("Formulas / second · higher is better")
    ax.set_axisbelow(True)
    ax.xaxis.grid(True, color="#e3e9ed")
    for spine in ax.spines.values():
        spine.set_visible(False)
    ax.tick_params(length=0)
    fig.suptitle("Mixed-page render-work throughput", x=.05, ha="left", fontsize=20, fontweight="bold", color="#163245")
    fig.text(.05, .86, "27 formulas across 3 pages · fresh compile + submit/layout on every operation", fontsize=10, color="#586776")
    fig.text(.05, .035, "Derived from batch work time; excludes vsync, GPU completion and screen presentation. Includes all fallback cases.", fontsize=8, color="#586776")
    fig.subplots_adjust(left=.33, right=.95, top=.75, bottom=.2)
    fig.savefig(out / "benchmark-page-throughput.png", dpi=180)
    plt.close(fig)
    for name in WORKLOADS:
        print(name, {e: {m: round(v["msPerFormula"] * 1000, 2) for m, v in w[name]["metrics"].items()} for e, w in summary["engines"].items()})


if __name__ == "__main__":
    main()
