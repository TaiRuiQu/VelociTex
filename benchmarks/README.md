# Benchmarks

VelociTex renders math with one C++ core for .NET and the browser. This page
describes the benchmark corpus, the measurement protocol and how to reproduce
the results. The current numbers are in [docs/benchmarks.md](../docs/benchmarks.md).

## Current results

Measured on the reference machine with the 36-formula benchmark corpus:

| Scenario                         |           VelociTex |         KaTeX 0.19.0 |  Speedup |
| -------------------------------- | ------------------: | -------------------: | -------: |
| Browser HTML + MathML (Chromium) | about 43 µs/formula | about 165 µs/formula | **3.9x** |
| Native C++ core (HTML + MathML)  |     29.2 µs/formula |   287.8 µs/formula\* | **9.9x** |

\* The native core and the KaTeX column are both measured inside a process
(Node for KaTeX). The browser column is measured in Chromium. The native core
is about **5.7x** faster than KaTeX in the browser; comparing numbers from the
same runtime gives 3.9x in Chromium and 9.9x against KaTeX in Node.

The browser result includes the public npm package, the JavaScript adapter,
the bundled stylesheet and fonts, and the WebAssembly core. The native result
includes parsing plus HTML and MathML construction and serialization.

## Corpus

`workloads.json` contains 12 workloads and **36 formula occurrences**:

- 6 common single formulas;
- 3 long single formulas;
- 3 mixed pages with 10, 9 and 8 formulas.

No formula may be skipped. Every unit is rendered with both engines and the
markup is compared byte for byte before timing.

## Correctness gates

| Check            | Result                                                |
| ---------------- | ----------------------------------------------------- |
| Native HTML      | 5,416 / 5,416                                         |
| Native MathML    | 5,416 / 5,416                                         |
| WASM documents   | 3,660 / 3,660 valid, 49 error documents, 41/41 ranges |
| Chromium         | 1,651 / 1,651                                         |
| Benchmark corpus | 36 / 36 byte identical                                |

The native gate uses `tests/native/katex-html-parity.mjs` and
`tests/native/katex-mathml-parity.mjs`; the browser gate uses
`tests/wasm/katex-browser-parity.mjs`.

## Browser protocol

Both engines run in one Chromium page. The page loads the npm package and the
locked KaTeX 0.19.0 distribution, renders every benchmark formula with both
engines, and verifies the markup before the timing starts.

- 10 warmup rounds per workload.
- 120 measured rounds; both engines run back to back inside every round.
- `performance.now()` has a 100 µs resolution, so a workload is repeated inside
  the timed region (20 times for a single formula, 5 for a long formula, 2 for a
  page) and the elapsed time is divided by the repeat count.
- The reported speedup is the median of the per-round ratios, not the fastest
  round. The per-workload tables aggregate the median times by formula count.

### Native protocol

The native driver uses the public C ABI with the same 36 formulas. The timed
region includes parsing, HTML construction and serialization, MathML
construction and serialization, and result destruction. It warms up for 25
rounds and measures 25 rounds of process CPU time on a quiet machine. The
KaTeX Node column uses the same corpus and the same 25 + 25 rounds.

## Environment

| Item        | Configuration                                 |
| ----------- | --------------------------------------------- |
| CPU         | Intel Xeon (Skylake) vCPU × 2                 |
| Memory      | 3.8 GiB                                       |
| OS          | Linux x64                                     |
| Browser     | Headless Chrome 141                           |
| Node.js     | 24.22.0                                       |
| Native      | GCC 11.4.0, `-O3`                             |
| WebAssembly | Emscripten 3.1.56 (clang 19), `-O3`, LTO, PGO |
| KaTeX       | 0.19.0 (locked distribution)                  |

## Reproduce

```bash
cmake -S native -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-html-parity.mjs --corpus all --limit 0
VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-mathml-parity.mjs --corpus all --limit 0

node tests/wasm/stage-core-dist.mjs
node tests/wasm/katex-browser-report.mjs npm/velocitex/dist
node tests/wasm/katex-browser-parity.mjs
node tests/wasm/katex-browser-benchmark.mjs npm/velocitex/dist
```

`katex-browser-report.mjs` prints the per-workload table and can write the raw
data with `--json artifacts/benchmarks/browser-report.json`.
`katex-browser-benchmark.mjs` is the acceptance gate; it keeps a 5x threshold,
so the 3.9x reference reading makes that assertion fail. The threshold is not
lowered; the measured value is reported as it is.

## Notes

- Timing excludes page startup, asset download, browser rasterization, GPU
  compositing and screen presentation. Both engines need those; they are not
  part of the typesetting engine's throughput.
- The browser benchmark uses the default `htmlAndMathml` output. Explicit
  `output: 'svg'`, `fastHtml: false` or `fastMathml: false` take the .NET WASM
  fallback path and have different performance characteristics.
- Absolute timings vary with hardware and load. Compare the paired per-round
  ratio rather than a single absolute reading.
- Raw benchmark campaigns are archived under `benchmarks/results/`.
