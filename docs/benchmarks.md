# 性能基准：1.0.0-preview6

## 性能

本机三类完整负载中，C++ WASM（含自动回退）的浏览器渲染工作吞吐为 KaTeX 的 **1.6–3.1 倍**；原生 C++ Core 的普通公式 MathML 生成为 **24.6 µs/公式（约 40,667 公式/秒）**，无需窗口，不受屏幕刷新率限制。

2026-10-20 · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).

| MathML generation | Native C++ ABI | C++ WASM + fallback | KaTeX | WASM relative throughput |
| ----------------- | -------------: | ------------------: | ----: | -----------------------: |
| Common formulas   |           24.6 |                 7.7 |  14.0 |                    1.81× |
| Complex formulas  |          208.6 |               137.4 | 117.6 |                    0.86× |
| Mixed pages       |           20.2 |                 3.7 |  11.7 |                    3.18× |

| Render work      | Native WinUI / Direct2D | WASM DOM + layout | KaTeX DOM + layout |
| ---------------- | ----------------------: | ----------------: | -----------------: |
| Common formulas  |                  1174.7 |             248.8 |              644.4 |
| Complex formulas |                 16228.2 |            2684.3 |             4196.5 |
| Mixed pages      |                   368.8 |             179.5 |              561.6 |

The complex-formula MathML workload takes 1.17× KaTeX time in WASM because fallback remains included. Native WinUI is slower than KaTeX on single/complex render work; its window/surface diagnostics can include graphics-queue backpressure and are separate from pure C++ engine performance. There is no claim of a universal speedup.

![Render-work throughput benchmark](benchmarks/benchmark-render-work.png)

All 36 formula occurrences are included, including automatic fallback. Three fresh processes × 15 calibrated batches per case; medians of batch averages, with actual page formula counts. No explicit frame waits or document cache. Startup/download excluded. Render work measures native surface submission or browser DOM + forced layout, with different output backends; it excludes a GPU completion fence, browser rasterization, and screen presentation. Results are specific to this hardware and corpus.

[Methodology, all cases, raw batches and reproduction commands](../benchmarks/README.md).

![无帧等待的渲染工作](benchmarks/benchmark-render-work.png)

![图文页面工作吞吐](benchmarks/benchmark-page-throughput.png)

## 渲染效果对比

Actual output at 32 px math em, captured at 2× resolution. Each row uses equal-scale canvases; table display scales them together. WASM uses the default C++ MathML route with automatic full-Core fallback. Native and KaTeX use their own font rendering; identical pixels are not claimed.

| Formula                | Windows native / Direct2D                                                                             | VelociTex WASM                                                                                    | KaTeX 0.19.0                                                                                        |
| ---------------------- | ----------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------------------------------- |
| Algebra                | <img src="benchmarks/rendering/algebra-native.png" width="280" alt="Algebra: native">                 | <img src="benchmarks/rendering/algebra-wasm.png" width="280" alt="Algebra: wasm">                 | <img src="benchmarks/rendering/algebra-katex.png" width="280" alt="Algebra: katex">                 |
| Calculus               | <img src="benchmarks/rendering/calculus-native.png" width="280" alt="Calculus: native">               | <img src="benchmarks/rendering/calculus-wasm.png" width="280" alt="Calculus: wasm">               | <img src="benchmarks/rendering/calculus-katex.png" width="280" alt="Calculus: katex">               |
| Piecewise & matrices   | <img src="benchmarks/rendering/structures-native.png" width="280" alt="Piecewise & matrices: native"> | <img src="benchmarks/rendering/structures-wasm.png" width="280" alt="Piecewise & matrices: wasm"> | <img src="benchmarks/rendering/structures-katex.png" width="280" alt="Piecewise & matrices: katex"> |
| Color, accents & fonts | <img src="benchmarks/rendering/color-native.png" width="280" alt="Color, accents & fonts: native">    | <img src="benchmarks/rendering/color-wasm.png" width="280" alt="Color, accents & fonts: wasm">    | <img src="benchmarks/rendering/color-katex.png" width="280" alt="Color, accents & fonts: katex">    |

C++ WASM 快速路径命中 **31/36** 条公式出现位置；其余全部自动回退并计入。逐项路由见 [原始汇总](benchmarks/benchmark-summary.json)。完整方法、软硬件、统计口径、局限和复现步骤见 [benchmark guide](../benchmarks/README.md)。旧版含帧等待的数据保留在旧结果目录，不再用于本版性能结论。

测试环境：12th Gen Intel(R) Core(TM) i5-12400F，31.8 GiB 内存；Windows_NT 10.0.26200 x64；.NET 10.0.13。浏览器为 `Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) HeadlessChrome/155.0.0.0 Safari/537.36`，DPR 1；原生宿主为 Release x64 JIT，浏览器回退为 Release WASM AOT。页面正文和宿主创建在计时之外，原生用图文行、浏览器用可换行段落，因此整页数据衡量其中的公式工作，不代表完整文档构建或正文排版。

## 逐项结果

单位为 µs/完整公式或完整页；本表的页值尚未除以公式数。每项先取进程内批次均值的中位数，再取三个进程的中位数。

| Input              | Formulas | C++ ABI MathML | .NET MathML | WASM MathML | KaTeX MathML | Native UI work | WASM render work | KaTeX render work |
| ------------------ | -------: | -------------: | ----------: | ----------: | -----------: | -------------: | ---------------: | ----------------: |
| single-energy      |        1 |            6.1 |         7.5 |         1.5 |          4.7 |          218.0 |             71.1 |             222.0 |
| single-quadratic   |        1 |           22.4 |        23.0 |         3.3 |         10.9 |         4631.3 |            205.0 |             692.9 |
| single-gaussian    |        1 |           31.3 |        30.9 |         5.0 |         16.6 |          441.2 |            166.6 |             677.0 |
| single-binomial    |        1 |           31.1 |        31.8 |        29.2 |         16.5 |          530.5 |            694.8 |             846.9 |
| single-determinant |        1 |           26.7 |        27.7 |         3.4 |         18.3 |          427.9 |            166.3 |             606.6 |
| single-derivative  |        1 |           30.3 |        30.1 |         4.3 |         16.2 |          687.8 |            207.1 |             744.6 |
| long-aligned       |        1 |          285.9 |       272.7 |       182.5 |        142.7 |        30848.3 |           3051.9 |            5481.9 |
| long-cases         |        1 |          128.5 |       134.0 |        90.1 |         75.1 |        21973.0 |           2355.9 |            3660.0 |
| long-maxwell       |        1 |          209.3 |       219.4 |       142.3 |        124.0 |         2214.1 |           2270.9 |            3407.5 |
| page-algebra       |       10 |          171.0 |       185.3 |        26.1 |        117.0 |         3798.1 |           1635.9 |            5230.0 |
| page-calculus      |        9 |          189.3 |       191.8 |        31.9 |        113.1 |         3575.8 |           1530.6 |            5249.4 |
| page-physics       |        8 |          184.4 |       192.0 |        41.6 |         89.2 |         3083.0 |           1799.7 |            4703.1 |
