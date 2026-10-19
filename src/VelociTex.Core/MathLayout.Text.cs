using System.Text;

namespace VelociTex;

internal static partial class MathLayout
{
    private static void TryShapeTextRow(SemanticNode[] nodes,int id,LayoutContext context,in MathOptions options,MathColor?[]colors,Box[]boxes,double[]x)
    {
        if(options.TextShaper is not IMathTextRunShaper shaper||nodes[id].FirstChild<0)return;
        double correction=0;
        for(int child=nodes[id].FirstChild;child>=0;)
        {
            ref readonly SemanticNode node=ref nodes[child];
            x[child]+=correction;
            if(node.Kind!=NodeKind.Symbol||!node.TextMode||node.OriginalCodePoint>0||!Contextual(node.CodePoint)){child=node.NextSibling;continue;}
            MathFont font=node.Font;MathColor?color=colors[child];int next=node.NextSibling;
            var text=new StringBuilder();text.Append(char.ConvertFromUtf32(node.CodePoint));
            double previousAdvance=boxes[child].Width;
            while(next>=0&&nodes[next].Kind==NodeKind.Symbol&&nodes[next].TextMode&&nodes[next].OriginalCodePoint==0&&nodes[next].Font==font&&colors[next]==color&&Contextual(nodes[next].CodePoint))
            {text.Append(char.ConvertFromUtf32(nodes[next].CodePoint));previousAdvance+=boxes[next].Width;next=nodes[next].NextSibling;}
            string value=text.ToString();
            if(!shaper.TryShapeRun(value,font,out MathTextRunShape shape)||!ValidTextRun(shape,options.MaxInputLength*16d))
                throw new CompileFailure(new(MathDiagnosticCode.UnknownSymbol,node.Range,"The text run could not be shaped by the configured platform provider."));
            ref Box first=ref boxes[child];first.Width=shape.Advance*context.Scale;first.TextRunShaped=true;first.TextRunShape=shape;first.RunText=value;
            for(int consumed=nodes[child].NextSibling;consumed>=0&&consumed!=next;consumed=nodes[consumed].NextSibling){boxes[consumed].TextShaped=false;boxes[consumed].TextRunShaped=false;boxes[consumed].Symbol=false;boxes[consumed].Width=0;}
            correction+=shape.Advance*context.Scale-previousAdvance;child=next;
        }
        boxes[id].Width+=correction;
    }
    private static bool Contextual(int cp)=>cp is >=0x0530 and <=0x08ff or >=0x0900 and <=0x10ff or >=0x1100 and <=0x11ff or >=0xa960 and <=0xa97f or >=0xd7b0 and <=0xd7ff or >=0x0300 and <=0x036f or 0x200c or 0x200d;
    private static bool ValidTextRun(in MathTextRunShape shape,double maximumAdvance) => double.IsFinite(shape.Advance)&&shape.Advance>=0&&shape.Advance<=maximumAdvance&&
        double.IsFinite(shape.InkBounds.X)&&double.IsFinite(shape.InkBounds.Y)&&double.IsFinite(shape.InkBounds.Width)&&double.IsFinite(shape.InkBounds.Height)&&shape.InkBounds.Width>=0&&shape.InkBounds.Height>=0&&
        (!shape.Paths.IsEmpty||shape.InkBounds.Width==0&&shape.InkBounds.Height==0);
}
