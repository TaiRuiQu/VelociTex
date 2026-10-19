namespace VelociTex;

/// <summary>Stateless, thread-safe formula compiler. Use a distinct workspace per concurrent call.</summary>
public sealed partial class MathEngine
{
    /// <summary>Compiles with bounded defaults and a temporary isolated workspace. Returns null output and a diagnostic on ordinary failure.</summary>
    public bool TryCompile(string latex, out MathDocument? document, out MathDiagnostic diagnostic)
        => TryCompile(latex, new MathOptions(), new MathWorkspace(), default, out document, out diagnostic);

    /// <summary>Validates, expands, parses, shapes and lays out a complete formula synchronously.</summary>
    /// <remarks>Use one workspace per concurrent call. A result never borrows workspace buffers.
    /// Errors and cancellation return false and roll back session changes. Null arguments throw.
    /// Unexpected exceptions from caller-supplied shaping providers propagate after cleanup.</remarks>
    public bool TryCompile(string latex, in MathOptions options, MathWorkspace workspace, CancellationToken cancellationToken, out MathDocument? document, out MathDiagnostic diagnostic)
    {
        ArgumentNullException.ThrowIfNull(latex); ArgumentNullException.ThrowIfNull(workspace);
        document = null; diagnostic = default;
        if (!Valid(options)) { diagnostic = new(MathDiagnosticCode.InvalidOptions, new(0, 0), "Options exceed supported resource or font-size bounds."); return false; }
        if (latex.Length > options.MaxInputLength) { diagnostic = new(MathDiagnosticCode.InputLimit, new(options.MaxInputLength, latex.Length - options.MaxInputLength), "Input length budget exceeded."); return false; }
        if (!workspace.Enter()) { diagnostic = new(MathDiagnosticCode.WorkspaceInUse, new(0, 0), "Workspace is already in use."); return false; }
        var sessionEntered = false;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (options.Session is not null && !(sessionEntered = options.Session.Enter()))
            { diagnostic = new(MathDiagnosticCode.SessionInUse, new(0, 0), "Macro session is already in use."); return false; }
            MacroExpander.Expand(latex, options, workspace, cancellationToken);
            var root = MathParser.Parse(latex, options, workspace, cancellationToken);
            var nodes = workspace.Nodes.ToArray();
            var layout = MathLayout.Build(nodes, root, options, cancellationToken);
            cancellationToken.ThrowIfCancellationRequested();
            document = new(latex, options, nodes, root, layout, workspace.Warnings.ToArray());
            options.Session?.Commit(workspace.Definitions);
            return true;
        }
        catch (CompileFailure failure) { diagnostic = failure.Diagnostic; return false; }
        catch (OperationCanceledException) { diagnostic = new(MathDiagnosticCode.Cancelled, new(0, latex.Length), "Compilation was cancelled."); return false; }
        finally { if (sessionEntered) options.Session!.Leave(); workspace.Leave(); }
    }

    /// <summary>Compiles directly to presentation MathML without layout, shaping or drawing-result allocation.</summary>
    /// <remarks>This is the MathML-only path for export and server use. It performs the same validation,
    /// macro expansion, parsing and strict-mode handling as <c>TryCompile</c>, but it deliberately
    /// skips layout and drawing geometry because presentation MathML does not require them. The returned
    /// warnings are independently owned. A caller that also needs a drawable document should use
    /// <c>TryCompile</c> and <see cref="MathMlWriter.Write(MathDocument,bool)"/>.</remarks>
    public bool TryCompileToMathMl(string latex, in MathOptions options, MathWorkspace workspace, CancellationToken cancellationToken,
        out string? mathml, out MathWarning[] warnings, out MathDiagnostic diagnostic)
    {
        ArgumentNullException.ThrowIfNull(latex); ArgumentNullException.ThrowIfNull(workspace);
        mathml = null; warnings = []; diagnostic = default;
        if (!Valid(options)) { diagnostic = new(MathDiagnosticCode.InvalidOptions, new(0, 0), "Options exceed supported resource or font-size bounds."); return false; }
        if (latex.Length > options.MaxInputLength) { diagnostic = new(MathDiagnosticCode.InputLimit, new(options.MaxInputLength, latex.Length - options.MaxInputLength), "Input length budget exceeded."); return false; }
        if (!workspace.Enter()) { diagnostic = new(MathDiagnosticCode.WorkspaceInUse, new(0, 0), "Workspace is already in use."); return false; }
        var sessionEntered = false;
        try
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (options.Session is not null && !(sessionEntered = options.Session.Enter()))
            { diagnostic = new(MathDiagnosticCode.SessionInUse, new(0, 0), "Macro session is already in use."); return false; }
            MacroExpander.Expand(latex, options, workspace, cancellationToken);
            var root = MathParser.Parse(latex, options, workspace, cancellationToken);
            var nodes = workspace.Nodes.ToArray();
            mathml = MathMlWriter.Write(latex, options, nodes, root);
            warnings = workspace.Warnings.ToArray();
            options.Session?.Commit(workspace.Definitions);
            return true;
        }
        catch (CompileFailure failure) { diagnostic = failure.Diagnostic; return false; }
        catch (OperationCanceledException) { diagnostic = new(MathDiagnosticCode.Cancelled, new(0, latex.Length), "Compilation was cancelled."); return false; }
        finally { if (sessionEntered) options.Session!.Leave(); workspace.Leave(); }
    }

    private static bool Valid(in MathOptions o) => double.IsFinite(o.FontSize) && o.FontSize is > 0 and <= 1024 &&        o.MaxInputLength is > 0 and <= 1_048_576 && o.MaxNodes is > 0 and <= 262_144 && o.MaxNesting is > 0 and <= 1024 &&
        o.MaxExpansion is > 0 and <= 1_000_000 && o.MaxTokens is > 0 and <= 1_048_576 &&
        !double.IsNaN(o.MaxSize) && o.MaxSize >= 0 && double.IsFinite(o.MinRuleThickness) && o.MinRuleThickness >= 0 &&
        double.IsFinite(o.DisplayWidth) && o.DisplayWidth is >= 0 and <= 32768 && o.EquationNumber is > 0 and <= 1_000_000 &&
        Enum.IsDefined(o.StrictMode);
}
