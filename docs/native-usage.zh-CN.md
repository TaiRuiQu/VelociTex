# 原生组件使用

[English](native-usage.md) | 中文

使用 .NET 10，从包源添加 `VelociTex.Core`；Windows 绘制与图片导出还需
`VelociTex.Rendering.Direct2D`。在已有 WinUI 3 应用中使用控件时添加
`VelociTex.WinUI`。Windows 渲染目标为 Windows 10 19041 及以上、x64。

## 编译公式

```csharp
using VelociTex;

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions { FontSize = 24, DisplayMode = true };
if (!engine.TryCompile(@"\int_0^1 x^2,dx", in options, workspace,
    CancellationToken.None, out var result, out var diagnostic))
{
    Console.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
    return;
}
Console.WriteLine($"{result!.LogicalSize}; baseline={result.Baseline}");
File.WriteAllText("formula.mathml", MathMlWriter.Write(result));
```

输入不包含外层 `$` 定界符；`DisplayMode` 控制展示样式。字号、尺寸、基线单位均为
DIP。工作区可串行复用，结果独立拥有数据，不受工作区复用或释放影响。并发编译
需分别创建工作区。

字符串宏通过 `MathOptions.Macros` 传入。默认各公式独立；需要跨公式保留定义时，
显式使用 `MacroSession`，详见 [API 约定](package-contract.md)。

## 图片导出与字体回退

```csharp
using VelociTex.Rendering.Direct2D;

using var renderer = new WindowsMathRenderer();
renderer.WritePng(result!, "formula.png");
renderer.WriteSvg(result!, "formula.svg");
```

渲染器私有加载捆绑字体，不会安装系统字体。渲染器和已准备的原生资源必须在创建
线程使用及释放；流重载不会关闭调用方的流。需要捆绑字体以外的文字时，在编译前
将 `MathOptions.TextShaper` 设为 `WindowsMathTextShaper`，实际覆盖取决于系统字体。
SVG 包含字形轮廓，查看时无需安装字体。

## WinUI 3 显示

在应用 XAML 中声明：

```xml
<math:MathView x:Name="Formula" xmlns:math="using:VelociTex.WinUI" />
```

编译成功后，在 UI 线程赋值：

```csharp
Formula.Result = result;
Formula.AccessibleDescription = "从零到一对 x 的平方积分";
```

设置 `null` 清空结果。默认颜色跟随主题，也可设置 `FormulaColor`；调用
`UseThemeColor()` 恢复主题色。支持 Ctrl+C 复制完整源公式，并向 UI Automation
提供只读源文本。应用可提供自然语言描述；不包含数学结构朗读导航或局部选择。

## 只生成 MathML

`MathEngine.TryCompileToMathMl` 跳过布局，相同输入和选项下与
`MathMlWriter.Write` 的托管结果一致。可选 `VelociTex.NativeMath` 优先使用 C++
生成 presentation MathML，不支持时回退托管引擎。Windows 应用需将
`VelociTex.NativeMath.dll` 放在应用目录，或通过 `VELOCITEX_NATIVE_DIR` 指定路径。
快速输出可能具有不同的标签结构且不附源文本注释；需要精确输出时直接使用托管 API。

## 错误与资源限制

非法或不支持的输入使 `TryCompile` 返回 `false` 和诊断，应用应保留原文。
诊断范围使用 UTF-16 偏移。`CompileForPresentation` 可将普通语法错误显示为
带颜色的原文。通过 `MathOptions` 设置输入、宏展开、节点和嵌套预算；取消为
协作式。PNG 每边不超过 8192 像素，总像素数不超过 4194304。

详见 [API、线程与资源约定](package-contract.md)、[输入安全](../SECURITY.md)
和[可运行的导出示例](../samples/VelociTex.Export.Sample/README.md)。
