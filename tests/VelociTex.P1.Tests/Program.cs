using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using VelociTex;
using VelociTex.Rendering.Direct2D;

// Dependency-free executable assertions run unchanged under JIT and Native AOT.
// JsonDocument is input tooling only; production parsing never uses JSON/DOM.
string output = Path.GetFullPath(args.Length > 0 ? args[0] : "artifacts/p1/jit");
Directory.CreateDirectory(output);
string fixturePath = Path.Combine(AppContext.BaseDirectory, "fixtures", "p1.json");
using JsonDocument corpus = JsonDocument.Parse(File.ReadAllBytes(fixturePath));
MathWorkspace workspace = new();
MathEngine engine = new();
using WindowsMathRenderer renderer = new(); // Test embedded fonts, not repository font paths.
List<(string Id, string Category, string Scenario, MathDocument? Document, MathDiagnostic Diagnostic)> results = [];
HashSet<string> selected = ["single-x", "both-scripts", "frac-display", "sqrt-frac", "roman", "macro-arg"];
int valid = 0, invalid = 0;
foreach (JsonElement fixture in corpus.RootElement.GetProperty("cases").EnumerateArray())
{
    string id = fixture.GetProperty("id").GetString()!;
    string category = fixture.GetProperty("category").GetString()!;
    string latex = fixture.GetProperty("latex").GetString()!;
    bool expected = fixture.GetProperty("expectation").GetString() == "valid";
    Dictionary<string, string>? macros = null;
    if (fixture.TryGetProperty("macros", out JsonElement definitions))
    {
        macros = [];
        foreach (JsonProperty macro in definitions.EnumerateObject()) macros.Add(macro.Name, macro.Value.GetString()!);
    }
    MathOptions options = new() { DisplayMode = fixture.TryGetProperty("displayMode", out var display) && display.GetBoolean(), Macros = macros };
    bool success = engine.TryCompile(latex, in options, workspace, default, out MathDocument? document, out MathDiagnostic diagnostic);
    Assert(success == expected, $"{id}: expected {expected}, got {diagnostic}");
    Assert(success ? document!.Source == latex && !diagnostic.IsError : document is null && diagnostic.IsError, $"{id}: success/error ownership");
    if (success) valid++; else invalid++;
    if (!success)
    {
        Assert(diagnostic.Range.Start >= 0 && diagnostic.Range.Length >= 0 && diagnostic.Range.Start + diagnostic.Range.Length <= latex.Length, $"{id}: diagnostic source bounds");
        results.Add((id, category, "24dip-100pct-light", null, diagnostic));
        Assert(engine.TryCompile("x+1", in options, workspace, default, out _, out _), $"{id}: recovery after parse failure");
        continue;
    }
    foreach ((double size, double scale, bool dark) in Scenarios(selected.Contains(id)))
    {
        MathOptions scenarioOptions = options with { FontSize = size };
        Assert(engine.TryCompile(latex, in scenarioOptions, workspace, default, out MathDocument? scenarioDocument, out diagnostic), $"{id}: scenario compile");
        document = scenarioDocument!;
        string scenario = FormattableString.Invariant($"{size:0}dip-{scale * 100:0}pct-{(dark ? "dark" : "light")}");
        string directory = Path.Combine(output, id, scenario);
        Directory.CreateDirectory(directory);
        MathDrawingColor color = dark ? new(238f / 255, 238f / 255, 238f / 255) : new(17f / 255, 17f / 255, 17f / 255);
        renderer.WritePng(document, Path.Combine(directory, "formula.png"), scale, 8, color);
        renderer.WriteSvg(document, Path.Combine(directory, "formula.svg"), 8, color);
        File.WriteAllText(Path.Combine(directory, "formula.mathml"), MathMlWriter.Write(document), new UTF8Encoding(false));
        Assert(document.LogicalSize.Width >= 0 && document.LogicalSize.Height >= 0 && double.IsFinite(document.Baseline), $"{id}: finite metrics");
        foreach (MathGlyph glyph in document.Glyphs.Span)
            Assert(glyph.GlyphId != 0 && double.IsFinite(glyph.X) && double.IsFinite(glyph.Y) && glyph.FontSize > 0, $"{id}: finite mapped glyph");
        using (PreparedMath prepared = renderer.Prepare(document)) Assert(ReferenceEquals(prepared.Document, document), $"{id}: renderer uses the same result");
        results.Add((id, category, scenario, document, diagnostic));
    }
}
Assert(valid >= 50 && invalid >= 10, "phase denominator");

// Results outlive scratch reuse, cancellation, invalid input and isolated calls.
MathOptions normal = new();
Assert(engine.TryCompile("x_i^2+\\frac{a}{b}", in normal, workspace, default, out MathDocument? retained, out _), "lifetime initial compile");
string before = Fingerprint(retained!);
for (int i = 0; i < 300; i++)
{
    string input = i % 3 == 0 ? "x^" : i % 3 == 1 ? "\\sqrt{1+y}" : "\\def\\foo{x}\\foo";
    engine.TryCompile(input, in normal, workspace, default, out _, out _);
}
Assert(before == Fingerprint(retained!), "result arrays do not borrow workspace buffers");
using CancellationTokenSource cancelled = new(); cancelled.Cancel();
Assert(!engine.TryCompile("x", in normal, workspace, cancelled.Token, out _, out MathDiagnostic cancelledDiagnostic) && cancelledDiagnostic.Code == MathDiagnosticCode.Cancelled, "pre-cancelled compile");
Assert(engine.TryCompile("x", in normal, workspace, default, out _, out _), "recovery after cancellation");
MathOptions smallInput = normal with { MaxInputLength = 4 };
Assert(!engine.TryCompile("abcde", in smallInput, workspace, default, out _, out var error) && error.Code == MathDiagnosticCode.InputLimit, "input budget");
MathOptions smallNesting = normal with { MaxNesting = 4 };
Assert(!engine.TryCompile("{{{{{x}}}}}", in smallNesting, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.NestingLimit, "nesting budget");
MathOptions smallNodes = normal with { MaxNodes = 4 };
Assert(!engine.TryCompile("abcdef", in smallNodes, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.NodeLimit, "node budget");
MathOptions smallExpansion = normal with { MaxExpansion = 8, Macros = new Dictionary<string, string> { ["\\r"] = "\\r" } };
Assert(!engine.TryCompile("\\r", in smallExpansion, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.ExpansionLimit, "macro expansion budget");
foreach (double badSize in new[] { double.NaN, double.PositiveInfinity, 0, -1 })
{
    MathOptions bad = normal with { FontSize = badSize };
    Assert(!engine.TryCompile("x", in bad, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.InvalidOptions, "invalid size rejected");
}
Assert(engine.TryCompile("\\def\\foo{x}\\foo", in normal, workspace, default, out _, out error), "local definition");
Assert(!engine.TryCompile("\\foo", in normal, workspace, default, out _, out error), "default formulas isolate macro definitions");
Assert(engine.TryCompile("x", in normal, workspace, default, out _, out _), "recovery after resource errors");
Assert(workspace.Enter(), "reserve workspace for busy check");
Assert(!engine.TryCompile("x", in normal, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.WorkspaceInUse, "concurrent reuse rejected");
workspace.Leave();
MacroSession session = new(); MathOptions sessionOptions = normal with { Session = session };
Assert(engine.TryCompile("\\gdef\\persist{x}", in sessionOptions, workspace, default, out _, out _), "session commits definition");
Assert(engine.TryCompile("\\persist", in sessionOptions, workspace, default, out var originalSession, out _), "session reads committed definition");
Assert(!engine.TryCompile("\\gdef\\persist{y}x^", in sessionOptions, workspace, default, out _, out _), "failed session transaction");
Assert(engine.TryCompile("\\persist", in sessionOptions, workspace, default, out var rolledBackSession, out _) && Fingerprint(originalSession!) == Fingerprint(rolledBackSession!), "failed definition rolled back");
Assert(session.Enter(), "reserve session for busy check");
Assert(!engine.TryCompile("x", in sessionOptions, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.SessionInUse, "concurrent session reuse rejected");
session.Leave();
MathOptions boundedGrowth = normal with { MaxInputLength = 10_000, MaxNodes = 5_000 };
Assert(!engine.TryCompile(new string('x', 6_000), in boundedGrowth, workspace, default, out _, out error) && error.Code == MathDiagnosticCode.NodeLimit, "large error grows temporary arena");
Assert(workspace.Nodes.Capacity <= 4096 && workspace.Expanded.Capacity <= 4096 && workspace.Pending.Capacity <= 4096, "large temporary capacities released");
MathOptions cancellationLoad = normal with { MaxInputLength = 500_000, MaxTokens = 500_000, MaxNodes = 262_144 };
string cancellationInput = new('x', 400_000);
using CancellationTokenSource duringWork = new(); duringWork.CancelAfter(1);
Assert(!engine.TryCompile(cancellationInput, in cancellationLoad, workspace, duringWork.Token, out _, out error) && error.Code == MathDiagnosticCode.Cancelled, "cancellation checked during work");
Assert(engine.TryCompile("x", in normal, workspace, default, out _, out _), "recovery after active cancellation");
Parallel.For(0, 100, i =>
{
    MathWorkspace isolated = new();
    Assert(engine.TryCompile("x_i^2+\\frac{a}{b}", in normal, isolated, default, out var concurrent, out _), "concurrent workspace compile");
    Assert(Fingerprint(concurrent!) == before, "concurrent immutable rules produce identical output");
});

// Path and stream overloads consume exactly the same document and preserve the
// caller's stream lifetime. A nonseekable wrapper catches accidental reliance
// on the managed sink supporting native COM IStream seeking.
using MemoryStream pngStream = new(), svgStream = new(), mathmlStream = new();
using (ForwardOnlyStream forward = new(pngStream)) renderer.WritePng(retained!, forward);
using (ForwardOnlyStream forward = new(svgStream)) renderer.WriteSvg(retained!, forward);
MathMlWriter.Write(retained!, mathmlStream);
string streamDirectory = Path.Combine(output, "stream-check"); Directory.CreateDirectory(streamDirectory);
renderer.WritePng(retained!, Path.Combine(streamDirectory, "path.png"));
renderer.WriteSvg(retained!, Path.Combine(streamDirectory, "path.svg"));
Assert(pngStream.ToArray().AsSpan().SequenceEqual(File.ReadAllBytes(Path.Combine(streamDirectory, "path.png"))), "PNG stream and path bytes agree");
Assert(svgStream.ToArray().AsSpan().SequenceEqual(File.ReadAllBytes(Path.Combine(streamDirectory, "path.svg"))), "SVG stream and path bytes agree");
Assert(Encoding.UTF8.GetString(mathmlStream.ToArray()) == MathMlWriter.Write(retained!), "MathML string and stream agree");
pngStream.WriteByte(0); svgStream.WriteByte(0); mathmlStream.WriteByte(0); // Open after write.
using (PreparedMath disposed = renderer.Prepare(retained!)) { disposed.Dispose(); AssertThrows<ObjectDisposedException>(() => renderer.Draw(disposed, 0, MathDrawingColor.Black), "disposed prepared result"); }
using (WindowsMathRenderer another = new()) using (PreparedMath owned = renderer.Prepare(retained!)) AssertThrows<ArgumentException>(() => another.Draw(owned, 0, MathDrawingColor.Black), "foreign renderer preparation");
Task.Run(() => AssertThrows<InvalidOperationException>(() => renderer.Prepare(retained!), "renderer cross-thread access")).GetAwaiter().GetResult();
AssertThrows<ArgumentOutOfRangeException>(() => renderer.WritePng(retained!, new MemoryStream(), scale: double.NaN), "PNG invalid scale");
AssertThrows<ArgumentOutOfRangeException>(() => renderer.WriteSvg(retained!, new MemoryStream(), padding: -1), "SVG invalid padding");
int edgeAssertions = CoreEdgeAssertions.Run();

// Allocation numbers include immutable results; these are a P1 subset baseline,
// not the P0 normal corpus or an old-CPU performance acceptance.
string[] workload = ["x+1", "x_i^2", "\\frac{a+b}{c}", "\\sqrt{1+x^2}", "\\mathbf{Ax}", "x^{y^2}"];
for (int i = 0; i < 200; i++) engine.TryCompile(workload[i % workload.Length], in normal, workspace, default, out _, out _);
long allocatedBefore = GC.GetAllocatedBytesForCurrentThread();
double[] timings = new double[6_000];
long[] allocations = new long[timings.Length];
for (int i = 0; i < timings.Length; i++)
{
    long allocationStart = GC.GetAllocatedBytesForCurrentThread();
    long start = Stopwatch.GetTimestamp();
    Assert(engine.TryCompile(workload[(i * 5) % workload.Length], in normal, workspace, default, out _, out _), "measured complete compile");
    timings[i] = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
    allocations[i] = GC.GetAllocatedBytesForCurrentThread() - allocationStart;
}
long managedAllocated = GC.GetAllocatedBytesForCurrentThread() - allocatedBefore;
using (FileStream samples = File.Create(Path.Combine(output, "compile-samples.csv")))
using (StreamWriter csv = new(samples))
{
    csv.WriteLine("sample,fixture,completeCompileMs,threadManagedAllocatedBytes");
    for (int i = 0; i < timings.Length; i++) csv.WriteLine(FormattableString.Invariant($"{i},{(i * 5) % workload.Length},{timings[i]:R},{allocations[i]}"));
}
double[] sorted = (double[])timings.Clone(); Array.Sort(sorted);
Process process = Process.GetCurrentProcess(); process.Refresh();
long privateBefore = process.PrivateMemorySize64; int handlesBefore = process.HandleCount;
for (int i = 0; i < 100; i++) using (WindowsMathRenderer fresh = new()) using (PreparedMath item = fresh.Prepare(retained!)) { }
GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); process.Refresh();
long privateAfter = process.PrivateMemorySize64; int handlesAfter = process.HandleCount;

using (FileStream stream = File.Create(Path.Combine(output, "report.json")))
using (Utf8JsonWriter json = new(stream, new JsonWriterOptions { Indented = true }))
{
    json.WriteStartObject();
    json.WriteString("kind", "P1 real formula engine; exact 0.19.0 differential corpus");
    json.WriteBoolean("nativeAot", !RuntimeFeature.IsDynamicCodeSupported);
    json.WriteNumber("processId", Environment.ProcessId); json.WriteString("utc", DateTimeOffset.UtcNow);
    json.WriteString("corpusSha256", Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(fixturePath))));
    json.WriteNumber("validFixtures", valid); json.WriteNumber("invalidFixtures", invalid);
    json.WriteNumber("additionalCoreEdgeAssertions", edgeAssertions);
    json.WriteStartObject("lifecycle"); json.WriteBoolean("resultIsolation", true); json.WriteBoolean("cancelAndRecovery", true); json.WriteBoolean("resourceLimits", true); json.WriteNumber("concurrentIndependentWorkspaces", 100); json.WriteNumber("rendererCreateDisposeCycles", 100); json.WriteNumber("privateBytesBeforeCycles", privateBefore); json.WriteNumber("privateBytesAfterCycles", privateAfter); json.WriteNumber("handlesBeforeCycles", handlesBefore); json.WriteNumber("handlesAfterCycles", handlesAfter); json.WriteEndObject();
    json.WriteStartObject("allocationBaseline"); json.WriteString("boundary", "Complete synchronous compile including options validation, parse, layout, immutable result; six P1 cases, warmed reused workspace, no formula cache. Thread managed allocation excludes native resources. 100 renderer lifecycle private bytes/handles are whole-process snapshots, not an exact native/GPU ledger."); json.WriteNumber("samples", timings.Length); json.WriteNumber("p50Ms", Percentile(sorted, .50)); json.WriteNumber("p95Ms", Percentile(sorted, .95)); json.WriteNumber("p99Ms", Percentile(sorted, .99)); json.WriteNumber("maximumMs", sorted[^1]); json.WriteNumber("sampleLoopManagedAllocatedBytes", managedAllocated); json.WriteNumber("minimumCompleteCompileManagedBytes", allocations.Min()); json.WriteNumber("maximumCompleteCompileManagedBytes", allocations.Max()); json.WriteEndObject();
    json.WriteStartArray("results");
    foreach (var item in results)
    {
        json.WriteStartObject(); json.WriteString("id", item.Id); json.WriteString("category", item.Category); json.WriteString("scenario", item.Scenario); json.WriteBoolean("success", item.Document is not null);
        if (item.Document is { } doc) WriteDocument(json, doc);
        else { json.WriteString("diagnostic", item.Diagnostic.Code.ToString()); json.WriteNumber("errorStart", item.Diagnostic.Range.Start); json.WriteNumber("errorLength", item.Diagnostic.Range.Length); }
        json.WriteEndObject();
    }
    json.WriteEndArray(); json.WriteEndObject();
}
Console.WriteLine($"PASS P1 {valid} valid / {invalid} invalid, {results.Count - invalid} exports; AOT={!RuntimeFeature.IsDynamicCodeSupported}; P95={Percentile(sorted, .95):F4}ms (P1 subset only)");

static IEnumerable<(double Size, double Scale, bool Dark)> Scenarios(bool matrix)
{
    yield return (24, 1, false);
    if (!matrix) yield break;
    foreach (double size in new[] { 16d, 24d, 32d }) foreach (double scale in new[] { 1d, 1.25, 1.5, 2d }) foreach (bool dark in new[] { false, true })
        if (size != 24 || scale != 1 || dark) yield return (size, scale, dark);
}
static void Assert(bool condition, string description) { if (!condition) throw new InvalidOperationException("FAIL " + description); }
static void AssertThrows<T>(Action action, string description) where T : Exception
{
    try { action(); } catch (T) { return; }
    throw new InvalidOperationException("FAIL expected " + typeof(T).Name + ": " + description);
}
static double Percentile(double[] sorted, double p) => sorted[(int)Math.Ceiling(sorted.Length * p) - 1];
static string Fingerprint(MathDocument doc)
{
    using MemoryStream memory = new(); using (Utf8JsonWriter json = new(memory)) { json.WriteStartObject(); WriteDocument(json, doc); json.WriteEndObject(); }
    return Convert.ToHexStringLower(SHA256.HashData(memory.ToArray()));
}

static void WriteDocument(Utf8JsonWriter json, MathDocument doc)
{
    json.WriteString("source", doc.Source); json.WriteNumber("fontSize", doc.Options.FontSize); json.WriteNumber("width", doc.LogicalSize.Width); json.WriteNumber("height", doc.LogicalSize.Height); json.WriteNumber("baseline", doc.Baseline);
    json.WriteStartObject("inkBounds"); json.WriteNumber("x", doc.InkBounds.X); json.WriteNumber("y", doc.InkBounds.Y); json.WriteNumber("width", doc.InkBounds.Width); json.WriteNumber("height", doc.InkBounds.Height); json.WriteEndObject();
    json.WriteNumber("root", doc.Root);
    json.WriteStartArray("nodes"); foreach (ref readonly SemanticNode node in doc.Nodes.AsSpan())
    {
        json.WriteStartObject(); json.WriteString("kind", node.Kind.ToString()); json.WriteNumber("first", node.FirstChild); json.WriteNumber("next", node.NextSibling); json.WriteNumber("second", node.SecondChild); json.WriteNumber("third", node.ThirdChild); json.WriteNumber("codePoint", node.CodePoint); json.WriteString("atom", node.Atom.ToString()); json.WriteString("font", node.Font.ToString()); json.WriteString("style", node.Style.ToString()); json.WriteBoolean("textMode", node.TextMode); json.WriteBoolean("mathOrd", node.IsMathOrd); json.WriteNumber("value", node.Value); json.WriteNumber("start", node.Range.Start); json.WriteNumber("length", node.Range.Length); json.WriteEndObject();
    } json.WriteEndArray();
    json.WriteStartArray("glyphs"); foreach (MathGlyph glyph in doc.Glyphs.Span)
    {
        json.WriteStartObject(); json.WriteString("text", char.ConvertFromUtf32(glyph.CodePoint)); json.WriteString("font", MathFontCatalog.GetName(glyph.FontId)); json.WriteNumber("glyphId", glyph.GlyphId); json.WriteNumber("x", glyph.X); json.WriteNumber("y", glyph.Y); json.WriteNumber("fontSize", glyph.FontSize); json.WriteEndObject();
    } json.WriteEndArray();
    json.WriteStartArray("rules"); foreach (MathRule rule in doc.Rules.Span)
    {
        json.WriteStartObject(); json.WriteNumber("x", rule.X); json.WriteNumber("y", rule.Y); json.WriteNumber("width", rule.Width); json.WriteNumber("height", rule.Height); json.WriteEndObject();
    } json.WriteEndArray();
    json.WriteStartArray("paths"); foreach (MathPath path in doc.Paths.Span)
    {
        json.WriteStartObject(); json.WriteNumber("x", path.X); json.WriteNumber("y", path.Y); json.WriteNumber("scaleX", path.ScaleX); json.WriteNumber("scaleY", path.ScaleY); json.WriteNumber("segments", path.Segments.Length); json.WriteEndObject();
    } json.WriteEndArray();
}

sealed class ForwardOnlyStream(Stream sink) : Stream
{
    public override bool CanRead => false;
    public override bool CanSeek => false;
    public override bool CanWrite => true;
    public override long Length => throw new NotSupportedException();
    public override long Position { get => throw new NotSupportedException(); set => throw new NotSupportedException(); }
    public override void Flush() => sink.Flush();
    public override void Write(byte[] buffer, int offset, int count) => sink.Write(buffer, offset, count);
    public override void Write(ReadOnlySpan<byte> buffer) => sink.Write(buffer);
    public override int Read(byte[] buffer, int offset, int count) => throw new NotSupportedException();
    public override long Seek(long offset, SeekOrigin origin) => throw new NotSupportedException();
    public override void SetLength(long value) => throw new NotSupportedException();
}
