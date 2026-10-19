using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using OhMyStudy.Documents;

string root = Path.GetFullPath(args.ElementAtOrDefault(0) ?? Directory.GetCurrentDirectory());
string downstream = Path.GetFullPath(args.ElementAtOrDefault(1) ?? Path.Combine(root, "..", "oh-my-study"));
string output = Path.GetFullPath(args.ElementAtOrDefault(2) ?? Path.Combine(root, "artifacts", "baseline"));
int repetitions = int.Parse(args.ElementAtOrDefault(3) ?? "7", System.Globalization.CultureInfo.InvariantCulture);
if (repetitions is < 2 or > 100) throw new ArgumentOutOfRangeException(nameof(repetitions));
Directory.CreateDirectory(output);
string typstPath = Path.Combine(downstream, "output", "migration", "toolchain", "typst-x86_64-pc-windows-msvc", "typst.exe");
string documentsPath = typeof(TypstDocumentService).Assembly.Location;
var service = new TypstDocumentService(new TypstToolchain(typstPath));
const string shortFormula = @"x_1^2+\frac{1}{2}";
const string complexFormula = @"\frac{x^2-1}{\sqrt{x+2}}+\sum_{k=1}^{n}k^2+\begin{pmatrix}1&2\\3&4\end{pmatrix}";
foreach (string formula in new[] { shortFormula, complexFormula })
    if (!LatexFormulaConverter.Convert(formula).IsSupported) throw new InvalidDataException("Baseline formula unsupported by the actual downstream converter.");
var inputs = new List<BaselineInput>();
foreach (var (name, formula) in new[] { ("short", shortFormula), ("complex", complexFormula) })
    foreach (int count in new[] { 1, 10, 100 })
    {
        string[] paragraphs = Enumerable.Range(1, count).Select(index => $"测量题目 {index}：已知 ${formula}$，说明条件并写出结论。").ToArray();
        inputs.Add(new(name, count, 520, paragraphs, Hash(Encoding.UTF8.GetBytes(string.Join("\n", paragraphs)))));
    }
await File.WriteAllTextAsync(Path.Combine(output, "inputs.json"), JsonSerializer.Serialize(inputs, BaselineJsonContext.Default.ListBaselineInput));

var samples = new List<BaselineSample>();
foreach (var input in inputs)
{
    string scenario = $"{input.Name}-{input.ParagraphCount}";
    string directory = Path.Combine(output, scenario);
    // Every service call starts a fresh real Typst process. Repetition warms the
    // harness/filesystem, not a persistent Typst engine or formula-result cache.
    for (int repetition = 0; repetition <= repetitions; repetition++)
    {
        using var current = Process.GetCurrentProcess();
        long beforeAllocation = GC.GetTotalAllocatedBytes(precise: true);
        current.Refresh();
        long workingSetBefore = current.WorkingSet64;
        long timestamp = Stopwatch.GetTimestamp();
        IReadOnlyList<ReadingPreview> previews = await service.RenderReadingAsync(input.Paragraphs, input.WidthPoints, directory);
        double elapsed = Stopwatch.GetElapsedTime(timestamp).TotalMilliseconds;
        long allocated = GC.GetTotalAllocatedBytes(precise: true) - beforeAllocation;
        current.Refresh();
        if (previews.Count != input.ParagraphCount || previews.Any(preview => !preview.Svg.Contains("<path", StringComparison.Ordinal) || preview.Svg.Contains("<text", StringComparison.Ordinal)))
            throw new InvalidDataException("Actual pipeline did not return the expected self-contained SVG previews.");
        long svgFileBytes = Directory.EnumerateFiles(directory, "reading-*.svg").Sum(file => new FileInfo(file).Length);
        samples.Add(new(scenario, repetition == 0 ? "first-for-scenario" : "measured", repetition, elapsed, allocated,
            workingSetBefore, current.WorkingSet64, current.PrivateMemorySize64, previews.Sum(preview => (long)preview.Svg.Length), svgFileBytes, previews.Count));
        Console.WriteLine($"{scenario} {repetition}/{repetitions}: {elapsed:F3} ms, {allocated} managed bytes, {previews.Count} SVGs");
    }
}
var summaries = samples.Where(sample => sample.Kind == "measured").GroupBy(sample => sample.Scenario)
    .Select(group =>
    {
        double[] times = group.Select(sample => sample.ElapsedMilliseconds).Order().ToArray();
        long[] allocations = group.Select(sample => sample.ManagedAllocatedBytes).Order().ToArray();
        return new BaselineSummary(group.Key, times.Length, times[times.Length / 2], times[(int)Math.Ceiling(times.Length * .95) - 1], times[0], times[^1], allocations[allocations.Length / 2]);
    }).ToList();
var report = new BaselineReport(1, DateTimeOffset.UtcNow.ToString("O"), RuntimeInformation.OSDescription, RuntimeInformation.FrameworkDescription,
    Environment.ProcessorCount, TypstToolchain.EngineVersion, typstPath, Hash(await File.ReadAllBytesAsync(typstPath)),
    documentsPath, Hash(await File.ReadAllBytesAsync(documentsPath)), repetitions,
    "Actual downstream RenderReadingAsync including splitting/conversion, source write, new Typst child, SVG reads and XML normalization; excludes host SVG parsing/drawing, UI, screen presentation and queues. Managed allocation is the harness process total across await; Typst child allocations and peak native memory are unmeasured. First scenario calls are not cold-OS or fresh-harness samples. P95 is nearest-rank from seven samples by default, descriptive only.",
    inputs, samples, summaries);
await File.WriteAllTextAsync(Path.Combine(output, "typst-reading-baseline.json"), JsonSerializer.Serialize(report, BaselineJsonContext.Default.BaselineReport));
Console.WriteLine("PASS actual downstream reading baseline; " + Path.Combine(output, "typst-reading-baseline.json"));

static string Hash(byte[] data) => Convert.ToHexStringLower(SHA256.HashData(data));

internal sealed record BaselineInput(string Name, int ParagraphCount, double WidthPoints, string[] Paragraphs, string Sha256);
internal sealed record BaselineSample(string Scenario, string Kind, int Repetition, double ElapsedMilliseconds, long ManagedAllocatedBytes,
    long WorkingSetBeforeBytes, long WorkingSetAfterBytes, long PrivateMemoryAfterBytes, long ReturnedSvgCharacters, long SvgFileBytes, int PreviewCount);
internal sealed record BaselineSummary(string Scenario, int Samples, double MedianMilliseconds, double P95Milliseconds, double MinMilliseconds, double MaxMilliseconds, long MedianManagedAllocatedBytes);
internal sealed record BaselineReport(int SchemaVersion, string TimestampUtc, string OperatingSystem, string Framework, int LogicalProcessors,
    string TypstVersion, string TypstPath, string TypstSha256, string DocumentsAssemblyPath, string DocumentsAssemblySha256, int Repetitions,
    string MeasurementBoundary, List<BaselineInput> Inputs, List<BaselineSample> Samples, List<BaselineSummary> Summaries);
[JsonSourceGenerationOptions(WriteIndented = true)]
[JsonSerializable(typeof(BaselineReport))]
[JsonSerializable(typeof(List<BaselineInput>))]
internal partial class BaselineJsonContext : JsonSerializerContext;
