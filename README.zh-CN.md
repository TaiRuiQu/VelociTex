# VelociTex

[English](README.md) | 中文

VelociTex 是独立的 TeX 数学排版与渲染组件，数学规则固定为 **KaTeX 0.19.0**。
支持 Windows 原生显示、PNG / SVG / MathML 导出、WinUI 3 控件和浏览器端调用。
原生排版由 C++ 核心完成，通过 .NET 10 绑定、DirectWrite 和 Direct2D 绘制，无需浏览器或 JavaScript 运行时。

<!-- benchmark:start -->

## Performance

2026-10-21 · Windows x64 · Release · KaTeX 0.19.0 · µs/formula (lower is better).

Both browser engines generate default HTML with accessibility MathML, using the same locked KaTeX CSS and fonts. Every benchmark formula is checked for byte-identical markup before timing.

Correctness validation passed for 3,659 valid core HTML/MathML fixtures and 46 browser integration formulas, including bundled stylesheet and font loading.

| Workload         | WASM HTML generation | KaTeX HTML generation | WASM DOM + layout | KaTeX DOM + layout |
| ---------------- | -------------------: | --------------------: | ----------------: | -----------------: |
| Common formulas  |                 43.4 |                  44.5 |             615.4 |              593.4 |
| Complex formulas |                426.2 |                 431.8 |            3649.2 |             3538.9 |
| Mixed pages      |                 44.9 |                  40.7 |             556.1 |              514.4 |

WASM/KaTeX relative throughput is **0.91–1.03×** for HTML generation and **0.93–0.97×** for DOM render work on this machine. Ratios below 1 mean WASM is slower.

![HTML generation](docs/benchmarks/benchmark-comparison.png)

![Browser render work](docs/benchmarks/benchmark-render-work.png)

All 36 formula occurrences are retained: 6 common, 3 complex, and 27 across three mixed pages. Three fresh processes × 15 calibrated batches per case; each case warms for at least 200 ms. Public async WASM API overhead is included. DOM timing forces layout after every formula. Startup, download, browser rasterization, GPU completion and screen presentation are excluded. Native C++/.NET/WinUI results and MathML serialization are separate diagnostics; the C++ Core does not expose this HTML generator.

[Methodology and reproduction](benchmarks/README.md).

## Rendering comparison

Actual output at 32 px math em and 2× capture resolution. The WASM column uses the current default HTML/CSS renderer and bundled fonts. All four previews have matching computed styles and dimensions in their visible HTML trees. Three have byte-identical markup; the combined color example differs in CSS declaration order and a redundant inherited color declaration. Captures are not asserted pixel-identical. Cells in each row share a canvas and scale. Native uses DirectWrite/Direct2D.

| Formula                | Windows native / Direct2D                                                                                  | VelociTex HTML/CSS WASM                                                                                | KaTeX 0.19.0                                                                                             |
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
或正文自动换行。不支持的语法返回诊断。KaTeX 兼容 HTML 路径与 KaTeX 的浏览器
字体回退一致；旧版 SVG 路径没有浏览器 DirectWrite 塑形器，使用捆绑数学字体以外
的文字前请阅读网页指南。

代码使用 MIT 许可；KaTeX 原字体及提取轮廓保留 **OFL-1.1** 和保留字体名称条款，
字体不属于 MIT 许可范围。
