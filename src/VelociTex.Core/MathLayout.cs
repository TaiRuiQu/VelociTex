namespace VelociTex;

/// <summary>
/// TeX box rules projected directly into compact boxes and drawing commands.
/// Coordinates remain double until the Windows drawing boundary. The numerical
/// rules correspond to the locked 0.19.0 buildHTML, supsub, genfrac and sqrt
/// builders; no HTML, CSS boxes, or per-node object graph is constructed.
/// </summary>
internal static class MathLayout
{
    private readonly record struct Frame(int Node, byte Style, bool Complete);
    private readonly record struct Placement(int Node, double X, double Y);
    private struct Box
    {
        internal double Width, Height, Depth, Italic;
        internal MathAtom Atom;
        internal bool Character, Symbol;
        internal int Font, GlyphIndex;
        internal MathPathSegment[]? Path;
        internal double PathX, PathY, PathScale, PathHeight;
    }

    private static ReadOnlySpan<byte> SupStyles => [4, 5, 4, 5, 6, 7, 6, 7];
    private static ReadOnlySpan<byte> SubStyles => [5, 5, 5, 5, 7, 7, 7, 7];
    private static ReadOnlySpan<byte> NumeratorStyles => [2, 3, 4, 5, 6, 7, 6, 7];
    private static ReadOnlySpan<byte> DenominatorStyles => [3, 3, 5, 5, 7, 7, 7, 7];

    internal static LayoutOutput Build(SemanticNode[] nodes, int root, in MathOptions options, CancellationToken cancellationToken)
    {
        int count = nodes.Length;
        var boxes = new Box[count];
        var styles = new byte[count];
        var x = new double[count];
        var y = new double[count];
        var frames = new Frame[checked(count * 2 + 4)];
        var sequence = new int[count];
        var collectStack = new int[count];
        var atoms = new MathAtom[count];
        int top = 0, batches = 0;
        frames[top++] = new(root, options.DisplayMode ? (byte)0 : (byte)2, false);
        while (top != 0)
        {
            if ((++batches & 63) == 0) cancellationToken.ThrowIfCancellationRequested();
            Frame frame = frames[--top];
            int id = frame.Node;
            ref readonly SemanticNode node = ref nodes[id];
            byte style = node.Kind == NodeKind.Style ? (byte)((int)node.Style * 2) : frame.Style;
            styles[id] = style;
            if (!frame.Complete)
            {
                frames[top++] = new(id, style, true);
                switch (node.Kind)
                {
                    case NodeKind.Symbol:
                    case NodeKind.Space:
                        break;
                    case NodeKind.Script:
                        Push(node.FirstChild, style);
                        Push(node.SecondChild, SupStyles[style]);
                        Push(node.ThirdChild, SubStyles[style]);
                        break;
                    case NodeKind.Fraction:
                        Push(node.FirstChild, NumeratorStyles[style]);
                        Push(node.SecondChild, DenominatorStyles[style]);
                        break;
                    case NodeKind.Radical:
                        Push(node.FirstChild, (byte)(style | 1));
                        if (node.SecondChild >= 0) Push(node.SecondChild, 6);
                        break;
                    default:
                        for (int child = node.FirstChild; child >= 0; child = nodes[child].NextSibling) Push(child, style);
                        break;
                }
                continue;
            }

            double scale = Scale(style);
            int metricIndex = MetricIndex(style);
            switch (node.Kind)
            {
                case NodeKind.Symbol:
                    boxes[id] = Symbol(node, scale);
                    break;
                case NodeKind.Space:
                    // The supported fractional spaces are mu measurements;
                    // quad and qquad are em measurements (units.ts).
                    boxes[id] = new() { Width = node.Value * scale * (Math.Abs(node.Value) < 1 ? MathConstants.quad(metricIndex) : 1) };
                    break;
                case NodeKind.Row:
                case NodeKind.Style:
                case NodeKind.Text:
                {
                    int length = Collect(nodes, node.FirstChild, sequence, collectStack);
                    for (int i = 0; i < length; i++) atoms[i] = boxes[sequence[i]].Atom;
                    int previous = -1;
                    for (int i = 0; i < length; i++)
                    {
                        if (nodes[sequence[i]].Kind == NodeKind.Space) continue;
                        MathAtom current = atoms[i];
                        if (previous >= 0 && atoms[previous] == MathAtom.Bin && CancelsBinOnRight(current)) atoms[previous] = MathAtom.Ord;
                        else if (current == MathAtom.Bin && (previous < 0 || CancelsBinOnLeft(atoms[previous]))) atoms[i] = MathAtom.Ord;
                        previous = i;
                    }
                    if (previous >= 0 && atoms[previous] == MathAtom.Bin) atoms[previous] = MathAtom.Ord;

                    byte glueStyle = style;
                    if (node.FirstChild >= 0 && nodes[node.FirstChild].NextSibling < 0 && nodes[node.FirstChild].Kind == NodeKind.Style)
                        glueStyle = styles[node.FirstChild];
                    double mu = MathConstants.quad(MetricIndex(glueStyle)) * Scale(glueStyle) / 18d;
                    Box box = new() { Atom = MathAtom.Ord };
                    previous = -1;
                    for (int i = 0; i < length; i++)
                    {
                        int child = sequence[i];
                        ref readonly Box childBox = ref boxes[child];
                        bool space = nodes[child].Kind == NodeKind.Space;
                        if (!space && previous >= 0) box.Width += Spacing(atoms[previous], atoms[i], styles[child] >= 4) * mu;
                        x[child] = box.Width;
                        y[child] = 0;
                        box.Width += childBox.Width;
                        box.Height = Math.Max(box.Height, childBox.Height);
                        box.Depth = Math.Max(box.Depth, childBox.Depth);
                        if (!space) previous = i;
                    }
                    if (length == 1 && node.Kind == NodeKind.Row) box.Character = boxes[sequence[0]].Character;
                    boxes[id] = box;
                    break;
                }
                case NodeKind.Font:
                {
                    int basis = node.FirstChild;
                    // Upstream normalizeArgument removes a one-element
                    // ordgroup before a font command reaches typesetting.
                    if (nodes[basis].Kind == NodeKind.Row && nodes[basis].FirstChild >= 0 && nodes[nodes[basis].FirstChild].NextSibling < 0)
                        basis = nodes[basis].FirstChild;
                    Box box = boxes[basis];
                    x[node.FirstChild] = y[node.FirstChild] = 0;
                    boxes[id] = box;
                    break;
                }
                case NodeKind.Script:
                {
                    Box basis = node.FirstChild >= 0 ? boxes[node.FirstChild] : new() { Atom = MathAtom.Ord };
                    Box sup = node.SecondChild >= 0 ? boxes[node.SecondChild] : default;
                    Box sub = node.ThirdChild >= 0 ? boxes[node.ThirdChild] : default;
                    bool hasSup = node.SecondChild >= 0, hasSub = node.ThirdChild >= 0;
                    double supShift = 0, subShift = 0;
                    if (!basis.Character)
                    {
                        byte supStyle = SupStyles[style], subStyle = SubStyles[style];
                        if (hasSup) supShift = basis.Height - MathConstants.supDrop(MetricIndex(supStyle)) * Scale(supStyle);
                        if (hasSub) subShift = basis.Depth + MathConstants.subDrop(MetricIndex(subStyle)) * Scale(subStyle);
                    }
                    double minSupShift = (style == 0 ? MathConstants.sup1(metricIndex) : (style & 1) != 0 ? MathConstants.sup3(metricIndex) : MathConstants.sup2(metricIndex)) * scale;
                    double xHeight = MathConstants.xHeight(metricIndex) * scale;
                    if (hasSup && hasSub)
                    {
                        supShift = Math.Max(Math.Max(supShift, minSupShift), sup.Depth + 0.25 * xHeight);
                        subShift = Math.Max(subShift, MathConstants.sub2(metricIndex) * scale);
                        double minimumGap = 4 * MathConstants.defaultRuleThickness(metricIndex) * scale;
                        if ((supShift - sup.Depth) - (sub.Height - subShift) < minimumGap)
                        {
                            subShift = minimumGap - (supShift - sup.Depth) + sub.Height;
                            double psi = 0.8 * xHeight - (supShift - sup.Depth);
                            if (psi > 0) { supShift += psi; subShift -= psi; }
                        }
                    }
                    else if (hasSub) subShift = Math.Max(Math.Max(subShift, MathConstants.sub1(metricIndex) * scale), sub.Height - 0.8 * xHeight);
                    else if (hasSup) supShift = Math.Max(Math.Max(supShift, minSupShift), sup.Depth + 0.25 * xHeight);
                    double correction = basis.Symbol ? basis.Italic : 0;
                    double scriptWidth = 0;
                    Box result = new() { Width = basis.Width, Height = Math.Max(0, basis.Height), Depth = Math.Max(0, basis.Depth), Atom = basis.Atom };
                    if (node.FirstChild >= 0) x[node.FirstChild] = y[node.FirstChild] = 0;
                    if (hasSup)
                    {
                        x[node.SecondChild] = basis.Width;
                        y[node.SecondChild] = -supShift;
                        scriptWidth = sup.Width + 0.05;
                        result.Height = Math.Max(result.Height, sup.Height + supShift);
                        result.Depth = Math.Max(result.Depth, sup.Depth - supShift);
                    }
                    if (hasSub)
                    {
                        x[node.ThirdChild] = basis.Width - correction;
                        y[node.ThirdChild] = subShift;
                        scriptWidth = Math.Max(scriptWidth, sub.Width - correction + 0.05);
                        result.Height = Math.Max(result.Height, sub.Height - subShift);
                        result.Depth = Math.Max(result.Depth, sub.Depth + subShift);
                    }
                    result.Width += Math.Max(0, scriptWidth);
                    boxes[id] = result;
                    break;
                }
                case NodeKind.Fraction:
                {
                    Box numerator = boxes[node.FirstChild], denominator = boxes[node.SecondChild];
                    double rule = MathConstants.defaultRuleThickness(metricIndex) * scale;
                    double numShift = (style < 2 ? MathConstants.num1(metricIndex) : MathConstants.num2(metricIndex)) * scale;
                    double denShift = (style < 2 ? MathConstants.denom1(metricIndex) : MathConstants.denom2(metricIndex)) * scale;
                    double clearance = (style < 2 ? 3 : 1) * rule;
                    double axis = MathConstants.axisHeight(metricIndex) * scale;
                    numShift += Math.Max(0, clearance - ((numShift - numerator.Depth) - (axis + 0.5 * rule)));
                    denShift += Math.Max(0, clearance - ((axis - 0.5 * rule) - (denominator.Height - denShift)));
                    double contentWidth = Math.Max(numerator.Width, denominator.Width);
                    x[node.FirstChild] = 0.12 + (contentWidth - numerator.Width) / 2;
                    y[node.FirstChild] = -numShift;
                    x[node.SecondChild] = 0.12 + (contentWidth - denominator.Width) / 2;
                    y[node.SecondChild] = denShift;
                    boxes[id] = new() { Width = contentWidth + 0.24, Height = Math.Max(numerator.Height + numShift, axis + 0.5 * rule), Depth = Math.Max(denominator.Depth + denShift, -axis + 0.5 * rule), Atom = MathAtom.Ord, PathX = 0.12, PathY = -axis - 0.5 * rule, PathScale = contentWidth, PathHeight = rule };
                    break;
                }
                case NodeKind.Radical:
                {
                    Box inner = boxes[node.FirstChild];
                    double innerHeight = inner.Height == 0 ? MathConstants.xHeight(metricIndex) * scale : inner.Height;
                    double theta = MathConstants.defaultRuleThickness(metricIndex) * scale;
                    double phi = style < 2 ? MathConstants.xHeight(metricIndex) * scale : theta;
                    double clearance = theta + phi / 4;
                    double minimumHeight = innerHeight + inner.Depth + clearance + theta;
                    double localHeight = minimumHeight / scale;
                    int size = SelectRadical(minimumHeight);
                    double multiplier = scale;
                    double texHeight, spanHeight, viewBoxHeight, advance, minWidth;
                    MathPathSegment[] path;
                    if (size == 0)
                    {
                        if (localHeight < 1) multiplier = 1;
                        else if (localHeight < 1.4) multiplier = 0.7;
                        texHeight = scale / multiplier;
                        spanHeight = 1.08 * scale / multiplier;
                        viewBoxHeight = 1080;
                        advance = 0.833 * scale / multiplier;
                        minWidth = 0.853 * scale;
                        path = RadicalPathData.sqrtMain;
                    }
                    else if (size < 5)
                    {
                        double maxHeight = size switch { 1 => 1.2, 2 => 1.8, 3 => 2.4, _ => 3 };
                        texHeight = maxHeight;
                        spanHeight = maxHeight + 0.08;
                        viewBoxHeight = 1080 * maxHeight;
                        advance = 1;
                        minWidth = 1.02 * scale;
                        path = size switch { 1 => RadicalPathData.sqrtSize1, 2 => RadicalPathData.sqrtSize2, 3 => RadicalPathData.sqrtSize3, _ => RadicalPathData.sqrtSize4 };
                    }
                    else
                    {
                        texHeight = minimumHeight;
                        spanHeight = minimumHeight + 0.08 * scale;
                        viewBoxHeight = Math.Floor(1000 * localHeight) + 80;
                        advance = 1.056 * scale;
                        minWidth = 0.742 * scale;
                        path = RadicalPathData.Tall(viewBoxHeight);
                    }
                    double rule = MathConstants.sqrtRuleThickness(metricIndex) * multiplier * scale;
                    double delimiterDepth = texHeight - rule;
                    if (delimiterDepth > innerHeight + inner.Depth + clearance) clearance = (clearance + delimiterDepth - innerHeight - inner.Depth) / 2;
                    double imageShift = texHeight - innerHeight - clearance - rule;
                    double width = Math.Max(inner.Width + advance, minWidth);
                    x[node.FirstChild] = advance;
                    y[node.FirstChild] = 0;
                    // The locked CSS sets svg width:100%, overriding the
                    // 400em SVG width attribute. xMinYMin slice therefore uses
                    // the maximum of the actual viewport's two scale ratios.
                    // A large radical's viewport height/viewBox ratio differs
                    // from 1/1000 because its padding is outside TeX height.
                    double pathScale = Math.Max(width / 400_000, spanHeight / viewBoxHeight);
                    Box result = new() { Width = width, Height = innerHeight + clearance + 2 * rule, Depth = Math.Max(inner.Depth, imageShift), Atom = MathAtom.Ord, Path = path, PathX = 0, PathY = imageShift - spanHeight, PathScale = pathScale, PathHeight = spanHeight };
                    if (node.SecondChild >= 0)
                    {
                        Box index = boxes[node.SecondChild];
                        double shift = 0.6 * (result.Height - result.Depth);
                        double rootWidth = index.Width - 5 * scale / 18;
                        double left = 5 * scale / 18;
                        x[node.SecondChild] = left;
                        y[node.SecondChild] = -shift;
                        x[node.FirstChild] += rootWidth;
                        result.PathX += rootWidth;
                        result.Width += rootWidth;
                        result.Height = Math.Max(result.Height, index.Height + shift);
                        result.Depth = Math.Max(result.Depth, index.Depth - shift);
                    }
                    boxes[id] = result;
                    break;
                }
                default:
                    throw new InvalidOperationException("Unknown compact semantic node kind.");
            }
        }

        cancellationToken.ThrowIfCancellationRequested();
        double fontSize = options.FontSize;
        Box outer = boxes[root];
        var glyphs = new List<MathGlyph>();
        var rules = new List<MathRule>();
        var paths = new List<MathPath>();
        var placements = new Placement[count + 4];
        int placementCount = 0;
        placements[placementCount++] = new(root, 0, outer.Height);
        double inkLeft = double.PositiveInfinity, inkTop = double.PositiveInfinity, inkRight = double.NegativeInfinity, inkBottom = double.NegativeInfinity;
        while (placementCount != 0)
        {
            if ((++batches & 63) == 0) cancellationToken.ThrowIfCancellationRequested();
            Placement placement = placements[--placementCount];
            int id = placement.Node;
            ref readonly SemanticNode node = ref nodes[id];
            ref readonly Box box = ref boxes[id];
            double px = placement.X, py = placement.Y;
            switch (node.Kind)
            {
                case NodeKind.Symbol:
                {
                    FontGlyph glyph = FontData.Glyphs[box.GlyphIndex];
                    double size = Scale(styles[id]) * fontSize;
                    glyphs.Add(new(box.Font, glyph.GlyphId, node.CodePoint, px * fontSize, py * fontSize, size));
                    GlyphInk ink = GlyphInkData.Bounds[box.GlyphIndex];
                    double units = size / FontData.Fonts[box.Font].UnitsPerEm;
                    if (ink.Left != ink.Right && ink.Top != ink.Bottom) AddInk(px * fontSize + ink.Left * units, py * fontSize - ink.Top * units, px * fontSize + ink.Right * units, py * fontSize - ink.Bottom * units);
                    break;
                }
                case NodeKind.Space:
                    break;
                case NodeKind.Row:
                case NodeKind.Style:
                case NodeKind.Text:
                {
                    int length = Collect(nodes, node.FirstChild, sequence, collectStack);
                    for (int i = length - 1; i >= 0; i--) Place(sequence[i]);
                    break;
                }
                case NodeKind.Font:
                    Place(node.FirstChild);
                    break;
                case NodeKind.Script:
                    if (node.ThirdChild >= 0) Place(node.ThirdChild);
                    if (node.SecondChild >= 0) Place(node.SecondChild);
                    if (node.FirstChild >= 0) Place(node.FirstChild);
                    break;
                case NodeKind.Fraction:
                {
                    double rx = (px + box.PathX) * fontSize, ry = (py + box.PathY) * fontSize;
                    double rw = box.PathScale * fontSize, rh = box.PathHeight * fontSize;
                    // The locked renderer paints .frac-line as a CSS bottom
                    // border: positive border widths have a minimum of one
                    // CSS pixel and truncate larger fractional CSS pixels.
                    // Keep the exact TeX rule for shifts and clearances above;
                    // apply this paint constraint only to the final DIP rule,
                    // with its bottom edge anchored to the TeX rule bottom.
                    // Reference: katex.scss .mfrac .frac-line and buildCommon's
                    // makeLineSpan; verified at 16/24/32 DIP and all DPRs.
                    double paintedHeight = Math.Max(1, Math.Floor(rh));
                    double paintedY = ry + rh - paintedHeight;
                    rules.Add(new(rx, paintedY, rw, paintedHeight));
                    if (rw > 0) AddInk(rx, paintedY, rx + rw, paintedY + paintedHeight);
                    Place(node.SecondChild);
                    Place(node.FirstChild);
                    break;
                }
                case NodeKind.Radical:
                {
                    double rx = (px + box.PathX) * fontSize, ry = (py + box.PathY) * fontSize;
                    double ps = box.PathScale * fontSize;
                    double rootOffset = box.PathX;
                    var clip = new MathBounds(rx, ry, (box.Width - rootOffset) * fontSize, box.PathHeight * fontSize);
                    paths.Add(new(box.Path!, rx, ry, ps, ps, clip));
                    MathBounds pb = PathBounds(box.Path!);
                    AddInk(Math.Max(clip.X, rx + pb.X * ps), Math.Max(clip.Y, ry + pb.Y * ps), Math.Min(clip.X + clip.Width, rx + (pb.X + pb.Width) * ps), Math.Min(clip.Y + clip.Height, ry + (pb.Y + pb.Height) * ps));
                    if (node.SecondChild >= 0) Place(node.SecondChild);
                    Place(node.FirstChild);
                    break;
                }
            }
            void Place(int child) => placements[placementCount++] = new(child, px + x[child], py + y[child]);
        }
        var bounds = double.IsPositiveInfinity(inkLeft) ? new MathBounds(0, 0, 0, 0) : new MathBounds(inkLeft, inkTop, inkRight - inkLeft, inkBottom - inkTop);
        return new(glyphs.ToArray(), rules.ToArray(), paths.ToArray(), new(Math.Max(0, outer.Width) * fontSize, (outer.Height + outer.Depth) * fontSize), outer.Height * fontSize, bounds);

        void Push(int child, byte childStyle) { if (child >= 0) frames[top++] = new(child, childStyle, false); }
        void AddInk(double left, double upper, double right, double bottom)
        {
            if (right <= left || bottom <= upper) return;
            inkLeft = Math.Min(inkLeft, left); inkTop = Math.Min(inkTop, upper);
            inkRight = Math.Max(inkRight, right); inkBottom = Math.Max(inkBottom, bottom);
        }
    }

    private static Box Symbol(in SemanticNode node, double scale)
    {
        int defaultFont = node.SymbolFont == MathFont.AMS ? 0 : node.IsMathOrd ? 10 : 8;
        int desired = node.Font switch
        {
            MathFont.MathItalic => 7, MathFont.MathNormal => 10, MathFont.Main or MathFont.MathRoman => 8, MathFont.MathBold => 5,
            MathFont.MathBoldItalic => node.IsMathOrd ? 9 : 5,
            MathFont.MathSans => 13, MathFont.MathSansBold => 11, MathFont.MathSansItalic => 12,
            MathFont.MathTypewriter => 19, MathFont.AMS => 0, MathFont.Caligraphic => 2,
            MathFont.Fraktur => 4, MathFont.Script => 14,
            _ => defaultFont,
        };
        // Explicit math fonts change ords. The upstream mathsym path retains the
        // source Main/AMS family for punctuation and binary/relation atoms.
        if (node.Atom != MathAtom.Ord && node.Font is not MathFont.MathBoldItalic) desired = defaultFont;
        if (!Find(desired, node.CodePoint, out int glyphIndex, out TexGlyphMetric metric))
        {
            if (node.Font == MathFont.MathBoldItalic && Find(5, node.CodePoint, out glyphIndex, out metric)) desired = 5;
            else if (Find(defaultFont, node.CodePoint, out glyphIndex, out metric)) desired = defaultFont;
            else throw new CompileFailure(new MathDiagnostic(MathDiagnosticCode.UnknownSymbol, node.Range, $"Locked font data has no glyph U+{node.CodePoint:X}."));
        }
        FontGlyph glyph = FontData.Glyphs[glyphIndex];
        double italic = node.TextMode || node.Font == MathFont.MathItalic ? 0 : metric.ItalicCorrection;
        return new() { Width = (glyph.AdvanceUnits / (double)FontData.Fonts[desired].UnitsPerEm + Math.Max(0, italic)) * scale, Height = metric.Height * scale, Depth = metric.Depth * scale, Italic = italic * scale, Atom = node.Atom, Character = true, Symbol = true, Font = desired, GlyphIndex = glyphIndex };
    }

    private static bool Find(int font, int codePoint, out int glyphIndex, out TexGlyphMetric metric)
    {
        FontDescriptor descriptor = FontData.Fonts[font];
        int low = descriptor.GlyphStart, high = low + descriptor.GlyphCount - 1;
        glyphIndex = -1; metric = default;
        while (low <= high)
        {
            int middle = (low + high) >>> 1, cp = FontData.Glyphs[middle].CodePoint;
            if (cp < codePoint) low = middle + 1;
            else if (cp > codePoint) high = middle - 1;
            else { glyphIndex = middle; break; }
        }
        if (glyphIndex < 0) return false;
        low = descriptor.MetricStart; high = low + descriptor.MetricCount - 1;
        while (low <= high)
        {
            int middle = (low + high) >>> 1, cp = FontData.TexMetrics[middle].CodePoint;
            if (cp < codePoint) low = middle + 1;
            else if (cp > codePoint) high = middle - 1;
            else { metric = FontData.TexMetrics[middle]; return true; }
        }
        return false;
    }

    // Styling fragments are transparent to the enclosing math list. Braced rows
    // remain ordinary atoms, preserving the distinction x+{+y} versus x++y.
    private static int Collect(SemanticNode[] nodes, int first, int[] destination, int[] stack)
    {
        int length = 0, depth = 0, current = first;
        while (current >= 0 || depth > 0)
        {
            if (current < 0) { current = stack[--depth]; continue; }
            ref readonly SemanticNode node = ref nodes[current];
            if (node.Kind == NodeKind.Style)
            {
                if (node.NextSibling >= 0) stack[depth++] = node.NextSibling;
                current = node.FirstChild;
                // The parser stores a style's implicit body in a transparent
                // row. Its atoms participate in the enclosing math list.
                if (current >= 0 && nodes[current].Kind == NodeKind.Row && nodes[current].Value == 0 && nodes[current].NextSibling < 0)
                    current = nodes[current].FirstChild;
            }
            else { destination[length++] = current; current = node.NextSibling; }
        }
        return length;
    }

    private static double Scale(byte style) => style >= 6 ? 0.5 : style >= 4 ? 0.7 : 1;
    private static int MetricIndex(byte style) => style >= 6 ? 2 : style >= 4 ? 1 : 0;
    private static bool CancelsBinOnLeft(MathAtom atom) => atom is MathAtom.Bin or MathAtom.Open or MathAtom.Rel or MathAtom.Op or MathAtom.Punct;
    private static bool CancelsBinOnRight(MathAtom atom) => atom is MathAtom.Rel or MathAtom.Close or MathAtom.Punct;
    private static int Spacing(MathAtom left, MathAtom right, bool tight)
    {
        if (tight) return (left, right) switch { (MathAtom.Ord, MathAtom.Op) or (MathAtom.Op, MathAtom.Ord) or (MathAtom.Op, MathAtom.Op) or (MathAtom.Close, MathAtom.Op) or (MathAtom.Inner, MathAtom.Op) => 3, _ => 0 };
        return (left, right) switch
        {
            (MathAtom.Ord, MathAtom.Op) or (MathAtom.Ord, MathAtom.Inner) or
            (MathAtom.Op, MathAtom.Ord) or (MathAtom.Op, MathAtom.Op) or (MathAtom.Op, MathAtom.Inner) or
            (MathAtom.Close, MathAtom.Op) or (MathAtom.Close, MathAtom.Inner) or
            (MathAtom.Punct, MathAtom.Ord) or (MathAtom.Punct, MathAtom.Op) or (MathAtom.Punct, MathAtom.Open) or (MathAtom.Punct, MathAtom.Close) or (MathAtom.Punct, MathAtom.Punct) or (MathAtom.Punct, MathAtom.Inner) or
            (MathAtom.Inner, MathAtom.Ord) or (MathAtom.Inner, MathAtom.Op) or (MathAtom.Inner, MathAtom.Open) or (MathAtom.Inner, MathAtom.Punct) or (MathAtom.Inner, MathAtom.Inner) => 3,
            (MathAtom.Ord, MathAtom.Bin) or (MathAtom.Bin, MathAtom.Ord) or (MathAtom.Bin, MathAtom.Op) or (MathAtom.Bin, MathAtom.Open) or (MathAtom.Bin, MathAtom.Inner) or (MathAtom.Close, MathAtom.Bin) or (MathAtom.Inner, MathAtom.Bin) => 4,
            (MathAtom.Ord, MathAtom.Rel) or (MathAtom.Op, MathAtom.Rel) or (MathAtom.Rel, MathAtom.Ord) or (MathAtom.Rel, MathAtom.Op) or (MathAtom.Rel, MathAtom.Open) or (MathAtom.Rel, MathAtom.Inner) or (MathAtom.Close, MathAtom.Rel) or (MathAtom.Punct, MathAtom.Rel) or (MathAtom.Inner, MathAtom.Rel) => 5,
            _ => 0,
        };
    }

    private static int SelectRadical(double height)
    {
        for (int size = 0; size < 5; size++)
        {
            int font = size == 0 ? 8 : size + 14;
            if (Find(font, 0x221a, out _, out TexGlyphMetric metric) && metric.Height + metric.Depth > height) return size;
        }
        return 5;
    }

    private static MathBounds PathBounds(ReadOnlySpan<MathPathSegment> segments)
    {
        double minX = double.PositiveInfinity, minY = double.PositiveInfinity, maxX = double.NegativeInfinity, maxY = double.NegativeInfinity;
        double x = 0, y = 0, sx = 0, sy = 0;
        foreach (MathPathSegment segment in segments)
        {
            switch (segment.Operation)
            {
                case MathPathOperation.Move:
                    x = sx = segment.X1; y = sy = segment.Y1; Include(x, y);
                    break;
                case MathPathOperation.Line:
                    x = segment.X1; y = segment.Y1; Include(x, y);
                    break;
                case MathPathOperation.Cubic:
                    CubicExtrema(x, segment.X1, segment.X2, segment.X3, true);
                    CubicExtrema(y, segment.Y1, segment.Y2, segment.Y3, false);
                    x = segment.X3; y = segment.Y3; Include(x, y);
                    break;
                case MathPathOperation.Close:
                    x = sx; y = sy; Include(x, y);
                    break;
            }
        }
        return new(minX, minY, maxX - minX, maxY - minY);

        void Include(double px, double py) { minX = Math.Min(minX, px); minY = Math.Min(minY, py); maxX = Math.Max(maxX, px); maxY = Math.Max(maxY, py); }
        void CubicExtrema(double p0, double p1, double p2, double p3, bool horizontal)
        {
            double a = -p0 + 3 * p1 - 3 * p2 + p3, b = 2 * (p0 - 2 * p1 + p2), c = p1 - p0;
            if (Math.Abs(a) < 1e-12) { if (Math.Abs(b) > 1e-12) Evaluate(-c / b); return; }
            double discriminant = b * b - 4 * a * c;
            if (discriminant < 0) return;
            double r = Math.Sqrt(discriminant);
            Evaluate((-b + r) / (2 * a)); Evaluate((-b - r) / (2 * a));
            void Evaluate(double t)
            {
                if (t <= 0 || t >= 1) return;
                double u = 1 - t, p = u * u * u * p0 + 3 * u * u * t * p1 + 3 * u * t * t * p2 + t * t * t * p3;
                if (horizontal) { minX = Math.Min(minX, p); maxX = Math.Max(maxX, p); }
                else { minY = Math.Min(minY, p); maxY = Math.Max(maxY, p); }
            }
        }
    }
}
