using System.Diagnostics;
using System.Globalization;
using System.Runtime.InteropServices.JavaScript;
using System.Runtime.Versioning;
using System.Text;
using System.Text.Json;
using VelociTex.Rendering.Svg;

namespace VelociTex.Wasm;

internal static class Program
{
    private static void Main() { }
}

/// <summary>The coarse-grained JS boundary; one call compiles and exports one formula.</summary>
[SupportedOSPlatform("browser")]
public static partial class BrowserBindings
{
    private static readonly MathEngine Engine = new();
    private static readonly MathWorkspace Workspace = new();

    /// <summary>Compiles and returns the requested output as compact JSON.</summary>
    [JSExport]
    public static string Compile(string source, string optionsJson, string idPrefix)
    {
        var (options, output) = ParseOptions(optionsJson);
        bool wantSvg = output != "mathml", wantMathMl = output != "svg";
        using var buffer = new MemoryStream();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            if (!Engine.TryCompile(source, in options, Workspace, default, out var document, out var diagnostic))
            {
                WriteDiagnostic(writer, diagnostic);
            }
            else
            {
                var viewport = MathSvgWriter.GetViewport(document!);
                writer.WriteBoolean("success", true);
                var start = Stopwatch.GetTimestamp();
                if (wantSvg) writer.WriteString("svg", MathSvgWriter.Write(document!, idPrefix));
                if (wantMathMl) writer.WriteString("mathml", MathMlWriter.Write(document!));
                writer.WriteNumber("exportMs", Stopwatch.GetElapsedTime(start).TotalMilliseconds);
                writer.WriteNumber("width", viewport.Width); writer.WriteNumber("height", viewport.Height);
                writer.WriteNumber("baseline", document!.Baseline - viewport.Y);
                writer.WriteNumber("logicalWidth", document.LogicalSize.Width); writer.WriteNumber("logicalHeight", document.LogicalSize.Height);
                writer.WriteStartArray("warnings");
                foreach (var warning in document.Warnings.Span)
                {
                    writer.WriteStartObject(); writer.WriteString("code", warning.Code); writer.WriteString("message", warning.Message);
                    writer.WriteNumber("start", warning.Range.Start); writer.WriteNumber("length", warning.Range.Length); writer.WriteEndObject();
                }
                writer.WriteEndArray();
            }
            writer.WriteEndObject();
        }
        return Encoding.UTF8.GetString(buffer.GetBuffer(), 0, checked((int)buffer.Length));
    }

    /// <summary>Compiles and returns the standalone SVG directly, avoiding JSON escaping and parsing.</summary>
    [JSExport]
    public static string CompileSvg(string source, string optionsJson, string idPrefix)
    {
        var (options, _) = ParseOptions(optionsJson);
        if (!Engine.TryCompile(source, in options, Workspace, default, out var document, out var diagnostic))
            return "E" + DiagnosticJson(diagnostic);
        return "S" + MathSvgWriter.Write(document!, idPrefix);
    }

    /// <summary>Compiles and returns presentation MathML directly, avoiding JSON escaping and parsing.</summary>
    [JSExport]
    public static string CompileMathMl(string source, string optionsJson, string idPrefix)
    {
        var (options, _) = ParseOptions(optionsJson);
        // MathML does not require layout, shaping or drawing geometry.
        if (!Engine.TryCompileToMathMl(source, in options, Workspace, default, out string? mathml, out _, out MathDiagnostic diagnostic))
            return "E" + DiagnosticJson(diagnostic);
        return "M" + mathml!;
    }

    /// <summary>Compiles without serializing output; used to separate core work from export and interop.</summary>
    [JSExport]
    public static string CompileMetrics(string source, string optionsJson)
    {
        var (options, _) = ParseOptions(optionsJson);
        long start = Stopwatch.GetTimestamp();
        bool success = Engine.TryCompile(source, in options, Workspace, default, out _, out _);
        double milliseconds = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        return milliseconds.ToString("R", CultureInfo.InvariantCulture) + "|" + (success ? "1" : "0");
    }

    /// <summary>Compiles with a minimal options path, isolating option parsing and JSON from core work.</summary>
    [JSExport]
    public static string CompileMetricsSimple(string source, bool displayMode)
    {
        MathOptions options = new() { DisplayMode = displayMode };
        long start = Stopwatch.GetTimestamp();
        bool success = Engine.TryCompile(source, in options, Workspace, default, out _, out _);
        double milliseconds = Stopwatch.GetElapsedTime(start).TotalMilliseconds;
        return milliseconds.ToString("R", CultureInfo.InvariantCulture) + "|" + (success ? "1" : "0");
    }

    /// <summary>Measures the JS/WASM interop call floor for diagnostics.</summary>
    [JSExport]
    public static string Ping(string value) => value;

    /// <summary>Measures compile and SVG serialization separately.</summary>
    [JSExport]
    public static string CompileMetricsSvg(string source, string optionsJson, string idPrefix)
    {
        var (options, _) = ParseOptions(optionsJson);
        long compileStart = Stopwatch.GetTimestamp();
        if (!Engine.TryCompile(source, in options, Workspace, default, out var document, out var diagnostic))
            return "E" + DiagnosticJson(diagnostic);
        double compile = Stopwatch.GetElapsedTime(compileStart).TotalMilliseconds;
        long exportStart = Stopwatch.GetTimestamp();
        string svg = MathSvgWriter.Write(document!, idPrefix);
        double export = Stopwatch.GetElapsedTime(exportStart).TotalMilliseconds;
        return compile.ToString("R", CultureInfo.InvariantCulture) + "|" + export.ToString("R", CultureInfo.InvariantCulture) + "|" + svg.Length;
    }

    /// <summary>Fast SVG path for default limits; avoids options JSON parsing and macro dictionaries.</summary>
    [JSExport]
    public static string CompileSvgSimple(string source, bool displayMode, string idPrefix)
    {
        MathOptions options = new() { DisplayMode = displayMode };
        if (!Engine.TryCompile(source, in options, Workspace, default, out var document, out var diagnostic))
            return "E" + DiagnosticJson(diagnostic);
        return "S" + MathSvgWriter.Write(document!, idPrefix);
    }

    /// <summary>Fast MathML path for default limits; skips layout.</summary>
    [JSExport]
    public static string CompileMathMlSimple(string source, bool displayMode, string idPrefix)
    {
        MathOptions options = new() { DisplayMode = displayMode };
        if (!Engine.TryCompileToMathMl(source, in options, Workspace, default, out string? mathml, out _, out MathDiagnostic diagnostic))
            return "E" + DiagnosticJson(diagnostic);
        return "M" + mathml!;
    }

    private static (MathOptions Options, string Output) ParseOptions(string optionsJson)
    {
        if (optionsJson.Length > 1_048_576) throw new ArgumentException("Options JSON exceeds the 1 MiB character limit.");
        using var json = JsonDocument.Parse(optionsJson);
        var o = json.RootElement;
        var defaults = new MathOptions();
        Dictionary<string, string>? macros = null;
        if (o.TryGetProperty("macros", out var definitions))
        {
            macros = new(StringComparer.Ordinal);
            foreach (var macro in definitions.EnumerateObject()) macros.Add(macro.Name, macro.Value.GetString()!);
        }
        var options = defaults with
        {
            FontSize = Number("fontSize", 24), DisplayMode = Flag("displayMode"), Macros = macros,
            MaxInputLength = Integer("maxInputLength", defaults.MaxInputLength), MaxNodes = Integer("maxNodes", defaults.MaxNodes),
            MaxNesting = Integer("maxNesting", defaults.MaxNesting), MaxTokens = Integer("maxTokens", defaults.MaxTokens),
            MaxExpansion = Integer("maxExpand", defaults.MaxExpansion), MaxSize = Number("maxSize", defaults.MaxSize),
            MinRuleThickness = Number("minRuleThickness", 0), ColorIsTextColor = Flag("colorIsTextColor"),
            Leqno = Flag("leqno"), Fleqn = Flag("fleqn"), DisplayWidth = Number("displayWidth", 0),
            StrictMode = o.TryGetProperty("strict", out var strict) ? strict.GetString() switch { "error" => MathStrictMode.Error, "ignore" => MathStrictMode.Ignore, _ => MathStrictMode.Warn } : MathStrictMode.Warn,
        };
        string output = o.TryGetProperty("output", out var outputElement) ? outputElement.GetString() ?? "html" : "html";
        return (options, output);

        bool Flag(string key) => o.TryGetProperty(key, out var value) && value.GetBoolean();
        int Integer(string key, int fallback) => o.TryGetProperty(key, out var value) ? value.GetInt32() : fallback;
        double Number(string key, double fallback) => o.TryGetProperty(key, out var value) ? value.GetDouble() : fallback;
    }

    private static string DiagnosticJson(MathDiagnostic diagnostic)
    {
        using var buffer = new MemoryStream();
        using (var writer = new Utf8JsonWriter(buffer)) { writer.WriteStartObject(); WriteDiagnostic(writer, diagnostic); writer.WriteEndObject(); }
        return Encoding.UTF8.GetString(buffer.GetBuffer(), 0, checked((int)buffer.Length));
    }
    private static void WriteDiagnostic(Utf8JsonWriter writer, MathDiagnostic diagnostic)
    {
        writer.WriteBoolean("success", false);
        writer.WriteString("code", diagnostic.Code.ToString()); writer.WriteString("message", diagnostic.Message);
        writer.WriteNumber("start", diagnostic.Range.Start); writer.WriteNumber("length", diagnostic.Range.Length);
    }
}
