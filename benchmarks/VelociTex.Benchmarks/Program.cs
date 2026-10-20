using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Text.Json;
using VelociTex;
using VelociTex.Rendering.Direct2D;

// Windows-native parse/export benchmark. All formulas come from the frozen
// benchmarks/workloads.json file, so the browser and WinUI harnesses measure the
// same inputs. Timed regions:
//   compileOnly  : LaTeX -> immutable MathDocument (parse, macro expansion, layout)
//   mathml       : LaTeX -> presentation MathML (no layout)
//   svg          : LaTeX -> standalone SVG through the Direct2D/SVG exporter
internal static class Program
{
    private sealed record Formula(string Id, string Latex, bool DisplayMode);
    private sealed record Workload(string Name, IReadOnlyList<IReadOnlyList<Formula>> Samples);

    private static int Main(string[] args)
    {
        if (Option(args, "--render-comparison") is { } comparisonOutput)
            return RenderComparison(Path.GetFullPath(comparisonOutput));

        string output = Path.GetFullPath(Option(args, "--out") ?? "artifacts/benchmarks/native");
        int iterations = int.Parse(Option(args, "--iterations") ?? "300", CultureInfo.InvariantCulture);
        Directory.CreateDirectory(output);

        string workloadPath = Option(args, "--workloads") ?? Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../workloads.json"));
        if (!File.Exists(workloadPath))
            workloadPath = Path.GetFullPath("benchmarks/workloads.json");
        using JsonDocument corpus = JsonDocument.Parse(File.ReadAllBytes(workloadPath));
        Dictionary<string, Workload> workloads = Load(corpus.RootElement);

        using var renderer = new WindowsMathRenderer();
        var engine = new MathEngine();
        using var workspace = new MathWorkspace();

        // Validate every case before timing. A skipped formula would silently bias
        // the comparison, so fail instead of filtering.
        var exports = new Dictionary<string, (int MathMl, int Svg)>();
        foreach (Workload workload in workloads.Values)
        {
            foreach (IReadOnlyList<Formula> sample in workload.Samples)
            {
                foreach (Formula item in sample)
                {
                    MathOptions options = Options(item);
                    if (!engine.TryCompile(item.Latex, in options, workspace, default, out MathDocument? document, out MathDiagnostic diagnostic))
                        throw new InvalidOperationException($"{item.Id}: {diagnostic.Code}: {diagnostic.Message}");
                    if (!engine.TryCompileToMathMl(item.Latex, in options, workspace, default, out string? mathml, out _, out diagnostic))
                        throw new InvalidOperationException($"{item.Id} MathML: {diagnostic.Code}: {diagnostic.Message}");
                    using var svg = new MemoryStream();
                    renderer.WriteSvg(document!, svg);
                    if (mathml!.Length < 20 || svg.Length < 20)
                        throw new InvalidOperationException($"{item.Id}: empty export.");
                    exports[item.Id] = (mathml.Length, (int)svg.Length);
                    GC.KeepAlive(document);
                }
            }
        }
        Console.WriteLine($"Validated {exports.Count} formulas.");

        // Warm every path (native font extraction, outline caches, JIT tiers).
        foreach (Workload workload in workloads.Values)
        {
            for (int round = 0; round < 12; round++)
            {
                foreach (IReadOnlyList<Formula> sample in workload.Samples)
                {
                    foreach (Formula item in sample) Warm(engine, workspace, renderer, item);
                }
            }
        }
        GC.Collect();
        GC.WaitForPendingFinalizers();

        Dictionary<string, WorkloadReport> report = [];
        foreach ((string name, Workload workload) in workloads)
        {
            int samples = name == "page" ? Math.Max(12, iterations / 8) : iterations;
            var compile = new List<double>(samples);
            var mathml = new List<double>(samples);
            var svg = new List<double>(samples);
            var random = new Random(0x19002026);
            for (int iteration = 0; iteration < samples; iteration++)
            {
                IReadOnlyList<Formula> sample = workload.Samples[iteration % workload.Samples.Count];
                var order = Enumerable.Range(0, sample.Count).OrderBy(_ => random.Next()).ToArray();
                long start = Stopwatch.GetTimestamp();
                foreach (int index in order) Compile(engine, workspace, sample[index]);
                compile.Add(Ms(start));
                start = Stopwatch.GetTimestamp();
                foreach (int index in order) MathMl(engine, workspace, sample[index]);
                mathml.Add(Ms(start));
                start = Stopwatch.GetTimestamp();
                foreach (int index in order) Svg(engine, workspace, renderer, sample[index]);
                svg.Add(Ms(start));
                if (iteration % 64 == 63)
                {
                    GC.Collect();
                    GC.WaitForPendingFinalizers();
                }
            }
            report[name] = new WorkloadReport(
                workload.Samples.Count,
                workload.Samples.Sum(s => s.Count),
                samples,
                Summarize(compile),
                Summarize(mathml),
                Summarize(svg));
        }

        var result = new
        {
            schemaVersion = 1,
            kind = "VelociTex Windows native parse/export benchmark",
            utc = DateTimeOffset.UtcNow,
            sourceRevision = Revision(),
            configuration = "Release",
            nativeAot = !RuntimeFeature.IsDynamicCodeSupported,
            processArchitecture = System.Runtime.InteropServices.RuntimeInformation.ProcessArchitecture.ToString(),
            os = Environment.OSVersion.ToString(),
            processorCount = Environment.ProcessorCount,
            runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            workloadSha256 = Sha256(File.ReadAllBytes(workloadPath)),
            timedRegion = "Stopwatch.GetTimestamp around one complete call. compileOnly includes parse, macros and layout; mathml skips layout; svg uses WindowsMathRenderer.WriteSvg. One page sample times every formula in that page sequentially.",
            iterations,
            workloads = report,
            exports = exports.ToDictionary(pair => pair.Key, pair => new { mathmlBytes = pair.Value.MathMl, svgBytes = pair.Value.Svg }),
        };
        File.WriteAllText(Path.Combine(output, "native-report.json"), JsonSerializer.Serialize(result, JsonOptions()));
        Console.WriteLine($"Wrote {Path.Combine(output, "native-report.json")}");
        foreach ((string name, WorkloadReport entry) in report)
            Console.WriteLine($"{name}: mathml p50={entry.MathMl.P50Ms:F4}ms svg p50={entry.Svg.P50Ms:F4}ms compile p50={entry.CompileOnly.P50Ms:F4}ms");
        return 0;
    }

    private static int RenderComparison(string output)
    {
        Directory.CreateDirectory(output);
        string path = Path.GetFullPath(Path.Combine(AppContext.BaseDirectory, "../../../../comparison.json"));
        if (!File.Exists(path)) path = Path.GetFullPath("benchmarks/comparison.json");
        using JsonDocument document = JsonDocument.Parse(File.ReadAllBytes(path));
        using var renderer = new WindowsMathRenderer();
        var engine = new MathEngine();
        using var workspace = new MathWorkspace();
        var records = new List<object>();
        foreach (JsonElement item in document.RootElement.EnumerateArray())
        {
            string id = item.GetProperty("id").GetString()!;
            string latex = item.GetProperty("latex").GetString()!;
            bool display = item.GetProperty("displayMode").GetBoolean();
            MathOptions options = new() { DisplayMode = display, FontSize = 32 };
            if (!engine.TryCompile(latex, in options, workspace, default, out MathDocument? result, out MathDiagnostic diagnostic))
                throw new InvalidOperationException($"{id}: {diagnostic.Code}: {diagnostic.Message}");
            string file = Path.Combine(output, $"{id}-native.png");
            // Transparent PNG at 2x for a crisp, white-background composition.
            renderer.WritePng(result!, file, scale: 2, padding: 12);
            (int width, int height) = PngSize(file);
            records.Add(new { id, title = item.GetProperty("title").GetString(), file = Path.GetFileName(file), pixelWidth = width, pixelHeight = height, logicalWidth = result!.LogicalSize.Width, logicalHeight = result.LogicalSize.Height });
            Console.WriteLine($"{id}: {width}x{height}px");
        }
        File.WriteAllText(Path.Combine(output, "native-figures.json"), JsonSerializer.Serialize(records, JsonOptions()));
        return 0;
    }

    private static void Warm(MathEngine engine, MathWorkspace workspace, WindowsMathRenderer renderer, Formula item)
    {
        MathOptions options = Options(item);
        _ = engine.TryCompileToMathMl(item.Latex, in options, workspace, default, out _, out _, out _);
        if (!engine.TryCompile(item.Latex, in options, workspace, default, out MathDocument? document, out _))
            throw new InvalidOperationException(item.Id);
        using var stream = new MemoryStream(4096);
        renderer.WriteSvg(document!, stream);
        Compile(engine, workspace, item);
    }

    private static void Compile(MathEngine engine, MathWorkspace workspace, Formula item)
    {
        MathOptions options = Options(item);
        if (!engine.TryCompile(item.Latex, in options, workspace, default, out MathDocument? document, out MathDiagnostic diagnostic))
            throw new InvalidOperationException($"{item.Id}: {diagnostic.Code}");
        GC.KeepAlive(document);
    }

    private static void MathMl(MathEngine engine, MathWorkspace workspace, Formula item)
    {
        MathOptions options = Options(item);
        if (!engine.TryCompileToMathMl(item.Latex, in options, workspace, default, out string? mathml, out _, out MathDiagnostic diagnostic))
            throw new InvalidOperationException($"{item.Id}: {diagnostic.Code}");
        if (mathml!.Length < 20) throw new InvalidOperationException($"{item.Id}: empty MathML");
    }

    private static void Svg(MathEngine engine, MathWorkspace workspace, WindowsMathRenderer renderer, Formula item)
    {
        MathOptions options = Options(item);
        if (!engine.TryCompile(item.Latex, in options, workspace, default, out MathDocument? document, out MathDiagnostic diagnostic))
            throw new InvalidOperationException($"{item.Id}: {diagnostic.Code}");
        using var stream = new MemoryStream(4096);
        renderer.WriteSvg(document!, stream);
        if (stream.Length < 20) throw new InvalidOperationException($"{item.Id}: empty SVG");
    }

    private static MathOptions Options(Formula item) => new() { DisplayMode = item.DisplayMode, FontSize = 24 };

    private static double Ms(long start) => (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;

    private sealed record WorkloadReport(int SamplesPerWorkload, int FormulasPerSample, int Samples, Summary CompileOnly, Summary MathMl, Summary Svg);
    private sealed record Summary(int Samples, double P50Ms, double P95Ms, double P99Ms, double MaximumMs, double MeanMs, double TotalMs);

    private static Summary Summarize(List<double> values)
    {
        double[] sorted = [.. values.Order()];
        double P(double p) => sorted[Math.Max(0, (int)Math.Ceiling(sorted.Length * p) - 1)];
        return new(sorted.Length, P(.5), P(.95), P(.99), sorted[^1], sorted.Average(), sorted.Sum());
    }

    private static Dictionary<string, Workload> Load(JsonElement root)
    {
        var result = new Dictionary<string, Workload>();
        foreach (JsonProperty workload in root.GetProperty("workloads").EnumerateObject())
        {
            var samples = new List<IReadOnlyList<Formula>>();
            int pageIndex = 0;
            foreach (JsonElement item in workload.Value.GetProperty("cases").EnumerateArray())
            {
                if (workload.Name == "page")
                {
                    // A page case is one mixed page: one measurement captures every
                    // formula in that page, matching the browser page benchmark.
                    var formulas = new List<Formula>();
                    int index = 0;
                    foreach (JsonElement paragraph in item.GetProperty("paragraphs").EnumerateArray())
                    {
                        foreach (JsonElement segment in paragraph.EnumerateArray())
                        {
                            if (!segment.TryGetProperty("latex", out JsonElement latex)) continue;
                            formulas.Add(new Formula($"{item.GetProperty("id").GetString()}-{index++}", latex.GetString()!, segment.GetProperty("displayMode").GetBoolean()));
                        }
                    }
                    samples.Add(formulas);
                    pageIndex++;
                }
                else
                {
                    samples.Add([new Formula(item.GetProperty("id").GetString()!, item.GetProperty("latex").GetString()!, item.GetProperty("displayMode").GetBoolean())]);
                }
            }
            if (workload.Name == "page" && pageIndex == 0) throw new InvalidOperationException("The page workload is empty.");
            result[workload.Name] = new Workload(workload.Name, samples);
        }
        return result;
    }

    private static string Option(string[] args, string name)
    {
        int index = Array.IndexOf(args, name);
        return index >= 0 && index + 1 < args.Length ? args[index + 1] : null!;
    }

    private static string Revision()
    {
        try
        {
            var start = new ProcessStartInfo("git", "rev-parse HEAD")
            { RedirectStandardOutput = true, UseShellExecute = false, CreateNoWindow = true };
            using Process process = Process.Start(start)!;
            string value = process.StandardOutput.ReadToEnd().Trim();
            process.WaitForExit();
            return value;
        }
        catch { return "unknown"; }
    }

    private static string Sha256(byte[] bytes) => Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(bytes));

    private static (int Width, int Height) PngSize(string path)
    {
        Span<byte> header = stackalloc byte[24];
        using FileStream stream = File.OpenRead(path);
        stream.ReadExactly(header);
        if (header[0] != 0x89 || header[1] != 0x50 || header[2] != 0x4E || header[3] != 0x47)
            throw new InvalidDataException($"{path} is not a PNG file.");
        int width = System.Buffers.Binary.BinaryPrimitives.ReadInt32BigEndian(header[16..20]);
        int height = System.Buffers.Binary.BinaryPrimitives.ReadInt32BigEndian(header[20..24]);
        return (width, height);
    }

    private static JsonSerializerOptions JsonOptions() => new() { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase };
}
