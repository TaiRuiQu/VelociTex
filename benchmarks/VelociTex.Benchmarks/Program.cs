using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Security.Cryptography;
using System.Text.Json;
using VelociTex;
using VelociTex.Rendering.Direct2D;
using VelociTex.Rendering.Svg;

namespace VelociTex.Benchmarks;

internal static class Program
{
    private readonly record struct Fixture(string Id, string Category, string Latex, bool DisplayMode);
    private readonly record struct Measurement(string Category, double Ms, long Bytes);
    private readonly record struct Summary(string Name, int Count, double P50, double P95, double P99, double Max, double Mean, long MinBytes, long MaxBytes);

    private static int Main(string[] args)
    {
        string output = Path.GetFullPath(Option(args, "--out") ?? "artifacts/performance/core");
        int sampleCount = int.Parse(Option(args, "--samples") ?? "6000", CultureInfo.InvariantCulture);
        bool validate = args.Contains("--validate", StringComparer.Ordinal);
        if (int.TryParse(Option(args, "--core") ?? Environment.GetEnvironmentVariable("VELOCITEX_BENCH_CORE"), out int benchmarkCore) &&
            benchmarkCore >= 0 && benchmarkCore < Environment.ProcessorCount)
        {
            using Process self = Process.GetCurrentProcess();
            self.ProcessorAffinity = (nint)(1L << benchmarkCore);
            self.PriorityClass = ProcessPriorityClass.High;
        }
        Directory.CreateDirectory(output);

        string corpusPath = Path.Combine(AppContext.BaseDirectory, "corpus.normal.json");
        if (!File.Exists(corpusPath)) corpusPath = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../benchmarks/corpus.normal.json"));
        using JsonDocument corpus = JsonDocument.Parse(File.ReadAllBytes(corpusPath));
        Fixture[] fixtures = corpus.RootElement.GetProperty("fixtures").EnumerateArray().Select(f => new Fixture(
            f.GetProperty("id").GetString()!, f.GetProperty("category").GetString()!, f.GetProperty("latex").GetString()!, f.GetProperty("displayMode").GetBoolean())).ToArray();
        if (fixtures.Length != 60 || fixtures.GroupBy(f => f.Category).Any(g => g.Count() != 10)) throw new InvalidOperationException("The frozen normal corpus changed shape.");

        MathEngine engine = new();
        using MathWorkspace workspace = new();
        WindowsMathTextShaper shaper = new();
        string fontDirectory = Path.Combine(AppContext.BaseDirectory, "Fonts");
        if (!File.Exists(Path.Combine(fontDirectory, "KaTeX_Main-Regular.ttf"))) throw new FileNotFoundException("The benchmark requires its copied private KaTeX fonts.", fontDirectory);
        using WindowsMathRenderer renderer = new(fontDirectory);
        MathOptions Options(Fixture input) => new() { DisplayMode = input.DisplayMode, FontSize = 24, TextShaper = input.Latex.Any(c => c > 127) ? shaper : null };

        MathDocument Compile(Fixture input, in MathOptions options)
        {
            if (!engine.TryCompile(input.Latex, in options, workspace, default, out MathDocument? document, out MathDiagnostic error))
                throw new InvalidOperationException(input.Id + ": " + error);
            return document!;
        }

        // Correctness smoke before timing: every frozen formula must produce all outputs.
        if (validate)
        {
            int bytes = 0;
            foreach (Fixture input in fixtures)
            {
                MathOptions options = Options(input);
                MathDocument document = Compile(input, in options);
                string mathMlOutput = MathMlWriter.Write(document);
                if (!engine.TryCompileToMathMl(input.Latex, in options, workspace, default, out string? directMathMl, out _, out MathDiagnostic directError) || directMathMl != mathMlOutput)
                    throw new InvalidOperationException(input.Id + ": direct MathML path disagrees: " + directError);
                using MemoryStream svgOutput = new();
                renderer.WriteSvg(document, svgOutput);
                if (!mathMlOutput.Contains("<math", StringComparison.Ordinal) || svgOutput.Length == 0) throw new InvalidOperationException(input.Id + ": invalid output.");
                bytes += mathMlOutput.Length + (int)svgOutput.Length;
            }
            Console.WriteLine($"PASS validate: {fixtures.Length} formulas, combined output bytes={bytes}");
        }

        // Warm static tables, native faces, shaping and native outline caches outside the timed region.
        foreach (Fixture input in fixtures)
        {
            MathOptions options = Options(input);
            for (int i = 0; i < 4; i++)
            {
                MathDocument document = Compile(input, in options);
                _ = MathMlWriter.Write(document);
                using MemoryStream warmSvg = new();
                renderer.WriteSvg(document, warmSvg);
            }
        }

        Random random = new(0x19002026);
        int[] Shuffled() { int[] order = Enumerable.Range(0, sampleCount).Select(i => i % fixtures.Length).ToArray(); random.Shuffle(order); return order; }

        if (args.Contains("--stages", StringComparer.Ordinal))
        {
            bool layoutProfile = args.Contains("--layout-profile", StringComparer.Ordinal);
            MathLayout.ProfileEnabled = layoutProfile;
            Measurement[][] layoutPhases = new Measurement[4][];
            for (int i = 0; i < layoutPhases.Length; i++) layoutPhases[i] = new Measurement[sampleCount];
            Measurement[][] stages = new Measurement[6][];
            string[] stageNames = ["macro", "parse", "nodes", "layout", "result", "mathml"];
            for (int stage = 0; stage < stages.Length; stage++) stages[stage] = new Measurement[sampleCount];
            int[] order = Shuffled();
            using StreamWriter csv = new(Path.Combine(output, "stages-samples.csv"));
            csv.WriteLine("sample,fixture,category,macroMs,parseMs,nodesMs,layoutMs,resultMs,macroBytes,parseBytes,nodesBytes,layoutBytes,resultBytes");
            for (int i = 0; i < order.Length; i++)
            {
                Fixture input = fixtures[order[i]];
                MathOptions options = Options(input);
                if (!workspace.Enter()) throw new InvalidOperationException("workspace busy");
                try
                {
                    long b0 = GC.GetAllocatedBytesForCurrentThread();
                    long t0 = Stopwatch.GetTimestamp();
                    MacroExpander.Expand(input.Latex, in options, workspace, default);
                    long b1 = GC.GetAllocatedBytesForCurrentThread(); long t1 = Stopwatch.GetTimestamp();
                    int root = MathParser.Parse(input.Latex, in options, workspace, default);
                    long b2 = GC.GetAllocatedBytesForCurrentThread(); long t2 = Stopwatch.GetTimestamp();
                    SemanticNode[] nodes = workspace.Nodes.ToArray();
                    long b3 = GC.GetAllocatedBytesForCurrentThread(); long t3 = Stopwatch.GetTimestamp();
                    LayoutOutput layout = MathLayout.Build(nodes, root, in options, default);
                    if (layoutProfile)
                    {
                        long frequency = Stopwatch.Frequency;
                        for (int phase = 0; phase < layoutPhases.Length; phase++)
                            layoutPhases[phase][i] = new(input.Category, MathLayout.ProfileTicks[phase] * 1000.0 / frequency, 0);
                    }
                    long b4 = GC.GetAllocatedBytesForCurrentThread(); long t4 = Stopwatch.GetTimestamp();
                    MathDocument document = new(input.Latex, in options, nodes, root, layout, workspace.Warnings.ToArray());
                    long b5 = GC.GetAllocatedBytesForCurrentThread(); long t5 = Stopwatch.GetTimestamp();
                    if (MathMlWriter.Write(document).Length == 0) throw new InvalidOperationException("empty MathML");
                    long b6 = GC.GetAllocatedBytesForCurrentThread(); long t6 = Stopwatch.GetTimestamp();
                    stages[0][i] = new(input.Category, Stopwatch.GetElapsedTime(t0, t1).TotalMilliseconds, b1 - b0);
                    stages[1][i] = new(input.Category, Stopwatch.GetElapsedTime(t1, t2).TotalMilliseconds, b2 - b1);
                    stages[2][i] = new(input.Category, Stopwatch.GetElapsedTime(t2, t3).TotalMilliseconds, b3 - b2);
                    stages[3][i] = new(input.Category, Stopwatch.GetElapsedTime(t3, t4).TotalMilliseconds, b4 - b3);
                    stages[4][i] = new(input.Category, Stopwatch.GetElapsedTime(t4, t5).TotalMilliseconds, b5 - b4);
                    stages[5][i] = new(input.Category, Stopwatch.GetElapsedTime(t5, t6).TotalMilliseconds, b6 - b5);
                    csv.WriteLine(FormattableString.Invariant($"{i},{input.Id},{input.Category},{stages[0][i].Ms:R},{stages[1][i].Ms:R},{stages[2][i].Ms:R},{stages[3][i].Ms:R},{stages[4][i].Ms:R},{stages[0][i].Bytes},{stages[1][i].Bytes},{stages[2][i].Bytes},{stages[3][i].Bytes},{stages[4][i].Bytes}"));
                }
                finally { workspace.Leave(); }
            }
            using (FileStream file = File.Create(Path.Combine(output, "stages-report.json")))
            using (Utf8JsonWriter json = new(file, new JsonWriterOptions { Indented = true }))
            {
                json.WriteStartObject();
                json.WriteBoolean("nativeAot", IsNativeAot());
                json.WriteNumber("sampleCount", sampleCount);
                json.WriteStartArray("stages");
                for (int stage = 0; stage < stages.Length; stage++)
                    WriteSummary(json, Summarize(stageNames[stage], stages[stage].Select(s => s.Ms), stages[stage].Select(s => s.Bytes)), named: false);
                json.WriteEndArray();
                json.WriteEndObject();
            }
            for (int stage = 0; stage < stages.Length; stage++)
            {
                Summary summary = Summarize(stageNames[stage], stages[stage].Select(s => s.Ms), stages[stage].Select(s => s.Bytes));
                Console.WriteLine($"{stageNames[stage],-8} p50={summary.P50:F5}ms p95={summary.P95:F5}ms mean={summary.Mean:F5}ms");
            }
            if (layoutProfile)
            {
                string[] phaseNames = ["arrays", "colors", "boxes", "emit"];
                for (int phase = 0; phase < phaseNames.Length; phase++)
                {
                    Summary summary = Summarize(phaseNames[phase], layoutPhases[phase].Select(s => s.Ms), layoutPhases[phase].Select(s => s.Bytes));
                    Console.WriteLine($"layout/{phaseNames[phase],-6} p50={summary.P50:F5}ms p95={summary.P95:F5}ms mean={summary.Mean:F5}ms");
                }
            }
            MathLayout.ProfileEnabled = false;
            return 0;
        }

        Measurement[] Measure(string metric)
        {
            int[] order = Shuffled();
            Measurement[] samples = new Measurement[order.Length];
            using StreamWriter csv = new(Path.Combine(output, metric + "-samples.csv"));
            csv.WriteLine("sample,fixture,category,ms,threadManagedAllocatedBytes");
            for (int i = 0; i < order.Length; i++)
            {
                Fixture input = fixtures[order[i]];
                MathOptions options = Options(input);
                long bytes = GC.GetAllocatedBytesForCurrentThread();
                long start = Stopwatch.GetTimestamp();
                if (metric == "mathml-direct")
                {
                    if (!engine.TryCompileToMathMl(input.Latex, in options, workspace, default, out string? direct, out _, out MathDiagnostic directError) || direct is null)
                        throw new InvalidOperationException(input.Id + ": " + directError);
                    if (direct.Length < 10) throw new InvalidOperationException(input.Id + ": empty direct MathML.");
                    double directMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
                    long directAllocated = GC.GetAllocatedBytesForCurrentThread() - bytes;
                    samples[i] = new(input.Category, directMs, directAllocated);
                    csv.WriteLine(FormattableString.Invariant($"{i},{input.Id},{input.Category},{directMs:R},{directAllocated}"));
                    continue;
                }
                MathDocument document = Compile(input, in options);
                switch (metric)
                {
                    case "compile":
                        break;
                    case "mathml":
                        if (MathMlWriter.Write(document).Length < 10) throw new InvalidOperationException(input.Id + ": empty MathML.");
                        break;
                    case "svg":
                        using (MemoryStream svgStream = new()) { renderer.WriteSvg(document, svgStream); if (svgStream.Length == 0) throw new InvalidOperationException(input.Id + ": empty SVG."); }
                        break;
                    case "portable-svg":
                        if (MathSvgWriter.Write(document, "bench").Length == 0) throw new InvalidOperationException(input.Id + ": empty portable SVG.");
                        break;
                    default:
                        throw new ArgumentOutOfRangeException(nameof(metric));
                }
                double ms = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
                long allocated = GC.GetAllocatedBytesForCurrentThread() - bytes;
                samples[i] = new(input.Category, ms, allocated);
                csv.WriteLine(FormattableString.Invariant($"{i},{input.Id},{input.Category},{ms:R},{allocated}"));
            }
            return samples;
        }

        Measurement[] compile = Measure("compile");
        Measurement[] mathml = Measure("mathml");
        Measurement[] svg = Measure("svg");
        Measurement[] portableSvg = Measure("portable-svg");
        Measurement[] directMathml = Measure("mathml-direct");
        Measurement[][] all = [compile, mathml, svg, portableSvg, directMathml];
        string[] names = ["compile", "compile+mathml", "compile+svg", "compile+portable-svg", "mathml-only"];
        Summary[] summaries = all.Select((samples, i) => Summarize(names[i], samples.Select(s => s.Ms), samples.Select(s => s.Bytes))).ToArray();
        string[] categories = fixtures.Select(f => f.Category).Distinct().ToArray();

        using (FileStream file = File.Create(Path.Combine(output, "core-report.json")))
        using (Utf8JsonWriter json = new(file, new JsonWriterOptions { Indented = true }))
        {
            json.WriteStartObject();
            json.WriteString("kind", "VelociTex core performance benchmark; not a KaTeX comparison by itself");
            json.WriteString("utc", DateTimeOffset.UtcNow);
            json.WriteBoolean("nativeAot", IsNativeAot());
            json.WriteBoolean("dynamicCodeSupported", RuntimeFeature.IsDynamicCodeSupported);
            json.WriteString("runtime", RuntimeInformation.FrameworkDescription);
            json.WriteString("os", RuntimeInformation.OSDescription);
            json.WriteString("architecture", RuntimeInformation.ProcessArchitecture.ToString());
            json.WriteString("executable", Environment.ProcessPath);
            json.WriteNumber("processId", Environment.ProcessId);
            json.WriteString("corpusSha256", Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(corpusPath))));
            json.WriteNumber("formulaCount", fixtures.Length);
            json.WriteNumber("sampleCount", sampleCount);
            json.WriteString("timedRegion", "Initialized fonts/shaper/renderer; no formula, semantic or layout result cache. compile covers validation, lexing, macros, semantics, layout, required shaping and owned result allocation. mathml/svg add serialization to the named output. Window rendering is measured separately.");
            json.WriteStartArray("summaries");
            foreach (Summary summary in summaries) WriteSummary(json, summary, named: false);
            json.WriteEndArray();
            json.WriteStartObject("categories");
            foreach (string category in categories)
            {
                json.WriteStartObject(category);
                for (int metric = 0; metric < all.Length; metric++)
                {
                    Measurement[] subset = all[metric].Where(s => s.Category == category).ToArray();
                    WriteSummary(json, Summarize(names[metric], subset.Select(s => s.Ms), subset.Select(s => s.Bytes)));
                }
                json.WriteEndObject();
            }
            json.WriteEndObject();
            json.WriteNumber("shapingCacheItems", shaper.Count);
            json.WriteNumber("shapingCacheEstimatedBytes", shaper.EstimatedRetainedBytes);
            json.WriteBoolean("outliersRemoved", false);
            json.WriteEndObject();
        }

        foreach (Summary summary in summaries)
            Console.WriteLine($"{summary.Name,-15} n={summary.Count,5} p50={summary.P50:F5}ms p95={summary.P95:F5}ms p99={summary.P99:F5}ms max={summary.Max:F5}ms alloc={summary.MinBytes}..{summary.MaxBytes}B");
        Console.WriteLine($"AOT={IsNativeAot()} dynamicCode={RuntimeFeature.IsDynamicCodeSupported} output={output}");
        return 0;
    }

    private static bool IsNativeAot() => !RuntimeFeature.IsDynamicCodeSupported;

    private static string? Option(string[] args, string name)
    {
        for (int i = 0; i + 1 < args.Length; i++) if (args[i] == name) return args[i + 1];
        return null;
    }

    private static Summary Summarize(string name, IEnumerable<double> values, IEnumerable<long> bytes)
    {
        double[] sorted = values.Order().ToArray();
        long[] allocations = bytes.ToArray();
        double Percentile(double p) => sorted[Math.Max(0, (int)Math.Ceiling(sorted.Length * p) - 1)];
        return new(name, sorted.Length, Percentile(.50), Percentile(.95), Percentile(.99), sorted[^1], sorted.Average(), allocations.Min(), allocations.Max());
    }

    private static void WriteSummary(Utf8JsonWriter json, Summary summary, bool named = true)
    {
        if (named) json.WriteStartObject(summary.Name); else { json.WriteStartObject(); json.WriteString("name", summary.Name); }
        json.WriteNumber("samples", summary.Count); json.WriteNumber("p50Ms", summary.P50); json.WriteNumber("p95Ms", summary.P95);
        json.WriteNumber("p99Ms", summary.P99); json.WriteNumber("maximumMs", summary.Max); json.WriteNumber("meanMs", summary.Mean);
        json.WriteNumber("minimumManagedBytes", summary.MinBytes); json.WriteNumber("maximumManagedBytes", summary.MaxBytes);
        json.WriteEndObject();
    }
}
