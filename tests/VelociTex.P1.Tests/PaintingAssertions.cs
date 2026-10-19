using System.Xml.Linq;
using VelociTex;
using VelociTex.Rendering.Direct2D;

internal static class PaintingAssertions
{
    internal static int Run(WindowsMathRenderer renderer,string output)
    {
        int count=0;void Check(bool value,string name){count++;if(!value)throw new InvalidOperationException("Painting: "+name);}
        MathEngine engine=new();using MathWorkspace workspace=new();MathOptions options=new();
        MathDocument Compile(string source){if(!engine.TryCompile(source,in options,workspace,default,out var d,out var error))throw new InvalidOperationException(source+error);return d!;}
        var phantom=Compile("\\phantom{xyz}");
        Check(phantom.Glyphs.Length==3&&phantom.PaintCommands.Length==0&&phantom.PaintStates.Length>0&&phantom.InkBounds.Width==0,"hidden provenance and empty ordered paint");
        using MemoryStream svg=new();renderer.WriteSvg(phantom,svg);svg.Position=0;var doc=XDocument.Load(svg);XNamespace ns="http://www.w3.org/2000/svg";
        Check(!doc.Descendants(ns+"use").Any(),"fully hidden SVG emits no glyph uses");
        string directory=Path.Combine(output,"painting");Directory.CreateDirectory(directory);
        renderer.WritePng(phantom,Path.Combine(directory,"hidden.png"));renderer.WriteSvg(phantom,Path.Combine(directory,"hidden.svg"));
        var colored=Compile("\\colorbox{yellow}{$\\textcolor{red}{x}$}");
        var first=colored.PaintCommands.Span[0];
        Check(first.Kind==MathPaintKind.Rule&&colored.PaintStates.Span[first.StateIndex].Color==new MathColor(255,255,0),"background is first yellow paint");
        var last=colored.PaintCommands.Span[^1];
        Check(last.Kind==MathPaintKind.Glyph&&colored.PaintStates.Span[last.StateIndex].Color==new MathColor(255,0,0),"foreground is later red paint");
        var reflected=Compile("\\reflectbox{abc}");
        Check(reflected.PaintCommands.Span.ToArray().Any(c=>reflected.PaintStates.Span[c.StateIndex].Transform.M11<0),"retained reflection state");
        var accented=Compile("\\hat{\\textcolor{red}{x}}");
        Check(accented.PaintCommands.Span.Length>=2,"body and accent both retained");
        foreach(var (name,value) in new[]{("colored",colored),("reflected",reflected),("accented",accented)})
        {renderer.WritePng(value,Path.Combine(directory,name+".png"));renderer.WriteSvg(value,Path.Combine(directory,name+".svg"));File.WriteAllText(Path.Combine(directory,name+".mathml"),MathMlWriter.Write(value));}
        return count;
    }
}
