namespace VelociTex;

internal static partial class MathLayout
{
    // Decorations belong to their compact semantic box. Their linked indices
    // avoid allocating a list or an object graph for every delimiter/accent.
    private enum DecorationKind { Glyph, Path, Rule }
    private struct Decoration
    {
        internal DecorationKind Kind;
        internal int Next, Font, GlyphIndex, CodePoint;
        internal double X, Y, Width, Height, ScaleX, ScaleY;
        internal MathPathSegment[]? Path;
        internal MathBounds Clip;
        internal bool Vertical, Exact, HasColor;
        internal MathColor Color;
    }

    private static Box BuildRow(SemanticNode[] nodes, int id, LayoutContext style, LayoutContext[] styles, Box[] boxes, double[] x, double[] y, int[] sequence, int[] collectStack, MathAtom[] atoms, in MathOptions options)
    {
        ref readonly SemanticNode node = ref nodes[id];
        int length = Collect(nodes, node.FirstChild, sequence, collectStack, styles);
        for (int i = 0; i < length; i++) atoms[i] = boxes[sequence[i]].Atom;
        for(int i=1;i<length;i++)
        {
            int before=sequence[i-1],current=sequence[i];
            if(nodes[before].Kind==NodeKind.Symbol&&nodes[current].Kind==NodeKind.Symbol&&nodes[before].Atom==MathAtom.Bin&&nodes[current].Atom==MathAtom.Bin&&
                nodes[before].Font==MathFont.AMS&&nodes[current].Font==MathFont.AMS&&boxes[before].Skew==boxes[current].Skew&&styles[before]==styles[current]) atoms[i]=atoms[i-1];
        }
        int previous = -1;
        for (int i = 0; i < length; i++)
        {
            if (nodes[sequence[i]].Kind == NodeKind.Space) continue;
            if(i>0 && nodes[sequence[i]].Kind==NodeKind.Symbol&&nodes[sequence[i-1]].Kind==NodeKind.Symbol&&
                nodes[sequence[i]].Atom==MathAtom.Bin&&nodes[sequence[i-1]].Atom==MathAtom.Bin&&nodes[sequence[i]].Font==MathFont.AMS&&nodes[sequence[i-1]].Font==MathFont.AMS&&
                boxes[sequence[i]].Skew==boxes[sequence[i-1]].Skew&&styles[sequence[i]]==styles[sequence[i-1]]) {atoms[i]=atoms[i-1];continue;}
            MathAtom current = atoms[i];
            if (previous >= 0 && atoms[previous] == MathAtom.Bin && CancelsBinOnRight(current)) atoms[previous] = MathAtom.Ord;
            else if (current == MathAtom.Bin && (previous < 0 || CancelsBinOnLeft(atoms[previous]))) atoms[i] = MathAtom.Ord;
            previous = i;
        }
        if (previous >= 0 && atoms[previous] == MathAtom.Bin) atoms[previous] = MathAtom.Ord;
        LayoutContext glueStyle = style;
        if (node.FirstChild >= 0 && nodes[node.FirstChild].NextSibling < 0 && nodes[node.FirstChild].Kind == NodeKind.Style)
            glueStyle = styles[node.FirstChild];
        double mu = MathConstants.quad(MetricIndex(glueStyle)) * Scale(glueStyle) / 18d;
        Box box = new() { Atom = MathAtom.Ord };
        previous = -1;
        for (int i = 0; i < length; i++)
        {
            int child = sequence[i]; ref readonly Box childBox = ref boxes[child];
            bool space = nodes[child].Kind == NodeKind.Space;
            if (previous >= 0)
            {
                int before = sequence[previous];
                // Upstream joins adjacent explicit-font symbols into one run,
                // retaining only its final italic correction. mathnormal is
                // deliberately excluded by canCombineChars.
                if (nodes[before].Kind == NodeKind.Symbol && nodes[child].Kind == NodeKind.Symbol &&
                    nodes[before].Font == nodes[child].Font && nodes[child].Font is not (MathFont.Default or MathFont.MathNormal) &&
                    nodes[before].TextMode == nodes[child].TextMode && nodes[before].IsMathOrd == nodes[child].IsMathOrd &&
                    boxes[before].Font == childBox.Font && boxes[before].Skew == childBox.Skew && atoms[previous] == MathAtom.Ord && atoms[i] == MathAtom.Ord && styles[before] == styles[child])
                    box.Width -= Math.Max(0, boxes[before].Italic);
            }
            if (!space && previous >= 0 && !boxes[sequence[previous]].NoAtom && !childBox.NoAtom) box.Width += Spacing(atoms[previous], atoms[i], styles[child] >= 4) * mu;
            x[child] = box.Width; y[child] = 0;
            box.Width += childBox.Width; box.Height = Math.Max(box.Height, childBox.Height); box.Depth = Math.Max(box.Depth, childBox.Depth);
            if (!space) previous = i;
        }
        if (length == 1 && node.Kind == NodeKind.Row) box.Character = boxes[sequence[0]].Character;
        bool hasNewLine = false;
        if (id == 0) for(int i=0;i<length;i++) if(nodes[sequence[i]].Kind==NodeKind.Space && nodes[sequence[i]].Text=="\\\\" && (!options.DisplayMode || options.StrictMode!=MathStrictMode.Error)) { hasNewLine=true; break; }
        if(hasNewLine){bool content=false;for(int i=0;i<length;i++)if(nodes[sequence[i]].Kind!=NodeKind.Space){content=true;break;}hasNewLine=content;}
        if (hasNewLine)
        {
            double maxWidth = 0, baseline = 0, previousDepth = 0; int lineStart = 0; bool firstLine = true;
            for (int end = 0; end <= length; end++)
            {
                if (end < length && !(nodes[sequence[end]].Kind == NodeKind.Space && nodes[sequence[end]].Text == "\\\\" && (!options.DisplayMode || options.StrictMode != MathStrictMode.Error))) continue;
                double h = 0,d = 0,w = 0;
                for(int i=lineStart;i<end;i++){int child=sequence[i];h=Math.Max(h,boxes[child].Height);d=Math.Max(d,boxes[child].Depth);w=Math.Max(w,x[child]+boxes[child].Width-(lineStart<end?x[sequence[lineStart]]:0));}
                if(!firstLine)baseline+=Math.Max(1.2*style.Scale,previousDepth+h);
                for(int i=lineStart;i<end;i++)y[sequence[i]]=baseline;
                if(lineStart<end){double origin=x[sequence[lineStart]];for(int i=lineStart;i<end;i++)x[sequence[i]]-=origin;}
                if(firstLine){box.Height=h;firstLine=false;} box.Depth=baseline+d; maxWidth=Math.Max(maxWidth,w);previousDepth=d;
                if(end<length)baseline+=Length(nodes[sequence[end]].Length1,style,options);
                lineStart=end+1;
            }
            box.Width=maxWidth;
        }
        return box;
    }

    private static Box BuildOperator(SemanticNode[]nodes,in SemanticNode node,bool centered, LayoutContext style, Box[] boxes, List<Decoration> decorations,Box?composed=null)
    {
        if (node.CodePoint == 0)
        {
            Box body = composed??(node.FirstChild < 0 ? default : boxes[node.FirstChild]);
            body.Atom = MathAtom.Op; body.Character = false; body.Symbol = false; body.FirstDecoration = 0;
            if(node.Text=="\\mathop"&&node.Value==0&&composed is null&&node.FirstChild>=0)
            {
                int symbol=node.FirstChild;
                while(nodes[symbol].Kind is NodeKind.Row or NodeKind.Font && nodes[symbol].FirstChild>=0&&nodes[nodes[symbol].FirstChild].NextSibling<0)symbol=nodes[symbol].FirstChild;
                if(boxes[symbol].Symbol){body.GlyphY=(body.Height-body.Depth)/2-MathConstants.axisHeight(style.Metrics)*style.Scale;body.Italic=boxes[symbol].Italic;}
            }
            return body;
        }
        int font = style < 2 && node.Text != "\\smallint" ? 16 : 15;
        int cp = node.CodePoint is 0x222f ? 0x222c : node.CodePoint is 0x2230 ? 0x222d : node.CodePoint;
        if (!Find(font, cp, out int glyphIndex, out TexGlyphMetric metric))
            throw new CompileFailure(new(MathDiagnosticCode.UnsupportedCommand, node.Range, "This operator requires a missing locked glyph."));
        double scale = Scale(style), italic = metric.ItalicCorrection * scale;
        Box result = new()
        {
            Width = (FontData.Glyphs[glyphIndex].AdvanceUnits / (double)FontData.Fonts[font].UnitsPerEm) * scale + Math.Max(0, italic),
            Height = metric.Height * scale, Depth = metric.Depth * scale, Italic = italic,
            Font = font, GlyphIndex = glyphIndex, GlyphScale = scale,
            GlyphY = ((metric.Height - metric.Depth) / 2 - MathConstants.axisHeight(MetricIndex(style))) * scale,
            Symbol = true, Atom = MathAtom.Op,
        };
        if (node.CodePoint is 0x222f or 0x2230)
        {
            result.DecorationsAfter=true;
            bool large = font == 16, triple = node.CodePoint == 0x2230;
            double width = (triple ? large ? 1.98 : 1.304 : large ? 1.472 : .957) * scale;
            double height = (large ? .659 : .499) * scale;
            double top = (large ? .08 * scale : 0) + result.GlyphY - height;
            double left = centered ? (result.Width - width) / 2 : 0;
            AddPath(ref result, MathPathCache.Get((triple ? "oiiintSize" : "oiintSize") + (large ? "2" : "1")), left, top, .001 * scale, .001 * scale, new(left, top, width, height), decorations);
        }
        return result;
    }

    private static bool UseLimits(in SemanticNode node, LayoutContext style) => node.Text=="\\operatorname"
        ? node.Text2=="star"&&(style.Style<2||node.AlwaysLimits&&!node.NeverLimits)
        : !node.NeverLimits && (node.AlwaysLimits || node.Limits && style.Style < 2);

    private static Box BuildLimits(in SemanticNode node, Box basis, Box sup, Box sub, LayoutContext style, Box[] boxes, double[] x, double[] y)
    {
        // assembleSupSub first encloses the operator in a span. Its extent
        // union clamps negative glyph depths before adding limit clearances.
        basis.Height=Math.Max(0,basis.Height);basis.Depth=Math.Max(0,basis.Depth);
        double scale = Scale(style); int m = MetricIndex(style);
        double slant = basis.Italic, shift = basis.GlyphY;
        bool hasSup = node.SecondChild >= 0, hasSub = node.ThirdChild >= 0;
        double supKern = Math.Max(MathConstants.bigOpSpacing1(m) * scale, MathConstants.bigOpSpacing3(m) * scale - sup.Depth);
        double subKern = Math.Max(MathConstants.bigOpSpacing2(m) * scale, MathConstants.bigOpSpacing4(m) * scale - sub.Height);
        double width = Math.Max(basis.Width, Math.Max(hasSup ? sup.Width + slant : 0, hasSub ? sub.Width - slant : 0));
        double spacer = hasSub && !boxes[node.ThirdChild].Character ? slant : 0;
        Box result = new() { Width = width + spacer, Height = basis.Height - shift, Depth = basis.Depth + shift, Atom = MathAtom.Op };
        x[node.FirstChild] = spacer + (width - basis.Width) / 2; y[node.FirstChild] = 0;
        if (hasSup)
        {
            x[node.SecondChild] = spacer + (width - sup.Width + slant) / 2;
            y[node.SecondChild] = -basis.Height + shift - supKern - sup.Depth;
            result.Height = Math.Max(result.Height, sup.Height - y[node.SecondChild] + MathConstants.bigOpSpacing5(m) * scale);
        }
        if (hasSub)
        {
            x[node.ThirdChild] = spacer + (width - sub.Width - slant) / 2;
            y[node.ThirdChild] = basis.Depth + shift + subKern + sub.Height;
            result.Depth = Math.Max(result.Depth, sub.Depth + y[node.ThirdChild] + MathConstants.bigOpSpacing5(m) * scale);
        }
        return result;
    }

    private static double SizeHeight(int size) => size switch { 1 => 1.2, 2 => 1.8, 3 => 2.4, _ => 3 };
    private static bool AlwaysStacks(int cp) => cp is '|' or 0x2223 or 0x2225 or 0x2016 or 0x2191 or 0x2193 or 0x2195 or 0x21d1 or 0x21d3 or 0x21d5 or 0x27ee or 0x27ef or 0x23b0 or 0x23b1;
    private static bool NeverStacks(int cp) => cp is 0x27e8 or 0x27e9 or '/' or '\\';

    private static Box MakeDelimiter(int codePoint, double requiredHeight, LayoutContext style, bool center, int explicitSize, MathAtom atom, List<Decoration> decorations)
    {
        if (codePoint is 0 or '.') return new() { Width = explicitSize > 0 ? 0 : .12, Atom = atom };
        int cp = codePoint switch { '<' => 0x27e8, '>' => 0x27e9, '|' => 0x2223, 0x2016 => 0x2225, _ => codePoint };
        double scale = Scale(style); int font = 8; double glyphScale = 1;
        bool stack = false;
        if (explicitSize > 0)
        { font = 14 + explicitSize; stack = AlwaysStacks(cp); }
        else
        {
            ReadOnlySpan<double> smallScales = [.5, .7, 1];
            int start = style >= 6 ? 0 : style >= 4 ? 1 : 2;
            bool selected = false;
            if (Find(8, cp, out _, out TexGlyphMetric small))
                for (int i = start; i < smallScales.Length; i++)
                    if ((small.Height + small.Depth) * smallScales[i] > requiredHeight)
                    { font = 8; glyphScale = smallScales[i]; selected = true; break; }
            if (!selected && !AlwaysStacks(cp))
                for (int size = 1; size <= 4; size++)
                    if (Find(14 + size, cp, out _, out TexGlyphMetric large) && ((large.Height + large.Depth) > requiredHeight || size == 4 && NeverStacks(cp)))
                    { font = 14 + size; selected = true; break; }
            stack = !selected;
        }
        if (stack) return StackedDelimiter(cp, requiredHeight, center ? scale : 1, atom, decorations);
        if (!Find(font, cp, out int glyphIndex, out TexGlyphMetric metric))
            throw new CompileFailure(new(MathDiagnosticCode.UnsupportedCommand, default, "The locked delimiter glyph is unavailable."));
        double paintShift = center ? (glyphScale - scale) * MathConstants.axisHeight(MetricIndex(style)) : 0;
        // centerSpan adjusts its reported box in the surrounding options'
        // em, while CSS top is painted in the sized delimiter span's em.
        // Those differ inside scripts; preserving both avoids a .0225em
        // script baseline error when a full-size delimiter is selected.
        double logicalShift = glyphScale == 0 ? 0 : paintShift * scale / glyphScale;
        return new()
        {
            Width = FontData.Glyphs[glyphIndex].AdvanceUnits / (double)FontData.Fonts[font].UnitsPerEm * glyphScale,
            Height = metric.Height * glyphScale - logicalShift, Depth = metric.Depth * glyphScale + logicalShift,
            Atom = atom, Symbol = true, Font = font, GlyphIndex = glyphIndex, GlyphScale = glyphScale, GlyphY = paintShift,
        };
    }

    private static Box StackedDelimiter(int cp, double requiredHeight, double axisScale, MathAtom atom, List<Decoration> decorations)
    {
        int font = 18, top, repeat, bottom, middle = 0; string? label = null; double pathWidth = 0;
        switch (cp)
        {
            case '(': top = 0x239b; repeat = 0x239c; bottom = 0x239d; label = "lparen"; pathWidth = .875; break;
            case ')': top = 0x239e; repeat = 0x239f; bottom = 0x23a0; label = "rparen"; pathWidth = .875; break;
            case '[': top = 0x23a1; repeat = 0x23a2; bottom = 0x23a3; label = "lbrack"; pathWidth = .667; break;
            case ']': top = 0x23a4; repeat = 0x23a5; bottom = 0x23a6; label = "rbrack"; pathWidth = .667; break;
            case 0x230a: top = repeat = 0x23a2; bottom = 0x23a3; label = "lfloor"; pathWidth = .667; break;
            case 0x230b: top = repeat = 0x23a5; bottom = 0x23a6; label = "rfloor"; pathWidth = .667; break;
            case 0x2308: top = 0x23a1; repeat = bottom = 0x23a2; label = "lceil"; pathWidth = .667; break;
            case 0x2309: top = 0x23a4; repeat = bottom = 0x23a5; label = "rceil"; pathWidth = .667; break;
            case '{': top = 0x23a7; middle = 0x23a8; repeat = 0x23aa; bottom = 0x23a9; break;
            case '}': top = 0x23ab; middle = 0x23ac; repeat = 0x23aa; bottom = 0x23ad; break;
            case 0x27ee: top = 0x23a7; repeat = 0x23aa; bottom = 0x23a9; break;
            case 0x27ef: top = 0x23ab; repeat = 0x23aa; bottom = 0x23ad; break;
            case 0x23b0: top = 0x23a7; repeat = 0x23aa; bottom = 0x23ad; break;
            case 0x23b1: top = 0x23ab; repeat = 0x23aa; bottom = 0x23a9; break;
            case 0x2223: font = 15; top = bottom = cp; repeat = 0x2223; label = "vert"; pathWidth = .333; break;
            case 0x2225: font = 15; top = bottom = cp; repeat = 0x2225; label = "doublevert"; pathWidth = .556; break;
            case 0x2191: font = 15; top = cp; repeat = bottom = 0x23d0; break;
            case 0x2193: font = 15; bottom = cp; repeat = top = 0x23d0; break;
            case 0x2195: font = 15; top = 0x2191; repeat = 0x23d0; bottom = 0x2193; break;
            case 0x21d1: font = 15; top = cp; repeat = bottom = 0x2016; break;
            case 0x21d3: font = 15; bottom = cp; repeat = top = 0x2016; break;
            case 0x21d5: font = 15; top = 0x21d1; repeat = 0x2016; bottom = 0x21d3; break;
            default: throw new CompileFailure(new(MathDiagnosticCode.UnsupportedCommand, default, "The locked stacked delimiter is not implemented."));
        }
        if (!Find(font, top, out int topGlyph, out TexGlyphMetric topMetric) || !Find(font, bottom, out int bottomGlyph, out TexGlyphMetric bottomMetric) || !Find(font, repeat, out _, out TexGlyphMetric repeatMetric))
            throw new CompileFailure(new(MathDiagnosticCode.UnknownSymbol, default, "Missing delimiter assembly metric."));
        double th = topMetric.Height + topMetric.Depth, bh = bottomMetric.Height + bottomMetric.Depth;
        double mh = 0; int middleGlyph = -1; TexGlyphMetric middleMetric = default;
        if (middle != 0 && Find(font, middle, out middleGlyph, out middleMetric)) mh = middleMetric.Height + middleMetric.Depth;
        double rh = repeatMetric.Height + repeatMetric.Depth;
        double repeatCount = Math.Max(0, Math.Ceiling((requiredHeight - th - bh - mh) / ((middle == 0 ? 1 : 2) * rh)));
        double realHeight = th + bh + mh + repeatCount * (middle == 0 ? 1 : 2) * rh;
        double depth = realHeight / 2 - .25 * axisScale, height = realHeight - depth;
        double width = pathWidth > 0 ? pathWidth : FontData.Glyphs[topGlyph].AdvanceUnits / (double)FontData.Fonts[font].UnitsPerEm;
        Box box = new() { Width = width, Height = height, Depth = depth, Atom = atom };
        if (label is not null)
        {
            double viewHeight = Math.Round(realHeight * 1000, MidpointRounding.AwayFromZero);
            AddPath(ref box, MathPathCache.TallPath(label, (long)Math.Round((realHeight - th - bh) * 1000, MidpointRounding.AwayFromZero)), 0, -height, .001, .001,
                new(0, -height, width, viewHeight / 1000), decorations);
            return box;
        }
        AddGlyph(ref box, font, bottomGlyph, bottom, 0, depth - bottomMetric.Depth, 1, decorations);
        AddGlyph(ref box, font, topGlyph, top, 0, -height + topMetric.Height, 1, decorations);
        double innerHeight = (realHeight - th - bh - mh) / (middle == 0 ? 1 : 2) + .016;
        double lowerTop = depth - bh + .008 - innerHeight;
        AddPath(ref box, MathPathCache.InnerPath(repeat, (long)Math.Round(innerHeight * 1000, MidpointRounding.AwayFromZero)), 0, lowerTop, .001, .001, new(0, lowerTop, width, innerHeight), decorations);
        if (middle != 0)
        {
            double middleBaseline = lowerTop + .008 - middleMetric.Depth;
            AddGlyph(ref box, font, middleGlyph, middle, 0, middleBaseline, 1, decorations);
            double upperTop = middleBaseline - middleMetric.Height + .008 - innerHeight;
            AddPath(ref box, MathPathCache.InnerPath(repeat, (long)Math.Round(innerHeight * 1000, MidpointRounding.AwayFromZero)), 0, upperTop, .001, .001, new(0, upperTop, width, innerHeight), decorations);
        }
        return box;
    }

    private static void AttachDelimiter(ref Box target, Box delimiter, double dx, List<Decoration> decorations)
    {
        target.Height = Math.Max(target.Height, delimiter.Height); target.Depth = Math.Max(target.Depth, delimiter.Depth);
        if (delimiter.Symbol) AddGlyph(ref target, delimiter.Font, delimiter.GlyphIndex, FontData.Glyphs[delimiter.GlyphIndex].CodePoint, dx, delimiter.GlyphY, delimiter.GlyphScale, decorations);
        for (int current = delimiter.FirstDecoration; current != 0;)
        {
            Decoration decoration = decorations[current - 1]; current = decoration.Next;
            decoration.X += dx; decoration.Clip = decoration.Clip with { X = decoration.Clip.X + dx };
            CheckDecorationBudget(decorations);
            decoration.Next = target.FirstDecoration; decorations.Add(decoration); target.FirstDecoration = decorations.Count;
        }
    }

    private static void AddGlyph(ref Box box, int font, int glyphIndex, int cp, double x, double y, double scale, List<Decoration> decorations)
    {
        CheckDecorationBudget(decorations);
        decorations.Add(new() { Kind = DecorationKind.Glyph, Next = box.FirstDecoration, Font = font, GlyphIndex = glyphIndex, CodePoint = cp, X = x, Y = y, ScaleX = scale });
        box.FirstDecoration = decorations.Count;
    }
    private static void AddPath(ref Box box, MathPathSegment[] path, double x, double y, double scaleX, double scaleY, MathBounds clip, List<Decoration> decorations)
    {
        CheckDecorationBudget(decorations);
        decorations.Add(new() { Kind = DecorationKind.Path, Next = box.FirstDecoration, Path = path, X = x, Y = y, ScaleX = scaleX, ScaleY = scaleY, Clip = clip }); box.FirstDecoration = decorations.Count;
    }
    private static void AddRule(ref Box box, double x, double y, double width, double height, List<Decoration> decorations, bool vertical = false, bool exact = false, MathColor? color = null)
    {
        CheckDecorationBudget(decorations);
        decorations.Add(new() { Kind = DecorationKind.Rule, Next = box.FirstDecoration, X = x, Y = y, Width = width, Height = height, Vertical = vertical, Exact = exact, HasColor = color.HasValue, Color = color.GetValueOrDefault() }); box.FirstDecoration = decorations.Count;
    }
    private static void CheckDecorationBudget(List<Decoration> decorations)
    {
        if (decorations.Count >= 262_144)
            throw new CompileFailure(new(MathDiagnosticCode.NodeLimit, default, "The bounded drawing-decoration budget was exceeded."));
    }

    private static Box BuildAccent(SemanticNode[] nodes, int id, LayoutContext style, in MathOptions options, Box[] boxes, double[] x, double[] y, List<Decoration> decorations)
    {
        ref readonly SemanticNode node = ref nodes[id]; Box body = node.FirstChild>=0?boxes[node.FirstChild]:default;
        double scale = Scale(style), theta = MathConstants.defaultRuleThickness(MetricIndex(style)) * scale;
        string label = node.Text ?? "";
        Box result = new() { Width = body.Width, Height = body.Height, Depth = body.Depth, Atom = MathAtom.Ord,DecorationsAfter=true };
        if(node.FirstChild>=0)x[node.FirstChild] = y[node.FirstChild] = 0;
        if (label is "\\overline" or "\\underline")
        {
            bool under = label == "\\underline";
            double line=Math.Max(theta,options.MinRuleThickness*scale);
            AddRule(ref result, 0, under ? body.Depth + 3 * theta : -body.Height - 3 * theta-line, body.Width, line, decorations);
            if (under) result.Depth += 4 * theta+line; else result.Height += 4 * theta+line;
            return result;
        }
        if (label.StartsWith("\\x", StringComparison.Ordinal) || label.StartsWith("\\\\cd", StringComparison.Ordinal))
        {
            Box lower = node.SecondChild >= 0 ? boxes[node.SecondChild] : default;
            bool cd = label.StartsWith("\\\\cd",StringComparison.Ordinal);
            StretchyInfo info = GetStretchy(label.TrimStart('\\'));
            double arrowHeight = info.Height * scale;
            double labelPad = cd ? .83334 : 1;
            result.Width = Math.Max(info.MinWidth * scale, Math.Max(node.FirstChild>=0?body.Width + labelPad * Scale(style.WithStyle(SupStyles[style.Style])):0, node.SecondChild >= 0 ? lower.Width + labelPad * Scale(style.WithStyle(SubStyles[style.Style])) : 0));
            double axis = MathConstants.axisHeight(MetricIndex(style)) * scale;
            double upperY = -axis - .5 * arrowHeight - .111 * scale;
            if (body.Depth > .25 * scale || label == "\\xleftequilibrium") upperY -= body.Depth;
            if(node.FirstChild>=0){x[node.FirstChild] = (result.Width - body.Width) / 2 - (cd ? .13889 * Scale(style.WithStyle(SupStyles[style.Style])) : 0); y[node.FirstChild] = upperY;}
            result.Height = Math.Max(axis + arrowHeight / 2, node.FirstChild>=0?body.Height - upperY:0);
            result.Depth = Math.Max(0, arrowHeight / 2 - axis);
            if (node.SecondChild >= 0)
            {
                x[node.SecondChild] = (result.Width - lower.Width) / 2 - (cd ? .13889 * Scale(style.WithStyle(SubStyles[style.Style])) : 0);
                y[node.SecondChild] = -axis + lower.Height + arrowHeight / 2 + .111 * scale;
                result.Depth = Math.Max(result.Depth, lower.Depth + y[node.SecondChild]);
            }
            AddStretchy(ref result, info, 0, -axis - arrowHeight / 2, result.Width, scale, decorations);
            result.Atom = MathAtom.Rel; return result;
        }
        if (label is "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket")
        {
            bool under = label.StartsWith("\\under", StringComparison.Ordinal); StretchyInfo info = GetStretchy(label[1..]);
            result.Width = Math.Max(body.Width, info.MinWidth * scale); x[node.FirstChild] = (result.Width - body.Width) / 2;
            double gap = .1 * scale, height = info.Height * scale;
            AddStretchy(ref result, info, 0, under ? body.Depth + gap : -body.Height - gap - height, result.Width, scale, decorations);
            if (under) result.Depth += gap + height; else result.Height += gap + height;
            result.Atom = MathAtom.Inner; return result;
        }
        double skew = CharacterSkew(nodes, node.FirstChild, boxes);
        bool wide = label is "\\widehat" or "\\widecheck" or "\\widetilde";
        if (wide || label is "\\overrightarrow" or "\\overleftarrow" or "\\Overrightarrow" or "\\overleftrightarrow" or "\\overgroup" or "\\overlinesegment" or "\\overleftharpoon" or "\\overrightharpoon" or "\\underrightarrow" or "\\underleftarrow" or "\\underleftrightarrow" or "\\undergroup" or "\\underlinesegment" or "\\utilde")
        {
            bool under = label.StartsWith("\\under", StringComparison.Ordinal) || label == "\\utilde";
            if (!wide) skew = 0;
            StretchyInfo info;
            if (wide || label == "\\utilde")
            {
                int count = 1, inner = node.FirstChild;
                if (nodes[inner].Kind == NodeKind.Row) { count = 0; for (int child = nodes[inner].FirstChild; child >= 0; child = nodes[child].NextSibling) count++; }
                int image = count > 5 ? 4 : count <= 1 ? 1 : count <= 3 ? 2 : 3;
                bool tilde = label is "\\widetilde" or "\\utilde";
                double vw = tilde ? image switch { 1 => 600, 2 => 1033, 3 => 2339, _ => 2340 } : image == 1 ? 1062 : 2364;
                double vh = tilde ? image switch { 1 => 260, 2 => 286, 3 => 306, _ => 312 } : image switch { 1 => 239, 2 => 300, 3 => 360, _ => 420 };
                double h = tilde ? image switch { 1 => .26, 2 => .286, 3 => .3, _ => .34 } : image switch { 1 => .24, 2 => .3, 3 => .3, _ => .42 };
                string name = (tilde ? "tilde" : label[1..]) + image;
                double underKern = label == "\\utilde" ? .12 * scale : 0;
                double width = Math.Max(0, body.Width - 2 * skew), top = under ? body.Depth + underKern : -body.Height - h * scale;
                AddPath(ref result, MathPathCache.Get(name), 2 * skew, top, width / vw, h * scale / vh, new(2 * skew, top, width, h * scale), decorations);
                if (under) result.Depth += h * scale + underKern; else result.Height += h * scale;
            }
            else
            {
                info = GetStretchy(label[1..]); result.Width = Math.Max(body.Width, info.MinWidth * scale);
                x[node.FirstChild] = under?0:(result.Width - body.Width) / 2;
                AddStretchy(ref result, info, 0, under ? body.Depth : -body.Height - info.Height * scale, result.Width, scale, decorations);
                if (under) result.Depth += info.Height * scale; else result.Height += info.Height * scale;
            }
            return result;
        }
        bool accentBelow = label == "\\c", accentFull = label == "\\textcircled";
        double clearance = accentBelow ? body.Height + body.Depth : accentFull ? body.Height : Math.Min(body.Height, MathConstants.xHeight(MetricIndex(style)) * scale);
        double accentWidth, accentHeight, accentDepth = 0;
        int accentFont = node.Font switch { MathFont.MathBold => 5, MathFont.MainBoldItalic => 6, MathFont.MathItalic => 7, MathFont.MathSans => 13, MathFont.MathSansBold => 11, MathFont.MathSansItalic => 12, MathFont.MathTypewriter => 19, _ => 8 }, accentGlyph = -1;
        if (label == "\\vec") { accentWidth = .471 * scale; accentHeight = .714 * scale; }
        else if (Find(accentFont, label=="\\^"?0x02c6:node.CodePoint, out accentGlyph, out TexGlyphMetric accent))
        { accentWidth = FontData.Glyphs[accentGlyph].AdvanceUnits / (double)FontData.Fonts[accentFont].UnitsPerEm * scale; accentHeight = accent.Height * scale; accentDepth = accent.Depth * scale; }
        else if (accentFont != 8 && Find(8,node.CodePoint,out accentGlyph,out TexGlyphMetric fallbackAccent))
        { accentFont = 8; accentWidth = FontData.Glyphs[accentGlyph].AdvanceUnits / (double)FontData.Fonts[8].UnitsPerEm * scale; accentHeight = fallbackAccent.Height * scale; accentDepth = fallbackAccent.Depth * scale; }
        else throw new CompileFailure(new(MathDiagnosticCode.UnsupportedCommand, node.Range, "This accent requires an unavailable locked glyph."));
        if (accentBelow) clearance += accentDepth;
        double ax = (accentFull ? (Math.Max(body.Width,accentWidth)-accentWidth)/2 : body.Width / 2 - accentWidth / 2) + skew;
        double ay = -body.Height + clearance - accentDepth + (accentFull ? .2 * scale : 0);
        if (label == "\\vec") AddPath(ref result, MathPathCache.Get("vec"), ax, ay - accentHeight, .001 * scale, .001 * scale, new(ax, ay - accentHeight, accentWidth, accentHeight), decorations);
        else AddGlyph(ref result, accentFont, accentGlyph, label=="\\^"?0x02c6:node.CodePoint, ax, ay, scale, decorations);
        result.Height = Math.Max(result.Height, accentHeight - ay + (accentFull ? .2 * scale : 0));
        result.Depth = Math.Max(result.Depth, accentDepth + ay - (accentFull ? .2 * scale : 0));
        if (accentFull) { result.Width = Math.Max(result.Width,accentWidth); x[node.FirstChild]=(result.Width-body.Width)/2; }
        return result;
    }

    private static double CharacterSkew(SemanticNode[] nodes, int id, Box[] boxes)
    {
        while (id >= 0)
        {
            if (nodes[id].Kind is NodeKind.Font or NodeKind.Style or NodeKind.Text || nodes[id].Kind == NodeKind.Row && nodes[id].FirstChild >= 0 && nodes[nodes[id].FirstChild].NextSibling < 0)
            { id = nodes[id].FirstChild; continue; }
            Box box = boxes[id];
            if (box.Symbol && Find(box.Font, nodes[id].CodePoint, out _, out TexGlyphMetric metric)) return metric.Skew * (box.GlyphScale == 0 ? 1 : box.GlyphScale);
            return 0;
        }
        return 0;
    }

    private readonly record struct StretchyInfo(string Left, string? Right, double MinWidth, double Height, bool AlignRight = false, string? Middle = null);
    private static StretchyInfo GetStretchy(string label) => label switch
    {
        "cdrightarrow" => new("rightarrow", null, 3, .522, true), "cdleftarrow" => new("leftarrow", null, 3, .522), "cdlongequal" => new("longequal", null, 3, .334),
        "overrightarrow" or "underrightarrow" => new("rightarrow", null, .888, .522, true),
        "overleftarrow" or "underleftarrow" => new("leftarrow", null, .888, .522),
        "xrightarrow" => new("rightarrow", null, 1.469, .522, true), "xleftarrow" => new("leftarrow", null, 1.469, .522),
        "Overrightarrow" => new("doublerightarrow", null, .888, .56, true),
        "xRightarrow" => new("doublerightarrow", null, 1.526, .56, true), "xLeftarrow" => new("doubleleftarrow", null, 1.526, .56),
        "overleftharpoon" or "xleftharpoonup" => new("leftharpoon", null, .888, .522),
        "overrightharpoon" or "xrightharpoonup" => new("rightharpoon", null, .888, .522, true),
        "xleftharpoondown" => new("leftharpoondown", null, .888, .522), "xrightharpoondown" => new("rightharpoondown", null, .888, .522, true),
        "xlongequal" => new("longequal", null, .888, .334), "xtwoheadleftarrow" => new("twoheadleftarrow", null, .888, .334), "xtwoheadrightarrow" => new("twoheadrightarrow", null, .888, .334, true),
        "overleftrightarrow" or "underleftrightarrow" => new("leftarrow", "rightarrow", .888, .522), "xleftrightarrow" => new("leftarrow", "rightarrow", 1.75, .522),
        "xLeftrightarrow" => new("doubleleftarrow", "doublerightarrow", 1.75, .56),
        "xrightleftharpoons" => new("leftharpoondownplus", "rightharpoonplus", 1.75, .716), "xleftrightharpoons" => new("leftharpoonplus", "rightharpoondownplus", 1.75, .716),
        "xhookleftarrow" => new("leftarrow", "righthook", 1.08, .522), "xhookrightarrow" => new("lefthook", "rightarrow", 1.08, .522),
        "overlinesegment" or "underlinesegment" => new("leftlinesegment", "rightlinesegment", .888, .522),
        "overgroup" => new("leftgroup", "rightgroup", .888, .342), "undergroup" => new("leftgroupunder", "rightgroupunder", .888, .342),
        "xmapsto" => new("leftmapsto", "rightarrow", 1.5, .522), "xtofrom" => new("leftToFrom", "rightToFrom", 1.75, .528),
        "xrightleftarrows" => new("baraboveleftarrow", "rightarrowabovebar", 1.75, .901),
        "xrightequilibrium" => new("baraboveshortleftharpoon", "rightharpoonaboveshortbar", 1.75, .716), "xleftequilibrium" => new("shortbaraboveleftharpoon", "shortrightharpoonabovebar", 1.75, .716),
        "overbrace" => new("leftbrace", "rightbrace", 1.6, .548, Middle: "midbrace"), "underbrace" => new("leftbraceunder", "rightbraceunder", 1.6, .548, Middle: "midbraceunder"),
        "overbracket" => new("leftbracketover", "rightbracketover", 1.6, .44), "underbracket" => new("leftbracketunder", "rightbracketunder", 1.6, .41),
        _ => throw new CompileFailure(new(MathDiagnosticCode.UnsupportedCommand, default, "This locked stretchy command is not implemented.")),
    };

    private static void AddStretchy(ref Box box, StretchyInfo info, double x, double y, double width, double scale, List<Decoration> decorations)
    {
        double height = info.Height * scale, pathScale = Math.Max(width / 400_000, scale / 1000);
        if (info.Right is null)
        {
            double dx = info.AlignRight ? width - 400_000 * pathScale : 0;
            AddPath(ref box, MathPathCache.Get(info.Left), x + dx, y, pathScale, pathScale, new(x, y, width, height), decorations); return;
        }
        if (info.Middle is null)
        {
            double half = width * .502;
            AddPath(ref box, MathPathCache.Get(info.Left), x, y, pathScale, pathScale, new(x, y, half, height), decorations);
            AddPath(ref box, MathPathCache.Get(info.Right), x + width - 400_000 * pathScale, y, pathScale, pathScale, new(x + width - half, y, half, height), decorations);
        }
        else
        {
            double quarter = width * .251;
            AddPath(ref box, MathPathCache.Get(info.Left), x, y, pathScale, pathScale, new(x, y, quarter, height), decorations);
            AddPath(ref box, MathPathCache.Get(info.Middle), x + width / 2 - 200_000 * pathScale, y, pathScale, pathScale, new(x + width / 4, y, width / 2, height), decorations);
            AddPath(ref box, MathPathCache.Get(info.Right), x + width - 400_000 * pathScale, y, pathScale, pathScale, new(x + width - quarter, y, quarter, height), decorations);
        }
    }

    private static Box BuildMatrix(SemanticNode[] nodes, in SemanticNode node, LayoutContext style, in MathOptions options, Box[] boxes, double[] x, double[] y, List<Decoration> decorations)
    {
        string environment = node.Text ?? "matrix";
        bool aligned = environment is "aligned" or "alignedat" or "split" or "align" or "align*" or "alignat" or "alignat*", gathered = environment is "gathered" or "gather" or "gather*";
        bool cd = environment == "CD", addJot = aligned || gathered || cd || environment is "align" or "align*" or "alignat" or "alignat*" or "gather" or "gather*";
        bool small = environment is "smallmatrix" or "subarray", cases = environment.Contains("cases", StringComparison.Ordinal);
        bool hskip = environment is "array" or "darray";
        double scale = Scale(style), stretch = small ? .5 : cases ? 1.2 : cd ? 1 : node.Value2>0?node.Value2:1;
        double baselineSkip = cd ? 3 * MathConstants.xHeight(style.Metrics) : 1.2;
        double strutHeight = .7 * baselineSkip * stretch * scale, strutDepth = .3 * baselineSkip * stretch * scale;
        double fontSize = options.FontSize;
        int columns = node.ColumnCount;
        if (columns == 0) for (int row = node.FirstChild; row >= 0; row = nodes[row].NextSibling)
        { int c = 0; for (int cell = nodes[row].FirstChild; cell >= 0; cell = nodes[cell].NextSibling) c++; columns = Math.Max(columns, c); }
        var widths = new double[columns]; var starts = new double[columns]; var alignments = new char[columns];
        string spec = node.Alignment ?? ""; int specColumn = 0;
        foreach (char align in spec) if (align is 'c' or 'l' or 'r') { if (specColumn < columns) alignments[specColumn++] = align; }
        for (int c = specColumn; c < columns; c++) alignments[c] = aligned ? (c % 2 == 0 ? 'r' : 'l') : cases ? 'l' : specColumn == 1 ? alignments[0] : 'c';
        if (aligned || cases) for (int c = 0; c < columns; c++) alignments[c] = cases ? 'l' : c % 2 == 0 ? 'r' : 'l';
        double totalHeight = 0;
        for (int row = node.FirstChild; row >= 0; row = nodes[row].NextSibling)
        {
            Box rowBox = new() { Height = strutHeight, Depth = strutDepth };
            int c = 0;
            for (int cell = nodes[row].FirstChild; cell >= 0; cell = nodes[cell].NextSibling, c++)
            { rowBox.Height = Math.Max(rowBox.Height, boxes[cell].Height); rowBox.Depth = Math.Max(rowBox.Depth, boxes[cell].Depth); if (c < columns) widths[c] = Math.Max(widths[c], boxes[cell].Width); }
            double gap = nodes[row].Length1.Number != 0 ? Length(nodes[row].Length1,style,options) : nodes[row].Value * scale;
            if (gap > 0) { rowBox.Depth = Math.Max(rowBox.Depth, gap + strutDepth); gap = 0; }
            if (addJot && nodes[row].NextSibling >= 0) rowBox.Depth += .3 * scale;
            if (Math.Abs(nodes[row].RuleAbove) > 1) totalHeight += (Math.Abs(nodes[row].RuleAbove) - 1) * .25 * scale;
            totalHeight += rowBox.Height; y[row] = totalHeight; totalHeight += rowBox.Depth + gap;
            boxes[row] = rowBox;
        }
        if (Math.Abs(node.RuleBelow) > 1) totalHeight += (Math.Abs(node.RuleBelow) - 1) * .25 * scale;
        double offset = totalHeight / 2 + MathConstants.axisHeight(MetricIndex(style)) * scale;
        // Smallmatrix applies scriptstyle to its cells even when the array
        // already sits in scriptstyle: each side contributes 0.2778*0.7em.
        double separation = small ? 2 * .2778 * .7 : cd ? .5 * scale : scale;
        double width = 0;
        var separatorPositions = new List<(double X, bool Dashed)>(); int specPosition = 0;
        double ruleThickness = .04 * scale;
        // katex.scss gives a vertical separator a one-CSS-pixel minimum
        // content width in addition to its rounded border, then negative
        // half-rule margins. Account for this paint-driven inline advance.
        double separatorAdvance = (1 + Math.Max(1, Math.Floor(ruleThickness * fontSize))) / fontSize - ruleThickness;
        for (int c = 0; c <= columns; c++)
        {
            int separatorCount = 0;
            while (specPosition < spec.Length && spec[specPosition] is '|' or ':')
            {
                if (separatorCount++ > 0) width += .2 * scale;
                separatorPositions.Add((width + 1 / fontSize - ruleThickness / 2, spec[specPosition++] == ':'));
                width += separatorAdvance;
            }
            if (c == columns) break;
            if (specPosition < spec.Length && spec[specPosition] is 'c' or 'l' or 'r') specPosition++;
            if (hskip) width += .5 * scale;
            else if (c > 0) width += aligned ? (c % 2 == 0 && environment == "aligned" ? scale : 0) : gathered ? 0 : separation;
            starts[c] = width; width += widths[c];
            if (hskip) width += .5 * scale;
        }
        double logicalHeight=offset,logicalDepth=totalHeight-offset;
        if(cd)
        {
            logicalHeight=logicalDepth=0;
            for(int row=node.FirstChild;row>=0;row=nodes[row].NextSibling)if(nodes[row].FirstChild>=0)
            {logicalHeight=Math.Max(logicalHeight,boxes[row].Height-(y[row]-offset));logicalDepth=Math.Max(logicalDepth,boxes[row].Depth+y[row]-offset);}
        }
        Box result = new() { Width = width, Height = logicalHeight, Depth = logicalDepth, Atom = MathAtom.Ord };
        for (int row = node.FirstChild; row >= 0; row = nodes[row].NextSibling)
        {
            y[row] -= offset; x[row] = 0; int c = 0;
            for (int cell = nodes[row].FirstChild; cell >= 0; cell = nodes[cell].NextSibling, c++)
            {
                x[cell] = starts[c] + (alignments[c] == 'r' ? widths[c] - boxes[cell].Width : alignments[c] == 'l' ? 0 : (widths[c] - boxes[cell].Width) / 2); y[cell] = 0;
            }
            for (int line = 0; line < Math.Abs(nodes[row].RuleAbove); line++)
                AddArrayHorizontal(ref result, y[row] - boxes[row].Height - line * .25 * scale, width, .04 * scale, nodes[row].RuleAbove < 0, fontSize, decorations);
        }
        for (int line = 0; line < Math.Abs(node.RuleBelow); line++) AddArrayHorizontal(ref result, result.Depth - line * .25 * scale, width, .04 * scale, node.RuleBelow < 0, fontSize, decorations);
        foreach (var separator in separatorPositions)
        {
            if (!separator.Dashed) AddRule(ref result, separator.X, -result.Height, .04 * scale, totalHeight, decorations, true);
            else
            {
                double stroke=Math.Max(1,Math.Floor(.04*scale*fontSize))/fontSize;
                double dash=3*stroke,count=Math.Max(1,Math.Round((totalHeight+2*stroke)/(5*stroke)));
                double gap=count<=1?0:Math.Max(0,(totalHeight-count*dash)/(count-1));
                for(int i=0;i<(int)count;i++){double top=i*(dash+gap);AddRule(ref result,separator.X,-result.Height+top,.04*scale,Math.Min(dash,totalHeight-top),decorations,true);}
            }
        }
        int equationNumber = options.EquationNumber;
        for (int row = node.FirstChild; row >= 0; row = nodes[row].NextSibling)
        {
            if (nodes[row].SecondChild >= 0)
            { int tag = nodes[row].SecondChild; x[tag] = options.Leqno ? 0 : Math.Max(width,options.DisplayWidth / options.FontSize)-boxes[tag].Width; y[tag] = 0; }
            else if (nodes[row].AlwaysLimits && !nodes[row].NeverLimits)
            {
                string label = "(" + (equationNumber++).ToString(System.Globalization.CultureInfo.InvariantCulture) + ")";
                double labelWidth = 0; foreach(char c in label) if(Find(8,c,out int glyph,out _)) labelWidth += FontData.Glyphs[glyph].AdvanceUnits/(double)FontData.Fonts[8].UnitsPerEm*scale;
                double labelX = options.Leqno ? 0 : Math.Max(width,options.DisplayWidth/options.FontSize)-labelWidth;
                Box rowBox = boxes[row];
                foreach(char c in label) if(Find(8,c,out int glyph,out _)) { AddGlyph(ref rowBox,8,glyph,c,labelX,0,scale,decorations); labelX += FontData.Glyphs[glyph].AdvanceUnits/(double)FontData.Fonts[8].UnitsPerEm*scale; }
                boxes[row] = rowBox;
            }
        }
        if (node.LeftDelimiter != 0 || node.RightDelimiter != 0)
        {
            double axis = .25 * scale, distance = Math.Max(result.Height - axis, result.Depth + axis);
            double required = Math.Max(distance * 901 / 500, 2 * distance - .5);
            Box left = MakeDelimiter(node.LeftDelimiter, required, style, true, 0, MathAtom.Open, decorations);
            Box right = MakeDelimiter(node.RightDelimiter, required, style, true, 0, MathAtom.Close, decorations);
            for (int row = node.FirstChild; row >= 0; row = nodes[row].NextSibling) x[row] += left.Width;
            for (int current = result.FirstDecoration; current != 0;)
            {
                Decoration decoration = decorations[current - 1]; int next = decoration.Next;
                decoration.X += left.Width; decoration.Clip = decoration.Clip with { X = decoration.Clip.X + left.Width };
                decorations[current - 1] = decoration; current = next;
            }
            AttachDelimiter(ref result, left, 0, decorations); AttachDelimiter(ref result, right, left.Width + width, decorations);
            result.Width += left.Width + right.Width; result.Atom = MathAtom.Inner;
        }
        return result;
    }
    private static void AddArrayHorizontal(ref Box result, double y, double width, double thickness, bool dashed, double fontSize, List<Decoration> decorations)
    {
        // makeVList anchors an array line's bottom at the row boundary.
        // Its border therefore occupies the preceding rule thickness.
        if (!dashed) AddRule(ref result, 0, y - thickness, width, thickness, decorations);
        else
        {
            // Dashed CSS borders fit a painted dash at both ends. A three-
            // stroke dash length is retained while gaps share the remainder.
            double stroke=Math.Max(1,Math.Floor(thickness*fontSize))/fontSize;
            double dash=3*stroke, count=Math.Max(1,Math.Round((width+2*stroke)/(5*stroke)));
            double gap=count<=1?0:Math.Max(0,(width-count*dash)/(count-1));
            for(int i=0;i<(int)count;i++){double left=i*(dash+gap);AddRule(ref result,left,y-thickness,Math.Min(dash,width-left),thickness,decorations);}
        }
    }
}
