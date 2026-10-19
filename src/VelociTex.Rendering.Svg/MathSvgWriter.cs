using System.Globalization;
using System.Text;
using System.Text.Json;
using System.Xml;

namespace VelociTex.Rendering.Svg;

/// <summary>Portable SVG rendering using locked, build-time extracted font contours.</summary>
public static class MathSvgWriter
{
    private sealed record Font(int UnitsPerEm, string[] Paths);
    private static readonly Lazy<Font[]> Fonts = new(LoadFonts);
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

    /// <summary>Writes standalone SVG. The caller supplies a document-unique ASCII ID prefix.</summary>
    public static string Write(MathDocument document, string idPrefix)
    {
        ArgumentNullException.ThrowIfNull(document);
        ArgumentException.ThrowIfNullOrEmpty(idPrefix);
        if (idPrefix.Length > 96 || !char.IsAsciiLetter(idPrefix[0]) || idPrefix.Any(c => !char.IsAsciiLetterOrDigit(c) && c is not '_' and not '-'))
            throw new ArgumentException("Use an ASCII letter followed by letters, digits, underscores or hyphens, at most 96 characters.", nameof(idPrefix));
        var bounds = GetViewport(document);
        var output = new StringBuilder();
        using var xml = XmlWriter.Create(output, new XmlWriterSettings { OmitXmlDeclaration = true, ConformanceLevel = ConformanceLevel.Fragment, NewLineHandling = NewLineHandling.Entitize });
        xml.WriteStartElement("svg", "http://www.w3.org/2000/svg");
        Attr("width", N(bounds.Width)); Attr("height", N(bounds.Height));
        Attr("viewBox", $"{N(bounds.X)} {N(bounds.Y)} {N(bounds.Width)} {N(bounds.Height)}");
        Attr("role", "img"); Attr("fill", "currentColor");
        xml.WriteElementString("title", document.Source);
        xml.WriteStartElement("defs");
        var glyphIds = new Dictionary<(int Font, ushort Glyph), string>();
        foreach (var glyph in document.Glyphs.Span)
        {
            var key = (glyph.FontId, glyph.GlyphId);
            if (glyphIds.ContainsKey(key)) continue;
            string id = idPrefix + "g" + glyphIds.Count;
            glyphIds.Add(key, id);
            var font = Fonts.Value[glyph.FontId];
            xml.WriteStartElement("path"); Attr("id", id); Attr("d", font.Paths[glyph.GlyphId]); xml.WriteEndElement();
            Bound();
        }
        for (int i = 0; i < document.Paths.Length; i++)
        {
            xml.WriteStartElement("clipPath"); Attr("id", idPrefix + "c" + i); Attr("clipPathUnits", "userSpaceOnUse");
            var clip = document.Paths.Span[i].ClipBounds;
            Rect(clip.X, clip.Y, clip.Width, clip.Height); xml.WriteEndElement();
            Bound();
        }
        xml.WriteEndElement();
        // An empty command list can intentionally describe an invisible formula.
        foreach (var command in document.PaintCommands.Span)
        {
            var state = document.PaintStates.Span[command.StateIndex];
            if (state.Color is { A: 0 }) continue;
            xml.WriteStartElement("g");
            if (state.Color is { } color)
            {
                Attr("fill", $"#{color.R:x2}{color.G:x2}{color.B:x2}");
                if (color.A != 255) Attr("fill-opacity", N(color.A / 255d));
            }
            if (state.Transform != MathTransform.Identity)
            {
                var t = state.Transform;
                Attr("transform", $"matrix({N(t.M11)} {N(t.M12)} {N(t.M21)} {N(t.M22)} {N(t.Dx)} {N(t.Dy)})");
            }
            switch (command.Kind)
            {
                case MathPaintKind.Glyph:
                    var glyph = document.Glyphs.Span[command.Index];
                    double scale = glyph.FontSize / Fonts.Value[glyph.FontId].UnitsPerEm;
                    xml.WriteStartElement("use"); Attr("href", "#" + glyphIds[(glyph.FontId, glyph.GlyphId)]);
                    Attr("transform", $"translate({N(glyph.X)} {N(glyph.Y)}) scale({N(scale)})"); xml.WriteEndElement();
                    break;
                case MathPaintKind.Rule:
                    var rule = document.Rules.Span[command.Index]; Rect(rule.X, rule.Y, rule.Width, rule.Height);
                    break;
                case MathPaintKind.Path:
                    var path = document.Paths.Span[command.Index];
                    xml.WriteStartElement("g"); Attr("clip-path", $"url(#{idPrefix}c{command.Index})");
                    xml.WriteStartElement("path");
                    Attr("transform", $"translate({N(path.X)} {N(path.Y)}) scale({N(path.ScaleX)} {N(path.ScaleY)})");
                    var data = new StringBuilder();
                    foreach (var segment in path.Segments.Span)
                    {
                        switch (segment.Operation)
                        {
                            case MathPathOperation.Move: data.Append($"M{N(segment.X1)} {N(segment.Y1)}"); break;
                            case MathPathOperation.Line: data.Append($"L{N(segment.X1)} {N(segment.Y1)}"); break;
                            case MathPathOperation.Cubic: data.Append($"C{N(segment.X1)} {N(segment.Y1)} {N(segment.X2)} {N(segment.Y2)} {N(segment.X3)} {N(segment.Y3)}"); break;
                            case MathPathOperation.Close: data.Append('Z'); break;
                        }
                        if (data.Length > MaxOutputCharacters) throw new InvalidOperationException("SVG output limit exceeded.");
                    }
                    Attr("d", data.ToString()); xml.WriteEndElement(); xml.WriteEndElement();
                    break;
                default: throw new InvalidOperationException("Unknown paint instruction.");
            }
            xml.WriteEndElement(); Bound();
        }
        xml.WriteEndElement(); xml.Flush(); Bound();
        return output.ToString();

        void Attr(string name, string value) => xml.WriteAttributeString(name, value);
        void Rect(double x, double y, double width, double height)
        {
            xml.WriteStartElement("rect"); Attr("x", N(x)); Attr("y", N(y)); Attr("width", N(width)); Attr("height", N(height)); xml.WriteEndElement();
        }
        void Bound()
        {
            if (output.Length > MaxOutputCharacters) throw new InvalidOperationException("SVG output limit exceeded.");
        }
    }

    private static string N(double value) => value.ToString("G17", CultureInfo.InvariantCulture);
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
