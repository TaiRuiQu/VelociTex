namespace VelociTex;

/// <summary>Stateless, thread-safe formula compiler. Use a distinct workspace per concurrent call.</summary>
public sealed class MathEngine
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
            document = new(latex, options, nodes, root, layout);
            options.Session?.Commit(workspace.Definitions);
            return true;
        }
        catch (CompileFailure failure) { diagnostic = failure.Diagnostic; return false; }
        catch (OperationCanceledException) { diagnostic = new(MathDiagnosticCode.Cancelled, new(0, latex.Length), "Compilation was cancelled."); return false; }
        finally { if (sessionEntered) options.Session!.Leave(); workspace.Leave(); }
    }

    private static bool Valid(in MathOptions o) => double.IsFinite(o.FontSize) && o.FontSize is > 0 and <= 1024 &&
        o.MaxInputLength is > 0 and <= 1_048_576 && o.MaxNodes is > 0 and <= 262_144 && o.MaxNesting is > 0 and <= 1024 &&
        o.MaxExpansion is > 0 and <= 1_000_000 && o.MaxTokens is > 0 and <= 1_048_576;
}
