using System.Diagnostics;

namespace VelociTex;

/// <summary>
/// TeX box rules projected directly into compact boxes and drawing commands.
/// Coordinates remain double until the Windows drawing boundary. The numerical
/// rules correspond to the locked 0.19.0 buildHTML, supsub, genfrac and sqrt
/// builders; no HTML, CSS boxes, or per-node object graph is constructed.
/// </summary>
internal static partial class MathLayout
{
    private readonly record struct Frame(int Node, LayoutContext Context, bool Complete);
    private readonly record struct Placement(int Node, double X, double Y, int State = 0, bool Decorations = false);
    private struct Box
    {
        internal double Width, Height, Depth, Italic;
        internal MathAtom Atom;
        internal bool Character, Symbol;
        internal bool NoAtom;
        internal int Font, GlyphIndex, FirstDecoration;
        internal double GlyphScale, GlyphY;
        internal double Skew;
        internal bool TextShaped;
        internal MathTextShape TextShape;
        internal bool TextRunShaped;
        internal MathTextRunShape TextRunShape;
        internal string? RunText;
        internal MathPathSegment[]? Path;
        internal double PathX, PathY, PathScale, PathHeight;
        internal bool DecorationsAfter;
    }

    private static ReadOnlySpan<byte> SupStyles => [4, 5, 4, 5, 6, 7, 6, 7];
    private static ReadOnlySpan<byte> SubStyles => [5, 5, 5, 5, 7, 7, 7, 7];
    private static ReadOnlySpan<byte> NumeratorStyles => [2, 3, 4, 5, 6, 7, 6, 7];
    private static ReadOnlySpan<byte> DenominatorStyles => [3, 3, 5, 5, 7, 7, 7, 7];
    private readonly record struct GlyphLookup(int GlyphIndex, TexGlyphMetric Metric, bool HasMetric);
    internal static bool ProfileEnabled;
    internal static readonly long[] ProfileTicks = new long[4];
    [ThreadStatic] private static LayoutScratch? Scratch;
    private sealed class LayoutScratch
    {
        internal Box[] Boxes = [];
        internal LayoutContext[] Styles = [];
        internal bool[] ScriptBases = [];
        internal double[] X = [], Y = [];
        internal Frame[] Frames = [];
        internal int[] Sequence = [], CollectStack = [], ColorStack = [];
        internal MathAtom[] Atoms = [];
        internal MathColor?[] Colors = [];
        internal bool[] Hidden = [];
        internal Placement[] Placements = [];
        internal readonly List<Decoration> Decorations = [];
        internal readonly List<MathGlyph> Glyphs = [];
        internal readonly List<MathRule> Rules = [];
        internal readonly List<MathPath> Paths = [];
        internal readonly List<MathTextGlyph> TextGlyphs = [];
        internal readonly List<MathTextRun> TextRuns = [];
        internal readonly List<MathPaintCommand> PaintCommands = [];
        internal readonly List<MathPaintState> PaintStates = [];
        internal readonly Dictionary<MathPaintState, int> PaintStateIds = [];
    }
    private static T[] Ensure<T>(ref T[] buffer, int length)
    {
        if (buffer.Length >= length) return buffer;
        buffer = new T[length];
        return buffer;
    }
    private static readonly Dictionary<long, GlyphLookup> GlyphLookups = BuildGlyphLookups();
    private static Dictionary<long, GlyphLookup> BuildGlyphLookups()
    {
        var map = new Dictionary<long, GlyphLookup>(4096);
        for (int font = 0; font < FontData.Fonts.Length; font++)
        {
            FontDescriptor descriptor = FontData.Fonts[font];
            for (int glyph = descriptor.GlyphStart; glyph < descriptor.GlyphStart + descriptor.GlyphCount; glyph++)
            {
                int codePoint = FontData.Glyphs[glyph].CodePoint;
                int low = descriptor.MetricStart, high = low + descriptor.MetricCount - 1;
                GlyphLookup lookup = new(glyph, default, false);
                while (low <= high)
                {
                    int middle = (low + high) >>> 1, current = FontData.TexMetrics[middle].CodePoint;
                    if (current == codePoint) { lookup = new(glyph, FontData.TexMetrics[middle], true); break; }
                    if (current < codePoint) low = middle + 1; else high = middle - 1;
                }
                map[(long)font << 32 | (uint)codePoint] = lookup;
            }
        }
        return map;
    }

    internal static LayoutOutput Build(SemanticNode[] nodes, int root, in MathOptions options, CancellationToken cancellationToken)
    {
        long profileStart = 0, profileArrays = 0, profileColors = 0, profileBuild = 0;
        if (ProfileEnabled) profileStart = Stopwatch.GetTimestamp();
        int count = nodes.Length;
        var scratch = Scratch ??= new LayoutScratch();
        var boxes = Ensure(ref scratch.Boxes, count);
        var styles = Ensure(ref scratch.Styles, count);
        var scriptBases = Ensure(ref scratch.ScriptBases, count);
        Array.Clear(scriptBases, 0, count);
        for (int i = 0; i < count; i++) if (nodes[i].Kind == NodeKind.Script && nodes[i].FirstChild >= 0) scriptBases[nodes[i].FirstChild] = true;
        var x = Ensure(ref scratch.X, count);
        var y = Ensure(ref scratch.Y, count);
        var frames = Ensure(ref scratch.Frames, checked(count * 2 + 4));
        var sequence = Ensure(ref scratch.Sequence, count);
        var collectStack = Ensure(ref scratch.CollectStack, count);
        var atoms = Ensure(ref scratch.Atoms, count);
        var colors = Ensure(ref scratch.Colors, count);
        Array.Clear(colors, 0, count);
        var hidden = Ensure(ref scratch.Hidden, count);
        Array.Clear(hidden, 0, count);
        var colorStack = Ensure(ref scratch.ColorStack, count); int colorTop = 0;
        if (ProfileEnabled) profileArrays = Stopwatch.GetTimestamp();
        colorStack[colorTop++] = root;
        while (colorTop != 0)
        {
            int colorNode = colorStack[--colorTop]; ref readonly SemanticNode colored = ref nodes[colorNode];
            if (colored.Kind == NodeKind.Color) colors[colorNode] = MathColorUtilities.TryParse(colored.Text2, out MathColor explicitColor) ? explicitColor : colors[colorNode];
            if (colored.Kind == NodeKind.Box && colored.Text is "\\phantom" or "\\vphantom" or "\\hphantom") hidden[colorNode] = true;
            if (colored.Kind is NodeKind.Row or NodeKind.Style or NodeKind.Font or NodeKind.Text or NodeKind.Color or NodeKind.Sizing or NodeKind.Class or NodeKind.Operator or NodeKind.Matrix or NodeKind.MatrixRow or NodeKind.MatrixCell || colored.Kind == NodeKind.Box && colored.Text is "\\phantom" or "\\hbox" or "\\pmb")
            { for (int child = colored.FirstChild; child >= 0; child = nodes[child].NextSibling) ColorChild(child); if (colored.Kind == NodeKind.MatrixRow) ColorChild(colored.SecondChild); }
            else { ColorChild(colored.FirstChild); ColorChild(colored.SecondChild); ColorChild(colored.ThirdChild); ColorChild(colored.FourthChild); }
            void ColorChild(int child) { if (child >= 0) { colors[child] = colors[colorNode]; hidden[child] = hidden[colorNode]; colorStack[colorTop++] = child; } }
        }
        var decorations = scratch.Decorations; decorations.Clear();
        if (ProfileEnabled) profileColors = Stopwatch.GetTimestamp();
        int top = 0, batches = 0;
        frames[top++] = new(root, new(options.DisplayMode ? (byte)0 : (byte)2), false);
        while (top != 0)
        {
            if ((++batches & 63) == 0) cancellationToken.ThrowIfCancellationRequested();
            Frame frame = frames[--top];
            int id = frame.Node;
            ref readonly SemanticNode node = ref nodes[id];
            LayoutContext style = node.Kind == NodeKind.Style ? frame.Context.WithStyle((byte)((int)node.Style * 2)) : node.Kind == NodeKind.Sizing ? frame.Context.WithSize((byte)node.Value) : frame.Context;
            styles[id] = style;
            if (!frame.Complete)
            {
                frames[top++] = new(id, style, true);
                switch (node.Kind)
                {
                    case NodeKind.Symbol:
                    case NodeKind.Space:
                    case NodeKind.Delimiter:
                    case NodeKind.Rule:
                        break;
                    case NodeKind.Choice:
                        Push(style.Style<2?node.FirstChild:style.Style<4?node.SecondChild:style.Style<6?node.ThirdChild:node.FourthChild,style);
                        break;
                    case NodeKind.MathMlChoice:
                        Push(node.FirstChild, style);
                        break;
                    case NodeKind.Tag:
                        Push(node.FirstChild, style); Push(node.SecondChild, style);
                        break;
                    case NodeKind.MatrixRow:
                        for (int child = node.FirstChild; child >= 0; child = nodes[child].NextSibling) Push(child, style);
                        Push(node.SecondChild,style); break;
                    case NodeKind.Script:
                        Push(node.FirstChild, style);
                        Push(node.SecondChild, style.WithStyle(SupStyles[style.Style]));
                        Push(node.ThirdChild, style.WithStyle(SubStyles[style.Style]));
                        break;
                    case NodeKind.Fraction:
                        Push(node.FirstChild, style.WithStyle(NumeratorStyles[style.Style]));
                        Push(node.SecondChild, style.WithStyle(DenominatorStyles[style.Style]));
                        break;
                    case NodeKind.Radical:
                        Push(node.FirstChild, style.WithStyle((byte)(style.Style | 1)));
                        if (node.SecondChild >= 0) Push(node.SecondChild, style.WithStyle(6));
                        break;
                    case NodeKind.Accent:
                        if (node.Text?.StartsWith("\\x", StringComparison.Ordinal) == true || node.Text?.StartsWith("\\\\cd", StringComparison.Ordinal) == true)
                        { Push(node.FirstChild, style.WithStyle(SupStyles[style.Style])); Push(node.SecondChild, style.WithStyle(SubStyles[style.Style])); }
                        else if (node.Text is "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket") Push(node.FirstChild, style.WithStyle(0));
                        else Push(node.FirstChild, node.Text == "\\underline" ? style : style.WithStyle((byte)(style.Style | 1)));
                        break;
                    case NodeKind.Box:
                        for (int child = node.FirstChild; child >= 0; child = nodes[child].NextSibling)
                            Push(child, node.Text is "cdleft" or "cdright" ? style.WithStyle(SupStyles[style.Style]) : style);
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
                {
                    Box? operatorBody=node.CodePoint==0&&node.FirstChild>=0&&nodes[node.FirstChild].NextSibling>=0?BuildRow(nodes,id,style,styles,boxes,x,y,sequence,collectStack,atoms,options):null;
                    boxes[id] = BuildOperator(nodes,node,options.DisplayMode, style, boxes, decorations,operatorBody);
                    if (node.FirstChild >= 0)
                    {
                        if(operatorBody is null)x[node.FirstChild]=0;
                        for(int child=node.FirstChild;child>=0;child=nodes[child].NextSibling)y[child]=boxes[id].GlyphY;
                    }
                    break;
                }
                case NodeKind.Delimiter:
                    boxes[id] = MakeDelimiter(node.CodePoint, node.Value > 0 ? SizeHeight((int)node.Value) : 0, style, node.Value == 0, (int)node.Value, node.Atom, decorations);
                    break;
                case NodeKind.Space:
                    // The supported fractional spaces are mu measurements;
                    // quad and qquad are em measurements (units.ts).
                    boxes[id] = new() { Width = node.Text == "\\\\" ? 0 : node.Text is "\\kern" or "\\mkern" or "\\hskip" or "\\mskip" or "\\hspace" or "\\hspace*" ? Length(node.Length1, style, options) : node.Value * scale * (Math.Abs(node.Value) < 1 ? MathConstants.quad(metricIndex) : 1) };
                    break;
                case NodeKind.Row:
                case NodeKind.Style:
                case NodeKind.Text:
                case NodeKind.MatrixCell:
                case NodeKind.MatrixRow:
                case NodeKind.Color:
                case NodeKind.Sizing:
                {
                    boxes[id] = BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options);
                    if(node.Kind==NodeKind.Row&&node.TextMode)TryShapeTextRow(nodes,id,style,options,colors,boxes,x);
                    break;
                }
                case NodeKind.Class:
                {
                    Box child = BuildRow(nodes, id, style, styles, boxes, x, y, sequence, collectStack, atoms, options); child.Atom = node.Atom; child.FirstDecoration = 0;
                    x[node.FirstChild] = y[node.FirstChild] = 0; boxes[id] = child; break;
                }
                case NodeKind.Choice:
                case NodeKind.MathMlChoice:
                {
                    int selected = node.Kind == NodeKind.MathMlChoice ? node.FirstChild : style.Style < 2 ? node.FirstChild : style.Style < 4 ? node.SecondChild : style.Style < 6 ? node.ThirdChild : node.FourthChild;
                    Box child = boxes[selected]; child.FirstDecoration = 0; x[selected] = y[selected] = 0; boxes[id] = child; break;
                }
                case NodeKind.Box:
                    boxes[id] = BuildBox(nodes, node, style, options, boxes, x, y, node.Text is "\\phantom" or "\\hbox" or "\\pmb" ? BuildRow(nodes,id,style,styles,boxes,x,y,sequence,collectStack,atoms,options) : null);
                    break;
                case NodeKind.Rule:
                {
                    double width = Length(node.Length1, style, options), height = Length(node.Length2, style, options), shift = Length(node.Length3, style, options);
                    Box result = new() { Width = width, Height = height + shift, Depth = -shift, Atom = MathAtom.Ord };
                    if (width > 0 && height > 0)
                    {
                        // The locked border-based rule has a one-pixel inline
                        // content strut plus its rounded CSS top border.
                        double paintedHeight=(1+Math.Max(1,Math.Floor(height*options.FontSize)))/options.FontSize;
                        AddRule(ref result, 0, -paintedHeight - shift, width, paintedHeight, decorations, exact: true);
                    }
                    boxes[id] = result; break;
                }
                case NodeKind.Enclose:
                    boxes[id] = BuildEnclose(nodes, node, style, options, boxes, x, y, decorations);
                    break;
                case NodeKind.Verb:
                    boxes[id] = BuildVerb(node, style, decorations);
                    break;
                case NodeKind.Tag:
                {
                    Box body = boxes[node.FirstChild], tag = node.SecondChild >= 0 ? boxes[node.SecondChild] : default;
                    x[node.FirstChild] = y[node.FirstChild] = 0;
                    if (node.SecondChild >= 0) { x[node.SecondChild] = options.Leqno ? 0 : Math.Max(body.Width, options.DisplayWidth / options.FontSize) - tag.Width; y[node.SecondChild] = 0; }
                    body.FirstDecoration = 0; boxes[id] = body; break;
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
                        LayoutContext supStyle = style.WithStyle(SupStyles[style.Style]), subStyle = style.WithStyle(SubStyles[style.Style]);
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
                    double specifiedRule=node.Text=="\\genfrac"?Length(node.Length1,style,options):node.Value*scale;
                    double rule = node.HasBar ? Math.Max(node.Value > 0 ? specifiedRule : defaultRule, options.MinRuleThickness * scale) : 0;
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
                    int length = Collect(nodes, nodes[node.FirstChild].FirstChild, sequence, collectStack, styles);
                    for (int i = 0; i < length; i++)
                    {
                        int middle = sequence[i];
                        if (nodes[middle].Kind == NodeKind.Delimiter && nodes[middle].Value == 0)
                            boxes[middle] = MakeDelimiter(nodes[middle].CodePoint, height, styles[middle], true, 0, MathAtom.Ord, decorations);
                    }
                    body = boxes[node.FirstChild] = BuildRow(nodes, node.FirstChild, styles[node.FirstChild], styles, boxes, x, y, sequence, collectStack, atoms,options);
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
                    boxes[id] = BuildAccent(nodes, id, style, options, boxes, x, y, decorations);
                    break;
                case NodeKind.Matrix:
                    boxes[id] = BuildMatrix(nodes, node, style, options, boxes, x, y, decorations);
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
                    double baseFactor = scale / Scale(style.Style);
                    int size = SelectRadical(minimumHeight / baseFactor);
                    double multiplier = Scale(style.Style);
                    double extraVinculum = Math.Max(0, options.MinRuleThickness - MathConstants.sqrtRuleThickness(metricIndex));
                    double texHeight, spanHeight, viewBoxHeight, advance, minWidth;
                    MathPathSegment[] path;
                    if (size == 0)
                    {
                        if (localHeight < 1) multiplier = 1;
                        else if (localHeight < 1.4) multiplier = 0.7;
                        texHeight = (1 + extraVinculum) * scale / multiplier;
                        spanHeight = (1.08 + extraVinculum) * scale / multiplier;
                        viewBoxHeight = 1080 + 1000 * extraVinculum;
                        advance = 0.833 * scale / multiplier;
                        minWidth = 0.853 * scale;
                        path = RadicalPathData.sqrtMain;
                    }
                    else if (size < 5)
                    {
                        double maxHeight = size switch { 1 => 1.2, 2 => 1.8, 3 => 2.4, _ => 3 };
                        texHeight = (maxHeight + extraVinculum) * baseFactor;
                        spanHeight = (maxHeight + extraVinculum + 0.08) * baseFactor;
                        viewBoxHeight = 1080 * maxHeight;
                        advance = baseFactor;
                        minWidth = 1.02 * scale;
                        path = size switch { 1 => RadicalPathData.sqrtSize1, 2 => RadicalPathData.sqrtSize2, 3 => RadicalPathData.sqrtSize3, _ => RadicalPathData.sqrtSize4 };
                    }
                    else
                    {
                        texHeight = minimumHeight + extraVinculum * scale;
                        spanHeight = minimumHeight + (extraVinculum + 0.08) * scale;
                        viewBoxHeight = Math.Floor(1000 * localHeight + extraVinculum) + 80;
                        advance = 1.056 * scale;
                        minWidth = 0.742 * scale;
                        path = MathPathCache.Radical(viewBoxHeight);
                    }
                    if (extraVinculum > 0) path = MathPathCache.RadicalWithRule(size, extraVinculum, viewBoxHeight);
                    double rule = (MathConstants.sqrtRuleThickness(metricIndex) + extraVinculum) * multiplier * scale;
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
        if (ProfileEnabled) profileBuild = Stopwatch.GetTimestamp();
        double fontSize = options.FontSize;
        Box outer = boxes[root];
        double rootOffset=0;
        if(options.DisplayMode&&options.DisplayWidth>0)
        {
            double available=options.DisplayWidth/fontSize;
            rootOffset=options.Fleqn?2:Math.Max(0,(available-outer.Width)/2);
            outer.Width=Math.Max(available,outer.Width+rootOffset);
            // Tags are aligned to the host's display box, while the formula
            // body receives the host's centering/flush-left offset.
            for(int i=0;i<count;i++)if(nodes[i].Kind is NodeKind.Tag or NodeKind.MatrixRow && nodes[i].SecondChild>=0)x[nodes[i].SecondChild]-=rootOffset;
        }
        var glyphs = scratch.Glyphs; glyphs.Clear();
        var rules = scratch.Rules; rules.Clear();
        var paths = scratch.Paths; paths.Clear();
        var textGlyphs = scratch.TextGlyphs; textGlyphs.Clear();
        var textRuns = scratch.TextRuns; textRuns.Clear();
        var paintCommands = scratch.PaintCommands; paintCommands.Clear();
        var paintStates = scratch.PaintStates; paintStates.Clear(); paintStates.Add(new(null, MathTransform.Identity));
        var paintStateIds = scratch.PaintStateIds; paintStateIds.Clear(); paintStateIds[paintStates[0]] = 0;
        var placements = Ensure(ref scratch.Placements, checked(count * 3 + 4));
        int placementCount = 0;
        int activePaintState = 0; bool activeVisible = true;
        placements[placementCount++] = new(root, rootOffset, outer.Height);
        double inkLeft = double.PositiveInfinity, inkTop = double.PositiveInfinity, inkRight = double.NegativeInfinity, inkBottom = double.NegativeInfinity;
        while (placementCount != 0)
        {
            if ((++batches & 63) == 0) cancellationToken.ThrowIfCancellationRequested();
            Placement placement = placements[--placementCount];
            if ((long)glyphs.Count + rules.Count + paths.Count >= 262_144)
                throw new CompileFailure(new(MathDiagnosticCode.NodeLimit, default, "The bounded drawing-instruction budget was exceeded."));
            int id = placement.Node;
            ref readonly SemanticNode node = ref nodes[id];
            ref readonly Box box = ref boxes[id];
            double px = placement.X, py = placement.Y;
            MathPaintState inherited = paintStates[placement.State];
            MathPaintState currentState = new(colors[id] ?? inherited.Color, inherited.Transform);
            int stateId = GetState(currentState);
            bool visible = !hidden[id] && currentState.Color?.A != 0;
            activePaintState = stateId; activeVisible = visible;
            if (!placement.Decorations && box.DecorationsAfter) placements[placementCount++] = placement with { Decorations = true, State = stateId };
            for (int decorationId = placement.Decorations || !box.DecorationsAfter ? box.FirstDecoration : 0; decorationId != 0;)
            {
                Decoration decoration = decorations[decorationId - 1];
                decorationId = decoration.Next;
                int decorationState = decoration.HasColor ? GetState(currentState with { Color = decoration.Color }) : stateId;
                activePaintState = decorationState; activeVisible = !hidden[id] && paintStates[decorationState].Color?.A != 0;
                if (decoration.Kind == DecorationKind.Glyph)
                    EmitGlyph(decoration.Font, decoration.GlyphIndex, decoration.CodePoint, px + decoration.X, py + decoration.Y, decoration.ScaleX);
                else if (decoration.Kind == DecorationKind.Rule)
                {
                    double left = (px + decoration.X) * fontSize, upper = (py + decoration.Y) * fontSize;
                    double width = decoration.Width * fontSize, height = decoration.Height * fontSize;
                    if (!decoration.Exact)
                    {
                        if (decoration.Vertical) { double exactWidth = width; width = Math.Max(1, Math.Floor(width)); left += exactWidth - width; }
                        else { double exactHeight = height; height = Math.Max(1, Math.Floor(height)); upper += exactHeight - height; }
                    }
                    rules.Add(new(left, upper, width, height)); AddPaint(MathPaintKind.Rule, rules.Count - 1, decorationState); AddInk(left, upper, left + width, upper + height);
                }
                else
                {
                    double dx = (px + decoration.X) * fontSize, dy = (py + decoration.Y) * fontSize;
                    double sx = decoration.ScaleX * fontSize, sy = decoration.ScaleY * fontSize;
                    MathBounds clip = new((px + decoration.Clip.X) * fontSize, (py + decoration.Clip.Y) * fontSize, decoration.Clip.Width * fontSize, decoration.Clip.Height * fontSize);
                    paths.Add(new(decoration.Path!, dx, dy, sx, sy, clip));
                    AddPaint(MathPaintKind.Path, paths.Count - 1, decorationState);
                    MathBounds pathBounds = PathBounds(decoration.Path!);
                    AddInk(Math.Max(clip.X, dx + pathBounds.X * sx), Math.Max(clip.Y, dy + pathBounds.Y * sy), Math.Min(clip.X + clip.Width, dx + (pathBounds.X + pathBounds.Width) * sx), Math.Min(clip.Y + clip.Height, dy + (pathBounds.Y + pathBounds.Height) * sy));
                }
            }
            activePaintState = stateId; activeVisible = visible;
            if (placement.Decorations) continue;
            if(box.TextRunShaped)
            {
                double em=Scale(styles[id])*fontSize;
                int firstRunPath=paths.Count;
                foreach(MathPath runPath in box.TextRunShape.Paths.Span)
                {
                    MathBounds clip=new(px*fontSize+runPath.ClipBounds.X*em,py*fontSize+runPath.ClipBounds.Y*em,runPath.ClipBounds.Width*em,runPath.ClipBounds.Height*em);
                    paths.Add(new(runPath.Segments,px*fontSize+runPath.X*em,py*fontSize+runPath.Y*em,runPath.ScaleX*em,runPath.ScaleY*em,clip));
                    AddPaint(MathPaintKind.Path,paths.Count-1);
                }
                MathBounds runInk=box.TextRunShape.InkBounds;
                AddInk(px*fontSize+runInk.X*em,py*fontSize+runInk.Y*em,px*fontSize+(runInk.X+runInk.Width)*em,py*fontSize+(runInk.Y+runInk.Height)*em);
                textRuns.Add(new(box.RunText!,px*fontSize,py*fontSize,em,firstRunPath,paths.Count-firstRunPath,box.TextRunShape.Advance*em));continue;
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
                        AddPaint(MathPaintKind.Path, paths.Count - 1);
                        textGlyphs.Add(new(node.CodePoint, px * fontSize, py * fontSize, size, shape.FontFamily));
                        AddInk(clip.X, clip.Y, clip.X + clip.Width, clip.Y + clip.Height);
                    }
                    if (box.Symbol) EmitGlyph(box.Font, box.GlyphIndex, FontData.Glyphs[box.GlyphIndex].CodePoint, px, py + box.GlyphY, box.GlyphScale == 0 ? Scale(styles[id]) : box.GlyphScale);
                    if (node.Kind == NodeKind.Operator && node.FirstChild >= 0) for(int child=node.FirstChild;child>=0;child=nodes[child].NextSibling)Place(child);
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
                case NodeKind.Color:
                case NodeKind.Sizing:
                {
                    int length = Collect(nodes, node.FirstChild, sequence, collectStack, styles);
                    if (node.Kind == NodeKind.MatrixRow && node.SecondChild >= 0) Place(node.SecondChild);
                    for (int i = length - 1; i >= 0; i--) Place(sequence[i]);
                    break;
                }
                case NodeKind.Font:
                case NodeKind.Delimited:
                    Place(node.FirstChild);
                    break;
                case NodeKind.Class:
                    for (int child = node.FirstChild; child >= 0; child = nodes[child].NextSibling) Place(child);
                    break;
                case NodeKind.Choice:
                    Place(styles[id].Style < 2 ? node.FirstChild : styles[id].Style < 4 ? node.SecondChild : styles[id].Style < 6 ? node.ThirdChild : node.FourthChild);
                    break;
                case NodeKind.MathMlChoice:
                    Place(node.FirstChild);
                    break;
                case NodeKind.Box:
                {
                    if (node.Text is "\\reflectbox" or "\\mathreflectbox")
                    {
                        MathTransform reflection = new(-1, 0, 0, 1, (2 * px + box.Width) * fontSize, 0);
                        int reflected = GetState(currentState with { Transform = Then(reflection, currentState.Transform) });
                        placements[placementCount++] = new(node.FirstChild, px + x[node.FirstChild], py + y[node.FirstChild], reflected);
                    }
                    else if (node.Text == "\\pmb")
                    {
                        Place(node.FirstChild);
                        double em = Scale(styles[id]) * fontSize;
                        int shadow = GetState(currentState with { Transform = Then(new(1, 0, 0, 1, .02 * em, .01 * em), currentState.Transform) });
                        placements[placementCount++] = new(node.FirstChild, px + x[node.FirstChild], py + y[node.FirstChild], shadow);
                    }
                    else for (int child = node.FirstChild; child >= 0; child = nodes[child].NextSibling) Place(child);
                    break;
                }
                case NodeKind.Enclose:
                    Place(node.FirstChild);
                    break;
                case NodeKind.Tag:
                    if (node.SecondChild >= 0) Place(node.SecondChild);
                    Place(node.FirstChild);
                    break;
                case NodeKind.Accent:
                    if (node.SecondChild >= 0) Place(node.SecondChild);
                    if(node.FirstChild>=0)Place(node.FirstChild);
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
                        AddPaint(MathPaintKind.Rule, rules.Count - 1);
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
                    double radicalOffset = box.PathX;
                    var clip = new MathBounds(rx, ry, (box.Width - radicalOffset) * fontSize, box.PathHeight * fontSize);
                    paths.Add(new(box.Path!, rx, ry, ps, ps, clip));
                    AddPaint(MathPaintKind.Path, paths.Count - 1);
                    MathBounds pb = PathBounds(box.Path!);
                    AddInk(Math.Max(clip.X, rx + pb.X * ps), Math.Max(clip.Y, ry + pb.Y * ps), Math.Min(clip.X + clip.Width, rx + (pb.X + pb.Width) * ps), Math.Min(clip.Y + clip.Height, ry + (pb.Y + pb.Height) * ps));
                    if (node.SecondChild >= 0) Place(node.SecondChild);
                    Place(node.FirstChild);
                    break;
                }
            }
            void Place(int child) => placements[placementCount++] = new(child, px + x[child], py + y[child], stateId);
            void AddPaint(MathPaintKind kind, int index, int? explicitState = null) { if (!hidden[id] && paintStates[explicitState ?? stateId].Color?.A != 0) paintCommands.Add(new(kind, index, explicitState ?? stateId)); }
        }
        var bounds = double.IsPositiveInfinity(inkLeft) ? new MathBounds(0, 0, 0, 0) : new MathBounds(inkLeft, inkTop, inkRight - inkLeft, inkBottom - inkTop);
        if (ProfileEnabled)
        {
            long end = Stopwatch.GetTimestamp();
            ProfileTicks[0] = profileArrays - profileStart; ProfileTicks[1] = profileColors - profileArrays;
            ProfileTicks[2] = profileBuild - profileColors; ProfileTicks[3] = end - profileBuild;
        }
        LayoutOutput output = new(glyphs.ToArray(), rules.ToArray(), paths.ToArray(), new(Math.Max(0, outer.Width) * fontSize, (outer.Height + outer.Depth) * fontSize), outer.Height * fontSize, bounds, textGlyphs.ToArray(), paintCommands.ToArray(), paintStates.ToArray(), textRuns.ToArray());
        glyphs.Clear(); rules.Clear(); paths.Clear(); textGlyphs.Clear(); textRuns.Clear(); paintCommands.Clear(); paintStates.Clear(); paintStateIds.Clear(); decorations.Clear();
        return output;

        void Push(int child, LayoutContext childStyle) { if (child >= 0) frames[top++] = new(child, childStyle, false); }
        void EmitGlyph(int font, int glyphIndex, int codePoint, double gx, double gy, double emSize)
        {
            FontGlyph glyph = FontData.Glyphs[glyphIndex]; double size = emSize * fontSize;
            glyphs.Add(new(font, glyph.GlyphId, codePoint, gx * fontSize, gy * fontSize, size));
            // Emission is synchronous within the current placement. The state
            // is looked up from the document's logical provenance color.
            if (activeVisible) paintCommands.Add(new(MathPaintKind.Glyph, glyphs.Count - 1, activePaintState));
            GlyphInk ink = GlyphInkData.Bounds[glyphIndex]; double units = size / FontData.Fonts[font].UnitsPerEm;
            if (ink.Left != ink.Right && ink.Top != ink.Bottom)
                AddInk(gx * fontSize + ink.Left * units, gy * fontSize - ink.Top * units, gx * fontSize + ink.Right * units, gy * fontSize - ink.Bottom * units);
        }
        int GetState(MathPaintState state) { if (paintStateIds.TryGetValue(state, out int existing)) return existing; int id = paintStates.Count; paintStates.Add(state); paintStateIds.Add(state, id); return id; }
        void AddInk(double left, double upper, double right, double bottom)
        {
            if (!activeVisible) return;
            if (right <= left || bottom <= upper) return;
            MathTransform transform = paintStates[activePaintState].Transform;
            double x1 = left * transform.M11 + upper * transform.M21 + transform.Dx, y1 = left * transform.M12 + upper * transform.M22 + transform.Dy;
            double x2 = right * transform.M11 + upper * transform.M21 + transform.Dx, y2 = right * transform.M12 + upper * transform.M22 + transform.Dy;
            double x3 = left * transform.M11 + bottom * transform.M21 + transform.Dx, y3 = left * transform.M12 + bottom * transform.M22 + transform.Dy;
            double x4 = right * transform.M11 + bottom * transform.M21 + transform.Dx, y4 = right * transform.M12 + bottom * transform.M22 + transform.Dy;
            left = Math.Min(Math.Min(x1,x2),Math.Min(x3,x4)); right = Math.Max(Math.Max(x1,x2),Math.Max(x3,x4));
            upper = Math.Min(Math.Min(y1,y2),Math.Min(y3,y4)); bottom = Math.Max(Math.Max(y1,y2),Math.Max(y3,y4));
            inkLeft = Math.Min(inkLeft, left); inkTop = Math.Min(inkTop, upper);
            inkRight = Math.Max(inkRight, right); inkBottom = Math.Max(inkBottom, bottom);
        }
    }

    private static Box Symbol(in SemanticNode node, double scale, IMathTextShaper? textShaper)
    {
        int defaultFont = node.SymbolFont == MathFont.AMS ? 0 : node.IsMathOrd ? 10 : 8;
        int desired = node.Font switch
        {
            MathFont.MathItalic => 7, MathFont.MathNormal => 10, MathFont.Main or MathFont.MathRoman => 8, MathFont.MathBold => 5, MathFont.MainBoldItalic => 6,
            MathFont.MathBoldItalic => node.IsMathOrd ? 9 : 5,
            MathFont.MathSans => 13, MathFont.MathSansBold => 11, MathFont.MathSansItalic => 12,
            MathFont.MathSansBoldItalic => 8,
            MathFont.MathTypewriter => 19, MathFont.AMS => 0, MathFont.Caligraphic => 2,
            MathFont.Fraktur => 4, MathFont.Script => 14,
            _ => defaultFont,
        };
        if (node.FontIdOverride >= 0) desired = node.FontIdOverride;
        int metricFont = desired;
        if(node.TextMode&&node.FontIdOverride==10)desired=7;
        if(node.OriginalCodePoint is >=0x1d468 and <0x1d49c && desired==6)desired=9;
        if(node.OriginalCodePoint is >=0x1d56c and <0x1d5a0 && desired==4)desired=3;
        // Explicit math fonts change ords. The upstream mathsym path retains the
        // source Main/AMS family for punctuation and binary/relation atoms.
        if (node.FontIdOverride < 0 && node.Atom != MathAtom.Ord && node.Font is not MathFont.MathBoldItalic) desired = defaultFont;
        MathFont shapeFont=desired is 10 or 7?MathFont.MathItalic:desired==9?MathFont.MathBoldItalic:node.Font;
        if(node.TextMode&&Contextual(node.CodePoint)&&textShaper is IMathTextRunShaper contextual&&contextual.TryShapeRun(char.ConvertFromUtf32(node.CodePoint),shapeFont,out MathTextRunShape singleRun)&&ValidTextRun(singleRun,16))
        {
            if(!FallbackMetricData.Find(desired,node.CodePoint,true,out TexGlyphMetric runMetric)&&node.CodePoint is >=0x0530 and <=0x058f or >=0x0900 and <=0x10ff)
                Find(desired,'M',out _,out runMetric);
            return new(){Width=singleRun.Advance*scale,Height=runMetric.Height*scale,Depth=runMetric.Depth*scale,Atom=node.Atom,TextRunShaped=true,TextRunShape=singleRun,RunText=char.ConvertFromUtf32(node.CodePoint)};
        }
        if (!Find(desired, node.CodePoint, out int glyphIndex, out TexGlyphMetric metric))
        {
            if (node.Font == MathFont.MathBoldItalic && Find(5, node.CodePoint, out glyphIndex, out metric)) desired = 5;
            else if (Find(defaultFont, node.CodePoint, out glyphIndex, out metric)) desired = defaultFont;
            else if (textShaper is not null && textShaper.TryShape(node.CodePoint, shapeFont, out MathTextShape shape) &&
                double.IsFinite(shape.Advance) && shape.Advance >= 0 && shape.Advance <= 16 && !shape.Segments.IsEmpty &&
                double.IsFinite(shape.InkBounds.X) && double.IsFinite(shape.InkBounds.Y) && double.IsFinite(shape.InkBounds.Width) && double.IsFinite(shape.InkBounds.Height) &&
                shape.InkBounds.Width >= 0 && shape.InkBounds.Height >= 0)
            {
                // Keep the locked metric lookup independent from font cmap:
                // mapped Cyrillic/Latin uses source approximations; supported
                // text scripts borrow M; completely undefined metrics stay 0.
                if (!FallbackMetricData.Find(desired,node.CodePoint,node.TextMode,out TexGlyphMetric fallback) && node.TextMode &&
                    node.CodePoint is >= 0x0100 and <= 0x024f or >= 0x0300 and <= 0x036f or >= 0x0400 and <= 0x04ff or >= 0x0530 and <= 0x058f or >= 0x0900 and <= 0x10ff or >= 0x3000 and <= 0x30ff or >= 0x4e00 and <= 0x9faf or >= 0xff00 and <= 0xff60 or >= 0xac00 and <= 0xd7af)
                    Find(desired,'M',out _,out fallback);
                double fallbackItalic = node.TextMode || node.Font == MathFont.MathItalic ? 0 : Math.Max(0,fallback.ItalicCorrection);
                return new() { Width = (shape.Advance + fallbackItalic) * scale, Height = fallback.Height * scale, Depth = fallback.Depth * scale, Italic=fallbackItalic*scale, Atom = node.Atom, TextShaped = true, TextShape = shape };
            }
            else throw new CompileFailure(new MathDiagnostic(MathDiagnosticCode.UnknownSymbol, node.Range, $"Locked font data has no glyph U+{node.CodePoint:X}."));
        }
        FontGlyph glyph = FontData.Glyphs[glyphIndex];
        if(metricFont!=desired&&FallbackMetricData.Find(metricFont,node.CodePoint,node.TextMode,out TexGlyphMetric sourceMetric))metric=sourceMetric;
        double italic = node.TextMode || node.Font == MathFont.MathItalic ? 0 : metric.ItalicCorrection;
        return new() { Width = (glyph.AdvanceUnits / (double)FontData.Fonts[desired].UnitsPerEm + Math.Max(0, italic)) * scale, Height = metric.Height * scale, Depth = metric.Depth * scale, Italic = italic * scale, Skew = metric.Skew * scale, Atom = node.Atom, Character = true, Symbol = true, Font = desired, GlyphIndex = glyphIndex };
    }

    private static bool Find(int font, int codePoint, out int glyphIndex, out TexGlyphMetric metric)
    {
        if (!GlyphLookups.TryGetValue((long)font << 32 | (uint)codePoint, out GlyphLookup found))
        { glyphIndex = -1; metric = default; return false; }
        glyphIndex = found.GlyphIndex;
        if (found.HasMetric) { metric = found.Metric; return true; }
        metric = default;
        // A glyph without a TeX metric borrows metrics from the matching
        // private face while retaining its own glyph index. This mirrors the
        // original table search; dropping the glyph index would index -1.
        if (font == 3 && Find(4, codePoint, out _, out metric)) return true;
        if (font == 1 && Find(2, codePoint, out _, out metric)) return true;
        return false;
    }

    // Styling fragments are transparent to the enclosing math list. Braced rows
    // remain ordinary atoms, preserving the distinction x+{+y} versus x++y.
    private static int Collect(SemanticNode[] nodes, int first, int[] destination, int[] stack, LayoutContext[] contexts)
    {
        int length = 0, depth = 0, current = first;
        while (current >= 0 || depth > 0)
        {
            if (current < 0) { current = stack[--depth]; continue; }
            ref readonly SemanticNode node = ref nodes[current];
            if (node.Kind is NodeKind.Style or NodeKind.Sizing or NodeKind.Color or NodeKind.Choice or NodeKind.MathMlChoice || node.Kind == NodeKind.Row && node.Value == 2 || node.Kind == NodeKind.Box && node.Text is "\\phantom" or "\\hbox")
            {
                if (node.NextSibling >= 0) stack[depth++] = node.NextSibling;
                current = node.Kind == NodeKind.Choice ? contexts[current].Style < 2 ? node.FirstChild : contexts[current].Style < 4 ? node.SecondChild : contexts[current].Style < 6 ? node.ThirdChild : node.FourthChild : node.FirstChild;
                // The parser stores a style's implicit body in a transparent
                // row. Its atoms participate in the enclosing math list.
                if (node.Kind is NodeKind.Style or NodeKind.Sizing or NodeKind.Color or NodeKind.Choice or NodeKind.MathMlChoice && current >= 0 && nodes[current].Kind == NodeKind.Row && nodes[current].NextSibling < 0)
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
            (MathAtom.Inner, MathAtom.Ord) or (MathAtom.Inner, MathAtom.Op) or (MathAtom.Inner, MathAtom.Open) or (MathAtom.Inner, MathAtom.Punct) => 3,
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
