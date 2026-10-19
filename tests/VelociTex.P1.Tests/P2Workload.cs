using System.Diagnostics;
using System.Globalization;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;
using System.Text.Json;
using VelociTex;
using VelociTex.Rendering.Direct2D;

internal static class P2Workload
{
    private readonly record struct Input(string Id, string Category, string Source, bool Display);
    private readonly record struct Sample(string Category, double Milliseconds, long Allocation);
    internal static void Run(string[] args)
    {
        string output = Path.GetFullPath(args.FirstOrDefault(x => !x.StartsWith("--", StringComparison.Ordinal)) ?? "artifacts/p2/workload");
        Directory.CreateDirectory(output);
        string fixturePath = Path.Combine(AppContext.BaseDirectory, "fixtures/corpus.normal.json");
        using var corpus = JsonDocument.Parse(File.ReadAllBytes(fixturePath));
        Input[] inputs = corpus.RootElement.GetProperty("fixtures").EnumerateArray().Select(f => new Input(
            f.GetProperty("id").GetString()!, f.GetProperty("category").GetString()!, f.GetProperty("latex").GetString()!, f.GetProperty("displayMode").GetBoolean())).ToArray();
        if (inputs.Length != 60 || inputs.GroupBy(f => f.Category).Any(g => g.Count() != 10)) throw new InvalidOperationException("Frozen workload changed");
        MathEngine engine = new(); using MathWorkspace workspace = new(); WindowsMathTextShaper shaper = new();
        using WindowsMathRenderer renderer = new();
        MathOptions Options(Input input) => new() { DisplayMode = input.Display, TextShaper = input.Source.Any(c => c > 127) ? shaper : null };
        MathDocument Compile(Input input)
        {
            var options = Options(input);
            if (!engine.TryCompile(input.Source, in options, workspace, default, out var document, out var error)) throw new InvalidOperationException(input.Id + ": " + error);
            return document!;
        }
        // Cold fallback outline preparation is separately visible. It remains part of compilation.
        var cold = new List<Sample>();
        foreach (var input in inputs)
        {
            long bytes = GC.GetAllocatedBytesForCurrentThread(), start = Stopwatch.GetTimestamp();
            _ = Compile(input); cold.Add(new(input.Category, Stopwatch.GetElapsedTime(start).TotalMilliseconds, GC.GetAllocatedBytesForCurrentThread() - bytes));
        }
        for (int i = 0; i < 300; i++) _ = Compile(inputs[i % inputs.Length]);
        var order = Enumerable.Range(0, 6_000).Select(i => i % 60).ToArray();
        Random random = new(0x19002026); random.Shuffle(order);
        var samples = new Sample[order.Length];
        using (var csv = new StreamWriter(Path.Combine(output, "complete-compile.csv")))
        {
            csv.WriteLine("sample,fixture,category,completeCompileMs,threadManagedAllocatedBytes");
            for (int i = 0; i < order.Length; i++)
            {
                var input = inputs[order[i]]; var options = Options(input);
                long allocated = GC.GetAllocatedBytesForCurrentThread(), start = Stopwatch.GetTimestamp();
                bool success = engine.TryCompile(input.Source, in options, workspace, default, out var document, out var error);
                double ms = Stopwatch.GetElapsedTime(start).TotalMilliseconds; long bytes = GC.GetAllocatedBytesForCurrentThread() - allocated;
                if (!success || document is null) throw new InvalidOperationException(input.Id + ": " + error);
                samples[i] = new(input.Category, ms, bytes);
                csv.WriteLine(FormattableString.Invariant($"{i},{input.Id},{input.Category},{ms:R},{bytes}"));
            }
        }
        using var report = new Utf8JsonWriter(File.Create(Path.Combine(output, "workload.json")), new() { Indented = true });
        report.WriteStartObject(); report.WriteString("phase", "P2"); report.WriteBoolean("nativeAot", !RuntimeFeature.IsDynamicCodeSupported);
        report.WriteNumber("processId", Environment.ProcessId); report.WriteString("utc", DateTimeOffset.UtcNow);
        report.WriteString("corpusSha256", Convert.ToHexStringLower(SHA256.HashData(File.ReadAllBytes(fixturePath))));
        report.WriteString("boundary", "Complete validation, lexing, macros, semantics, layout, required text shaping and independently owned drawing result. No formula/semantic/layout cache. Static font data and bounded scalar-outline shaping cache warmed. No per-call forced GC, outliers retained.");
        report.WriteBoolean("oldCpuAcceptance", false);
        report.WriteString("hardwareEvidence", "docs/reference/development-hardware.json; actual current process machine only");
        Summary(report, "overall", samples); Summary(report, "coldFirstPass", cold.ToArray());
        report.WriteStartObject("categories"); foreach (var category in inputs.Select(f => f.Category).Distinct()) Summary(report, category, samples.Where(s => s.Category == category).ToArray()); report.WriteEndObject();
        report.WriteStartArray("batches");
        foreach (int count in new[] { 1, 10, 50, 100, 1000 })
        {
            MathDocumentCache cache = new(16 * 1024 * 1024, 256); MathDocument[] retained = new MathDocument[count];
            using Process process = Process.GetCurrentProcess(); process.Refresh(); long privateBefore = process.PrivateMemorySize64; int handlesBefore = process.HandleCount;
            long allocation = GC.GetAllocatedBytesForCurrentThread(), start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++) retained[i] = Compile(inputs[i % inputs.Length]);
            double noCacheMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds; long completeAllocation = GC.GetAllocatedBytesForCurrentThread() - allocation;
            // Unique identity per requested formula makes the miss/hit boundary explicit.
            string[] sources = Enumerable.Range(0, count).Select(i => inputs[i % 60].Source + "% cache key " + i.ToString(CultureInfo.InvariantCulture)).ToArray();
            int misses = 0, hits = 0; start = Stopwatch.GetTimestamp();
            for (int i = 0; i < count; i++) { var options = Options(inputs[i % 60]); if (!cache.TryCompile(engine, sources[i], in options, workspace, default, out _, out var error, out bool hit)) throw new InvalidOperationException(error.ToString()); if (!hit) misses++; }
            double missMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds; start = Stopwatch.GetTimestamp();
            for (int i = count - 1; i >= 0; i--) { var options = Options(inputs[i % 60]); if (!cache.TryCompile(engine, sources[i], in options, workspace, default, out _, out var error, out bool hit)) throw new InvalidOperationException(error.ToString()); if (hit) hits++; }
            double hitMs = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            process.Refresh();
            report.WriteStartObject(); report.WriteNumber("formulas", count); report.WriteNumber("completeNoCacheMs", noCacheMs); report.WriteNumber("completeManagedAllocationBytes", completeAllocation);
            report.WriteNumber("cacheMissBatchMs", missMs); report.WriteNumber("actualMisses", misses); report.WriteNumber("cacheRepeatBatchMs", hitMs); report.WriteNumber("actualRepeatHits", hits);
            report.WriteNumber("cacheItems", cache.Count); report.WriteNumber("cacheEstimatedManagedBytes", cache.EstimatedRetainedBytes);
            report.WriteNumber("privateBytesBefore", privateBefore); report.WriteNumber("privateBytesWithResultsAndCache", process.PrivateMemorySize64); report.WriteNumber("handlesBefore", handlesBefore); report.WriteNumber("handlesAfter", process.HandleCount);
            cache.Clear(); Array.Clear(retained); GC.Collect(); GC.WaitForPendingFinalizers(); GC.Collect(); process.Refresh();
            report.WriteNumber("privateBytesAfterClearAndGc", process.PrivateMemorySize64); report.WriteNumber("managedHeapAfterClearAndGc", GC.GetTotalMemory(false)); report.WriteEndObject();
        }
        report.WriteEndArray();
        report.WriteStartArray("nativeAndExports");
        foreach (var input in inputs.Where((_, i) => i < 6))
        {
            var document = Compile(input); long start = Stopwatch.GetTimestamp(); using var prepared = renderer.Prepare(document); double prepare = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            using NativeDrawMeasurement target = new(document);
            target.Begin(); start = Stopwatch.GetTimestamp(); renderer.Draw(prepared, target.Target, MathDrawingColor.Black, target.OffsetX, target.OffsetY); double submit = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            start = Stopwatch.GetTimestamp(); target.End(); double render = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            start = Stopwatch.GetTimestamp(); using (MemoryStream png = new()) renderer.WritePng(document, png); double pngTime = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            start = Stopwatch.GetTimestamp(); using (MemoryStream svg = new()) renderer.WriteSvg(document, svg); double svgTime = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            start = Stopwatch.GetTimestamp(); _ = MathMlWriter.Write(document); double mathmlTime = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
            report.WriteStartObject(); report.WriteString("fixture", input.Id); report.WriteNumber("prepareMs", prepare); report.WriteNumber("drawSubmitMs", submit); report.WriteNumber("endDrawCompletedWicSoftwareRenderMs", render); report.WriteNumber("pngExportMs", pngTime); report.WriteNumber("svgExportMs", svgTime); report.WriteNumber("mathmlExportMs", mathmlTime); report.WriteEndObject();
        }
        report.WriteEndArray(); report.WriteString("nativeTimingLimit", "Initialized WIC bitmap Direct2D CPU target. EndDraw completes software bitmap drawing; no swap-chain/GPU presentation timing is claimed. Native allocation/GPU peak remains unmeasured.");
        report.WriteNumber("shapingCacheItems", shaper.Count); report.WriteNumber("shapingCacheEstimatedBytes", shaper.EstimatedRetainedBytes);
        report.WriteBoolean("hiddenCompilationQueue", false); report.WriteEndObject();
        Console.WriteLine($"PASS frozen normal workload: {samples.Length} full calls, AOT={!RuntimeFeature.IsDynamicCodeSupported}, P95={Percentile(samples.Select(s => s.Milliseconds), .95):F4}ms; tested hardware only");
    }
    private static double Percentile(IEnumerable<double> values, double p) { var sorted = values.Order().ToArray(); return sorted[(int)Math.Ceiling(sorted.Length * p) - 1]; }
    private static void Summary(Utf8JsonWriter json, string name, Sample[] samples)
    {
        json.WriteStartObject(name); json.WriteNumber("samples", samples.Length); json.WriteNumber("p50Ms", Percentile(samples.Select(s => s.Milliseconds), .50)); json.WriteNumber("p95Ms", Percentile(samples.Select(s => s.Milliseconds), .95)); json.WriteNumber("p99Ms", Percentile(samples.Select(s => s.Milliseconds), .99)); json.WriteNumber("maximumMs", samples.Max(s => s.Milliseconds)); json.WriteNumber("minimumThreadManagedBytes", samples.Min(s => s.Allocation)); json.WriteNumber("maximumThreadManagedBytes", samples.Max(s => s.Allocation)); json.WriteEndObject();
    }
}
