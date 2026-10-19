using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using System.Xml.Linq;
using VelociTex;
using VelociTex.Rendering.Direct2D;

// This file is also copied unchanged into the isolated PackageReference consumer.
// It uses public APIs only; the package consumer has no project references or IVT.
var output = Path.GetFullPath(args.FirstOrDefault(a => !a.StartsWith("--", StringComparison.Ordinal)) ?? "artifacts/export-sample");
Directory.CreateDirectory(output);
var nativeAot = !RuntimeFeature.IsDynamicCodeSupported;
if (args.Contains("--expect-aot") && !nativeAot || args.Contains("--expect-jit") && nativeAot)
    throw new InvalidOperationException("The consumer did not run in the requested runtime mode.");
var report = new SampleRunReport
{
    ProcessId = Environment.ProcessId, NativeAot = nativeAot,
    Framework = RuntimeInformation.FrameworkDescription, OperatingSystem = RuntimeInformation.OSDescription,
    CoreAssembly = typeof(MathEngine).Assembly.GetName().FullName!,
    RenderingAssembly = typeof(WindowsMathRenderer).Assembly.GetName().FullName!,
};
void Require(bool condition, string message)
{
    if (!condition) throw new InvalidOperationException("Export consumer assertion: " + message);
    report.Checks.Add(message);
}
string Hash(byte[] bytes) => Convert.ToHexStringLower(SHA256.HashData(bytes));
var fontDirectory = Path.Combine(output, "caller-owned-fonts");
Directory.CreateDirectory(fontDirectory);
using (var fontLock = JsonDocument.Parse(File.ReadAllText(Path.Combine(AppContext.BaseDirectory, "font-lock.json"))))
{
    var expected = fontLock.RootElement.GetProperty("ttfFonts").EnumerateArray()
        .ToDictionary(f => f.GetProperty("file").GetString()!, f => f.GetProperty("sha256").GetString()!, StringComparer.Ordinal);
    Require(MathFontCatalog.Count == 20 && expected.Count == 20, "twenty locked private fonts are available");
    for (var index = 0; index < MathFontCatalog.Count; index++)
    {
        var file = MathFontCatalog.GetResourceFileName(index);
        using var source = MathFontCatalog.OpenFont(index);
        using var memory = new MemoryStream(); source.CopyTo(memory); var bytes = memory.ToArray();
        var digest = Hash(bytes);
        Require(expected.TryGetValue(file, out var expectedHash) && digest == expectedHash, "locked font SHA-256: " + file);
        File.WriteAllBytes(Path.Combine(fontDirectory, file), bytes);
        report.Fonts.Add(new() { File = file, Name = MathFontCatalog.GetName(index), Bytes = bytes.Length, Sha256 = digest });
    }
}

var engine = new MathEngine();
using var workspace = new MathWorkspace();
var textShaper = new WindowsMathTextShaper(maximumItems: 16, maximumBytes: 512 * 1024);
var options = new MathOptions { FontSize = 24, DisplayMode = true, TextShaper = textShaper };
MathDocument Compile(string source, MathOptions? selected = null)
{
    var settings = selected ?? options;
    if (!engine.TryCompile(source, settings, workspace, default, out var document, out var error))
        throw new InvalidOperationException(source + ": " + error);
    return document!;
}
var inputs = new (string Id, string Source)[]
{
    ("integral", @"\int_0^1 x^2\,dx"),
    ("matrix", @"A=\begin{pmatrix}1&\frac12\\\sqrt3&4\end{pmatrix}"),
    ("cjk", @"\text{平均值}=\frac{1}{3}\sum_{i=1}^3x_i"),
    ("operator", @"\operatorname*{argmax}_{x\in X}f(x)"),
    ("accent-delimiter", @"\widehat{ABC}+\left\langle\frac{a}{b}\right\rangle"),
};
var documents = inputs.Select(p => (p.Id, Document: Compile(p.Source))).ToArray();
var retained = documents.Single(p => p.Id == "cjk").Document;
Require(retained.TextGlyphs.Length == 3 && retained.Paths.Length > 0, "CJK fallback uses actual retained outlines");
Require(retained.TextGlyphs.Span.ToArray().All(g => !string.IsNullOrWhiteSpace(g.FontFamily)), "actual fallback font family is reported");
Require(retained.Options.TextShaper is null, "document does not retain the fallback provider");
var retainedMathMl = MathMlWriter.Write(retained);
textShaper.Clear(); workspace.Dispose();
Require(textShaper.Count == 0 && textShaper.EstimatedRetainedBytes == 0, "fallback outline cache can be cleared independently");
Require(MathMlWriter.Write(retained) == retainedMathMl, "document survives workspace disposal and fallback cache clear");
using (var renderer = new WindowsMathRenderer(fontDirectory))
{
    foreach (var (id, document) in documents)
    {
        using (var prepared = renderer.Prepare(document)) Require(ReferenceEquals(prepared.Document, document), "preparation retains independent document: " + id);
        var formula = new SampleFormulaReport
        {
            Id = id, Source = document.Source, Width = document.LogicalSize.Width,
            Height = document.LogicalSize.Height, Baseline = document.Baseline,
            Glyphs = document.Glyphs.Length, FallbackGlyphs = document.TextGlyphs.Length, Paths = document.Paths.Length,
            FallbackFamilies = document.TextGlyphs.Span.ToArray().Select(g => g.FontFamily).Distinct().ToArray(),
        };
        foreach (var extension in new[] { "png", "svg", "mathml" })
        {
            using var stream = new MemoryStream();
            switch (extension)
            {
                case "png": renderer.WritePng(document, stream, scale: 1.5); break;
                case "svg": renderer.WriteSvg(document, stream); break;
                default: MathMlWriter.Write(document, stream); break;
            }
            Require(stream.CanWrite && stream.Length > 0, "export retains caller stream: " + id + "." + extension);
            var bytes = stream.ToArray();
            if (extension == "png") Require(bytes.AsSpan(0, 8).SequenceEqual(new byte[] { 137, 80, 78, 71, 13, 10, 26, 10 }), "real PNG signature: " + id);
            else
            {
                var xml = XDocument.Parse(Encoding.UTF8.GetString(bytes));
                Require(xml.Root?.Name.LocalName == (extension == "svg" ? "svg" : "math"), "well-formed XML export: " + id + "." + extension);
                if (extension == "svg") Require(!xml.Descendants().Attributes().Any(a => a.Name.LocalName == "href" && !a.Value.StartsWith('#')), "SVG has no external resources: " + id);
                else Require(xml.Descendants().Any(e => e.Name.LocalName == "annotation" && e.Value == document.Source), "MathML retains original source: " + id);
            }
            var file = id + "." + extension; File.WriteAllBytes(Path.Combine(output, file), bytes);
            formula.Exports.Add(new() { File = file, Bytes = bytes.Length, Sha256 = Hash(bytes) });
        }
        report.Formulas.Add(formula);
    }
    var disposed = renderer.Prepare(retained); disposed.Dispose();
    try { renderer.Draw(disposed, 0, MathDrawingColor.Black); throw new InvalidOperationException("Disposed preparation was accepted."); }
    catch (ObjectDisposedException) { report.Checks.Add("disposed native preparation is rejected"); }
}
Require(report.Fonts.All(f => File.Exists(Path.Combine(fontDirectory, f.File))), "renderer preserves the caller-owned private font directory");
// The default renderer independently extracts embedded fonts and releases its own native faces.
using (var defaultRenderer = new WindowsMathRenderer()) defaultRenderer.WritePng(retained, Path.Combine(output, "default-font-owner.png"));
Require(File.Exists(Path.Combine(output, "default-font-owner.png")), "default private-font owner can render the retained result");

var cache = new MathDocumentCache(maximumBytes: 256 * 1024, maximumItems: 3);
var cacheOptions = options with { TextShaper = null };
Require(cache.TryCompile(engine, "x+1", cacheOptions, workspace, default, out var first, out _, out var firstHit) && !firstHit, "first bounded-cache lookup compiles");
Require(cache.TryCompile(engine, "x+1", cacheOptions, workspace, default, out var second, out _, out var secondHit) && secondHit && ReferenceEquals(first, second), "second bounded-cache lookup returns the owned result");
using (var cancelled = new CancellationTokenSource())
{
    cancelled.Cancel();
    Require(!cache.TryCompile(engine, "x+1", cacheOptions, workspace, cancelled.Token, out var cancelledDocument, out var cancelledError, out var cancelledHit)
        && cancelledDocument is null && cancelledError.Code == MathDiagnosticCode.Cancelled && !cancelledHit, "cancelled lookup never returns a cached result");
}
for (var index = 0; index < 12; index++) Require(cache.TryCompile(engine, "x_{" + index + "}", cacheOptions, workspace, default, out _, out _, out _), "bounded-cache workload formula: " + index);
Require(cache.Count <= cache.MaximumItems && cache.EstimatedRetainedBytes <= cache.MaximumBytes, "cache enforces count and estimated byte budgets");
var countBeforeMacro = cache.Count;
Require(cache.TryCompile(engine, @"\def\a{x}\a", cacheOptions, workspace, default, out _, out _, out var macroHit) && !macroHit && cache.Count == countBeforeMacro, "definition-bearing formula bypasses result caching");
Require(cache.TryCompile(engine, @"\text{平均值}", options, workspace, default, out _, out _, out var fallbackHit) && !fallbackHit && cache.Count == countBeforeMacro, "custom fallback provider bypasses result caching");
Require(!engine.TryCompile(@"\frac{x}", options, workspace, default, out var bad, out var diagnostic) && bad is null && diagnostic.IsError, "invalid input produces a structured diagnostic");
Require(Compile("x+2").Source == "x+2", "workspace recovers after cancellation and parse failure");
var tiny = options with { MaxNodes = 2 };
Require(!engine.TryCompile("x+y", tiny, workspace, default, out _, out var nodeError) && nodeError.Code == MathDiagnosticCode.NodeLimit, "node budget failure remains structured");
Require(Compile("x+3").Source == "x+3", "workspace recovers after a resource budget failure");
var session = new MacroSession(); var sessionOptions = options with { Session = session };
Compile(@"\gdef\consumerMacro{x}", sessionOptions);
Require(!engine.TryCompile(@"\gdef\consumerMacro{y}\unknownConsumerCommand", sessionOptions, workspace, default, out _, out _), "failed macro transaction is rejected");
Require(MathMlWriter.Write(Compile(@"\consumerMacro", sessionOptions)).Contains("<mi>x</mi>", StringComparison.Ordinal), "failed macro transaction preserves the earlier definition");
cache.Clear();
Require(cache.Count == 0 && cache.EstimatedRetainedBytes == 0, "result cache releases its owned references");
Require(MathMlWriter.Write(first!).Contains("x", StringComparison.Ordinal), "results remain valid after result-cache clear");
report.CacheMaximumItems = cache.MaximumItems; report.CacheMaximumBytes = cache.MaximumBytes;
File.WriteAllText(Path.Combine(output, "consumer-report.json"), JsonSerializer.Serialize(report, SampleJsonContext.Default.SampleRunReport), new UTF8Encoding(false));
Console.WriteLine($"Public API consumer passed: {report.Checks.Count} checks, {report.Fonts.Count} original fonts, {report.Formulas.Count} formulas, Native AOT={nativeAot}.");

internal sealed class SampleRunReport
{
    public int SchemaVersion { get; init; } = 1;
    public string PackageVersion { get; init; } = "0.2.0-preview.1";
    public int ProcessId { get; init; }
    public bool NativeAot { get; init; }
    public string Framework { get; init; } = "";
    public string OperatingSystem { get; init; } = "";
    public string CoreAssembly { get; init; } = "";
    public string RenderingAssembly { get; init; } = "";
    public int CacheMaximumItems { get; set; }
    public long CacheMaximumBytes { get; set; }
    public List<SampleFontReport> Fonts { get; } = new();
    public List<SampleFormulaReport> Formulas { get; } = new();
    public List<string> Checks { get; } = new();
}
internal sealed class SampleFontReport
{
    public string File { get; init; } = "";
    public string Name { get; init; } = "";
    public int Bytes { get; init; }
    public string Sha256 { get; init; } = "";
}
internal sealed class SampleFormulaReport
{
    public string Id { get; init; } = "";
    public string Source { get; init; } = "";
    public double Width { get; init; }
    public double Height { get; init; }
    public double Baseline { get; init; }
    public int Glyphs { get; init; }
    public int FallbackGlyphs { get; init; }
    public int Paths { get; init; }
    public string[] FallbackFamilies { get; init; } = [];
    public List<SampleExportReport> Exports { get; } = new();
}
internal sealed class SampleExportReport
{
    public string File { get; init; } = "";
    public long Bytes { get; init; }
    public string Sha256 { get; init; } = "";
}
[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(SampleRunReport))]
internal partial class SampleJsonContext : JsonSerializerContext;
