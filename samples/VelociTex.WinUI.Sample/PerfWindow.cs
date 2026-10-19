using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using VelociTex.Rendering.Direct2D;
using Windows.UI;

namespace VelociTex.WinUI.Sample;

/// <summary>Measures the real WinUI display path: Result assignment, synchronous draw submission, and composition frames.</summary>
internal sealed class PerfWindow : Window
{
    private readonly string _output;
    private readonly Grid _root = new();
    private readonly MathView _view = new();
    private readonly MathEngine _engine = new();
    private readonly MathWorkspace _workspace = new();
    private readonly WindowsMathTextShaper _shaper = new();
    private readonly List<(string Id, string Category, MathDocument Document)> _documents = [];
    private readonly List<(string Id, MathDocument Document, bool Display)> _pageDocuments = [];
    private readonly List<MathView> _pageViews = [];

    internal PerfWindow(string[] args)
    {
        int index = Array.IndexOf(args, "--perf");
        _output = Path.GetFullPath(index >= 0 && index + 1 < args.Length ? args[index + 1] : "artifacts/performance/winui");
        Title = "VelociTex — end-to-end performance";
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1280, 900));
        _root.Background = new SolidColorBrush(ColorHelper.FromArgb(255, 246, 247, 250));
        _view.HorizontalAlignment = HorizontalAlignment.Center;
        _view.VerticalAlignment = VerticalAlignment.Center;
        _root.Children.Add(_view);
        Content = new ScrollViewer { Content = _root, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        Closed += (_, _) => { foreach (MathView view in _pageViews) view.Dispose(); _view.Dispose(); };
        _root.Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            Directory.CreateDirectory(_output);
            string corpusPath = Path.Combine(AppContext.BaseDirectory, "corpus.normal.json");
            using JsonDocument corpus = JsonDocument.Parse(File.ReadAllBytes(corpusPath));
            foreach (JsonElement fixture in corpus.RootElement.GetProperty("fixtures").EnumerateArray())
            {
                string id = fixture.GetProperty("id").GetString()!;
                string category = fixture.GetProperty("category").GetString()!;
                string latex = fixture.GetProperty("latex").GetString()!;
                bool display = fixture.GetProperty("displayMode").GetBoolean();
                MathOptions options = new() { FontSize = 24, DisplayMode = display, TextShaper = latex.Any(c => c > 127) ? _shaper : null };
                if (!_engine.TryCompile(latex, options, _workspace, default, out MathDocument? document, out MathDiagnostic error))
                    throw new InvalidOperationException($"{id}: {error}");
                _documents.Add((id, category, document!));
            }
            if (_documents.Count != 60) throw new InvalidOperationException("The frozen corpus changed shape.");
            foreach (var item in _documents) _pageDocuments.Add((item.Id, item.Document, false));
            for (int n = 1; n <= 40; n++)
            {
                string latex = (n % 3) switch
                {
                    0 => $@"\frac{{{n}+\sqrt{{x_{{{n}}}^2+{n + 1}}}}}{{1+x_{{{n}}}^2}}+\sum_{{k=1}}^{{{n % 9 + 2}}}\frac{{1}}{{k^2+{n}}}",
                    1 => $@"A_{{{n}}}=\begin{{pmatrix}}{n}&1&-1\\2&{n + 1}&0\\-1&0&{n + 2}\end{{pmatrix}}",
                    _ => $@"\lim_{{x\to0}}\frac{{\sin({n}x)}}{{x}}=\int_0^{{{n}}} e^{{-t^2}}\,dt",
                };
                bool display = n % 4 == 0;
                MathOptions options = new() { FontSize = 24, DisplayMode = display };
                if (!_engine.TryCompile(latex, options, _workspace, default, out MathDocument? document, out MathDiagnostic error))
                    throw new InvalidOperationException($"page-{n}: {error}");
                _pageDocuments.Add(($"page-{n}", document!, display));
            }
            await Task.Delay(500);

            // Cold: the first assignment includes renderer/device/font setup.
            double coldSync = 0, coldFirst = double.NaN, coldSecond = double.NaN;
            {
                Stopwatch watch = Stopwatch.StartNew();
                _view.Result = _documents[0].Document;
                coldSync = watch.Elapsed.TotalMilliseconds;
                coldFirst = await WaitFrameAsync(watch);
                coldSecond = await WaitFrameAsync(watch);
            }

            // Warm the renderer caches, glyph geometry and composition path.
            for (int i = 0; i < 120; i++)
            {
                _view.Result = _documents[i % _documents.Count].Document;
                await WaitFrameAsync(Stopwatch.StartNew());
                await Task.Yield();
            }
            await WaitFrameAsync(Stopwatch.StartNew());

            // The compositor cadence is environment-dependent (remote sessions
            // can be 30 Hz). Record it so frame-bound results are interpretable.
            List<double> frameIntervals = [];
            {
                Stopwatch frameWatch = Stopwatch.StartNew();
                double previous = await WaitFrameAsync(frameWatch);
                for (int i = 0; i < 30; i++)
                {
                    double next = await WaitFrameAsync(frameWatch);
                    if (double.IsFinite(next) && double.IsFinite(previous)) frameIntervals.Add(next - previous);
                    previous = next;
                }
            }

            int samplesPerFormula = int.TryParse(Environment.GetEnvironmentVariable("VELOCITEX_PERF_SAMPLES"), out int configured) ? configured : 6;
            List<Sample> samples = new(_documents.Count * samplesPerFormula);
            int[] order = Enumerable.Range(0, _documents.Count * samplesPerFormula).Select(i => i % _documents.Count).ToArray();
            Random random = new(0x19002026); random.Shuffle(order);
            _view.Result = _documents[order[0]].Document; await WaitFrameAsync(Stopwatch.StartNew()); await WaitFrameAsync(Stopwatch.StartNew());
            foreach (int index in order)
            {
                var item = _documents[index];
                Stopwatch watch = Stopwatch.StartNew();
                _view.Result = item.Document;
                double sync = watch.Elapsed.TotalMilliseconds;
                double first = await WaitFrameAsync(watch);
                double second = await WaitFrameAsync(watch);
                samples.Add(new(item.Id, item.Category, sync, first, second));
            }

            // Page workload: one hundred formula controls in one layout/render pass.
            // Shared DirectWrite/WARP/Composition resources make this representative
            // of a real page rather than one cold device per control.
            _view.Visibility = Visibility.Collapsed;
            StackPanel pagePanel = new() { Orientation = Orientation.Vertical, Width = 1200 };
            _root.Children.Add(pagePanel);
            List<MathView> pageViews = new(_pageDocuments.Count);
            foreach (var item in _pageDocuments) { MathView pageView = new(); pagePanel.Children.Add(pageView); pageViews.Add(pageView); _pageViews.Add(pageView); }
            _root.UpdateLayout();
            await Task.Delay(400);
            pageViews[0].Result = _pageDocuments[0].Document;
            await WaitFrameAsync(Stopwatch.StartNew());
            pageViews[0].Result = null;
            await WaitFrameAsync(Stopwatch.StartNew());
            Stopwatch pageWatch = Stopwatch.StartNew();
            for (int i = 0; i < _pageDocuments.Count; i++) pageViews[i].Result = _pageDocuments[i].Document;
            double pageSync = pageWatch.Elapsed.TotalMilliseconds;
            double pageFirstFrame = await WaitFrameAsync(pageWatch);
            double pageSecondFrame = await WaitFrameAsync(pageWatch);

            using (FileStream file = File.Create(Path.Combine(_output, "winui-e2e.json")))
            using (Utf8JsonWriter json = new(file, new JsonWriterOptions { Indented = true }))
            {
                json.WriteStartObject();
                json.WriteString("kind", "VelociTex WinUI end-to-end display benchmark; Canvas.Composition surface; initialized fonts and process");
                json.WriteString("utc", DateTimeOffset.UtcNow);
                json.WriteBoolean("nativeAot", !System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported);
                json.WriteNumber("processId", Environment.ProcessId);
                json.WriteNumber("logicalProcessors", Environment.ProcessorCount);
                json.WriteNumber("rasterizationScale", _root.XamlRoot?.RasterizationScale ?? double.NaN);
                json.WriteString("timedRegion", "Stopwatch starts immediately before MathView.Result assignment. sync is the synchronous return after preparation and Direct2D drawing into the Composition surface; first and second are subsequent CompositionTarget.Rendering frame callbacks. The call also replaces the retained result and may recreate the fixed-size composition surface when logical size changes.");
                json.WriteStartObject("coldFirstAssignment");
                json.WriteNumber("syncMs", coldSync); json.WriteNumber("firstFrameMs", coldFirst); json.WriteNumber("secondFrameMs", coldSecond);
                json.WriteEndObject();
                json.WriteStartObject("warm");
                WritePercentiles(json, "sync", samples.Select(s => s.Sync));
                WritePercentiles(json, "firstFrame", samples.Select(s => s.FirstFrame));
                WritePercentiles(json, "secondFrame", samples.Select(s => s.SecondFrame));
                json.WriteEndObject();
                json.WriteStartObject("compositorFrameInterval");
                WritePercentiles(json, "interval", frameIntervals);
                json.WriteEndObject();
                json.WriteStartObject("categories");
                foreach (string category in _documents.Select(d => d.Category).Distinct())
                {
                    var subset = samples.Where(s => s.Category == category).ToArray();
                    json.WriteStartObject(category);
                    WritePercentiles(json, "sync", subset.Select(s => s.Sync));
                    WritePercentiles(json, "firstFrame", subset.Select(s => s.FirstFrame));
                    WritePercentiles(json, "secondFrame", subset.Select(s => s.SecondFrame));
                    json.WriteEndObject();
                }
                json.WriteEndObject();
                json.WriteStartObject("page100");
                json.WriteNumber("formulas", _pageDocuments.Count);
                json.WriteNumber("syncMs", pageSync); json.WriteNumber("syncPerFormulaMs", pageSync / _pageDocuments.Count);
                json.WriteNumber("firstFrameMs", pageFirstFrame); json.WriteNumber("secondFrameMs", pageSecondFrame);
                json.WriteEndObject();
                json.WriteNumber("sampleCount", samples.Count);
                json.WriteStartArray("samples");
                foreach (Sample sample in samples)
                {
                    json.WriteStartObject(); json.WriteString("id", sample.Id); json.WriteNumber("syncMs", sample.Sync);
                    json.WriteNumber("firstFrameMs", sample.FirstFrame); json.WriteNumber("secondFrameMs", sample.SecondFrame); json.WriteEndObject();
                }
                json.WriteEndArray();
                json.WriteEndObject();
            }

            // Real-window screenshot after the measured loop proves actual display.
            nint windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
            try
            {
                await WindowCapture.CaptureAsync(windowHandle, Path.Combine(_output, "winui-e2e.png"));
            }
            catch (Exception captureError)
            {
                File.WriteAllText(Path.Combine(_output, "capture-error.txt"), captureError.ToString());
                try { GdiWindowCapture.Capture(windowHandle, Path.Combine(_output, "winui-e2e.bmp")); }
                catch (Exception gdiError) { File.WriteAllText(Path.Combine(_output, "capture-gdi-error.txt"), gdiError.ToString()); }
            }
            Console.WriteLine($"WINUI-E2E sync p50={Percentile(samples.Select(s => s.Sync), .5):F3}ms first p50={Percentile(samples.Select(s => s.FirstFrame), .5):F3}ms second p50={Percentile(samples.Select(s => s.SecondFrame), .5):F3}ms");
            File.WriteAllText(Path.Combine(_output, "winui-e2e.txt"), FormattableString.Invariant(
                $"sync p50={Percentile(samples.Select(s => s.Sync), .5):F4} p95={Percentile(samples.Select(s => s.Sync), .95):F4}; first p50={Percentile(samples.Select(s => s.FirstFrame), .5):F4} p95={Percentile(samples.Select(s => s.FirstFrame), .95):F4}; second p50={Percentile(samples.Select(s => s.SecondFrame), .5):F4} p95={Percentile(samples.Select(s => s.SecondFrame), .95):F4}"));
        }
        catch (Exception error)
        {
            Directory.CreateDirectory(_output);
            File.WriteAllText(Path.Combine(_output, "winui-e2e-error.txt"), error.ToString());
        }
        finally
        {
            Close();
            Application.Current.Exit();
        }
    }

    private static async Task<double> WaitFrameAsync(Stopwatch watch)
    {
        var completion = new TaskCompletionSource<double>(TaskCreationOptions.RunContinuationsAsynchronously);
        void Handler(object? sender, object args)
        {
            CompositionTarget.Rendering -= Handler;
            completion.TrySetResult(watch.Elapsed.TotalMilliseconds);
        }
        long start = watch.ElapsedTicks;
        CompositionTarget.Rendering += Handler;
        Task finished = await Task.WhenAny(completion.Task, Task.Delay(1000));
        if (finished != completion.Task)
        {
            CompositionTarget.Rendering -= Handler;
            return double.NaN;
        }
        return await completion.Task;
    }

    private static void WritePercentiles(Utf8JsonWriter json, string name, IEnumerable<double> values)
    {
        double[] sorted = values.Where(double.IsFinite).Order().ToArray();
        json.WriteStartObject(name);
        json.WriteNumber("samples", sorted.Length);
        if (sorted.Length != 0)
        {
            json.WriteNumber("p50Ms", Percentile(sorted, .50)); json.WriteNumber("p95Ms", Percentile(sorted, .95));
            json.WriteNumber("p99Ms", Percentile(sorted, .99)); json.WriteNumber("maximumMs", sorted[^1]);
            json.WriteNumber("meanMs", sorted.Average());
        }
        json.WriteEndObject();
    }

    private static double Percentile(IEnumerable<double> values, double percentile)
    {
        double[] sorted = values.Where(double.IsFinite).Order().ToArray();
        return sorted[Math.Max(0, (int)Math.Ceiling(sorted.Length * percentile) - 1)];
    }

    private readonly record struct Sample(string Id, string Category, double Sync, double FirstFrame, double SecondFrame);
}
