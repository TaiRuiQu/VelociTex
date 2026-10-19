using System.Runtime.CompilerServices;

[assembly: InternalsVisibleTo("VelociTex.P1.Tests")]
[assembly: InternalsVisibleTo("VelociTex.Native.Probe")]

namespace VelociTex;

public readonly record struct SourceRange(int Start, int Length);
public readonly record struct MathSize(double Width, double Height);
public readonly record struct MathBounds(double X, double Y, double Width, double Height);
public readonly record struct MathGlyph(int FontId, ushort GlyphId, int CodePoint, double X, double Y, double FontSize);
public readonly record struct MathRule(double X, double Y, double Width, double Height);
public enum MathPathOperation { Move, Line, Cubic, Close }
public readonly record struct MathPathSegment(MathPathOperation Operation, double X1, double Y1, double X2 = 0, double Y2 = 0, double X3 = 0, double Y3 = 0);
public readonly record struct MathPath(ReadOnlyMemory<MathPathSegment> Segments, double X, double Y, double ScaleX, double ScaleY, MathBounds ClipBounds);
public enum MathAtom { Ord, Op, Bin, Rel, Open, Close, Punct, Inner }
public enum MathStyle { Display, Text, Script, ScriptScript }
public enum MathFont { Default, Main, MathItalic, MathRoman, MathBold, MathBoldItalic, MathSans, MathSansBold, MathSansItalic, MathTypewriter, AMS, Caligraphic, Fraktur, Script, MathNormal }

/// <summary>Bounded compilation settings; all dimensions are device-independent pixels.</summary>
public readonly record struct MathOptions
{
    public MathOptions() { }
    public double FontSize { get; init; } = 24;
    public bool DisplayMode { get; init; }
    public int MaxInputLength { get; init; } = 32_768;
    public int MaxNodes { get; init; } = 65_536;
    public int MaxNesting { get; init; } = 128;
    public int MaxExpansion { get; init; } = 1_000;
    public int MaxTokens { get; init; } = 65_536;
    public IReadOnlyDictionary<string, string>? Macros { get; init; }
    public MacroSession? Session { get; init; }
}

public enum MathDiagnosticCode
{
    None, InvalidOptions, InputLimit, NodeLimit, NestingLimit, ExpansionLimit, TokenLimit,
    UnexpectedToken, MissingArgument, UnclosedGroup, UnexpectedGroupEnd, DoubleSuperscript,
    DoubleSubscript, UnsupportedCommand, UnknownSymbol, InvalidMacroDefinition, WorkspaceInUse, SessionInUse, Cancelled,
}

public readonly record struct MathDiagnostic(MathDiagnosticCode Code, SourceRange Range, string Message)
{
    public bool IsError => Code != MathDiagnosticCode.None;
}

/// <summary>Immutable, independently owned output. Glyph origins and paths use logical-box coordinates.</summary>
public sealed class MathDocument
{
    internal MathDocument(string source, in MathOptions options, SemanticNode[] nodes, int root, LayoutOutput layout)
    {
        Source = source; Options = options with { Macros = null, Session = null }; Nodes = nodes; Root = root;
        LogicalSize = layout.LogicalSize; Baseline = layout.Baseline; InkBounds = layout.InkBounds;
        Glyphs = layout.Glyphs; Rules = layout.Rules; Paths = layout.Paths;
    }
    public string Source { get; }
    public MathOptions Options { get; }
    public MathSize LogicalSize { get; }
    public double Baseline { get; }
    public double BaselineDip => Baseline;
    public MathBounds InkBounds { get; }
    public ReadOnlyMemory<MathGlyph> Glyphs { get; }
    public ReadOnlyMemory<MathRule> Rules { get; }
    public ReadOnlyMemory<MathPath> Paths { get; }
    internal SemanticNode[] Nodes { get; }
    internal int Root { get; }
}

public static class MathFontCatalog
{
    public static int Count => FontData.Fonts.Length;
    public static string GetName(int fontId) => FontData.Fonts[fontId].Name;
    public static string GetResourceFileName(int fontId) => FontData.Fonts[fontId].ResourceFileName;
    public static Stream OpenFont(int fontId) => typeof(MathFontCatalog).Assembly.GetManifestResourceStream("VelociTex.Core.Resources.Fonts." + GetResourceFileName(fontId))
        ?? throw new InvalidOperationException("Embedded font resource is missing.");
}

internal enum NodeKind { Row, Symbol, Script, Fraction, Radical, Style, Font, Text, Space }
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
    // True for upstream mathord; textord remains upright by default even in math mode.
    internal bool IsMathOrd;
    internal SemanticNode(NodeKind kind, SourceRange range)
    {
        this = default; Kind = kind; Range = range;
        FirstChild = NextSibling = SecondChild = ThirdChild = -1;
        Style = MathStyle.Text;
    }
}

internal readonly record struct LayoutOutput(MathGlyph[] Glyphs, MathRule[] Rules, MathPath[] Paths, MathSize LogicalSize, double Baseline, MathBounds InkBounds);
