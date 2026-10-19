using System.Globalization;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.Json;

namespace VelociTex.Rendering.Svg;

/// <summary>Portable SVG rendering using locked, build-time extracted font contours.</summary>
public static class MathSvgWriter
{
    private sealed record Font(int UnitsPerEm, string[] Paths);
    private static readonly Lazy<Font[]> Fonts = new(LoadFonts);
    private static readonly ConditionalWeakTable<MathPathSegment[], string> PathDataCache = new();
    private const int MaxOutputCharacters = 16 * 1024 * 1024;

    /// <summary>Returns the union of logical and ink bounds, with a one-DIP minimum extent.</summary>
    public static MathBounds GetViewport(MathDocument document)
    {
        ArgumentNullException.ThrowIfNull(document);
        var ink = document.InkBounds;
        double x = Math.Min(0, ink.X), y = Math.Min(0, ink.Y);
        double width = Math.Max(document.LogicalSize.Width, ink.X + ink.Width) - x;
        double height = Math.Max(document.LogicalSize.Height, ink.Y + ink.Height) - y;
        if (!double.IsFinite(x) || !double.IsFinite(y) || !double.IsFinite(width) || !double.IsFinite(height) || width > 32768 || height > 32768 || width < 0 || height < 0)
            throw new InvalidOperationException("SVG geometry exceeds the 32768 pixel export boundary.");
        return new(x, y, Math.Max(1, width), Math.Max(1, height));
    }

    /// <summary>Standalone SVG plus glyph definitions that may be shared by a page.</summary>
    public readonly record struct SharedSvg(string Svg, string Definitions);

    /// <summary>Writes standalone SVG. The caller supplies a document-unique ASCII ID prefix.</summary>
    public static string Write(MathDocument document, string idPrefix)
    {
        ArgumentNullException.ThrowIfNull(document);
        ValidatePrefix(idPrefix);
        return WriteCore(document, idPrefix, idPrefix, stableGlyphIds: false, includeGlyphDefinitions: true, definitions: null);
    }

    /// <summary>Writes SVG with stable glyph IDs and separate definitions for page-level sharing.</summary>
    public static SharedSvg WriteShared(MathDocument document, string glyphPrefix, string clipPrefix)
    {
        ArgumentNullException.ThrowIfNull(document);
        ValidatePrefix(glyphPrefix); ValidatePrefix(clipPrefix);
        var definitions = new StringBuilder(2048);
        string svg = WriteCore(document, glyphPrefix, clipPrefix, stableGlyphIds: true, includeGlyphDefinitions: false, definitions);
        return new(svg, definitions.ToString());
    }

    private static void ValidatePrefix(string idPrefix)
    {
        ArgumentException.ThrowIfNullOrEmpty(idPrefix);
        if (idPrefix.Length > 96 || !char.IsAsciiLetter(idPrefix[0]) || idPrefix.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not '_' and not '-'))
            throw new ArgumentException("Use an ASCII letter followed by letters, digits, underscores or hyphens, at most 96 characters.", nameof(idPrefix));
    }

    private static string WriteCore(MathDocument document, string glyphPrefix, string clipPrefix, bool stableGlyphIds, bool includeGlyphDefinitions, StringBuilder? definitions)
    {
        var bounds = GetViewport(document);
        var output = new StringBuilder(1024 + document.Glyphs.Length * 96);
        output.Append("<svg xmlns=\"http://www.w3.org/2000/svg\"");
        NumberAttribute("width", bounds.Width); NumberAttribute("height", bounds.Height);
        output.Append(" viewBox=\"");
        Number(bounds.X); output.Append(' '); Number(bounds.Y); output.Append(' ');
        Number(bounds.Width); output.Append(' '); Number(bounds.Height); output.Append('"');
        output.Append(" role=\"img\" fill=\"currentColor\"><title>");
        Escaped(document.Source, attribute: false);
        output.Append("</title><defs>");
        var glyphIds = new Dictionary<(int Font, ushort Glyph), string>(document.Glyphs.Length);
        foreach (var glyph in document.Glyphs.Span)
        {
            var key = (glyph.FontId, glyph.GlyphId);
            if (glyphIds.ContainsKey(key)) continue;
            string id = stableGlyphIds ? glyphPrefix + "g" + glyph.FontId + "_" + glyph.GlyphId : glyphPrefix + "g" + glyphIds.Count;
            glyphIds.Add(key, id);
            var font = Fonts.Value[glyph.FontId];
            if (includeGlyphDefinitions)
                output.Append("<path id=\"").Append(id).Append("\" d=\"").Append(font.Paths[glyph.GlyphId]).Append("\"/>");
            else
                definitions!.Append("<path id=\"").Append(id).Append("\" d=\"").Append(font.Paths[glyph.GlyphId]).Append("\"/>");
            Bound();
        }
        for (int i = 0; i < document.Paths.Length; i++)
        {
            output.Append("<clipPath id=\"").Append(clipPrefix).Append('c').Append(i).Append("\" clipPathUnits=\"userSpaceOnUse\">");
            var clip = document.Paths.Span[i].ClipBounds;
            Rect(clip.X, clip.Y, clip.Width, clip.Height); output.Append("</clipPath>");
            Bound();
        }
        output.Append("</defs>");
        // An empty command list can intentionally describe an invisible formula.
        foreach (var command in document.PaintCommands.Span)
        {
            var state = document.PaintStates.Span[command.StateIndex];
            if (state.Color is { A: 0 }) continue;
            bool wrapped = state.Color is not null || state.Transform != MathTransform.Identity;
            if (wrapped)
            {
                output.Append("<g");
                if (state.Color is { } color)
                {
                    output.Append(" fill=\"#");
                    HexByte(color.R); HexByte(color.G); HexByte(color.B); output.Append('"');
                    if (color.A != 255) NumberAttribute("fill-opacity", color.A / 255d);
                }
                if (state.Transform != MathTransform.Identity)
                {
                    var t = state.Transform;
                    output.Append(" transform=\"matrix(");
                    Number(t.M11); output.Append(' '); Number(t.M12); output.Append(' ');
                    Number(t.M21); output.Append(' '); Number(t.M22); output.Append(' ');
                    Number(t.Dx); output.Append(' '); Number(t.Dy); output.Append(")\"");
                }
                output.Append('>');
            }
            switch (command.Kind)
            {
                case MathPaintKind.Glyph:
                    var glyph = document.Glyphs.Span[command.Index];
                    double scale = glyph.FontSize / Fonts.Value[glyph.FontId].UnitsPerEm;
                    output.Append("<use href=\"#").Append(glyphIds[(glyph.FontId, glyph.GlyphId)]).Append("\" transform=\"translate(");
                    Number(glyph.X); output.Append(' '); Number(glyph.Y); output.Append(") scale(");
                    Number(scale); output.Append(")\"/>");
                    break;
                case MathPaintKind.Rule:
                    var rule = document.Rules.Span[command.Index]; Rect(rule.X, rule.Y, rule.Width, rule.Height);
                    break;
                case MathPaintKind.Path:
                    var path = document.Paths.Span[command.Index];
                    output.Append("<g clip-path=\"url(#").Append(clipPrefix).Append('c').Append(command.Index).Append(")\"><path transform=\"translate(");
                    Number(path.X); output.Append(' '); Number(path.Y); output.Append(") scale(");
                    Number(path.ScaleX); output.Append(' '); Number(path.ScaleY); output.Append(")\" d=\"");
                    output.Append(PathData(path.Segments));
                    output.Append("\"/></g>");
                    break;
                default: throw new InvalidOperationException("Unknown paint instruction.");
            }
            if (wrapped) output.Append("</g>");
            Bound();
        }
        output.Append("</svg>"); Bound();
        return output.ToString();

        void NumberAttribute(string name, double value)
        {
            output.Append(' ').Append(name).Append("=\"");
            Number(value);
            output.Append('"');
        }
        void Number(double value)
        {
            AppendNumber(output, value);
        }
        void HexByte(byte value)
        {
            Span<char> buffer = stackalloc char[2];
            if (!value.TryFormat(buffer, out _, "x2", CultureInfo.InvariantCulture)) throw new InvalidOperationException("Unable to format an SVG color.");
            output.Append(buffer);
        }
        void Escaped(string value, bool attribute)
        {
            int start = 0;
            for (int i = 0; i < value.Length; i++)
            {
                string? replacement = value[i] switch
                {
                    '&' => "&amp;", '<' => "&lt;", '>' => "&gt;", '"' when attribute => "&quot;",
                    '\r' => "&#xD;", '\n' => "&#xA;", '\t' => "&#x9;", _ => null
                };
                if (replacement is null) continue;
                if (i > start) output.Append(value, start, i - start);
                output.Append(replacement); start = i + 1;
            }
            if (start < value.Length) output.Append(value, start, value.Length - start);
        }
        void Rect(double x, double y, double width, double height)
        {
            output.Append("<rect"); NumberAttribute("x", x); NumberAttribute("y", y); NumberAttribute("width", width); NumberAttribute("height", height); output.Append("/>");
        }
        void Bound()
        {
            if (output.Length > MaxOutputCharacters) throw new InvalidOperationException("SVG output limit exceeded.");
        }
    }

    private static string PathData(ReadOnlyMemory<MathPathSegment> segments)
    {
        if (MemoryMarshal.TryGetArray(segments, out ArraySegment<MathPathSegment> array) && array.Array is not null)
            return PathDataCache.GetValue(array.Array, static key => BuildPathData(key));
        return BuildPathData(segments.Span);
    }
    private static string BuildPathData(MathPathSegment[] segments) => BuildPathData(segments.AsSpan());
    private static string BuildPathData(ReadOnlySpan<MathPathSegment> segments)
    {
        var data = new StringBuilder(segments.Length * 24);
        foreach (var segment in segments)
        {
            switch (segment.Operation)
            {
                case MathPathOperation.Move: data.Append('M'); AppendNumber(data, segment.X1); data.Append(' '); AppendNumber(data, segment.Y1); break;
                case MathPathOperation.Line: data.Append('L'); AppendNumber(data, segment.X1); data.Append(' '); AppendNumber(data, segment.Y1); break;
                case MathPathOperation.Cubic:
                    data.Append('C'); AppendNumber(data, segment.X1); data.Append(' '); AppendNumber(data, segment.Y1); data.Append(' ');
                    AppendNumber(data, segment.X2); data.Append(' '); AppendNumber(data, segment.Y2); data.Append(' ');
                    AppendNumber(data, segment.X3); data.Append(' '); AppendNumber(data, segment.Y3); break;
                case MathPathOperation.Close: data.Append('Z'); break;
            }
            if (data.Length > MaxOutputCharacters) throw new InvalidOperationException("SVG output limit exceeded.");
        }
        return data.ToString();
    }
    private static void AppendNumber(StringBuilder output, double value)
    {
        if (!double.IsFinite(value)) throw new InvalidOperationException("Unable to format an SVG coordinate.");
        // Six decimal DIP places exceed the retained geometry precision and are
        // far cheaper than round-trip formatting on the WASM runtime.
        if (value is > -9e12 and < 9e12)
        {
            long scaled = (long)Math.Round(value * 1_000_000, MidpointRounding.AwayFromZero);
            if (scaled == 0) { output.Append('0'); return; }
            if (scaled < 0) { output.Append('-'); scaled = -scaled; }
            Span<char> digits = stackalloc char[20];
            int position = digits.Length;
            long integer = scaled / 1_000_000;
            do { digits[--position] = (char)('0' + (int)(integer % 10)); integer /= 10; } while (integer != 0);
            output.Append(digits[position..]);
            long fraction = scaled % 1_000_000;
            if (fraction == 0) return;
            Span<char> decimals = stackalloc char[6];
            for (int i = 5; i >= 0; i--) { decimals[i] = (char)('0' + (int)(fraction % 10)); fraction /= 10; }
            int length = 6;
            while (length > 0 && decimals[length - 1] == '0') length--;
            output.Append('.').Append(decimals[..length]);
            return;
        }
        Span<char> buffer = stackalloc char[32];
        if (!value.TryFormat(buffer, out int written, "G17", CultureInfo.InvariantCulture)) throw new InvalidOperationException("Unable to format an SVG coordinate.");
        output.Append(buffer[..written]);
    }
    private static Font[] LoadFonts()
    {
        using var stream = typeof(MathSvgWriter).Assembly.GetManifestResourceStream("VelociTex.Rendering.Svg.Generated.glyph-outlines.json")
            ?? throw new InvalidOperationException("Embedded SVG outlines are missing.");
        using var json = JsonDocument.Parse(stream);
        var fonts = json.RootElement.GetProperty("fonts").EnumerateArray().ToArray();
        if (fonts.Length != MathFontCatalog.Count) throw new InvalidOperationException("SVG font catalog mismatch.");
        return fonts.Select((font, i) => font.GetProperty("name").GetString() == MathFontCatalog.GetName(i)
            ? new Font(font.GetProperty("unitsPerEm").GetInt32(), font.GetProperty("paths").EnumerateArray().Select(p => p.GetString()!).ToArray())
            : throw new InvalidOperationException("SVG font ordering mismatch.")).ToArray();
    }
}
