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
    /// <remarks>This facade maps host error presentation independently from TryCompile's stable
    /// false/diagnostic contract. Resource failures, cancellation, unavailable glyphs and busy
    /// workspaces never trigger another unbounded attempt. Literal fallback never expands macros
    /// or interprets commands, and its dimensions, fonts and drawing allocations are completed
    /// synchronously. The original formula diagnostic is retained even when fallback succeeds.</remarks>
    public MathPresentationResult CompileForPresentation(string source,in MathPresentationOptions settings,MathWorkspace workspace,CancellationToken cancellationToken=default)
    {
        var options=settings.Compilation;
        if(TryCompile(source,in options,workspace,cancellationToken,out var document,out var diagnostic))return new(source,document,diagnostic,false);
        if(!settings.RenderErrors||diagnostic.Code is MathDiagnosticCode.InvalidOptions or MathDiagnosticCode.InputLimit or MathDiagnosticCode.NodeLimit or MathDiagnosticCode.TokenLimit or MathDiagnosticCode.NestingLimit or MathDiagnosticCode.ExpansionLimit or MathDiagnosticCode.Cancelled or MathDiagnosticCode.WorkspaceInUse or MathDiagnosticCode.SessionInUse or MathDiagnosticCode.UnknownSymbol)
            return new(source,null,diagnostic,false);
        if(!workspace.Enter())return new(source,null,diagnostic,false);
        try
        {
            var nodes=workspace.Nodes;
            nodes.Add(new(NodeKind.Row,new(0,source.Length)){FirstChild=1});
            string errorInk=$"#{settings.ErrorColor.R:x2}{settings.ErrorColor.G:x2}{settings.ErrorColor.B:x2}"+(settings.ErrorColor.A==255?string.Empty:$"{settings.ErrorColor.A:x2}");
            nodes.Add(new(NodeKind.Color,new(0,source.Length)){FirstChild=2,Text2=errorInk});
            nodes.Add(new(NodeKind.Text,new(0,source.Length)){FirstChild=3,TextMode=true,Font=MathFont.MathRoman,Text="\\text"});
            nodes.Add(new(NodeKind.Row,new(0,source.Length)){TextMode=true,Font=MathFont.MathRoman});
            int previous=-1,position=0;
            foreach(var rune in source.EnumerateRunes())
            {
                cancellationToken.ThrowIfCancellationRequested();
                if(nodes.Count>=options.MaxNodes)return new(source,null,diagnostic,false);
                int id=nodes.Count,codePoint=rune.Value;
                // Control whitespace remains readable and bounded without injecting
                // markup or formula commands. Original source remains in Source and annotation.
                if(codePoint is '\r' or '\n' or '\t')codePoint=' ';
                nodes.Add(new(NodeKind.Symbol,new(position,rune.Utf16SequenceLength)){CodePoint=codePoint,TextMode=true,Font=MathFont.MathRoman});
                if(previous<0){var row=nodes[3];row.FirstChild=id;nodes[3]=row;}else{var before=nodes[previous];before.NextSibling=id;nodes[previous]=before;}
                previous=id;position+=rune.Utf16SequenceLength;
            }
            var owned=nodes.ToArray();var layout=MathLayout.Build(owned,0,in options,cancellationToken);
            document=new(source,in options,owned,0,layout);
            return new(source,document,diagnostic,true);
        }
        catch(CompileFailure){return new(source,null,diagnostic,false);}
        catch(OperationCanceledException){return new(source,null,new(MathDiagnosticCode.Cancelled,new(0,source.Length),"Compilation was cancelled."),false);}
        finally{workspace.Leave();}
    }
}
