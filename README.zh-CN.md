# VelociTex

[English](README.md) | 中文

VelociTex 是独立的 TeX 数学排版与渲染组件，数学规则固定为 **KaTeX 0.19.0**。
支持 Windows 原生显示、PNG / SVG / MathML 导出、WinUI 3 控件和浏览器端调用。
原生排版由 C++ 核心完成，通过 .NET 10 绑定、DirectWrite 和 Direct2D 绘制，无需浏览器或 JavaScript 运行时。

<!-- benchmark:start -->

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

![Render-work throughput benchmark](docs/benchmarks/benchmark-render-work.png)

All 36 formula occurrences are included, including automatic fallback. Three fresh processes × 15 calibrated batches per case; medians of batch averages, with actual page formula counts. No explicit frame waits or document cache. Startup/download excluded. Render work measures native surface submission or browser DOM + forced layout, with different output backends; it excludes a GPU completion fence, browser rasterization, and screen presentation. Results are specific to this hardware and corpus.

[Methodology, all cases, raw batches and reproduction commands](benchmarks/README.md).

## 渲染效果对比

Actual output at 32 px math em, captured at 2× resolution. Each row uses equal-scale canvases; table display scales them together. WASM uses the default C++ MathML route with automatic full-Core fallback. Native and KaTeX use their own font rendering; identical pixels are not claimed.

| Formula                | Windows native / Direct2D                                                                                  | VelociTex WASM                                                                                         | KaTeX 0.19.0                                                                                             |
| ---------------------- | ---------------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------------------------------------------------- |
| Algebra                | <img src="docs/benchmarks/rendering/algebra-native.png" width="280" alt="Algebra: native">                 | <img src="docs/benchmarks/rendering/algebra-wasm.png" width="280" alt="Algebra: wasm">                 | <img src="docs/benchmarks/rendering/algebra-katex.png" width="280" alt="Algebra: katex">                 |
| Calculus               | <img src="docs/benchmarks/rendering/calculus-native.png" width="280" alt="Calculus: native">               | <img src="docs/benchmarks/rendering/calculus-wasm.png" width="280" alt="Calculus: wasm">               | <img src="docs/benchmarks/rendering/calculus-katex.png" width="280" alt="Calculus: katex">               |
| Piecewise & matrices   | <img src="docs/benchmarks/rendering/structures-native.png" width="280" alt="Piecewise & matrices: native"> | <img src="docs/benchmarks/rendering/structures-wasm.png" width="280" alt="Piecewise & matrices: wasm"> | <img src="docs/benchmarks/rendering/structures-katex.png" width="280" alt="Piecewise & matrices: katex"> |
| Color, accents & fonts | <img src="docs/benchmarks/rendering/color-native.png" width="280" alt="Color, accents & fonts: native">    | <img src="docs/benchmarks/rendering/color-wasm.png" width="280" alt="Color, accents & fonts: wasm">    | <img src="docs/benchmarks/rendering/color-katex.png" width="280" alt="Color, accents & fonts: katex">    |

<!-- benchmark:end -->

## 选择组件

| 组件                           | 用途                                        |
| ------------------------------ | ------------------------------------------- |
| `VelociTex.Core`               | 编译公式，获取尺寸、基线和诊断，导出 MathML |
| `VelociTex.Rendering.Direct2D` | Windows x64 原生绘制及 PNG / SVG 导出       |
| `VelociTex.WinUI`              | 使用原生 `MathView` 控件显示公式            |
| npm `velocitex`                | 浏览器渲染与 TypeScript 类型声明            |

源码分发另包含跨平台 SVG 后端和可选的 C++ MathML 适配器。

原生组件要求 .NET 10。Windows 渲染目标为 Windows 10 19041 及以上、x64；
WinUI 应用需配置自己的 Windows App SDK 宿主。支持 Native AOT 与裁剪。
浏览器分发包自带 WebAssembly 资源，无需访客安装 .NET。
请使用包源中可用的组件版本；npm 预览版使用 `next` 通道。

## 编译与导出

```csharp
using VelociTex;
using VelociTex.Rendering.Direct2D;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (engine.TryCompile(@"\frac{a}{b}+\sqrt{x}", in options, workspace,
    CancellationToken.None, out var document, out var diagnostic))
{
    using var renderer = new WindowsMathRenderer();
    renderer.WritePng(document!, "formula.png");
    renderer.WriteSvg(document!, "formula.svg");
    File.WriteAllText("formula.mathml", MathMlWriter.Write(document!));
}
else
{
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
}
```

## 网页使用

```sh
npm install velocitex@next
npx velocitex-copy-assets public/velocitex
```

```js
import { init, renderAll } from 'velocitex';
await init({ assetBaseUrl: '/velocitex/' });
await renderAll();
```

## 用户文档

- [原生组件使用](docs/native-usage.zh-CN.md) · [English](docs/native-usage.md)
- [性能基准](docs/benchmarks.md) · 方法、原始数据与图表
- [API、资源所有权与限制](docs/package-contract.md)
- [网页 API 与部署](docs/wasm-js.zh-CN.md) · [English](npm/velocitex/README.md)
- [导出示例](samples/VelociTex.Export.Sample/README.md)
- [网页示例](samples/VelociTex.Web.Sample/README.md)
- [输入安全](SECURITY.md) · [第三方许可](THIRD-PARTY-NOTICES.md)

每次处理一条完整公式，不承担完整 LaTeX 文档编译、任意 TeX 宏包加载、公式编辑
或正文自动换行。不支持的语法返回诊断。浏览器字体回退有限，使用捆绑数学字体
以外的文字前请阅读网页指南。

代码使用 MIT 许可；KaTeX 原字体及提取轮廓保留 **OFL-1.1** 和保留字体名称条款，
字体不属于 MIT 许可范围。
