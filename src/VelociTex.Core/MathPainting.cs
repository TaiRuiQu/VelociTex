namespace VelociTex;

/// <summary>An explicit sRGB color used by retained formula painting commands.</summary>
public readonly record struct MathColor(byte R, byte G, byte B, byte A = 255);

/// <summary>A two-dimensional affine transform in DIP coordinates.</summary>
/// <remarks>The transformed point is (x*M11+y*M21+Dx, x*M12+y*M22+Dy).</remarks>
public readonly record struct MathTransform(double M11, double M12, double M21, double M22, double Dx, double Dy)
{
    /// <summary>The transform that preserves every point.</summary>
    public static MathTransform Identity => new(1, 0, 0, 1, 0, 0);
}

/// <summary>The retained drawing array addressed by a painting command.</summary>
public enum MathPaintKind
{
    /// <summary>A glyph in <see cref="MathDocument.Glyphs"/>.</summary>
    Glyph,
    /// <summary>A filled rectangle in <see cref="MathDocument.Rules"/>.</summary>
    Rule,
    /// <summary>A filled path in <see cref="MathDocument.Paths"/>.</summary>
    Path,
}

/// <summary>One painting operation in its required overlap order.</summary>
/// <param name="Kind">The drawing array to address.</param>
/// <param name="Index">The zero-based drawing-array index.</param>
/// <param name="StateIndex">The zero-based index in the document's painting states.</param>
public readonly record struct MathPaintCommand(MathPaintKind Kind, int Index, int StateIndex);

/// <summary>Shared painting state; a null color inherits the host's foreground color.</summary>
public readonly record struct MathPaintState(MathColor? Color, MathTransform Transform);
