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

    [JSExport]
    public static string Compile(string source, string optionsJson, string idPrefix)
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
        using var buffer = new MemoryStream();
        using (var writer = new Utf8JsonWriter(buffer))
        {
            writer.WriteStartObject();
            if (!Engine.TryCompile(source, in options, Workspace, default, out var document, out var diagnostic))
            {
                writer.WriteBoolean("success", false);
                writer.WriteString("code", diagnostic.Code.ToString()); writer.WriteString("message", diagnostic.Message);
                writer.WriteNumber("start", diagnostic.Range.Start); writer.WriteNumber("length", diagnostic.Range.Length);
            }
            else
            {
                var viewport = MathSvgWriter.GetViewport(document!);
                writer.WriteBoolean("success", true);
                writer.WriteString("svg", MathSvgWriter.Write(document!, idPrefix));
                writer.WriteString("mathml", MathMlWriter.Write(document!));
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

        bool Flag(string key) => o.TryGetProperty(key, out var value) && value.GetBoolean();
        int Integer(string key, int fallback) => o.TryGetProperty(key, out var value) ? value.GetInt32() : fallback;
        double Number(string key, double fallback) => o.TryGetProperty(key, out var value) ? value.GetDouble() : fallback;
    }
}
