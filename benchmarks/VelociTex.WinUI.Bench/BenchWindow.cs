using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using VelociTex.Rendering.Direct2D;
using Windows.UI;

namespace VelociTex.WinUI.Bench;

/// <summary>
/// Measures the real WinUI display path: compile, result assignment, and the
/// composition frames that actually present the formula. A mixed page is built
/// from RichTextBlock paragraphs hosting TextBlock runs and inline MathView
/// controls so the page workload exercises body text and formulas together.
/// </summary>
internal sealed class BenchWindow : Window
{
    private readonly string[] _args;
    private readonly string _output;
    private readonly Grid _root = new();
    private readonly MathView _singleView = new();
    private readonly MathEngine _engine = new();
    private readonly MathWorkspace _workspace = new();
    private readonly List<PageCase> _pages = [];
    private FrameworkElement? _pageText;

    internal BenchWindow(string[] args)
    {
        _args = args;
        int index = Array.IndexOf(args, "--out");
        _output = Path.GetFullPath(index >= 0 && index + 1 < args.Length ? args[index + 1] : "artifacts/benchmarks/winui");
        Title = "VelociTex 鈥?Windows native end-to-end benchmark";
        AppWindow.Resize(new Windows.Graphics.SizeInt32(1280, 900));
        _root.Background = new SolidColorBrush(Color.FromArgb(255, 255, 255, 255));
        _root.Padding = new Thickness(24);
        _singleView.HorizontalAlignment = HorizontalAlignment.Left;
        _singleView.VerticalAlignment = VerticalAlignment.Top;
        _root.Children.Add(_singleView);
        Content = new ScrollViewer { Content = _root, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Auto };
        Closed += (_, _) => _singleView.Dispose();
        _root.Loaded += OnLoaded;
    }

    private async void OnLoaded(object sender, RoutedEventArgs e)
    {
        try
        {
            Directory.CreateDirectory(_output);
            string workloadPath = Path.Combine(AppContext.BaseDirectory, "workloads.json");
            if (!File.Exists(workloadPath)) throw new FileNotFoundException("The benchmark workload file is missing.", workloadPath);
            using JsonDocument corpus = JsonDocument.Parse(File.ReadAllBytes(workloadPath));
            var single = new List<Formula>();
            var longFormulas = new List<Formula>();
            foreach (JsonProperty workload in corpus.RootElement.GetProperty("workloads").EnumerateObject())
            {
                if (workload.Name == "page") continue;
                foreach (JsonElement item in workload.Value.GetProperty("cases").EnumerateArray())
                {
                    var formula = new Formula(item.GetProperty("id").GetString()!, item.GetProperty("latex").GetString()!, item.GetProperty("displayMode").GetBoolean());
                    (workload.Name == "single" ? single : longFormulas).Add(formula);
                }
            }
            foreach (JsonElement item in corpus.RootElement.GetProperty("workloads").GetProperty("page").GetProperty("cases").EnumerateArray())
            {
                var paragraphs = new List<List<Segment>>();
                var formulas = new List<Formula>();
                int index = 0;
                foreach (JsonElement paragraph in item.GetProperty("paragraphs").EnumerateArray())
                {
                    var segments = new List<Segment>();
                    foreach (JsonElement segment in paragraph.EnumerateArray())
                    {
                        if (segment.TryGetProperty("latex", out JsonElement latex))
                        {
                            var formula = new Formula($"{item.GetProperty("id").GetString()}-{index++}", latex.GetString()!, segment.GetProperty("displayMode").GetBoolean());
                            formulas.Add(formula);
                            segments.Add(new Segment(null, formula));
                        }
                        else segments.Add(new Segment(segment.GetProperty("text").GetString()!, null));
                    }
                    paragraphs.Add(segments);
                }
                _pages.Add(new PageCase(item.GetProperty("id").GetString()!, item.GetProperty("title").GetString()!, paragraphs, formulas));
            }

            await Task.Delay(600);

            // Warm DirectWrite, Direct2D, composition, fonts and JIT.
            foreach (Formula formula in single.Concat(longFormulas))
            {
                MathDocument document = Compile(formula);
                _singleView.Result = document;
                await WaitFrameAsync(Stopwatch.StartNew());
            }
            await WaitFrameAsync(Stopwatch.StartNew());

            List<double> frameIntervals = [];
            {
                Stopwatch watch = Stopwatch.StartNew();
                double previous = await WaitFrameAsync(watch);
                for (int i = 0; i < 30; i++)
                {
                    double next = await WaitFrameAsync(watch);
                    if (double.IsFinite(next) && double.IsFinite(previous)) frameIntervals.Add(next - previous);
                    previous = next;
                }
            }

            var random = new Random(0x19002026);
            var singleSamples = new List<Sample>();
            var longSamples = new List<Sample>();
            foreach ((string name, List<Formula> formulas) in new[] { ("single", single), ("long", longFormulas) })
            {
                var order = Enumerable.Range(0, formulas.Count * 12).OrderBy(_ => random.Next()).ToArray();
                foreach (int index in order)
                {
                    Formula formula = formulas[index % formulas.Count];
                    Stopwatch watch = Stopwatch.StartNew();
                    MathDocument document = Compile(formula);
                    double compile = watch.Elapsed.TotalMilliseconds;
                    _singleView.Result = document;
                    double sync = watch.Elapsed.TotalMilliseconds;
                    double first = await WaitFrameAsync(watch);
                    double second = await WaitFrameAsync(watch);
                    (name == "single" ? singleSamples : longSamples).Add(new Sample(formula.Id, compile, sync, first, second));
                }
            }

            var pageRounds = new List<PageSample>();
            foreach (PageCase page in _pages)
            {
                for (int round = 0; round < 6; round++)
                {
                    _singleView.Visibility = Visibility.Collapsed;
                    if (_pageText is not null) _root.Children.Remove(_pageText);
                    var (text, views) = BuildPage(page);
                    _root.Children.Add(text);
                    _root.UpdateLayout();
                    await WaitFrameAsync(Stopwatch.StartNew());
                    await WaitFrameAsync(Stopwatch.StartNew());

                    Stopwatch watch = Stopwatch.StartNew();
                    double compile = 0;
                    for (int i = 0; i < page.Formulas.Count; i++)
                    {
                        long start = Stopwatch.GetTimestamp();
                        views[i].Result = Compile(page.Formulas[i]);
                        compile += (Stopwatch.GetTimestamp() - start) * 1000.0 / Stopwatch.Frequency;
                    }
                    double sync = watch.Elapsed.TotalMilliseconds;
                    double first = await WaitFrameAsync(watch);
                    double second = await WaitFrameAsync(watch);
                    pageRounds.Add(new PageSample(page.Id, page.Formulas.Count, compile, sync, first, second));
                }
            }

            // Screenshot evidence: show the last page after the measured loop.
            _singleView.Visibility = Visibility.Collapsed;
            nint windowHandle = WinRT.Interop.WindowNative.GetWindowHandle(this);
            await Task.Delay(500);
            try { GdiWindowCapture.Capture(windowHandle, Path.Combine(_output, "winui-page.bmp")); }
            catch (Exception error) { File.WriteAllText(Path.Combine(_output, "capture-error.txt"), error.ToString()); }

            var report = new
            {
                schemaVersion = 1,
                kind = "VelociTex Windows native WinUI end-to-end benchmark",
                utc = DateTimeOffset.UtcNow,
                processId = Environment.ProcessId,
                logicalProcessors = Environment.ProcessorCount,
                nativeAot = !System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported,
                dpiScale = _root.XamlRoot?.RasterizationScale ?? double.NaN,
                timedRegion = "Stopwatch starts immediately before MathEngine.TryCompile; then MathDocument is assigned to MathView.Result and the measurement ends on the second CompositionTarget.Rendering callback after assignment. The page workload compiles every formula, assigns every inline view in one pass, then reports the second presented frame for the complete page.",
                warmup = "Renderer, WARP device, font extraction, glyph outlines and the composition path are warmed before timing.",
                single = new { samples = singleSamples.Select(ToRecord), summary = Summarize(singleSamples) },
                longFormulas = new { samples = longSamples.Select(ToRecord), summary = Summarize(longSamples) },
                page = new { rounds = pageRounds.Select(p => new { id = p.Id, formulas = p.Formulas, compileMs = p.Compile, syncMs = p.Sync, firstFrameMs = p.FirstFrame, secondFrameMs = p.SecondFrame, secondFramePerFormulaMs = p.SecondFrame / p.Formulas }), summary = SummarizePages(pageRounds) },
                frameInterval = Describe(frameIntervals),
            };
            File.WriteAllText(Path.Combine(_output, "winui-report.json"), JsonSerializer.Serialize(report, new JsonSerializerOptions { WriteIndented = true, PropertyNamingPolicy = JsonNamingPolicy.CamelCase }));
            Console.WriteLine($"single second-frame p50={Percentile(singleSamples.Select(s => s.SecondFrame), .5):F3}ms; long p50={Percentile(longSamples.Select(s => s.SecondFrame), .5):F3}ms; page p50={Percentile(pageRounds.Select(p => p.SecondFrame), .5):F3}ms");
        }
        catch (Exception error)
        {
            Directory.CreateDirectory(_output);
            File.WriteAllText(Path.Combine(_output, "winui-error.txt"), error.ToString());
            Console.Error.WriteLine(error);
        }
        finally
        {
            Close();
            Application.Current.Exit();
        }
    }

    private (FrameworkElement Page, List<MathView> Views) BuildPage(PageCase page)
    {
        var panel = new StackPanel
        {
            Width = 1120,
            Spacing = 10,
            Margin = new Thickness(0, 8, 0, 8),
        };
        var title = new TextBlock
        {
            Text = page.Title,
            FontSize = 22,
            FontWeight = Microsoft.UI.Text.FontWeights.SemiBold,
            Foreground = new SolidColorBrush(Color.FromArgb(255, 24, 27, 33)),
            Margin = new Thickness(0, 0, 0, 6),
        };
        panel.Children.Add(title);
        var views = new List<MathView>();
        foreach (List<Segment> paragraph in page.Paragraphs)
        {
            var row = new StackPanel
            {
                Orientation = Orientation.Horizontal,
                Spacing = 2,
                Margin = new Thickness(0, 2, 0, 2),
                VerticalAlignment = VerticalAlignment.Center,
            };
            foreach (Segment segment in paragraph)
            {
                if (segment.Text is not null)
                {
                    row.Children.Add(new TextBlock
                    {
                        Text = segment.Text,
                        FontSize = 17,
                        LineHeight = 38,
                        VerticalAlignment = VerticalAlignment.Center,
                        Foreground = new SolidColorBrush(Color.FromArgb(255, 24, 27, 33)),
                    });
                }
                else
                {
                    MathView view = new() { VerticalAlignment = VerticalAlignment.Center, Margin = new Thickness(2, 0, 2, 0) };
                    row.Children.Add(view);
                    views.Add(view);
                }
            }
            panel.Children.Add(row);
        }
        _pageText = panel;
        return (panel, views);
    }

    private MathDocument Compile(Formula formula)
    {
        MathOptions options = new() { DisplayMode = formula.DisplayMode, FontSize = 24 };
        if (!_engine.TryCompile(formula.Latex, in options, _workspace, default, out MathDocument? document, out MathDiagnostic diagnostic))
            throw new InvalidOperationException($"{formula.Id}: {diagnostic.Code}: {diagnostic.Message}");
        return document!;
    }

    private static async Task<double> WaitFrameAsync(Stopwatch watch)
    {
        var completion = new TaskCompletionSource<double>(TaskCreationOptions.RunContinuationsAsynchronously);
        void Handler(object? sender, object args)
        {
            CompositionTarget.Rendering -= Handler;
            completion.TrySetResult(watch.Elapsed.TotalMilliseconds);
        }
        CompositionTarget.Rendering += Handler;
        Task finished = await Task.WhenAny(completion.Task, Task.Delay(1500));
        if (finished != completion.Task)
        {
            CompositionTarget.Rendering -= Handler;
            return double.NaN;
        }
        return await completion.Task;
    }

    private static object ToRecord(Sample sample) => new { id = sample.Id, compileMs = sample.Compile, syncMs = sample.Sync, firstFrameMs = sample.FirstFrame, secondFrameMs = sample.SecondFrame };
    private static object Summarize(List<Sample> samples) => new { samples = samples.Count, compile = Describe(samples.Select(s => s.Compile)), sync = Describe(samples.Select(s => s.Sync)), firstFrame = Describe(samples.Select(s => s.FirstFrame)), secondFrame = Describe(samples.Select(s => s.SecondFrame)) };
    private static object SummarizePages(List<PageSample> samples) => new { samples = samples.Count, compile = Describe(samples.Select(s => s.Compile)), sync = Describe(samples.Select(s => s.Sync)), firstFrame = Describe(samples.Select(s => s.FirstFrame)), secondFrame = Describe(samples.Select(s => s.SecondFrame)), secondFramePerFormula = Describe(samples.Select(s => s.SecondFrame / s.Formulas)) };
    private static object Describe(IEnumerable<double> values)
    {
        double[] sorted = values.Where(double.IsFinite).Order().ToArray();
        if (sorted.Length == 0) return new { samples = 0 };
        return new { samples = sorted.Length, p50Ms = Percentile(sorted, .5), p95Ms = Percentile(sorted, .95), p99Ms = Percentile(sorted, .99), maximumMs = sorted[^1], meanMs = sorted.Average() };
    }
    private static double Percentile(IEnumerable<double> values, double p)
    {
        double[] sorted = values.Where(double.IsFinite).Order().ToArray();
        return sorted.Length == 0 ? double.NaN : sorted[Math.Max(0, (int)Math.Ceiling(sorted.Length * p) - 1)];
    }

    private sealed record Formula(string Id, string Latex, bool DisplayMode);
    private sealed record Segment(string? Text, Formula? Formula);
    private sealed record PageCase(string Id, string Title, List<List<Segment>> Paragraphs, List<Formula> Formulas);
    private readonly record struct Sample(string Id, double Compile, double Sync, double FirstFrame, double SecondFrame);
    private readonly record struct PageSample(string Id, int Formulas, double Compile, double Sync, double FirstFrame, double SecondFrame);
}
