namespace VelociTex;

internal static partial class MathLayout
{
    private static Box BuildBox(SemanticNode[] nodes, in SemanticNode node, LayoutContext context, in MathOptions options, Box[] boxes, double[] x, double[] y, Box? composed = null)
    {
        Box body = composed ?? boxes[node.FirstChild];
        Box result = body; result.FirstDecoration = 0; result.Symbol = false; result.TextShaped = false;
        x[node.FirstChild] = y[node.FirstChild] = 0;
        switch (node.Text)
        {
            case "\\mathllap": x[node.FirstChild] = -body.Width; result.Width = 0; result.Atom = MathAtom.Ord; break;
            case "\\mathclap": x[node.FirstChild] = -body.Width / 2; result.Width = 0; result.Atom = MathAtom.Ord; break;
            case "\\mathrlap": case "\\vphantom": result.Width = 0; result.Atom = MathAtom.Ord; break;
            case "\\smash":
                string selection = node.Alignment ?? "tb";
                bool valid = true; foreach (char c in selection) if (c is not ('t' or 'b')) { valid = false; break; }
                if (valid && selection.Contains('t')) result.Height = 0;
                if (valid && selection.Contains('b')) result.Depth = 0;
                result.Atom = MathAtom.Ord; result.Character = false; break;
            case "\\raisebox":
                double shift = Length(node.Length1, context, options);
                y[node.FirstChild] = -shift; result.Height += shift; result.Depth -= shift; result.Character = false; result.NoAtom=true; break;
            case "\\vcenter":
                double dy = (body.Height - body.Depth) / 2 - MathConstants.axisHeight(context.Metrics) * context.Scale;
                y[node.FirstChild] = dy; result.Height -= dy; result.Depth += dy; result.Character = false; result.NoAtom=true; break;
            case "\\pmb": result.Atom = node.Atom; result.Character = false; break;
            case "\\reflectbox": case "\\mathreflectbox": result.Atom = MathAtom.Ord; result.Character = false; break;
            case "cdleft": case "cdright":
                result.Width = result.Height = result.Depth = 0;
                double labelScale = context.WithStyle(SupStyles[context.Style]).Scale;
                x[node.FirstChild] = node.Text == "cdleft" ? -body.Width - .3 * labelScale : .3 * labelScale;
                // The absolute label's bottom is anchored to the vertical
                // arrow parent's depth (set in cdparent below). CSS bottom
                // and its depth correction use the script-sized em.
                double lineDescent=(GlyphInkData.FontDescents[8]+.1)*labelScale;
                y[node.FirstChild] = body.Depth * labelScale / context.Scale - .8 * labelScale-lineDescent; break;
            case "cdparent":
                for (int child = nodes[node.FirstChild].FirstChild; child >= 0; child = nodes[child].NextSibling)
                    if (nodes[child].Kind == NodeKind.Box && nodes[child].Text is "cdleft" or "cdright") {x[child] = body.Width / 2;y[child]=body.Depth;}
                break;
        }
        return result;
    }

    private static Box BuildVerb(in SemanticNode node, LayoutContext context, List<Decoration> decorations)
    {
        Box result = new() { Atom = MathAtom.Ord };
        LayoutContext textContext = context.WithStyle(context.Style < 2 ? (byte)0 : (byte)2);
        double scale = textContext.Scale;
        string text = (node.Text2 ?? node.Text ?? "").Replace(' ', node.Value != 0 ? '\u2423' : '\u00a0');
        foreach (char c in text)
        {
            int cp = c == 160 ? 32 : c;
            if (!Find(19, cp, out int glyph, out TexGlyphMetric metric))
                throw new CompileFailure(new(MathDiagnosticCode.UnknownSymbol, node.Range, "The verbatim character has no locked typewriter glyph."));
            AddGlyph(ref result, 19, glyph, cp, result.Width, 0, scale, decorations);
            result.Width += FontData.Glyphs[glyph].AdvanceUnits / (double)FontData.Fonts[19].UnitsPerEm * scale;
            result.Height = Math.Max(result.Height, metric.Height * scale); result.Depth = Math.Max(result.Depth, metric.Depth * scale);
        }
        return result;
    }

    private static Box BuildEnclose(SemanticNode[] nodes, in SemanticNode node, LayoutContext context, in MathOptions options, Box[] boxes, double[] x, double[] y, List<Decoration> decorations)
    {
        Box body = boxes[node.FirstChild]; double scale = context.Scale;
        double rule = Math.Max(MathConstants.defaultRuleThickness(context.Metrics), options.MinRuleThickness) * scale;
        string label = node.Text ?? "";
        bool cancel = label is "\\cancel" or "\\bcancel" or "\\xcancel";
        Box result = new() { Width = body.Width, Height = body.Height, Depth = body.Depth, Atom = MathAtom.Ord, DecorationsAfter = node.Text2 is null };
        x[node.FirstChild] = y[node.FirstChild] = 0;
        if (cancel)
        {
            double padding = body.Character ? 0 : .2 * scale;
            double vertical = body.Character ? .2 * scale : 0;
            double left = -padding, top = -body.Height - vertical, width = body.Width + 2 * padding, height = body.Height + body.Depth + 2 * vertical;
            if (label is "\\cancel" or "\\xcancel") AddStroke(ref result, 0, height, width, 0, .046 * scale, decorations,left,top,options.FontSize);
            if (label is "\\bcancel" or "\\xcancel") AddStroke(ref result, 0, 0, width, height, .046 * scale, decorations,left,top,options.FontSize);
            return result;
        }
        if (label == "\\sout")
        { AddRule(ref result, 0, -.5 * MathConstants.xHeight(context.Metrics) * scale - rule, body.Width, rule, decorations); return result; }
        if (label == "\\phase")
        {
            double lineWeight = Length(new(.6, TexUnit.Pt), context, options), clearance = Length(new(.35, TexUnit.Ex), context, options);
            double height = body.Height + body.Depth + lineWeight + clearance, padding = height / 2 + lineWeight;
            result.Width += padding; x[node.FirstChild] = padding;
            double baseScale = Scale(context.Style), viewHeight = Math.Floor(1000 * height / baseScale);
            MathPathSegment[] phase =
            [new(MathPathOperation.Move,400000,viewHeight),new(MathPathOperation.Line,0,viewHeight),new(MathPathOperation.Line,viewHeight/2,0),new(MathPathOperation.Line,viewHeight/2+65,45),new(MathPathOperation.Line,145,viewHeight-80),new(MathPathOperation.Line,400000,viewHeight-80),new(MathPathOperation.Close,0,0)];
            double pathScale = Math.Max(result.Width/400000,height/viewHeight);
            AddPath(ref result,phase,0,-body.Height,pathScale,pathScale,new(0,-body.Height,result.Width,height),decorations);
            result.Depth += lineWeight + clearance; return result;
        }
        bool angl = label == "\\angl", colorBox = label == "\\colorbox", framed = label is "\\fbox" or "\\fcolorbox" or "\\boxed";
        double thickness = framed ? Math.Max(MathConstants.fboxrule(context.Metrics), options.MinRuleThickness) * scale : rule;
        double horizontal = angl ? .03889 * scale : .3 * scale;
        double topPadding = angl ? 4 * rule : .3 * scale + (colorBox ? 0 : thickness);
        double bottomPadding = angl ? Math.Max(0, .25 * scale - body.Depth) : topPadding;
        result.Width += horizontal * 2; result.Height += topPadding; result.Depth += bottomPadding;
        x[node.FirstChild] = horizontal;
        MathColor? border = MathColorUtilities.TryParse(node.Text3, out MathColor borderColor) ? borderColor : null;
        if (framed || angl)
        {
            double total = result.Height + result.Depth;
            AddRule(ref result, 0, -result.Height, result.Width, thickness, decorations, color: border);
            AddRule(ref result, result.Width - thickness, -result.Height, thickness, total, decorations, vertical: true, color: border);
            if (!angl)
            {
                AddRule(ref result, 0, result.Depth - thickness, result.Width, thickness, decorations, color: border);
                AddRule(ref result, 0, -result.Height, thickness, total, decorations, vertical: true, color: border);
            }
        }
        if (MathColorUtilities.TryParse(node.Text2, out MathColor background))
            AddRule(ref result, 0, -result.Height, result.Width, result.Height + result.Depth, decorations, exact: true, color: background);
        return result;
    }

    private static void AddStroke(ref Box result, double x1, double y1, double x2, double y2, double thickness, List<Decoration> decorations,double offsetX=0,double offsetY=0,double coordinateScale=1)
    {
        double length = Math.Sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
        if (length == 0) return;
        double dx = -(y2-y1) / length * thickness / 2, dy = (x2-x1) / length * thickness / 2;
        MathPathSegment[] path =
        [new(MathPathOperation.Move,x1+dx,y1+dy),new(MathPathOperation.Line,x2+dx,y2+dy),new(MathPathOperation.Line,x2-dx,y2-dy),new(MathPathOperation.Line,x1-dx,y1-dy),new(MathPathOperation.Close,0,0)];
        if(coordinateScale!=1)for(int i=0;i<path.Length;i++)path[i]=path[i] with{X1=path[i].X1*coordinateScale,Y1=path[i].Y1*coordinateScale};
        double left = Math.Min(x1,x2)-thickness, top = Math.Min(y1,y2)-thickness;
        AddPath(ref result,path,offsetX,offsetY,1/coordinateScale,1/coordinateScale,new(left+offsetX,top+offsetY,Math.Abs(x2-x1)+2*thickness,Math.Abs(y2-y1)+2*thickness),decorations);
    }
}
