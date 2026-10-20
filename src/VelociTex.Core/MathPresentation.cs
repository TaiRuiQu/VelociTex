using System.Text;

namespace VelociTex;

/// <summary>Host presentation settings for successful formulas and readable original-source errors.</summary>
public readonly record struct MathPresentationOptions
{
    /// <summary>Creates bounded formula settings and the locked default error foreground.</summary>
    public MathPresentationOptions() { }
    /// <summary>Complete compilation settings, including a caller-selected fallback text shaper.</summary>
    public MathOptions Compilation { get; init; } = new();
    /// <summary>When true, ordinary syntax failures may produce a literal original-source drawing result.</summary>
    public bool RenderErrors { get; init; } = true;
    /// <summary>Explicit sRGB error foreground. Defaults to KaTeX's #cc0000.</summary>
    public MathColor ErrorColor { get; init; } = new(204,0,0);
}

/// <summary>Presentation outcome retaining the complete source and its original structured diagnostic.</summary>
/// <param name="Source">Complete input without host delimiters.</param>
/// <param name="Document">A successful formula or literal error result; null when no bounded drawing result is available.</param>
/// <param name="Diagnostic">Original formula failure; None for successful mathematical compilation.</param>
/// <param name="IsErrorFallback">True only when Document draws literal original text instead of interpreting formula syntax.</param>
public readonly record struct MathPresentationResult(string Source,MathDocument? Document,MathDiagnostic Diagnostic,bool IsErrorFallback);

public sealed partial class MathEngine
{
    /// <summary>Compiles a complete formula and optionally renders ordinary syntax errors as colored literal source.</summary>
    public MathPresentationResult CompileForPresentation(string source,in MathPresentationOptions settings,MathWorkspace workspace,CancellationToken cancellationToken=default)
    {
        var options=settings.Compilation;
        CompileNative(source,in options,workspace,cancellationToken,true,settings,out var document,out var diagnostic,out bool fallback);
        return new(source,document,diagnostic,fallback);
    }
}
