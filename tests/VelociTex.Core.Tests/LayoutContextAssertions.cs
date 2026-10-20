using VelociTex;

internal static class LayoutContextAssertions
{
    internal static int Run()
    {
        int checks = 0;
        void Check(bool success, string name)
        { checks++; if (!success) throw new InvalidOperationException("Display layout: " + name); }
        MathEngine engine = new();
        using MathWorkspace workspace = new();
        MathDocument Compile(string source, MathOptions settings)
        { if (!engine.TryCompile(source, in settings, workspace, default, out var document, out var diagnostic)) throw new InvalidOperationException(diagnostic.ToString()); return document!; }
        const string bodySource = "a=1", tagSource = "a=1\\tag{A}";
        MathOptions naturalOptions = new() { DisplayMode = true };
        MathDocument body = Compile(bodySource, naturalOptions);
        foreach (bool leftTag in new[] { false, true })
            foreach (bool flushLeft in new[] { false, true })
            {
                MathOptions settings = naturalOptions with { DisplayWidth = 240, Leqno = leftTag, Fleqn = flushLeft };
                MathDocument display = Compile(tagSource, settings);
                Check(Math.Abs(display.LogicalSize.Width - 240) < 1e-8, "declared display width");
                double bodyLeft = flushLeft ? 48 : (240 - body.LogicalSize.Width) / 2;
                MathGlyph naturalA = body.Glyphs.Span[0], displayA = Find(display, 'a');
                Check(Math.Abs(displayA.X - naturalA.X - bodyLeft) < 1e-8, "body offset independently follows centering or two-em flush-left inset");
                MathGlyph tagLeft = Find(display, '('), tagRight = Find(display, ')');
                double rightAdvance = GlyphAdvance(tagRight);
                Check(leftTag ? Math.Abs(tagLeft.X) < 1e-8 : Math.Abs(tagRight.X + rightAdvance - 240) < 1e-8, "complete tag is anchored to the host's selected edge");
                Check(display.InkBounds.X >= -1 && display.InkBounds.X + display.InkBounds.Width <= 241, "painted formula and tag remain inside the display canvas");
            }
        return checks;
    }
    private static MathGlyph Find(MathDocument document, int codePoint)
    { foreach (MathGlyph glyph in document.Glyphs.Span) if (glyph.CodePoint == codePoint) return glyph; throw new InvalidOperationException("Missing expected boundary glyph."); }
    private static double GlyphAdvance(MathGlyph glyph)
    {
        var font = FontData.Fonts[glyph.FontId];
        for (int i = font.GlyphStart; i < font.GlyphStart + font.GlyphCount; i++)
            if (FontData.Glyphs[i].GlyphId == glyph.GlyphId)
                return FontData.Glyphs[i].AdvanceUnits / (double)font.UnitsPerEm * glyph.FontSize;
        throw new InvalidOperationException("Missing locked advance metric.");
    }
}
