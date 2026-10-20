using VelociTex;

namespace VelociTex.NativeMath;

/// <summary>MathML compiler that tries the C++ fast path and falls back to the full managed engine.</summary>
public static class FastMathEngine
{
    private static readonly MathEngine Engine = new();

    /// <summary>Compiles a formula to presentation MathML.</summary>
    /// <remarks>The native path is used only for default options without macros, a macro session,
    /// a text shaper or a non-default font size. Unsupported syntax and unsupported options fall
    /// back to <see cref="MathEngine.TryCompileToMathMl"/>, so the result is never a partial AST.</remarks>
    public static bool TryCompileToMathMl(string latex, in MathOptions options, out string? mathml, out MathWarning[] warnings, out MathDiagnostic diagnostic)
    {
        ArgumentNullException.ThrowIfNull(latex);
        if (options == new MathOptions { DisplayMode = options.DisplayMode } &&
            NativeMathMl.TryRender(latex, options.DisplayMode, out string native))
        {
            mathml = native; warnings = []; diagnostic = default;
            return true;
        }
        var workspace = new MathWorkspace();
        return Engine.TryCompileToMathMl(latex, in options, workspace, default, out mathml, out warnings, out diagnostic);
    }
}
