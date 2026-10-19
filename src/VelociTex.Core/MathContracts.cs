using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("VelociTex.P1.Tests")]
[assembly: InternalsVisibleTo("VelociTex.Native.Probe")]

namespace VelociTex;

/// <summary>A UTF-16 source slice; no source string is borrowed by this value.</summary>
/// <param name="Start">Zero-based UTF-16 offset.</param><param name="Length">UTF-16 code unit count.</param>
public readonly record struct SourceRange(int Start, int Length);
/// <summary>Logical dimensions of a complete formula in DIP.</summary>
/// <param name="Width">Natural horizontal advance.</param><param name="Height">Logical ascent plus depth.</param>
public readonly record struct MathSize(double Width, double Height);
/// <summary>An axis-aligned rectangle in the coordinate system specified by its owner.</summary>
/// <param name="X">Left coordinate.</param><param name="Y">Top coordinate; Y increases downward.</param>
/// <param name="Width">Horizontal extent.</param><param name="Height">Vertical extent.</param>
public readonly record struct MathBounds(double X, double Y, double Width, double Height);
/// <summary>One mapped glyph from an unchanged embedded KaTeX font.</summary>
/// <param name="FontId">Index in <see cref="MathFontCatalog"/>.</param><param name="GlyphId">Verified TrueType glyph index.</param>
/// <param name="CodePoint">Original rendered Unicode scalar.</param><param name="X">Baseline origin X in DIP.</param>
/// <param name="Y">Baseline origin Y in DIP from the logical box top.</param><param name="FontSize">Font em size in DIP.</param>
public readonly record struct MathGlyph(int FontId, ushort GlyphId, int CodePoint, double X, double Y, double FontSize);
/// <summary>A filled rectangular rule in formula DIP coordinates.</summary>
/// <param name="X">Left coordinate.</param><param name="Y">Top coordinate.</param><param name="Width">Rule width.</param><param name="Height">Painted thickness.</param>
public readonly record struct MathRule(double X, double Y, double Width, double Height);
/// <summary>Operations in a normalized filled path.</summary>
public enum MathPathOperation
{
    /// <summary>Starts a new contour at the first point.</summary>
    Move,
    /// <summary>Adds a straight segment ending at the first point.</summary>
    Line,
    /// <summary>Adds a cubic Bézier with two control points and an endpoint.</summary>
    Cubic,
    /// <summary>Closes the current contour.</summary>
    Close
}
/// <summary>One path operation; unused coordinates are zero.</summary>
/// <param name="Operation">Contour operation.</param><param name="X1">First X coordinate.</param><param name="Y1">First Y coordinate.</param>
/// <param name="X2">Second control X for a cubic.</param><param name="Y2">Second control Y for a cubic.</param>
/// <param name="X3">Cubic endpoint X.</param><param name="Y3">Cubic endpoint Y.</param>
public readonly record struct MathPathSegment(MathPathOperation Operation, double X1, double Y1, double X2 = 0, double Y2 = 0, double X3 = 0, double Y3 = 0);
/// <summary>An independently owned nonzero-filled path with an affine scale and translation.</summary>
/// <param name="Segments">Normalized contours.</param><param name="X">Translation X in DIP.</param><param name="Y">Translation Y in DIP.</param>
/// <param name="ScaleX">Horizontal scale into DIP.</param><param name="ScaleY">Vertical scale into DIP.</param><param name="ClipBounds">Clip rectangle already expressed in formula DIP.</param>
public readonly record struct MathPath(ReadOnlyMemory<MathPathSegment> Segments, double X, double Y, double ScaleX, double ScaleY, MathBounds ClipBounds);
/// <summary>TeX atom classes controlling inter-atom spacing.</summary>
public enum MathAtom
{
    /// <summary>Ordinary atom.</summary>
    Ord,
    /// <summary>Large or named operator.</summary>
    Op,
    /// <summary>Binary operator, subject to contextual reclassification.</summary>
    Bin,
    /// <summary>Relation.</summary>
    Rel,
    /// <summary>Opening delimiter.</summary>
    Open,
    /// <summary>Closing delimiter.</summary>
    Close,
    /// <summary>Punctuation.</summary>
    Punct,
    /// <summary>Inner construction.</summary>
    Inner
}
/// <summary>The four public TeX size styles; cramped states are handled internally.</summary>
public enum MathStyle
{
    /// <summary>Display style.</summary>
    Display,
    /// <summary>Inline text style.</summary>
    Text,
    /// <summary>First-level script style.</summary>
    Script,
    /// <summary>Second and deeper script style.</summary>
    ScriptScript
}
/// <summary>Locked math and text font selections; per-character fallback follows upstream rules.</summary>
public enum MathFont
{
    /// <summary>Context-dependent upstream default.</summary>
    Default,
    /// <summary>Main regular symbol font.</summary>
    Main,
    /// <summary>Math italic selection.</summary>
    MathItalic,
    /// <summary>Upright roman selection.</summary>
    MathRoman,
    /// <summary>Bold upright selection.</summary>
    MathBold,
    /// <summary>Bold symbol selection.</summary>
    MathBoldItalic,
    /// <summary>Sans-serif regular selection.</summary>
    MathSans,
    /// <summary>Sans-serif bold selection.</summary>
    MathSansBold,
    /// <summary>Sans-serif italic selection.</summary>
    MathSansItalic,
    /// <summary>Monospaced selection.</summary>
    MathTypewriter,
    /// <summary>AMS blackboard and extended symbols.</summary>
    AMS,
    /// <summary>Calligraphic capitals.</summary>
    Caligraphic,
    /// <summary>Fraktur selection.</summary>
    Fraktur,
    /// <summary>Script capitals.</summary>
    Script,
    /// <summary>Explicit normal math selection.</summary>
    MathNormal
}

/// <summary>Bounded compilation settings; all dimensions are device-independent pixels.</summary>
public readonly record struct MathOptions
{
    /// <summary>Creates bounded defaults. Use this constructor; default(MathOptions) has zero limits and is invalid.</summary>
    public MathOptions() { }
    /// <summary>Formula em size in DIP, greater than zero and at most 1024; default 24.</summary>
    public double FontSize { get; init; } = 24;
    /// <summary>Starts in display style when true; otherwise inline text style.</summary>
    public bool DisplayMode { get; init; }
    /// <summary>Input UTF-16 budget, default 32768; supported maximum 1048576.</summary>
    public int MaxInputLength { get; init; } = 32_768;
    /// <summary>Compact semantic node budget, default 65536; supported maximum 262144.</summary>
    public int MaxNodes { get; init; } = 65_536;
    /// <summary>Syntax nesting budget, default 128; supported maximum 1024.</summary>
    public int MaxNesting { get; init; } = 128;
    /// <summary>Macro expansion count budget, default 1000; supported maximum 1000000.</summary>
    public int MaxExpansion { get; init; } = 1_000;
    /// <summary>Lexical and expanded token budget, default 65536; supported maximum 1048576.</summary>
    public int MaxTokens { get; init; } = 65_536;
    /// <summary>Per-call string macro definitions; do not mutate during compilation.</summary>
    public IReadOnlyDictionary<string, string>? Macros { get; init; }
    /// <summary>Optional serial macro context. Successful calls commit; failure and cancellation roll back.</summary>
    public MacroSession? Session { get; init; }
    /// <summary>Optional actual fallback outline provider, invoked inside compilation. Null reports unavailable text glyphs.</summary>
    public IMathTextShaper? TextShaper { get; init; }
}

/// <summary>Stable categories of ordinary compilation failures.</summary>
public enum MathDiagnosticCode
{
    /// <summary>No failure.</summary>
    None,
    /// <summary>Invalid size or resource limits.</summary>
    InvalidOptions,
    /// <summary>Source or aggregate macro input limit exceeded.</summary>
    InputLimit,
    /// <summary>Semantic node limit exceeded.</summary>
    NodeLimit,
    /// <summary>Syntax nesting limit exceeded.</summary>
    NestingLimit,
    /// <summary>Macro expansion limit exceeded.</summary>
    ExpansionLimit,
    /// <summary>Token limit exceeded.</summary>
    TokenLimit,
    /// <summary>Unexpected syntax.</summary>
    UnexpectedToken,
    /// <summary>A required argument is absent.</summary>
    MissingArgument,
    /// <summary>A group, delimiter or environment is unclosed.</summary>
    UnclosedGroup,
    /// <summary>A closing brace has no matching opening brace.</summary>
    UnexpectedGroupEnd,
    /// <summary>A base has duplicate superscripts.</summary>
    DoubleSuperscript,
    /// <summary>A base has duplicate subscripts.</summary>
    DoubleSubscript,
    /// <summary>The requested construction is not yet implemented or lies outside scope.</summary>
    UnsupportedCommand,
    /// <summary>The symbol or required font glyph is unavailable.</summary>
    UnknownSymbol,
    /// <summary>A macro definition is malformed or violates definition semantics.</summary>
    InvalidMacroDefinition,
    /// <summary>The call-exclusive workspace is already busy.</summary>
    WorkspaceInUse,
    /// <summary>The serial macro session is already busy.</summary>
    SessionInUse,
    /// <summary>Compilation was cancelled and transient changes rolled back.</summary>
    Cancelled,
}

/// <summary>An ordinary compilation error with a bounded source range.</summary>
/// <param name="Code">Stable error category.</param><param name="Range">Source UTF-16 range.</param><param name="Message">Diagnostic detail for logs, not a localized interface prompt.</param>
public readonly record struct MathDiagnostic(MathDiagnosticCode Code, SourceRange Range, string Message)
{
    /// <summary>True when the code is not None.</summary>
    public bool IsError => Code != MathDiagnosticCode.None;
}

/// <summary>Immutable, independently owned output. Glyph origins and paths use logical-box coordinates.</summary>
public sealed class MathDocument
{
    internal MathDocument(string source, in MathOptions options, SemanticNode[] nodes, int root, LayoutOutput layout)
    {
        Source = source; Options = options with { Macros = null, Session = null, TextShaper = null }; Nodes = nodes; Root = root;
        LogicalSize = layout.LogicalSize; Baseline = layout.Baseline; InkBounds = layout.InkBounds;
        Glyphs = layout.Glyphs; Rules = layout.Rules; Paths = layout.Paths;
        TextGlyphs = layout.TextGlyphs;
    }
    /// <summary>Complete original source without host math delimiters; suitable for whole-formula copying.</summary>
    public string Source { get; }
    /// <summary>Compilation settings with mutable macro, session and shaping-provider references removed.</summary>
    public MathOptions Options { get; }
    /// <summary>Natural logical width and ascent-plus-depth in DIP.</summary>
    public MathSize LogicalSize { get; }
    /// <summary>Baseline distance in DIP below the logical top.</summary>
    public double Baseline { get; }
    /// <summary>Explicitly named alias for Baseline.</summary>
    public double BaselineDip => Baseline;
    /// <summary>Painted bounds in DIP; may overhang the logical box.</summary>
    public MathBounds InkBounds { get; }
    /// <summary>Independently owned mapped font glyph instructions.</summary>
    public ReadOnlyMemory<MathGlyph> Glyphs { get; }
    /// <summary>Independently owned painted rectangular rules.</summary>
    public ReadOnlyMemory<MathRule> Rules { get; }
    /// <summary>Independently owned filled and clipped paths.</summary>
    public ReadOnlyMemory<MathPath> Paths { get; }
    /// <summary>Fallback text provenance; its outlines are already included in Paths.</summary>
    public ReadOnlyMemory<MathTextGlyph> TextGlyphs { get; }
    internal SemanticNode[] Nodes { get; }
    internal int Root { get; }
}

/// <summary>Access to the unchanged embedded KaTeX fonts, licensed separately under OFL-1.1.</summary>
public static class MathFontCatalog
{
    /// <summary>Number of embedded font faces.</summary>
    public static int Count => FontData.Fonts.Length;
    /// <summary>Returns the stable locked face name for a zero-based index.</summary>
    public static string GetName(int fontId) => FontData.Fonts[fontId].Name;
    /// <summary>Returns the original resource file name for a zero-based index.</summary>
    public static string GetResourceFileName(int fontId) => FontData.Fonts[fontId].ResourceFileName;
    /// <summary>Opens an independently owned read-only font stream. The caller disposes the stream.</summary>
    public static Stream OpenFont(int fontId) => typeof(MathFontCatalog).Assembly.GetManifestResourceStream("VelociTex.Core.Resources.Fonts." + GetResourceFileName(fontId))
        ?? throw new InvalidOperationException("Embedded font resource is missing.");
}

internal enum NodeKind { Row, Symbol, Script, Fraction, Radical, Style, Font, Text, Space, Operator, Delimiter, Delimited, Accent, Matrix, MatrixRow, MatrixCell }
internal struct SemanticNode
{
    internal NodeKind Kind;
    internal SourceRange Range;
    internal int FirstChild, NextSibling, SecondChild, ThirdChild, CodePoint;
    internal MathAtom Atom;
    internal MathFont Font;
    internal MathFont SymbolFont;
    internal MathStyle Style;
    internal bool TextMode;
    internal double Value;
    internal string? Text, Alignment;
    internal int LeftDelimiter, RightDelimiter, ColumnCount, RuleAbove, RuleBelow;
    internal bool HasBar, Limits, AlwaysLimits, NeverLimits;
    // True for upstream mathord; textord remains upright by default even in math mode.
    internal bool IsMathOrd;
    internal SemanticNode(NodeKind kind, SourceRange range)
    {
        this = default; Kind = kind; Range = range;
        FirstChild = NextSibling = SecondChild = ThirdChild = -1;
        Style = MathStyle.Text;
        HasBar = true;
    }
}

internal readonly record struct LayoutOutput(MathGlyph[] Glyphs, MathRule[] Rules, MathPath[] Paths, MathSize LogicalSize, double Baseline, MathBounds InkBounds, MathTextGlyph[] TextGlyphs);
