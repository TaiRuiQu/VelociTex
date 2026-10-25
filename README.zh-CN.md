# VelociTex

[English](README.md) | 中文

VelociTex 是独立的 TeX 数学排版与渲染组件，数学规则固定为 **KaTeX 0.19.0**。
支持 Windows 原生显示、PNG / SVG / MathML 导出、WinUI 3 控件和浏览器端调用。
原生排版由 C++ 核心完成，通过 .NET 10 绑定、DirectWrite 和 Direct2D 绘制，无需浏览器或 JavaScript 运行时。

<!-- benchmark:start -->

## 一个 C++ 核心，两个端点

仓库里只有**一套**公式解析与渲染实现：`native/core` 中的 C++ 核心。两个对外端点
都调用它，没有任何其他代码解析公式。

| 端点                      | 接入方式                                     |
| ------------------------- | -------------------------------------------- |
| .NET（`VelociTex.Core`）  | `native/core/api.cpp` 的 C ABI               |
| 浏览器（npm `velocitex`） | `native/core` 编译出的 `velocitex-core.wasm` |

浏览器包不再包含 JavaScript 公式解析器，也不再包含 Rust 公式解析器：WebAssembly
模块就是 C++ 核心，输出的 HTML 与 MathML 与 .NET 端点来自同一份代码。

## 与 KaTeX 完全一致的浏览器 HTML

npm 包的默认浏览器路径对受支持的核心公式输出与 **KaTeX 0.19.0 逐字节相同**的
HTML 和 MathML，不再以 MathML 代替排版。它使用锁定的 KaTeX 0.19.0 样式表和未修改的
KaTeX 字体。原生校验门逐字节比对 **5,411 条公式**——1,605 条真实世界公式、60 条精选
功能语料、36 条冻结的基准工作负载，以及仓库自带的 `tests/fixtures/core.json`
（3,710 条已注册符号／函数／宏／环境）——**HTML 与 MathML 两路都逐字节比对**；
浏览器校验门在 Chromium 中重复其中 1,651 条（真实世界语料、基准工作负载与精选样本）。

与 KaTeX 自带 JavaScript 渲染器的吞吐对比由
`tests/wasm/katex-browser-benchmark.mjs` 测量：当前 C++ WASM 在 2 核参考机上
**快约 3.8 倍**（近期读数 3.73×–3.94×），**尚未达到项目要求的 5 倍**（基准脚本对两个引擎同样预热 25 轮，
再测 120 轮配对样本，报告逐轮比值的中位数，读数不受负载波动影响）。但 **C++ Core 本身在原生构建下
已经比 KaTeX 快约 5–6 倍**（同一语料、同一套测量口径下 26.6 µs/公式 vs
浏览器里的 156–189 µs/公式）——剩下的差距来自 WebAssembly 与浏览器这一层，而不是算法本身。
引用倍数时必须写清 KaTeX 跑在哪个运行时里：同一份 `katex.mjs` 在 Node 里比在
Chromium 里慢约 40%（232–288 µs/公式 vs 156–244 µs/公式），原生内核对 Node 读数
是 8.7–10.8 倍，对 Chromium 读数是 5.9–7.1 倍。
这里的 26.6 µs 是 clang 20 的读数：同一份源码用 GCC 11.4 编译慢 12%
（同一语料、同一套口径下 32.2 µs/公式），而 WASM 构建用的正是 clang——按同编译器
口径衡量，WebAssembly 这一层的税是 +44%，且全部落在渲染器上，不在解析器。
浏览器路径 = 原生内核 × 1.37（WASM 的税）× 1.11–1.19（浏览器相对 Node）

- 6 µs（JS 那一层，实测只占整条路径的 12–15%：公开 API 对裸 `vt_render` 的比值是 1.146），
  因此**浏览器里的 5 倍需要原生内核约 15.5–19.5 µs**，比现在还快 30–45%——这比"三段都重写一遍"
  还要多：三段重写只能到 ~4.5 倍，若重写顺带把 WASM 的税压到 +35% 是 ~4.7 倍，
  若 JS 那一层只剩下解码那 0.42 µs 才是 ~5.4 倍。当前数据、各因子的拆解、差距分析与后续
  优化路线记录在 `docs/benchmarks.md`。

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
