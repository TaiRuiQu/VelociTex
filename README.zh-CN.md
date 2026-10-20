# VelociTex

[English](README.md) | 中文

VelociTex 是独立的 TeX 数学排版与渲染组件，数学规则固定为 **KaTeX 0.19.0**。
支持 Windows 原生显示、PNG / SVG / MathML 导出、WinUI 3 控件和浏览器端调用。
原生渲染基于 .NET 10、DirectWrite 和 Direct2D，无需浏览器或 JavaScript 运行时。

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
