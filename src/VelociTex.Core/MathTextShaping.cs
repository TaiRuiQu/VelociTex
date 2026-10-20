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

/// <summary>Optional contextual shaping for complete text runs absent from the locked math fonts.</summary>
/// <remarks>The compiler invokes this inside its complete timed region. The returned outlines
/// are independently owned, em-normalized and include glyph positioning, contextual forms,
/// ligatures and bidirectional ordering chosen by the actual platform font system.</remarks>
public interface IMathTextRunShaper : IMathTextShaper
{
    /// <summary>Shapes a complete bounded text run with a baseline at zero and downward Y.</summary>
    bool TryShapeRun(string text, MathFont font, out MathTextRunShape shape);
}

/// <summary>Actual contextual text advance and positioned em-normalized font outlines.</summary>
/// <param name="Advance">Natural advance in em.</param><param name="Paths">Positioned independently owned outlines.</param>
/// <param name="InkBounds">Conservative union of all positioned outline ink.</param>
public readonly record struct MathTextRunShape(double Advance, ReadOnlyMemory<MathPath> Paths, MathBounds InkBounds);

/// <summary>Original text provenance for contextual outlines already included in a document's Paths.</summary>
/// <param name="Text">Original run text.</param><param name="X">Baseline X in DIP.</param><param name="Y">Baseline Y in DIP.</param><param name="FontSize">Actual em size in DIP.</param>
/// <param name="FirstPath">First positioned outline index in the document's Paths.</param><param name="PathCount">Number of run outline paths.</param>
/// <param name="Advance">Actual shaped run advance in DIP.</param>
public readonly record struct MathTextRun(string Text, double X, double Y, double FontSize, int FirstPath = 0, int PathCount = 0, double Advance = 0);

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
