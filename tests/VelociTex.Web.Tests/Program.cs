using System.Text.Json;
using System.Xml.Linq;
using VelociTex;
using VelociTex.Rendering.Svg;

// Portable JIT/Native AOT oracle for actual browser WASM parity. No Windows shaper:
// missing glyphs must remain explicit diagnostics in both runtimes.
string target = Path.GetFullPath(args.FirstOrDefault() ?? "artifacts/wasm/parity.json");
Directory.CreateDirectory(Path.GetDirectoryName(target)!);
using var corpus = JsonDocument.Parse(File.ReadAllBytes(Path.Combine(AppContext.BaseDirectory, "core.json")));
using var output = File.Create(target);
using var writer = new Utf8JsonWriter(output);
var engine = new MathEngine();
var workspace = new MathWorkspace();
int valid = 0, invalid = 0, missingGlyphs = 0;
writer.WriteStartArray();
foreach (var fixture in corpus.RootElement.GetProperty("cases").EnumerateArray())
{
    string source = fixture.GetProperty("latex").GetString()!;
    var options = new MathOptions { StrictMode = MathStrictMode.Ignore };
    var jsOptions = new Dictionary<string, JsonElement>();
    if (fixture.TryGetProperty("displayMode", out var display)) { options = options with { DisplayMode = display.GetBoolean() }; jsOptions["displayMode"] = display; }
    if (fixture.TryGetProperty("macros", out var macros))
    {
        options = options with { Macros = macros.EnumerateObject().ToDictionary(m => m.Name, m => m.Value.GetString()!) };
        jsOptions["macros"] = macros;
    }
    if (fixture.TryGetProperty("options", out var settings))
    {
        foreach (var setting in settings.EnumerateObject())
        {
            var value = setting.Value;
            options = setting.Name switch
            {
                "displayMode" => options with { DisplayMode = value.GetBoolean() },
                "maxExpand" => options with { MaxExpansion = value.GetInt32() },
                "maxSize" => options with { MaxSize = value.GetDouble() },
                "minRuleThickness" => options with { MinRuleThickness = value.GetDouble() },
                "colorIsTextColor" => options with { ColorIsTextColor = value.GetBoolean() },
                "leqno" => options with { Leqno = value.GetBoolean() },
                "fleqn" => options with { Fleqn = value.GetBoolean() },
                "strict" => options with { StrictMode = value.ValueKind == JsonValueKind.True ? MathStrictMode.Error : value.ValueKind == JsonValueKind.False ? MathStrictMode.Ignore : value.GetString() switch { "error" => MathStrictMode.Error, "warn" => MathStrictMode.Warn, _ => MathStrictMode.Ignore } },
                // Sessions and presentation are deliberately outside this per-formula parity contract.
                "globalGroup" or "output" or "throwOnError" or "errorColor" => options,
                _ => throw new InvalidOperationException("Unmapped fixture setting: " + setting.Name),
            };
            if (setting.Name is not ("globalGroup" or "output" or "throwOnError" or "errorColor" or "strict")) jsOptions[setting.Name] = value;
        }
    }
    bool success = engine.TryCompile(source, in options, workspace, default, out var document, out var diagnostic);
    writer.WriteStartObject();
    writer.WriteString("id", fixture.GetProperty("id").GetString()); writer.WriteString("source", source);
    writer.WriteStartObject("options");
    foreach (var option in jsOptions) { writer.WritePropertyName(option.Key); option.Value.WriteTo(writer); }
    writer.WriteString("strict", options.StrictMode.ToString().ToLowerInvariant());
    writer.WriteEndObject(); writer.WriteBoolean("success", success);
    if (success)
    {
        string svg = MathSvgWriter.Write(document!, "parity");
        var xml = XDocument.Parse(svg);
        XNamespace ns = "http://www.w3.org/2000/svg";
        var definitions = xml.Descendants().Attributes("id").Select(a => a.Value).ToHashSet();
        foreach (var reference in xml.Descendants(ns + "use").Attributes("href"))
            Assert(definitions.Contains(reference.Value[1..]), "Every glyph reference resolves");
        Assert(!svg.Contains("NaN", StringComparison.Ordinal) && !svg.Contains("Infinity", StringComparison.Ordinal), "Finite export coordinates");
        writer.WriteString("svg", svg); writer.WriteString("mathml", MathMlWriter.Write(document!)); valid++;
    }
    else
    {
        writer.WriteString("code", diagnostic.Code.ToString()); writer.WriteNumber("start", diagnostic.Range.Start); writer.WriteNumber("length", diagnostic.Range.Length);
        invalid++; if (diagnostic.Code == MathDiagnosticCode.UnknownSymbol) missingGlyphs++;
    }
    writer.WriteEndObject();
}
writer.WriteEndArray(); writer.Flush();
Assert(engine.TryCompile("x", out var small, out _), "Seed compilation");
bool rejected = false;
try { MathSvgWriter.Write(small!, "\" onload=\"alert(1)"); } catch (ArgumentException) { rejected = true; }
Assert(rejected, "Reject unsafe ID prefixes");
Assert(engine.TryCompile("\\phantom{x}", out var phantom, out _), "Invisible formula");
Assert(!MathSvgWriter.Write(phantom!, "hidden").Contains("<use", StringComparison.Ordinal), "Phantom remains invisible");
Console.WriteLine($"Portable SVG assertions passed; parity oracle: {valid} valid, {invalid} diagnostic cases ({missingGlyphs} missing-font cases), {new FileInfo(target).Length} bytes.");

static void Assert(bool condition, string description)
{
    if (!condition) throw new InvalidOperationException(description);
}
