using VelociTex;
using VelociTex.Rendering.Direct2D;

var source = args.Length > 0 ? args[0] : @"\frac{a}{b}+\sqrt{x}";
var output = Path.GetFullPath(args.Length > 1 ? args[1] : "output");
var engine = new MathEngine();
using var workspace = new MathWorkspace();
var options = new MathOptions
{
    FontSize = 24,
    DisplayMode = true,
    TextShaper = new WindowsMathTextShaper(),
};
if (!engine.TryCompile(source, in options, workspace, CancellationToken.None,
    out var document, out var diagnostic))
{
    Console.Error.WriteLine($"{diagnostic.Code}: {diagnostic.Message}");
    return 1;
}

Directory.CreateDirectory(output);
using var renderer = new WindowsMathRenderer();
renderer.WritePng(document!, Path.Combine(output, "formula.png"));
renderer.WriteSvg(document!, Path.Combine(output, "formula.svg"));
File.WriteAllText(Path.Combine(output, "formula.mathml"), MathMlWriter.Write(document!));
Console.WriteLine($"Exported PNG, SVG and MathML to {output}");
return 0;
