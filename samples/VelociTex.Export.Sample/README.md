# Formula export example / 公式导出示例

Requires Windows x64 and the repository's .NET SDK. Run from the repository root:

```powershell
dotnet run --project samples/VelociTex.Export.Sample -p:Platform=x64 -- '\frac{a}{b}+\sqrt{x}' output
```

The first argument is a complete TeX formula without outer delimiters. The
second is the output directory (default `output`). The program writes
`formula.png`, `formula.svg` and `formula.mathml`. Invalid input prints a
diagnostic and exits with code 1.

需要 Windows x64 和仓库指定的 .NET SDK。在仓库根目录执行上述命令；第一个参数为
不含外层定界符的完整公式，第二个参数为输出目录，默认 `output`。成功时生成
PNG、SVG 和 MathML；非法输入显示诊断并返回退出码 1。

[Native usage](../../docs/native-usage.md) · [原生组件使用](../../docs/native-usage.zh-CN.md)
