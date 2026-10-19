using System.Xml.Linq;
using VelociTex;
using VelociTex.Rendering.Direct2D;

internal static class PresentationAssertions
{
    internal static int Run(WindowsMathRenderer renderer,string output)
    {
        int checks=0;void Check(bool value,string message){checks++;if(!value)throw new InvalidOperationException("Presentation: "+message);}
        MathEngine engine=new();using MathWorkspace workspace=new();
        var settings=new MathPresentationOptions{Compilation=new(){TextShaper=new WindowsMathTextShaper()},ErrorColor=new(18,52,86)};
        var success=engine.CompileForPresentation("x+1",in settings,workspace);
        Check(success.Document is not null&&!success.Diagnostic.IsError&&!success.IsErrorFallback,"valid math");
        const string bad="\\undefinedRegistration<&>";
        var error=engine.CompileForPresentation(bad,in settings,workspace);
        Check(error.Document is not null&&error.IsErrorFallback&&error.Source==bad&&error.Diagnostic.Code==MathDiagnosticCode.UnsupportedCommand,"literal original diagnostic");
        var xml=XDocument.Parse(MathMlWriter.Write(error.Document!));XNamespace ns="http://www.w3.org/1998/Math/MathML";
        Check(xml.Descendants(ns+"annotation").Single().Value==bad,"source annotation exact");
        Check(string.Concat(xml.Descendants(ns+"mtext").Select(e=>e.Value))==bad,"literal text remains literal XML");
        Check(error.Document!.PaintStates.Span.ToArray().Any(s=>s.Color==settings.ErrorColor),"explicit error color");
        var directory=Path.Combine(output,"presentation");Directory.CreateDirectory(directory);
        renderer.WritePng(error.Document,Path.Combine(directory,"error.png"));renderer.WriteSvg(error.Document,Path.Combine(directory,"error.svg"));File.WriteAllText(Path.Combine(directory,"error.mathml"),MathMlWriter.Write(error.Document));
        var noFallback=settings with{RenderErrors=false};
        Check(engine.CompileForPresentation(bad,in noFallback,workspace).Document is null,"error facade disabled");
        var limited=settings with{Compilation=settings.Compilation with{MaxInputLength=4}};
        Check(engine.CompileForPresentation(bad,in limited,workspace).Diagnostic.Code==MathDiagnosticCode.InputLimit,"limits never retried");
        using CancellationTokenSource cancellation=new();cancellation.Cancel();
        Check(engine.CompileForPresentation(bad,in settings,workspace,cancellation.Token).Diagnostic.Code==MathDiagnosticCode.Cancelled,"cancellation never retried");
        Check(engine.TryCompile("x",new MathOptions(),workspace,default,out _,out _),"workspace recovery");
        return checks;
    }
}
