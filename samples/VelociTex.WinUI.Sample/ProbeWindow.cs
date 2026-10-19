using System.Text.Json;
using Microsoft.UI;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Documents;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using Path = System.IO.Path;
using Windows.ApplicationModel.DataTransfer;
using VelociTex.Rendering.Direct2D;

namespace VelociTex.WinUI.Sample;

internal sealed class ProbeWindow : Window
{
    private readonly StackPanel root = new() { Padding = new Thickness(24), Spacing = 16 };
    private readonly CompositionProbe drawing = new();
    private readonly CompositionProbe paragraphDrawing = new() { IsParagraph = true, Width = 780, Height = 160 };
    private readonly StackPanel rejectedCases = new() { Spacing = 16, Visibility = Visibility.Collapsed };
    private readonly List<(RichTextBlock Host, Run Text, Border Box, double Baseline)> inlineCases = [];
    private readonly string? output;

    public ProbeWindow(string[] args)
    {
        Title = "VelociTex — 原生绘制探针";
        for (int i = 0; i < args.Length - 1; i++) if (args[i] == "--smoke") output = Path.GetFullPath(args[i + 1]);
        AppWindow.Resize(new Windows.Graphics.SizeInt32(960, 820));
        root.RequestedTheme = ElementTheme.Light;
        root.Background = new SolidColorBrush(Colors.White);
        root.Children.Add(new TextBlock { Text = "原生数学绘制", FontSize = 28, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        root.Children.Add(new TextBlock { Text = "P0 硬编码探针 · 尚未接入公式解析", FontSize = 14, Foreground = new SolidColorBrush(Colors.Gray) });
        root.Children.Add(new ScrollViewer { Content = drawing, HorizontalScrollBarVisibility = ScrollBarVisibility.Auto, VerticalScrollBarVisibility = ScrollBarVisibility.Disabled });
        root.Children.Add(new TextBlock { Text = "正文与行内公式", FontSize = 20, FontWeight = Microsoft.UI.Text.FontWeights.SemiBold });
        root.Children.Add(paragraphDrawing);
        var copy = new Button { Content = "复制段落" };
        copy.Click += (_, _) => { var data = new DataPackage(); data.SetText(ParagraphSource); Clipboard.SetContent(data); };
        root.Children.Add(copy);
        rejectedCases.Children.Add(new TextBlock { Text = "未采用的 InlineUIContainer 基线对照", FontSize = 20 });
        rejectedCases.Children.Add(new TextBlock { Text = "红线标记公式自身的基线；默认底部对齐产生随公式高度变化的偏差。", FontSize = 14, TextWrapping = TextWrapping.Wrap });
        root.Children.Add(rejectedCases);
        AddInlineCase("短公式", 94, 36, 26);
        AddInlineCase("分式", 94, 58, 34);
        AddInlineCase("矩阵", 94, 88, 50);
        Content = root;
        root.Loaded += OnLoaded;
        root.SizeChanged += (_, _) => { if (paragraphDrawing.IsLoaded) { paragraphDrawing.Width = Math.Max(160, Math.Min(780, root.ActualWidth - 48)); paragraphDrawing.Rebuild(); } };
        Closed += (_, _) => { drawing.Dispose(); paragraphDrawing.Dispose(); };
    }

    private void AddInlineCase(string label, double width, double height, double baseline)
    {
        var host = new RichTextBlock { FontSize = 24, IsTextSelectionEnabled = true, TextWrapping = TextWrapping.Wrap };
        var text = new Run { Text = $"{label}之前 Ag " };
        var grid = new Grid { Width = width, Height = height };
        grid.Children.Add(new TextBlock { Text = label == "矩阵" ? "[ a  b ]\n[ c  d ]" : label == "分式" ? "a + b\n——\nc" : "x + y", FontSize = 16, LineHeight = 17, LineStackingStrategy = LineStackingStrategy.BlockLineHeight, HorizontalAlignment = HorizontalAlignment.Center, VerticalAlignment = VerticalAlignment.Center, TextAlignment = TextAlignment.Center });
        grid.Children.Add(new Rectangle { Height = 1, Fill = new SolidColorBrush(Colors.IndianRed), VerticalAlignment = VerticalAlignment.Top, Margin = new Thickness(0, baseline, 0, 0) });
        var box = new Border { Child = grid, BorderBrush = new SolidColorBrush(Colors.LightGray), BorderThickness = new Thickness(1) };
        var paragraph = new Paragraph();
        paragraph.Inlines.Add(text);
        paragraph.Inlines.Add(new InlineUIContainer { Child = box });
        paragraph.Inlines.Add(new Run { Text = "，之后有标点与正文。This sentence wraps at narrow widths." });
        host.Blocks.Add(paragraph);
        rejectedCases.Children.Add(host);
        inlineCases.Add((host, text, box, baseline + 1));
    }

    private async void OnLoaded(object sender, RoutedEventArgs args)
    {
        if (output is null) return;
        try
        {
            Directory.CreateDirectory(output);
            File.Delete(Path.Combine(output, "smoke-error.txt"));
            await Task.Delay(500);
            var hwnd = WinRT.Interop.WindowNative.GetWindowHandle(this);
            foreach (var (theme, name) in new[] { (ElementTheme.Light, "light"), (ElementTheme.Dark, "dark") })
            {
                root.RequestedTheme = theme;
                root.Background = new SolidColorBrush(theme == ElementTheme.Dark ? ColorHelper.FromArgb(255, 32, 32, 32) : Colors.White);
                drawing.Theme = theme == ElementTheme.Dark ? ProbeTheme.Dark : ProbeTheme.Light;
                paragraphDrawing.Theme = drawing.Theme;
                foreach (double scale in new[] { 1d, 1.25, 1.5, 2d })
                {
                    drawing.Rebuild(scale);
                    paragraphDrawing.Rebuild(scale);
                    root.UpdateLayout();
                    await Task.Delay(180);
                    await WindowCapture.CaptureAsync(hwnd, Path.Combine(output, $"{name}-{scale * 100:0}.png"));
                }
            }
            AppWindow.Resize(new Windows.Graphics.SizeInt32(580, 900));
            root.UpdateLayout();
            await Task.Delay(200);
            await WindowCapture.CaptureAsync(hwnd, Path.Combine(output, "narrow-dark.png"));
            paragraphDrawing.Visibility = Visibility.Collapsed;
            rejectedCases.Visibility = Visibility.Visible;
            root.UpdateLayout();
            await Task.Delay(180);
            await WindowCapture.CaptureAsync(hwnd, Path.Combine(output, "inline-ui-container-rejected.png"));
            rejectedCases.Visibility = Visibility.Collapsed;
            paragraphDrawing.Visibility = Visibility.Visible;
            drawing.Visibility = Visibility.Collapsed;
            await Task.Delay(80);
            drawing.Visibility = Visibility.Visible;
            drawing.Rebuild();
            paragraphDrawing.Rebuild();
            root.UpdateLayout();
            await Task.Delay(180);
            await WindowCapture.CaptureAsync(hwnd, Path.Combine(output, "reloaded.png"));
            using var reportStream = File.Create(Path.Combine(output, "winui-report.json"));
            using var json = new Utf8JsonWriter(reportStream, new JsonWriterOptions { Indented = true });
            json.WriteStartObject();
            json.WriteString("scope", "P0 hard-coded probe; no formula compatibility claimed");
            json.WriteNumber("processId", Environment.ProcessId);
            json.WriteString("capturedAt", DateTimeOffset.Now);
            json.WriteString("executableSha256", Convert.ToHexStringLower(System.Security.Cryptography.SHA256.HashData(File.ReadAllBytes(Environment.ProcessPath!))));
            json.WriteBoolean("nativeAot", !System.Runtime.CompilerServices.RuntimeFeature.IsDynamicCodeSupported);
            json.WriteString("runtime", System.Runtime.InteropServices.RuntimeInformation.FrameworkDescription);
            json.WriteString("os", System.Runtime.InteropServices.RuntimeInformation.OSDescription);
            json.WriteNumber("actualXamlRasterizationScale", root.XamlRoot.RasterizationScale);
            json.WriteString("dpiTestBoundary", "Surface raster density 100/125/150/200%; actual OS monitor DPI unchanged");
            json.WriteString("selectedParagraphHost", "DirectWrite IDWriteInlineObject with static unmanaged callbacks, drawn into Composition surface");
            json.WriteNumber("narrowDirectWriteWidth", paragraphDrawing.ParagraphMetrics.Width);
            json.WriteNumber("narrowDirectWriteHeight", paragraphDrawing.ParagraphMetrics.Height);
            json.WriteNumber("narrowDirectWriteLineCount", paragraphDrawing.ParagraphMetrics.LineCount);
            NativeInlineBaselineProbe.WriteReport(Path.Combine(output, "directwrite-baseline.json"));
            json.WriteStartArray("inlineUiContainer");
            foreach (var item in inlineCases)
            {
                var rect = item.Text.ContentStart.GetCharacterRect(LogicalDirection.Forward);
                var top = item.Box.TransformToVisual(item.Host).TransformPoint(new Windows.Foundation.Point());
                json.WriteStartObject();
                json.WriteString("text", item.Text.Text);
                json.WriteNumber("textRectY", rect.Y); json.WriteNumber("textRectHeight", rect.Height);
                json.WriteNumber("childTop", top.Y); json.WriteNumber("childHeight", item.Box.ActualHeight);
                json.WriteNumber("formulaBaseline", top.Y + item.Baseline);
                json.WriteNumber("hostFirstBaseline", item.Host.BaselineOffset);
                json.WriteNumber("baselineDifference", top.Y + item.Baseline - item.Host.BaselineOffset);
                json.WriteEndObject();
            }
            json.WriteEndArray();
            json.WriteString("copy", "Selected DirectWrite probe offers whole-paragraph source copying. Ordinary body text selection, per-formula keyboard focus and UIA are not implemented by this P0 host and must be restored in downstream P3.");
            json.WriteEndObject(); json.Flush();
        }
        catch (Exception error)
        {
            Directory.CreateDirectory(output);
            File.WriteAllText(Path.Combine(output, "smoke-error.txt"), error.ToString());
        }
        finally { Close(); }
    }
    private const string ParagraphSource = @"Body text \frac{a+b}{c}, a fraction; and \begin{pmatrix}1&0\\0&1\end{pmatrix}, a matrix. End.";
}
