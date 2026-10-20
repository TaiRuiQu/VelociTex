using System.Diagnostics;
using System.Text.Json;
using VelociTex;
using VelociTex.NativeMath;

var corpusPath = args.Length > 0 ? args[0] : Path.Combine("benchmarks", "corpus.normal.json");
int iterations = args.Length > 1 ? int.Parse(args[1]) : 5000;
using var document = JsonDocument.Parse(File.ReadAllText(corpusPath));
var fixtures = document.RootElement.GetProperty("fixtures").EnumerateArray()
    .Select(f => (Id: f.GetProperty("id").GetString()!, Latex: f.GetProperty("latex").GetString()!, Display: f.GetProperty("displayMode").GetBoolean()))
    .ToArray();

Console.WriteLine($"native available={NativeMathMl.IsAvailable} fixtures={fixtures.Length}");
if (!NativeMathMl.IsAvailable) return 2;

var defaultOptions = new MathOptions();
if (!FastMathEngine.TryCompileToMathMl("x^2", defaultOptions, out string? probe, out _, out _) || string.IsNullOrEmpty(probe)) return 8;
if (!FastMathEngine.TryCompileToMathMl("\\color{red}{x}", defaultOptions, out string? fallbackProbe, out _, out _) || string.IsNullOrEmpty(fallbackProbe)) return 9;
Console.WriteLine("fallback probe: native x^2 and managed \\color both compiled");

// Validate first, then warm both paths.
foreach (var fixture in fixtures)
{
    try
    {
        if (!NativeMathMl.TryRender(fixture.Latex, fixture.Display, out string mathml))
        { Console.WriteLine($"native unsupported: {fixture.Id}"); return 3; }
        if (mathml.Length == 0) { Console.WriteLine($"empty: {fixture.Id}"); return 4; }
    }
    catch (Exception error)
    {
        Console.Error.WriteLine($"{fixture.Id}: {error}");
        return 7;
    }
}

for (int i = 0; i < 200; i++)
    foreach (var fixture in fixtures) NativeMathMl.TryRender(fixture.Latex, fixture.Display, out _);

var nativeSamples = new List<double>(iterations * fixtures.Length);
long allocated = GC.GetAllocatedBytesForCurrentThread();
for (int i = 0; i < iterations; i++)
    foreach (var fixture in fixtures)
    {
        long start = Stopwatch.GetTimestamp();
        NativeMathMl.TryRender(fixture.Latex, fixture.Display, out string mathml);
        nativeSamples.Add((Stopwatch.GetTimestamp() - start) * 1_000_000.0 / Stopwatch.Frequency);
        if (mathml.Length == 0) return 5;
    }
long nativeAllocated = GC.GetAllocatedBytesForCurrentThread() - allocated;

var native = Summary(nativeSamples);
Console.WriteLine($"native  n={nativeSamples.Count} mean={native.Mean:F4}us p50={native.P50:F4}us p95={native.P95:F4}us p99={native.P99:F4}us alloc={nativeAllocated / (double)nativeSamples.Count:F0}B");
Console.WriteLine($"native vs KaTeX 53.5us budget for 3x: {(53.527 / native.Mean):F2}x");
return 0;

static (double Mean, double P50, double P95, double P99) Summary(List<double> samples)
{
    samples.Sort();
    return (samples.Average(), samples[samples.Count / 2], samples[samples.Count * 95 / 100], samples[samples.Count * 99 / 100]);
}
