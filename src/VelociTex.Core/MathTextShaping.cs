namespace VelociTex;

/// <summary>Supplies actual font outlines for text scalars absent from the locked math fonts.</summary>
/// <remarks>Shaping runs inside complete compilation, before the immutable drawing result is
/// returned. Implementations define their threading and font availability contracts. A false
/// result produces an explicit missing-glyph diagnostic. This scalar interface does not promise
/// contextual shaping of Arabic, Indic scripts, or bidirectional paragraph layout.</remarks>
public interface IMathTextShaper
{
    /// <summary>Shapes one unsupported text scalar in em-normalized coordinates with a downward Y axis.</summary>
    bool TryShape(int codePoint, MathFont font, out MathTextShape shape);
}

/// <summary>An independently owned fallback outline, its actual advance, conservative ink bounds and font family.</summary>
/// <param name="Advance">Horizontal advance in em units.</param>
/// <param name="Segments">Outline relative to a baseline at zero; Y increases downward.</param>
/// <param name="InkBounds">Conservative outline bounds in em units.</param>
/// <param name="FontFamily">Actual system font selected by the shaping implementation.</param>
public readonly record struct MathTextShape(double Advance, ReadOnlyMemory<MathPathSegment> Segments, MathBounds InkBounds, string FontFamily);

/// <summary>Semantic provenance of a fallback text outline already included in the document's paths.</summary>
/// <param name="CodePoint">Original Unicode scalar.</param>
/// <param name="X">Horizontal baseline origin in DIP.</param>
/// <param name="Y">Vertical baseline origin in DIP.</param>
/// <param name="FontSize">Actual font em size in DIP.</param>
/// <param name="FontFamily">Actual selected system font family.</param>
public readonly record struct MathTextGlyph(int CodePoint, double X, double Y, double FontSize, string FontFamily);
