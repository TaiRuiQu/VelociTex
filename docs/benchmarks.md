# 性能基准

VelociTex 用同一个 C++ 核心服务 .NET 和浏览器两个端点。本页给出 v1.0.0
在固定语料上的实测性能、与 KaTeX 0.19.0 的对比，以及完整的复现方法。

## 结果摘要

| 场景                             | 公式数 |       VelociTex |      KaTeX 0.19.0 |     加速 |
| -------------------------------- | -----: | --------------: | ----------------: | -------: |
| 浏览器 HTML + MathML（Chromium） |     36 | **约 43 µs/条** |  **约 165 µs/条** | **3.9x** |
| 原生 C++ 核心（HTML + MathML）   |     36 |  **29.2 µs/条** | **287.8 µs/条\*** | **9.9x** |

\* 原生核心与 Node 里的 KaTeX 都是进程内测量；浏览器的数字包含 WebAssembly
模块、JS 适配层、样式和字体注入，以及浏览器调用开销。两组数字来自同一台机器、
同一份 36 条公式语料，区别只在运行环境。

浏览器是产品的主要使用场景：在 Chromium 中，完整 npm 包渲染同样的公式比
KaTeX 快 **3.9 倍**。在参考机上，KaTeX 每轮（36 条公式）约 5.5–6.3 ms，
VelociTex 约 1.4–1.8 ms，即约 39–44 µs/条 vs 153–175 µs/条。

## 浏览器基准

语料是仓库冻结的 36 条公式，分为 6 条常用单公式、3 条长公式和 3 个混合
页面（27 条公式）。每条公式都先与 KaTeX 做逐字节比对，确认 HTML 与
MathML 完全一致后才进入计时。

| 工作负载   | 公式数 |         KaTeX |    VelociTex |      加速 |
| ---------- | -----: | ------------: | -----------: | --------: |
| 常用单公式 |      6 |    118–130 µs |     27–30 µs | **4.33x** |
| 长公式     |      3 |    760–853 µs |   200–227 µs | **4.00x** |
| 混合页面   |     27 |    100–119 µs |     26–31 µs | **3.80x** |
| **合计**   | **36** | **约 165 µs** | **约 43 µs** |  **3.9x** |

表中速度是三次独立运行的中位数，µs 为每公式耗时范围。

逐工作负载的加速比（三次运行中位数）：

| 工作负载             | 加速比 |
| -------------------- | -----: |
| `single-energy`      |  3.50x |
| `single-quadratic`   |  3.80x |
| `single-gaussian`    |  4.33x |
| `single-binomial`    |  4.67x |
| `single-determinant` |  4.00x |
| `single-derivative`  |  4.60x |
| `long-aligned`       |  3.80x |
| `long-cases`         |  3.22x |
| `long-maxwell`       |  3.70x |
| `page-algebra`       |  3.60x |
| `page-calculus`      |  3.80x |
| `page-physics`       |  3.60x |

## 正确性

性能数据只有在输出一致时才有意义。v1.0.0 的逐字节校验结果：

| 校验            | 结果                                            |
| --------------- | ----------------------------------------------- |
| 原生 HTML       | 5,411 / 5,411                                   |
| 原生 MathML     | 5,411 / 5,411                                   |
| WASM 有效文档   | 3,660 / 3,660（含 49 条错误文档、41/41 源范围） |
| Chromium 浏览器 | 1,651 / 1,651                                   |
| 基准语料        | 36 / 36 条公式与 KaTeX 逐字节相同               |

原生校验覆盖真实世界公式、精选功能语料、冻结基准工作负载和仓库 fixture
语料；浏览器校验覆盖真实世界语料、基准工作负载和精选样本。

## 测量方法

浏览器测量使用一个 Chromium 页面同时加载 VelociTex npm 包和 KaTeX 0.19.0
的锁定发行版。两个引擎渲染同一批公式，计时前先验证 markup 完全一致。

- 验收脚本先预热 25 轮，再测 120 轮；每一轮完整渲染 36 条公式，两个引擎
  在同一轮内背靠背运行。
- 分工作负载报告先预热 10 轮，再测 120 轮；每一轮同样遍历全部 36 条公式，
  同时按工作负载记录耗时。
- `performance.now()` 的分辨率是 100 µs，单条公式只有几十微秒，因此在计时
  区间内重复渲染该工作负载（单公式 20 次、长公式 5 次、页面 2 次），再除以
  重复次数。
- 报告值是逐轮比值的**中位数**，不是最快一轮；中位数不受单次系统抖动影响。
- 表格按工作负载汇总：先取每个工作负载的中位耗时，再按公式数加权。

原生核心测量使用 GCC 11.4、`-O3` 构建，同一份 36 条公式，计时区间包含
解析、HTML 构建与序列化、MathML 构建与序列化，以及结果销毁；先预热 25 轮，
再测 25 轮，取进程 CPU 时间的中位数。KaTeX 的 Node 数字使用同一语料、同样
的 25 + 25 轮和进程 CPU 时间。

### 环境

| 项目        | 配置                                             |
| ----------- | ------------------------------------------------ |
| CPU         | Intel Xeon (Skylake) vCPU × 2                    |
| 内存        | 3.8 GiB                                          |
| 系统        | Linux x64                                        |
| 浏览器      | Headless Chrome 141                              |
| Node.js     | 24.22.0                                          |
| 原生编译器  | GCC 11.4.0                                       |
| WASM 编译器 | Emscripten 3.1.56（clang 19），`-O3` + LTO + PGO |
| KaTeX       | 0.19.0（锁定发行版）                             |

## 复现

```bash
# 原生核心
cmake -S native -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8

# 原生逐字节校验
VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-html-parity.mjs --corpus all --limit 0
VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-mathml-parity.mjs --corpus all --limit 0

# 浏览器包与基准
node tests/wasm/stage-core-dist.mjs
node tests/wasm/katex-browser-report.mjs npm/velocitex/dist
node tests/wasm/katex-browser-parity.mjs
node tests/wasm/katex-browser-benchmark.mjs npm/velocitex/dist
```

`katex-browser-report.mjs` 输出本页使用的分工作负载表格，并可加
`--json artifacts/benchmarks/browser-report.json` 保存原始数据。
`katex-browser-benchmark.mjs` 是仓库的验收门禁，默认要求 5x；本机 v1.0.0
读数约为 3.9x，因此该断言会失败。本页不调整门禁，只报告实际测量值。

## 说明

- 计时不包含页面启动、资源下载、浏览器栅格化、GPU 合成和屏幕呈现；这些
  环节两个引擎同样需要，且不属于排版引擎的吞吐。
- 浏览器基准使用默认 `htmlAndMathml` 输出；显式 `output: 'svg'`、
  `fastHtml: false` 或 `fastMathml: false` 会走 .NET WASM 回退路径，性能特征
  不同。
- 所有数字来自同一台共享参考机，绝对值会随负载和硬件变化；比较同一页、同一
  轮的相对加速比更可靠。
