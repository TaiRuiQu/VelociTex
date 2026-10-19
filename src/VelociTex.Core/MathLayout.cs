namespace VelociTex;

/// <summary>
/// TeX box rules projected directly into compact boxes and drawing commands.
/// Coordinates remain double until the Windows drawing boundary. The numerical
/// rules correspond to the locked 0.19.0 buildHTML, supsub, genfrac and sqrt
/// builders; no HTML, CSS boxes, or per-node object graph is constructed.
/// </summary>
internal static partial class MathLayout
{
    private readonly record struct Frame(int Node, byte Style, bool Complete);
    private readonly record struct Placement(int Node, double X, double Y);
    private struct Box
    {
        internal double Width, Height, Depth, Italic;
        internal MathAtom Atom;
        internal bool Character, Symbol;
        internal int Font, GlyphIndex, FirstDecoration;
        internal double GlyphScale, GlyphY;
        internal double Skew;
        internal bool TextShaped;
        internal MathTextShape TextShape;
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
        var decorations = new List<Decoration>();
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
                    case NodeKind.Delimiter:
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
                    case NodeKind.Accent:
                        if (node.Text?.StartsWith("\\x", StringComparison.Ordinal) == true)
                        { Push(node.FirstChild, SupStyles[style]); Push(node.SecondChild, SubStyles[style]); }
                        else if (node.Text is "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket") Push(node.FirstChild, 0);
                        else Push(node.FirstChild, node.Text == "\\underline" ? style : (byte)(style | 1));
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
                    boxes[id] = Symbol(node, scale, options.TextShaper);
                    break;
                case NodeKind.Operator:
                    boxes[id] = BuildOperator(node, style, boxes, decorations);
                    if (node.FirstChild >= 0) x[node.FirstChild] = y[node.FirstChild] = 0;
                    break;
                case NodeKind.Delimiter:
                    boxes[id] = MakeDelimiter(node.CodePoint, node.Value > 0 ? SizeHeight((int)node.Value) : 0, style, node.Value == 0, (int)node.Value, node.Atom, decorations);
                    break;
                case NodeKind.Space:
                    // The supported fractional spaces are mu measurements;
                    // quad and qquad are em measurements (units.ts).
                    boxes[id] = new() { Width = node.Value * scale * (Math.Abs(node.Value) < 1 ? MathConstants.quad(metricIndex) : 1) };
                    break;
                case NodeKind.Row:
                case NodeKind.Style:
                case NodeKind.Text:
                case NodeKind.MatrixCell:
                case NodeKind.MatrixRow:
                {
                    boxes[id] = BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms);
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
                    // The wrapper borrows dimensions, while its owned child
                    // remains responsible for emitting drawing decorations.
                    box.FirstDecoration = 0;
                    x[node.FirstChild] = y[node.FirstChild] = 0;
                    boxes[id] = box;
                    break;
                }
                case NodeKind.Script:
                {
                    Box basis = node.FirstChild >= 0 ? boxes[node.FirstChild] : new() { Atom = MathAtom.Ord };
                    Box scriptBasis = basis;
                    if (node.FirstChild >= 0 && nodes[node.FirstChild].Kind == NodeKind.Accent &&
                        nodes[node.FirstChild].Text is "\\acute" or "\\grave" or "\\ddot" or "\\tilde" or "\\bar" or "\\breve" or "\\check" or "\\hat" or "\\vec" or "\\dot" or "\\mathring")
                    {
                        int inner = nodes[node.FirstChild].FirstChild;
                        if (inner >= 0 && boxes[inner].Character) scriptBasis = boxes[inner];
                    }
                    Box sup = node.SecondChild >= 0 ? boxes[node.SecondChild] : default;
                    Box sub = node.ThirdChild >= 0 ? boxes[node.ThirdChild] : default;
                    bool hasSup = node.SecondChild >= 0, hasSub = node.ThirdChild >= 0;
                    if (node.FirstChild >= 0 && nodes[node.FirstChild].Kind == NodeKind.Operator &&
                        UseLimits(nodes[node.FirstChild], style))
                    {
                        boxes[id] = BuildLimits(node, basis, sup, sub, style, boxes, x, y);
                        break;
                    }
                    if (node.FirstChild >= 0 && nodes[node.FirstChild].Kind == NodeKind.Accent &&
                        (nodes[node.FirstChild].Text is "\\overbrace" or "\\overbracket" && !hasSub || nodes[node.FirstChild].Text is "\\underbrace" or "\\underbracket" && !hasSup))
                    {
                        double width = Math.Max(basis.Width, hasSup ? sup.Width : sub.Width);
                        x[node.FirstChild] = (width - basis.Width) / 2; y[node.FirstChild] = 0;
                        Box braceResult = new() { Width = width, Height = basis.Height, Depth = basis.Depth, Atom = MathAtom.Inner };
                        if (hasSup) { x[node.SecondChild] = (width - sup.Width) / 2; y[node.SecondChild] = -basis.Height - .2 * scale - sup.Depth; braceResult.Height = sup.Height - y[node.SecondChild]; }
                        if (hasSub) { x[node.ThirdChild] = (width - sub.Width) / 2; y[node.ThirdChild] = basis.Depth + .2 * scale + sub.Height; braceResult.Depth = sub.Depth + y[node.ThirdChild]; }
                        boxes[id] = braceResult; break;
                    }
                    double supShift = 0, subShift = 0;
                    if (!scriptBasis.Character)
                    {
                        byte supStyle = SupStyles[style], subStyle = SubStyles[style];
                        if (hasSup) supShift = scriptBasis.Height - MathConstants.supDrop(MetricIndex(supStyle)) * Scale(supStyle);
                        if (hasSub) subShift = scriptBasis.Depth + MathConstants.subDrop(MetricIndex(subStyle)) * Scale(subStyle);
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
                    double correction = scriptBasis.Symbol ? scriptBasis.Italic : 0;
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
                    if (node.Text == "\\cfrac") { numerator.Height = Math.Max(numerator.Height, .85 * scale); numerator.Depth = Math.Max(numerator.Depth, .35 * scale); }
                    double defaultRule = MathConstants.defaultRuleThickness(metricIndex) * scale;
                    double rule = node.HasBar ? node.Value > 0 ? node.Value * scale : defaultRule : 0;
                    double numShift = (style < 2 ? MathConstants.num1(metricIndex) : node.HasBar ? MathConstants.num2(metricIndex) : MathConstants.num3(metricIndex)) * scale;
                    double denShift = (style < 2 ? MathConstants.denom1(metricIndex) : MathConstants.denom2(metricIndex)) * scale;
                    double clearance = (style < 2 ? node.HasBar ? 3 : 7 : node.HasBar ? 1 : 3) * (node.HasBar ? rule : defaultRule);
                    double axis = MathConstants.axisHeight(metricIndex) * scale;
                    if (node.HasBar)
                    {
                        numShift += Math.Max(0, clearance - ((numShift - numerator.Depth) - (axis + 0.5 * rule)));
                        denShift += Math.Max(0, clearance - ((axis - 0.5 * rule) - (denominator.Height - denShift)));
                    }
                    else
                    {
                        double missing = Math.Max(0, clearance - ((numShift - numerator.Depth) - (denominator.Height - denShift))) / 2;
                        numShift += missing; denShift += missing;
                    }
                    double contentWidth = Math.Max(numerator.Width, denominator.Width);
                    double delimiterSize = style < 2 ? MathConstants.delim1(metricIndex) : MathConstants.delim2(style >= 6 ? 1 : metricIndex);
                    Box left = MakeDelimiter(node.LeftDelimiter, delimiterSize, style, true, 0, MathAtom.Open, decorations);
                    Box right = node.Text == "\\cfrac" ? default : MakeDelimiter(node.RightDelimiter, delimiterSize, style, true, 0, MathAtom.Close, decorations);
                    x[node.FirstChild] = left.Width + (contentWidth - numerator.Width) / 2;
                    y[node.FirstChild] = -numShift;
                    x[node.SecondChild] = left.Width + (contentWidth - denominator.Width) / 2;
                    y[node.SecondChild] = denShift;
                    Box fraction = new() { Width = contentWidth + left.Width + right.Width, Height = Math.Max(numerator.Height + numShift, axis + 0.5 * rule), Depth = Math.Max(denominator.Depth + denShift, -axis + 0.5 * rule), Atom = MathAtom.Ord, PathX = left.Width, PathY = -axis - 0.5 * rule, PathScale = contentWidth, PathHeight = rule };
                    AttachDelimiter(ref fraction, left, 0, decorations);
                    AttachDelimiter(ref fraction, right, left.Width + contentWidth, decorations);
                    boxes[id] = fraction;
                    break;
                }
                case NodeKind.Delimited:
                {
                    Box body = boxes[node.FirstChild];
                    double axis = MathConstants.axisHeight(metricIndex) * scale;
                    double distance = Math.Max(body.Height - axis, body.Depth + axis);
                    double height = Math.Max(distance * 901 / 500, 2 * distance - .5);
                    int length = Collect(nodes, nodes[node.FirstChild].FirstChild, sequence, collectStack);
                    for (int i = 0; i < length; i++)
                    {
                        int middle = sequence[i];
                        if (nodes[middle].Kind == NodeKind.Delimiter && nodes[middle].Value == 0)
                            boxes[middle] = MakeDelimiter(nodes[middle].CodePoint, height, styles[middle], true, 0, MathAtom.Ord, decorations);
                    }
                    body = boxes[node.FirstChild] = BuildRow(nodes, node.FirstChild, styles[node.FirstChild], styles, boxes, x, y, sequence, collectStack, atoms);
                    Box left = MakeDelimiter(node.LeftDelimiter, height, style, true, 0, MathAtom.Open, decorations);
                    Box right = MakeDelimiter(node.RightDelimiter, height, style, true, 0, MathAtom.Close, decorations);
                    x[node.FirstChild] = left.Width; y[node.FirstChild] = 0;
                    Box result = new() { Width = left.Width + body.Width + right.Width, Height = body.Height, Depth = body.Depth, Atom = MathAtom.Inner };
                    AttachDelimiter(ref result, left, 0, decorations);
                    AttachDelimiter(ref result, right, left.Width + body.Width, decorations);
                    boxes[id] = result;
                    break;
                }
                case NodeKind.Accent:
                    boxes[id] = BuildAccent(nodes, id, style, boxes, x, y, decorations);
                    break;
                case NodeKind.Matrix:
                    boxes[id] = BuildMatrix(nodes, node, style, options.FontSize, boxes, x, y, decorations);
                    break;
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
        var textGlyphs = new List<MathTextGlyph>();
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
            for (int decorationId = box.FirstDecoration; decorationId != 0;)
            {
                Decoration decoration = decorations[decorationId - 1];
                decorationId = decoration.Next;
                if (decoration.Kind == DecorationKind.Glyph)
                    EmitGlyph(decoration.Font, decoration.GlyphIndex, decoration.CodePoint, px + decoration.X, py + decoration.Y, decoration.ScaleX);
                else if (decoration.Kind == DecorationKind.Rule)
                {
                    double left = (px + decoration.X) * fontSize, upper = (py + decoration.Y) * fontSize;
                    double width = decoration.Width * fontSize, height = decoration.Height * fontSize;
                    if (decoration.Vertical) { double exactWidth = width; width = Math.Max(1, Math.Floor(width)); left += exactWidth - width; }
                    else { double exactHeight = height; height = Math.Max(1, Math.Floor(height)); upper += exactHeight - height; }
                    rules.Add(new(left, upper, width, height)); AddInk(left, upper, left + width, upper + height);
                }
                else
                {
                    double dx = (px + decoration.X) * fontSize, dy = (py + decoration.Y) * fontSize;
                    double sx = decoration.ScaleX * fontSize, sy = decoration.ScaleY * fontSize;
                    MathBounds clip = new((px + decoration.Clip.X) * fontSize, (py + decoration.Clip.Y) * fontSize, decoration.Clip.Width * fontSize, decoration.Clip.Height * fontSize);
                    paths.Add(new(decoration.Path!, dx, dy, sx, sy, clip));
                    MathBounds pathBounds = PathBounds(decoration.Path!);
                    AddInk(Math.Max(clip.X, dx + pathBounds.X * sx), Math.Max(clip.Y, dy + pathBounds.Y * sy), Math.Min(clip.X + clip.Width, dx + (pathBounds.X + pathBounds.Width) * sx), Math.Min(clip.Y + clip.Height, dy + (pathBounds.Y + pathBounds.Height) * sy));
                }
            }
            switch (node.Kind)
            {
                case NodeKind.Symbol:
                case NodeKind.Delimiter:
                case NodeKind.Operator:
                {
                    if (box.TextShaped)
                    {
                        MathTextShape shape = box.TextShape;
                        double size = Scale(styles[id]) * fontSize;
                        MathBounds clip = new(px * fontSize + shape.InkBounds.X * size, py * fontSize + shape.InkBounds.Y * size, shape.InkBounds.Width * size, shape.InkBounds.Height * size);
                        paths.Add(new(shape.Segments, px * fontSize, py * fontSize, size, size, clip));
                        textGlyphs.Add(new(node.CodePoint, px * fontSize, py * fontSize, size, shape.FontFamily));
                        AddInk(clip.X, clip.Y, clip.X + clip.Width, clip.Y + clip.Height);
                    }
                    if (box.Symbol) EmitGlyph(box.Font, box.GlyphIndex, FontData.Glyphs[box.GlyphIndex].CodePoint, px, py + box.GlyphY, box.GlyphScale == 0 ? Scale(styles[id]) : box.GlyphScale);
                    if (node.Kind == NodeKind.Operator && node.FirstChild >= 0) Place(node.FirstChild);
                    break;
                }
                case NodeKind.Space:
                    break;
                case NodeKind.Row:
                case NodeKind.Style:
                case NodeKind.Text:
                case NodeKind.Matrix:
                case NodeKind.MatrixRow:
                case NodeKind.MatrixCell:
                {
                    int length = Collect(nodes, node.FirstChild, sequence, collectStack);
                    for (int i = length - 1; i >= 0; i--) Place(sequence[i]);
                    break;
                }
                case NodeKind.Font:
                case NodeKind.Delimited:
                    Place(node.FirstChild);
                    break;
                case NodeKind.Accent:
                    if (node.SecondChild >= 0) Place(node.SecondChild);
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
                    if (node.HasBar && rh > 0)
                    {
                        double paintedHeight = Math.Max(1, Math.Floor(rh));
                        double paintedY = ry + rh - paintedHeight;
                        rules.Add(new(rx, paintedY, rw, paintedHeight));
                        if (rw > 0) AddInk(rx, paintedY, rx + rw, paintedY + paintedHeight);
                    }
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
        return new(glyphs.ToArray(), rules.ToArray(), paths.ToArray(), new(Math.Max(0, outer.Width) * fontSize, (outer.Height + outer.Depth) * fontSize), outer.Height * fontSize, bounds, textGlyphs.ToArray());

        void Push(int child, byte childStyle) { if (child >= 0) frames[top++] = new(child, childStyle, false); }
        void EmitGlyph(int font, int glyphIndex, int codePoint, double gx, double gy, double emSize)
        {
            FontGlyph glyph = FontData.Glyphs[glyphIndex]; double size = emSize * fontSize;
            glyphs.Add(new(font, glyph.GlyphId, codePoint, gx * fontSize, gy * fontSize, size));
            GlyphInk ink = GlyphInkData.Bounds[glyphIndex]; double units = size / FontData.Fonts[font].UnitsPerEm;
            if (ink.Left != ink.Right && ink.Top != ink.Bottom)
                AddInk(gx * fontSize + ink.Left * units, gy * fontSize - ink.Top * units, gx * fontSize + ink.Right * units, gy * fontSize - ink.Bottom * units);
        }
        void AddInk(double left, double upper, double right, double bottom)
        {
            if (right <= left || bottom <= upper) return;
            inkLeft = Math.Min(inkLeft, left); inkTop = Math.Min(inkTop, upper);
            inkRight = Math.Max(inkRight, right); inkBottom = Math.Max(inkBottom, bottom);
        }
    }

    private static Box Symbol(in SemanticNode node, double scale, IMathTextShaper? textShaper)
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
            else if (node.TextMode && textShaper is not null && textShaper.TryShape(node.CodePoint, node.Font, out MathTextShape shape) &&
                double.IsFinite(shape.Advance) && shape.Advance >= 0 && shape.Advance <= 16 && !shape.Segments.IsEmpty &&
                double.IsFinite(shape.InkBounds.X) && double.IsFinite(shape.InkBounds.Y) && double.IsFinite(shape.InkBounds.Width) && double.IsFinite(shape.InkBounds.Height) &&
                shape.InkBounds.Width >= 0 && shape.InkBounds.Height >= 0)
            {
                // KaTeX's unsupported text fallback borrows the Main M TeX
                // height/depth; actual advance and ink come from the shaper.
                Find(8, 'M', out _, out TexGlyphMetric fallback);
                return new() { Width = shape.Advance * scale, Height = fallback.Height * scale, Depth = fallback.Depth * scale, Atom = node.Atom, TextShaped = true, TextShape = shape };
            }
            else throw new CompileFailure(new MathDiagnostic(MathDiagnosticCode.UnknownSymbol, node.Range, $"Locked font data has no glyph U+{node.CodePoint:X}."));
        }
        FontGlyph glyph = FontData.Glyphs[glyphIndex];
        double italic = node.TextMode || node.Font == MathFont.MathItalic ? 0 : metric.ItalicCorrection;
        return new() { Width = (glyph.AdvanceUnits / (double)FontData.Fonts[desired].UnitsPerEm + Math.Max(0, italic)) * scale, Height = metric.Height * scale, Depth = metric.Depth * scale, Italic = italic * scale, Skew = metric.Skew * scale, Atom = node.Atom, Character = true, Symbol = true, Font = desired, GlyphIndex = glyphIndex };
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
            if (node.Kind == NodeKind.Style || node.Kind == NodeKind.Row && node.Value == 2)
            {
                if (node.NextSibling >= 0) stack[depth++] = node.NextSibling;
                current = node.FirstChild;
                // The parser stores a style's implicit body in a transparent
                // row. Its atoms participate in the enclosing math list.
                if (node.Kind == NodeKind.Style && current >= 0 && nodes[current].Kind == NodeKind.Row && nodes[current].Value == 0 && nodes[current].NextSibling < 0)
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
