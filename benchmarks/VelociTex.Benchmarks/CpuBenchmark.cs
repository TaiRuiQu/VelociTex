using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Text.Json;
using VelociTex;
using VelociTex.Rendering.Direct2D;

internal static class CpuBenchmark
{
    private sealed record Formula(string Latex, bool DisplayMode);
    internal static int Run(string[] args)
    {
        string Option(string name, string fallback) => Array.IndexOf(args, name) is var i && i >= 0 ? args[i + 1] : fallback;
        string output = Path.GetFullPath(Option("--out", "artifacts/benchmarks/native"));
        Directory.CreateDirectory(output);
        var config = new BatchTiming.Config(int.Parse(Option("--samples", "15")), double.Parse(Option("--batch-ms", "25"), System.Globalization.CultureInfo.InvariantCulture));
        byte[] bytes = File.ReadAllBytes("benchmarks/workloads.json");
        using var corpus = JsonDocument.Parse(bytes);
        using var renderer = new WindowsMathRenderer();
        using var workspace = new MathWorkspace();
        var engine = new MathEngine();
        var workloads = new Dictionary<string, List<object>>();
        foreach (var workload in corpus.RootElement.GetProperty("workloads").EnumerateObject())
        {
            var records = new List<object>();
            foreach (var unit in workload.Value.GetProperty("cases").EnumerateArray())
            {
                string id = unit.GetProperty("id").GetString()!;
                IEnumerable<JsonElement> elements = workload.Name == "page"
                    ? unit.GetProperty("paragraphs").EnumerateArray().SelectMany(p => p.EnumerateArray()).Where(s => s.TryGetProperty("latex", out _))
                    : [unit];
                Formula[] formulas = elements.Select(f => new Formula(f.GetProperty("latex").GetString()!, f.GetProperty("displayMode").GetBoolean())).ToArray();
                void Compile(bool svg)
                {
                    foreach (var f in formulas)
                    {
                        MathOptions options = new() { FontSize = 24, DisplayMode = f.DisplayMode };
                        if (!engine.TryCompile(f.Latex, in options, workspace, default, out var doc, out var diagnostic))
                            throw new InvalidOperationException($"{id}: {diagnostic}");
                        if (svg)
                        {
                            using var stream = new MemoryStream();
                            renderer.WriteSvg(doc!, stream);
                            if (stream.Length < 20)
                                throw new InvalidOperationException(id);
                        }
                        GC.KeepAlive(doc);
                    }
                }
                void Mathml()
                {
                    foreach (var f in formulas)
                    {
                        MathOptions options = new() { FontSize = 24, DisplayMode = f.DisplayMode };
                        if (!engine.TryCompileToMathMl(f.Latex, in options, workspace, default, out var markup, out _, out var diagnostic) || !markup!.Contains("<math", StringComparison.Ordinal))
                            throw new InvalidOperationException($"{id}: {diagnostic}");
                    }
                }
                var warm = Stopwatch.StartNew();
                do
                { Mathml(); Compile(false); Compile(true); } while (warm.Elapsed.TotalMilliseconds < config.WarmupMs);
                var mathml = BatchTiming.Measure(Mathml, config);
                var compile = BatchTiming.Measure(() => Compile(false), config);
                var svgResult = BatchTiming.Measure(() => Compile(true), config);
                records.Add(new { id, formulaCount = formulas.Length, mathml, compile, svg = svgResult });
                Console.WriteLine($"{id}: MathML {mathml.Summary.MedianMs * 1000:F2} us/unit");
            }
            workloads.Add(workload.Name, records);
        }
        var result = new
        {
            schemaVersion = 2,
            engine = "native",
            utc = DateTimeOffset.UtcNow,
            nativeAot = !RuntimeFeature.IsDynamicCodeSupported,
            runtime = System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription,
            os = Environment.OSVersion.ToString(),
            processorCount = Environment.ProcessorCount,
            workloadSha256 = Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(bytes)),
            timedRegion = "Batched full public .NET API calls to the native C++ Core; fresh parse per operation; no document cache, no frame wait, natural GC/finalization included. SVG is an export diagnostic, not equivalent to browser HTML.",
            result = new { config, workloads },
        };
        File.WriteAllText(Path.Combine(output, "native-report.json"), JsonSerializer.Serialize(result, new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
        return 0;
    }
}
