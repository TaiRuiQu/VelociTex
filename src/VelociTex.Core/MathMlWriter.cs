using System.Buffers;
using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;

namespace VelociTex;

/// <summary>Presentation MathML projected directly from retained semantics, independently of drawing commands.</summary>
public static class MathMlWriter
{
    /// <summary>Writes presentation MathML from a compiled formula, optionally retaining the exact TeX source as an annotation.</summary>
    public static string Write(MathDocument document, bool includeAnnotation = true)
    {
        ArgumentNullException.ThrowIfNull(document);
        return Write(document.Source, document.Options, document.Nodes, document.Root, includeAnnotation);
    }

    /// <summary>Writes presentation MathML from retained semantics without requiring layout or drawing data.</summary>
    internal static string Write(string source, MathOptions options, SemanticNode[] semantic, int root, bool includeAnnotation = true)
    {
        var output = new StringBuilder(256 + semantic.Length * 40 + source.Length);
        Write(source, options, semantic, root, output, includeAnnotation);
        return output.ToString();
    }

    /// <summary>Writes UTF-8 presentation MathML while leaving the caller-owned output stream open.</summary>
    public static void Write(MathDocument document, Stream output, bool includeAnnotation = true)
    {
        ArgumentNullException.ThrowIfNull(output);
        string text = Write(document, includeAnnotation);
        using var writer = new StreamWriter(output, new UTF8Encoding(false), 4096, leaveOpen: true);
        writer.Write(text);
    }

    private static void Write(string source, MathOptions options, SemanticNode[] semantic, int root, StringBuilder writer, bool includeAnnotation)
    {
        int length = semantic.Length;
        int[] mapped = ArrayPool<int>.Shared.Rent(length);
        MathStyle[] styles = ArrayPool<MathStyle>.Shared.Rent(length);
        byte[] sizes = ArrayPool<byte>.Shared.Rent(length);
        string?[] colors = ArrayPool<string?>.Shared.Rent(length);
        int[] parents = ArrayPool<int>.Shared.Rent(length);
        Array.Fill(mapped, -1, 0, length);
        Array.Fill(parents, -1, 0, length);
        Array.Clear(sizes, 0, length);
        Array.Clear(colors, 0, length);
        try
        {
        sizes[root] = 6;
        styles[root] = options.DisplayMode ? MathStyle.Display : MathStyle.Text;
        using MmlBuffer nodes = new(length * 2 + 8);
        var stack = new List<(int Node, bool Visit)>(length + 4) { (root, false) };
        while (stack.Count != 0)
        {
            var (index, visit) = stack[^1]; stack.RemoveAt(stack.Count - 1);
            var node = semantic[index];
            if (!visit)
            {
                stack.Add((index, true));
                var currentStyle = node.Kind == NodeKind.Style ? node.Style : node.Kind == NodeKind.Sizing && styles[index] >= MathStyle.Script ? MathStyle.Text : styles[index];
                styles[index] = currentStyle;
                if (node.Kind == NodeKind.Sizing) sizes[index] = (byte)node.Value;
                if (node.Kind == NodeKind.Color) colors[index] = node.Text2;
                if (node.Kind is NodeKind.Row or NodeKind.Matrix or NodeKind.MatrixRow or NodeKind.MatrixCell or NodeKind.Color or NodeKind.Sizing or NodeKind.Class or NodeKind.Operator || node.Kind == NodeKind.Box && node.Text is "\\phantom" or "\\hbox" or "\\pmb")
                { for (var child = node.FirstChild; child >= 0; child = semantic[child].NextSibling) VisitChild(child); if(node.Kind==NodeKind.MatrixRow)VisitChild(node.SecondChild); }
                else
                {
                    VisitChild(node.FirstChild); VisitChild(node.SecondChild); VisitChild(node.ThirdChild); VisitChild(node.FourthChild);
                }
                continue;
                void VisitChild(int child) { if (child >= 0) { parents[child] = index; styles[child] = currentStyle; sizes[child] = sizes[index]; colors[child] = colors[index]; stack.Add((child,false)); } }
            }
            switch (node.Kind)
            {
                case NodeKind.Symbol:
                    var tag = node.IsMathOrd ? "mi" : node.TextMode && node.Atom==MathAtom.Ord || node.CodePoint is 32 or 160 ? "mtext" : node.Atom != MathAtom.Ord || node.CodePoint == 0x2032 ? "mo" : node.OriginalCodePoint==0&&node.CodePoint is >= '0' and <= '9' ? "mn" : "mi";
                    var variant = node.CodePoint is 32 or 160 ? null : Variant(node);
                    var value = char.ConvertFromUtf32(node.OriginalCodePoint > 0 ? node.OriginalCodePoint : node.TextMode && node.CodePoint == 32 ? 160 : node.CodePoint);
                    mapped[index] = Add(new(tag, value, variant) { Separator = node.Atom == MathAtom.Punct, NoStretch = node.Atom is MathAtom.Open or MathAtom.Close, SuppressSpacing=node.CodePoint==0x2032&&parents[index]>=0&&semantic[parents[index]].Kind==NodeKind.Row&&semantic[parents[index]].Value==1&&semantic[parents[index]].FirstChild==index&&node.NextSibling<0 });
                    break;
                case NodeKind.Space:
                    if(node.Text is "\\nobreak" or "\\allowbreak") {mapped[index]=Add(new("mspace"));break;}
                    if(node.Text=="cdnull"){mapped[index]=Add(new("mi"," ","normal"));break;}
                    if(node.Text=="\\\\")
                    { mapped[index]=Add(new("mspace"){LineBreak=!options.DisplayMode||options.StrictMode!=MathStrictMode.Error?"newline":null,Height=node.Length1.Number!=0?Em(Length(node.Length1,index)):null});break; }
                    if (node.Text is "\\kern" or "\\mkern" or "\\hskip" or "\\mskip")
                    { mapped[index] = MakeSpace(Length(node.Length1, index)); break; }
                    var spaceText = node.Value switch
                    {
                        >= .1666 and <= .1667 => "\u2009", >= .2222 and <= .2223 => "\u2005", >= .2777 and <= .2778 => "\u2005\u200a",
                        >= -.1667 and <= -.1666 => "\u2009\u2063", _ => null,
                    };
                    mapped[index] = Add(new(spaceText is null ? "mspace" : "#text", spaceText) { Width = spaceText is null ? node.Value.ToString("0.####", CultureInfo.InvariantCulture) + "em" : null });
                    break;
                case NodeKind.Row:
                    var first = -1; var last = -1; var count = 0;
                    for (var child = node.FirstChild; child >= 0; child = semantic[child].NextSibling)
                    {
                        var current = mapped[child];
                        if (last >= 0 && Merge(last, current)) continue;
                        if (last >= 0 && Ref(current).Tag is "msup" or "msub" && IsNumber(last) && Ref(Ref(current).First).Tag == "mn")
                        {
                            var baseId = Ref(current).First;
                            var prefix = Ref(last).Buffer ?? new StringBuilder(Ref(last).Text);
                            if (Ref(baseId).Buffer is { } baseBuffer) prefix.Append(baseBuffer); else prefix.Append(Ref(baseId).Text);
                            Ref(baseId).Buffer = prefix; Ref(baseId).Text = null;
                            if (first == last) { first = last = current; }
                            else { var before = first; while (Ref(before).Next != last) before = Ref(before).Next; Ref(before).Next = current; last = current; }
                            continue;
                        }
                        if (first < 0) first = current; else Ref(last).Next = current;
                        last = current; count++;
                    }
                    bool literalOrdgroup=true;
                    if(parents[index]>=0)
                    {
                        ref readonly SemanticNode owner=ref semantic[parents[index]];
                        if(owner.Kind is NodeKind.Choice or NodeKind.MathMlChoice||owner.Kind==NodeKind.Box&&owner.Text is "\\vphantom" or "\\phantom"||owner.Kind==NodeKind.Class&&owner.Text is not("\\boldsymbol" or "\\bm"))literalOrdgroup=false;
                    }
                    if (count == 1 && node.Value == 1 && literalOrdgroup && !node.TextMode && Ref(first).Tag == "mo" && node.FirstChild>=0&&
                        semantic[node.FirstChild].Kind is NodeKind.Symbol or NodeKind.Operator) Ref(first).SuppressSpacing = true;
                    mapped[index] = count == 1 && (index != root || Ref(first).Tag == "mrow") ? first : Add(new("mrow") { First = first });
                    break;
                case NodeKind.Font:
                    var fontChild = mapped[node.FirstChild];
                    if (node.Font == MathFont.MathBoldItalic)
                    {
                        int atom = node.FirstChild;
                        if (semantic[atom].Kind == NodeKind.Row && semantic[atom].FirstChild >= 0) atom = semantic[atom].FirstChild;
                        var fontTag = semantic[atom].Atom is MathAtom.Bin or MathAtom.Rel ? "mo" : "mi";
                        bool character = Ref(fontChild).Tag is "mi" or "mn" or "mo" && Ref(fontChild).First < 0;
                        if (character) { Ref(fontChild).Tag = fontTag; mapped[index] = fontChild; }
                        else mapped[index] = Add(new(fontTag) { First = fontChild });
                        if (semantic[atom].Atom == MathAtom.Bin) { Ref(mapped[index]).LSpace = "0.22em"; Ref(mapped[index]).RSpace = "0.22em"; }
                    }
                    else mapped[index] = fontChild;
                    break;
                case NodeKind.Text:
                    mapped[index] = mapped[node.FirstChild]; break;
                case NodeKind.Style:
                    var styleChild = mapped[node.FirstChild];
                    bool cdCell=parents[index]>=0&&semantic[parents[index]].Kind==NodeKind.MatrixCell&&parents[parents[index]]>=0&&parents[parents[parents[index]]]>=0&&semantic[parents[parents[parents[index]]]].Text=="CD";
                    if(cdCell&&semantic[node.FirstChild].Kind==NodeKind.Row&&semantic[node.FirstChild].FirstChild<0)styleChild=-1;
                    if (styleChild>=0&&semantic[node.FirstChild].Kind == NodeKind.Row && semantic[node.FirstChild].Value == 0 && Ref(styleChild).Tag == "mrow" &&
                        (parents[index] < 0 || semantic[parents[index]].Kind != NodeKind.MatrixCell) &&
                        (semantic[node.FirstChild].FirstChild < 0 || semantic[semantic[node.FirstChild].FirstChild].NextSibling >= 0)) styleChild = Ref(styleChild).First;
                    mapped[index] = Add(new("mstyle") { First = styleChild, Style = node.Style }); break;
                case NodeKind.Fraction:
                    var numerator = mapped[node.FirstChild]; var denominator = mapped[node.SecondChild];
                    Ref(numerator).Next = denominator;
                    var fraction = Add(new("mfrac") { First = numerator, LineThickness = !node.HasBar ? "0px" : node.Value > 0 ? Em(node.Text=="\\genfrac"?Length(node.Length1,index):node.Value) : null });
                    mapped[index] = WithFences(fraction, node.LeftDelimiter, node.RightDelimiter); break;
                case NodeKind.Radical:
                    var radicand = mapped[node.FirstChild];
                    if (node.SecondChild >= 0) Ref(radicand).Next = mapped[node.SecondChild];
                    mapped[index] = Add(new(node.SecondChild >= 0 ? "mroot" : "msqrt") { First = radicand }); break;
                case NodeKind.Script:
                    var @base = mapped[node.FirstChild]; var scriptLast = @base;
                    if (node.ThirdChild >= 0) { var sub = mapped[node.ThirdChild]; Ref(scriptLast).Next = sub; scriptLast = sub; }
                    if (node.SecondChild >= 0) Ref(scriptLast).Next = mapped[node.SecondChild];
                    var opNode=semantic[node.FirstChild];
                    var limits = opNode.Kind == NodeKind.Operator && (opNode.Text=="\\operatorname"
                        ? opNode.Text2=="star"&&(styles[index]==MathStyle.Display||opNode.AlwaysLimits&&!opNode.NeverLimits)
                        : !opNode.NeverLimits&&((opNode.Limits||opNode.AlwaysLimits)&&styles[index]==MathStyle.Display||opNode.AlwaysLimits&&(node.SecondChild<0||node.ThirdChild<0)));
                    var brace = semantic[node.FirstChild].Kind == NodeKind.Accent &&
                        (semantic[node.FirstChild].Text is "\\overbrace" or "\\overbracket" && node.ThirdChild < 0 || semantic[node.FirstChild].Text is "\\underbrace" or "\\underbracket" && node.SecondChild < 0);
                    mapped[index] = Add(new(limits || brace ? node.SecondChild < 0 ? "munder" : node.ThirdChild < 0 ? "mover" : "munderover" : node.SecondChild < 0 ? "msub" : node.ThirdChild < 0 ? "msup" : "msubsup") { First = @base }); break;
                case NodeKind.Operator:
                    if (node.CodePoint > 0)
                        mapped[index] = Add(new("mo", char.ConvertFromUtf32(node.CodePoint)) { LargeOpFalse = node.Text == "\\smallint" });
                    else if (node.Text == "\\mathop")
                        mapped[index] = Add(new("mo") { First = Unwrap(LinkChildren(node.FirstChild)) });
                    else
                    {
                        var name = node.Text is "\\operatorname@" or "\\operatornamewithlimits" or "\\operatorname" or "\\operatorname*" or "\\liminf" or "\\limsup";
                        string? text = node.Text == "\\liminf" ? "lim\u2009inf" : node.Text == "\\limsup" ? "lim\u2009sup" : name ? TextContent(mapped[node.FirstChild]).Replace('\u2212', '-').Replace('\u2217', '*') : node.Text?.TrimStart('\\');
                        bool plainName=!name||PlainOperatorName(node.FirstChild);
                        var identifier = Add(new("mi", plainName?text:null, name ? "normal" : null){First=plainName?-1:Unwrap(mapped[node.FirstChild])});
                        var apply = Add(new("mo", "\u2061")); Ref(identifier).Next = apply;
                        mapped[index] = Add(new(parents[index] >= 0 && semantic[parents[index]].Kind == NodeKind.Script ? "mrow" : "#fragment") { First = identifier });
                    }
                    break;
                case NodeKind.Delimiter:
                    mapped[index] = Add(new("mo", node.CodePoint is 0 or '.' ? "" : char.ConvertFromUtf32(node.Value == 0 && node.CodePoint == '|' ? '|' : FenceCodePoint(node.CodePoint)))
                    {
                        Fence = node.Value == 0 || node.Atom is MathAtom.Open or MathAtom.Close ? "true" : "false",
                        Stretchy = node.Value > 0 ? "true" : null,
                        MinSize = node.Value > 0 ? Em(node.Value switch { 1 => 1.2, 2 => 1.8, 3 => 2.4, _ => 3 }) : null,
                        MaxSize = node.Value > 0 ? Em(node.Value switch { 1 => 1.2, 2 => 1.8, 3 => 2.4, _ => 3 }) : null,
                        LSpace = node.Value == 0 ? "0.05em" : null, RSpace = node.Value == 0 ? "0.05em" : null,
                    });
                    break;
                case NodeKind.Delimited:
                    mapped[index] = WithFences(mapped[node.FirstChild], node.LeftDelimiter, node.RightDelimiter, true); break;
                case NodeKind.Accent:
                    if (node.Text?.StartsWith("\\x", StringComparison.Ordinal) == true || node.Text?.StartsWith("\\\\cd", StringComparison.Ordinal) == true)
                    {
                        var arrow = Add(new("mo", char.ConvertFromUtf32(AccentCodePoint(node.Text))) { Stretchy = "true", MinSize = "3.0em" });
                        var upper = Add(new("mpadded") { First = node.FirstChild<0 ? -1 : mapped[node.FirstChild], Width = "+0.6em", LSpace = "0.3em" });
                        if (node.SecondChild >= 0)
                        { var lower = Add(new("mpadded") { First = mapped[node.SecondChild], Width = "+0.6em", LSpace = "0.3em" }); Ref(arrow).Next = lower; Ref(lower).Next = upper; }
                        else Ref(arrow).Next = upper;
                        mapped[index] = Add(new(node.SecondChild >= 0 ? "munderover" : "mover") { First = arrow });
                    }
                    else
                    {
                        bool under = node.Text?.StartsWith("\\under", StringComparison.Ordinal) == true || node.Text == "\\utilde";
                        bool horizontalBrace = node.Text is "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket";
                        bool stretchy = node.Text is not ("\\acute" or "\\grave" or "\\ddot" or "\\tilde" or "\\bar" or "\\breve" or "\\check" or "\\hat" or "\\vec" or "\\dot" or "\\mathring" or "\\'" or "\\`" or "\\^" or "\\~" or "\\=" or "\\u" or "\\." or "\\\"" or "\\c" or "\\r" or "\\H" or "\\v" or "\\textcircled");
                        var accent = Add(new("mo", node.Text is "\\overlinesegment" or "\\underlinesegment"?"undefined":char.ConvertFromUtf32(AccentCodePoint(node.Text ?? "", node.CodePoint))) { Stretchy = stretchy ? "true" : null });
                        var body = mapped[node.FirstChild]; Ref(body).Next = accent;
                        mapped[index] = Add(new(under ? "munder" : "mover") { First = body, Accent = !under && !horizontalBrace ? "true" : null, AccentUnder = under && !horizontalBrace ? "true" : null });
                    }
                    break;
                case NodeKind.MatrixCell:
                    mapped[index] = Add(new("mtd") { First = mapped[node.FirstChild] }); break;
                case NodeKind.MatrixRow:
                    int rowChildren = LinkChildren(node.FirstChild);
                    if (node.SecondChild >= 0 || node.AlwaysLimits && !node.NeverLimits)
                    {
                        int leftGlue = Add(new("mtd") { ClassName = "mtr-glue" }), rightGlue = Add(new("mtd") { ClassName = "mtr-glue" }), equationTag = Add(new("mtd") { ClassName = "mml-eqn-num" });
                        int lastCell = rowChildren; if (lastCell >= 0) while(Ref(lastCell).Next >= 0)lastCell = Ref(lastCell).Next;
                        Ref(leftGlue).Next=rowChildren; if(lastCell>=0)Ref(lastCell).Next=rightGlue;else Ref(leftGlue).Next=rightGlue;
                        if(options.Leqno){Ref(equationTag).Next=leftGlue;rowChildren=equationTag;}else{Ref(rightGlue).Next=equationTag;rowChildren=leftGlue;}
                    }
                    mapped[index] = Add(new("mtr") { First = rowChildren }); break;
                case NodeKind.Matrix:
                    var table = Add(new("mtable") { First = LinkChildren(node.FirstChild) });
                    table = SetTableAttributes(table, node);
                    if (node.Text is "smallmatrix" or "subarray") table = Add(new("mstyle") { First = table, ScriptLevelOnly = true });
                    mapped[index] = WithFences(table, node.LeftDelimiter, node.RightDelimiter); break;
                case NodeKind.Color:
                    int colorFirst=LinkChildren(node.FirstChild);
                    if(node.FirstChild>=0&&semantic[node.FirstChild].Kind==NodeKind.Row&&semantic[node.FirstChild].Value==0)
                    {
                        if(semantic[node.FirstChild].FirstChild<0)colorFirst=-1;
                        else if(semantic[semantic[node.FirstChild].FirstChild].NextSibling>=0)colorFirst=Unwrap(colorFirst);
                    }
                    mapped[index] = Add(new("mstyle") { First = colorFirst, MathColor = node.Text2 }); break;
                case NodeKind.Sizing:
                    mapped[index] = Add(new("mstyle") { First = Unwrap(LinkChildren(node.FirstChild)), MathSize = Em(MathLayout.MathMlSizeScale((int)node.Value)) }); break;
                case NodeKind.Choice:
                    mapped[index] = mapped[styles[index] == MathStyle.Display ? node.FirstChild : styles[index] == MathStyle.Text ? node.SecondChild : styles[index] == MathStyle.Script ? node.ThirdChild : node.FourthChild]; break;
                case NodeKind.MathMlChoice:
                    mapped[index] = mapped[node.SecondChild]; break;
                case NodeKind.Box:
                    mapped[index] = BuildBoxMathMl(node, index); break;
                case NodeKind.Class:
                    var classFirst = LinkChildren(node.FirstChild);
                    int declaredClassChild=node.FirstChild;
                    if(declaredClassChild>=0&&semantic[declaredClassChild].Kind==NodeKind.Row&&semantic[declaredClassChild].FirstChild>=0&&semantic[semantic[declaredClassChild].FirstChild].NextSibling<0)declaredClassChild=semantic[declaredClassChild].FirstChild;
                    bool plainClassBody=declaredClassChild>=0&&semantic[declaredClassChild].Kind is NodeKind.Row or NodeKind.Symbol;
                    if(node.Text is not ("\\boldsymbol" or "\\bm")&&plainClassBody)classFirst=Unwrap(classFirst);
                    int classChild = classFirst;
                    bool classCharacter = classChild >= 0 && Ref(classChild).Next < 0 && Ref(classChild).Tag is "mi" or "mo" or "mn" && Ref(classChild).First < 0;
                    int semanticClassChild=node.FirstChild;
                    if(semanticClassChild>=0&&semantic[semanticClassChild].Kind==NodeKind.Row&&semantic[semanticClassChild].FirstChild>=0&&semantic[semantic[semanticClassChild].FirstChild].NextSibling<0)semanticClassChild=semantic[semanticClassChild].FirstChild;
                    if(semanticClassChild>=0&&semantic[semanticClassChild].Kind==NodeKind.MathMlChoice)classCharacter=false;
                    string classTag = node.Atom == MathAtom.Inner ? "mpadded" : node.Atom == MathAtom.Ord ? "mi" : "mo";
                    if (classCharacter && node.Atom != MathAtom.Inner) { Ref(classChild).Tag = classTag; mapped[index] = classChild; }
                    else mapped[index] = Add(new(classTag) { First = classFirst });
                    if (node.Atom == MathAtom.Bin) { Ref(mapped[index]).LSpace = "0.22em"; Ref(mapped[index]).RSpace = "0.22em"; }
                    else if (node.Atom == MathAtom.Punct) { Ref(mapped[index]).LSpace = "0em"; Ref(mapped[index]).RSpace = "0.17em"; }
                    else if (node.Atom is MathAtom.Open or MathAtom.Close && node.Text is not ("\\html@mathml")) { Ref(mapped[index]).LSpace = "0em"; Ref(mapped[index]).RSpace = "0em"; }
                    break;
                case NodeKind.Rule:
                    var rule = Add(new("mspace") { MathBackground = colors[index] ?? "black", Width = Em(Length(node.Length1,index)), Height = Em(Length(node.Length2,index)) });
                    double ruleShift = Length(node.Length3,index);
                    mapped[index] = Add(new("mpadded") { First = rule, Height = Em(ruleShift), Depth = ruleShift < 0 ? Em(-ruleShift) : null, VOffset = Em(ruleShift) }); break;
                case NodeKind.Enclose:
                    mapped[index] = BuildEncloseMathMl(node,index); break;
                case NodeKind.Verb:
                    mapped[index] = Add(new("mtext", (node.Text2 ?? node.Text ?? "").Replace(' ', node.Value != 0 ? '\u2423' : '\u00a0'), "monospace")); break;
                case NodeKind.Tag:
                    int tagBody = mapped[node.FirstChild], tagLabel = node.SecondChild >= 0 ? mapped[node.SecondChild] : Add(new("mrow"));
                    int padLeft = Add(new("mtd") { Width = "50%" }), formulaCell = Add(new("mtd") { First = tagBody }), padRight = Add(new("mtd") { Width = "50%" }), tagCell = Add(new("mtd") { First = tagLabel });
                    Ref(padLeft).Next = formulaCell; Ref(formulaCell).Next = padRight; Ref(padRight).Next = tagCell;
                    int tagRow = Add(new("mtr") { First = padLeft }); mapped[index] = Add(new("mtable") { First = tagRow, Width = "100%" }); break;
            }
        }
        const string ns = "http://www.w3.org/1998/Math/MathML";
        writer.Append("<math xmlns=\"").Append(ns).Append('"');
        if (options.DisplayMode) writer.Append(" display=\"block\"");
        writer.Append('>');
        if (includeAnnotation) writer.Append("<semantics>");
        var tasks = new List<(int Node, bool Close)>(semantic.Length * 2 + 4) { (mapped[root], false) };
        while (tasks.Count != 0)
        {
            var (index, close) = tasks[^1]; tasks.RemoveAt(tasks.Count - 1);
            var node = nodes[index];
            if (close) { writer.Append("</").Append(node.Tag).Append('>'); continue; }
            if (node.Next >= 0) tasks.Add((node.Next, false));
            if (node.Tag == "#text") { AppendText(writer, node.Text); continue; }
            if (node.Tag == "#fragment") { if (node.First >= 0) tasks.Add((node.First, false)); continue; }
            writer.Append('<').Append(node.Tag);
            if (node.Variant is not null) Attribute(writer, "mathvariant", node.Variant);
            if (node.Separator) writer.Append(" separator=\"true\"");
            if (node.NoStretch) writer.Append(" stretchy=\"false\"");
            if (node.SuppressSpacing) { if (node.LSpace is null) writer.Append(" lspace=\"0em\""); if (node.RSpace is null) writer.Append(" rspace=\"0em\""); }
            if (node.Width is not null) Attribute(writer, "width", node.Width);
            if (node.LineThickness is not null) Attribute(writer, "linethickness", node.LineThickness);
            if (node.Fence is not null) Attribute(writer, "fence", node.Fence);
            if (node.Stretchy is not null) Attribute(writer, "stretchy", node.Stretchy);
            if (node.MinSize is not null) Attribute(writer, "minsize", node.MinSize);
            if (node.MaxSize is not null) Attribute(writer, "maxsize", node.MaxSize);
            if (node.LSpace is not null) Attribute(writer, "lspace", node.LSpace);
            if (node.RSpace is not null) Attribute(writer, "rspace", node.RSpace);
            if (node.Accent is not null) Attribute(writer, "accent", node.Accent);
            if (node.AccentUnder is not null) Attribute(writer, "accentunder", node.AccentUnder);
            if (node.LargeOpFalse) writer.Append(" largeop=\"false\"");
            if (node.RowSpacing is not null) Attribute(writer, "rowspacing", node.RowSpacing);
            if (node.ColumnSpacing is not null) Attribute(writer, "columnspacing", node.ColumnSpacing);
            if (node.ColumnAlign is not null) Attribute(writer, "columnalign", node.ColumnAlign);
            if (node.ColumnLines is not null) Attribute(writer, "columnlines", node.ColumnLines);
            if (node.RowLines is not null) Attribute(writer, "rowlines", node.RowLines);
            if (node.Notation is not null) Attribute(writer, "notation", node.Notation);
            if (node.MathColor is not null) Attribute(writer, "mathcolor", node.MathColor);
            if (node.MathBackground is not null) Attribute(writer, "mathbackground", node.MathBackground);
            if (node.MathSize is not null) Attribute(writer, "mathsize", node.MathSize);
            if (node.Height is not null) Attribute(writer, "height", node.Height);
            if (node.Depth is not null) Attribute(writer, "depth", node.Depth);
            if (node.VOffset is not null) Attribute(writer, "voffset", node.VOffset);
            if (node.CssStyle is not null) Attribute(writer, "style", node.CssStyle);
            if (node.ClassName is not null) Attribute(writer, "class", node.ClassName);
            if (node.LineBreak is not null) Attribute(writer, "linebreak", node.LineBreak);
            if (node.Tag == "mstyle" && node.MathColor is null && node.MathSize is null && node.CssStyle is null)
            {
                Attribute(writer, "scriptlevel", node.ScriptLevelOnly ? "1" : node.Style is MathStyle.Display or MathStyle.Text ? "0" : node.Style == MathStyle.Script ? "1" : "2");
                if (!node.ScriptLevelOnly) Attribute(writer, "displaystyle", node.Style == MathStyle.Display ? "true" : "false");
            }
            writer.Append('>');
            if (node.Buffer is not null) AppendText(writer, node.Buffer.ToString()); else if (node.Text is not null) AppendText(writer, node.Text);
            tasks.Add((index, true));
            if (node.First >= 0) tasks.Add((node.First, false));
        }
        if (includeAnnotation)
        { writer.Append("<annotation encoding=\"application/x-tex\">"); AppendText(writer, source); writer.Append("</annotation></semantics>"); }
        writer.Append("</math>");

        int Add(MmlNode node) => nodes.Add(node);
        ref MmlNode Ref(int index) => ref nodes[index];
        bool PlainOperatorName(int first)
        {
            if(first<0)return true;
            if(semantic[first].Kind==NodeKind.Row)return PlainOperatorName(semantic[first].FirstChild);
            for(int child=first;child>=0;child=semantic[child].NextSibling)if(semantic[child].Kind is not(NodeKind.Symbol or NodeKind.Space))return false;
            return true;
        }
        int MakeSpace(double width)
        {
            string? text = width switch
            {
                >= .05555 and <= .05556 => "\u200a", >= .1666 and <= .1667 => "\u2009", >= .2222 and <= .2223 => "\u2005", >= .2777 and <= .2778 => "\u2005\u200a",
                >= -.05556 and <= -.05555 => "\u200a\u2063", >= -.1667 and <= -.1666 => "\u2009\u2063", >= -.2223 and <= -.2222 => "\u205f\u2063", >= -.2778 and <= -.2777 => "\u2005\u2063", _ => null,
            };
            return Add(new(text is null ? "mspace" : "#text", text) { Width = text is null ? Em(width) : null });
        }
        double Length(TexLength length, int nodeIndex) => MathLayout.MathMlLength(length, styles[nodeIndex], sizes[nodeIndex], options);
        int BuildBoxMathMl(in SemanticNode box, int nodeIndex)
        {
            int body = box.FirstChild >= 0 ? mapped[box.FirstChild] : Add(new("mrow"));
            switch (box.Text)
            {
                case "\\phantom": return Add(new("mphantom") { First = LinkChildren(box.FirstChild) });
                case "\\vphantom":
                    int phantom = Add(new("mphantom") { First = Unwrap(body) }); return Add(new("mpadded") { First = phantom, Width = "0px" });
                case "\\smash":
                    string selection = box.Alignment ?? "tb"; bool valid = selection.All(c => c is 't' or 'b');
                    return Add(new("mpadded") { First = body, Height = valid && selection.Contains('t') ? "0px" : null, Depth = valid && selection.Contains('b') ? "0px" : null });
                case "\\mathllap": case "\\mathclap": case "\\mathrlap":
                    return Add(new("mpadded") { First = body, Width = "0px", LSpace = box.Text == "\\mathllap" ? "-1width" : box.Text == "\\mathclap" ? "-0.5width" : null });
                case "\\raisebox": return Add(new("mpadded") { First = body, VOffset = box.Length1.Number.ToString("0.################",CultureInfo.InvariantCulture) + box.Length1.UnitText });
                case "\\vcenter": return Add(new("mrow") { First = Add(new("mpadded") { First = body, ClassName = "vcenter" }) });
                case "\\hbox": return Add(new("mrow") { First = LinkChildren(box.FirstChild) });
                case "\\pmb": return Add(new("mstyle") { First = Unwrap(LinkChildren(box.FirstChild)), CssStyle = "text-shadow: 0.02em 0.01em 0.04px" });
                case "cdparent": return Add(new("mrow") { First = body });
                case "cdleft": case "cdright":
                    int labelRow = Add(new("mrow") { First = body });
                    int labelPad = Add(new("mpadded") { First = labelRow, Width = "0", LSpace = box.Text == "cdleft" ? "-1width" : null, VOffset = "0.7em" });
                    return Add(new("mstyle") { First = labelPad, Style = MathStyle.Script });
                default: return body;
            }
        }
        int BuildEncloseMathMl(in SemanticNode enclose, int nodeIndex)
        {
            bool colorBox = enclose.Text?.Contains("colorbox",StringComparison.Ordinal) == true;
            MmlNode item = new(colorBox ? "mpadded" : "menclose") { First = mapped[enclose.FirstChild], MathBackground = enclose.Text2 };
            item.Notation = enclose.Text switch { "\\cancel" => "updiagonalstrike", "\\bcancel" => "downdiagonalstrike", "\\xcancel" => "updiagonalstrike downdiagonalstrike", "\\phase" => "phasorangle", "\\sout" => "horizontalstrike", "\\fbox" => "box", "\\angl" => "actuarial", _ => null };
            if (colorBox)
            {
                item.Width = "+6pt"; item.Height = "+6pt"; item.LSpace = "3pt"; item.VOffset = "3pt";
                if (enclose.Text == "\\fcolorbox") item.CssStyle = "border: " + Em(Math.Max(.04,options.MinRuleThickness)) + " solid " + enclose.Text3;
            }
            return Add(item);
        }
        int Unwrap(int id) => id >= 0 && Ref(id).Tag == "mrow" ? Ref(id).First : id;
        int LinkChildren(int first)
        { int result = -1, last = -1; for (int child = first; child >= 0; child = semantic[child].NextSibling) { int current=mapped[child];if(current<0)throw new InvalidOperationException("An unvisited semantic child reached MathML linking.");if(last>=0&&Merge(last,current))continue;if (result < 0) result = current; else Ref(last).Next = current; last = current; } return result; }
        int WithFences(int content, int left, int right, bool unwrap = false)
        {
            if (left is 0 or '.' && right is 0 or '.') return content;
            int first = unwrap ? Unwrap(content) : content, last = first;
            if (last >= 0) while (Ref(last).Next >= 0) last = Ref(last).Next;
            if (left is not (0 or '.')) { var fence = Add(new("mo", char.ConvertFromUtf32(FenceCodePoint(left))) { Fence = "true" }); Ref(fence).Next = first; first = fence; if (last < 0) last = fence; }
            if (right is not (0 or '.')) { var fence = Add(new("mo", char.ConvertFromUtf32(FenceCodePoint(right))) { Fence = "true" }); if (last >= 0) Ref(last).Next = fence; else first = fence; }
            return Add(new("mrow") { First = first });
        }
        string TextContent(int id)
        {
            var result = new StringBuilder(); var pending = new List<int> { id };
            while (pending.Count != 0) { int current = pending[^1]; pending.RemoveAt(pending.Count - 1); var item = nodes[current]; if (item.Next >= 0) pending.Add(item.Next); if (item.Buffer is not null) result.Append(item.Buffer); else result.Append(item.Text); if (item.First >= 0) pending.Add(item.First); }
            return result.ToString();
        }
        int SetTableAttributes(int table, in SemanticNode matrix)
        {
            string env = matrix.Text ?? "matrix", spec = matrix.Alignment ?? "";
            bool small = env is "smallmatrix" or "subarray", align = env is "aligned" or "alignedat" or "split" or "align" or "align*" or "alignat" or "alignat*", gather = env is "gathered" or "gather" or "gather*", cd=env=="CD", cases = env.Contains("cases", StringComparison.Ordinal);
            Ref(table).RowSpacing = Em(small ? .1 : cases ? .36 : .16+(matrix.Value2>0?matrix.Value2-1:0)+(align||gather||cd?.09:0));
            var alignments = new List<string>(); var columnLines = new List<string>(); bool previousAlign = false;
            string notation = "";
            int start = 0, end = spec.Length;
            if (start < end && spec[start] is '|' or ':') { notation += "top "; start++; }
            if (start < end && spec[end - 1] is '|' or ':') { notation += "bottom "; end--; }
            for (int i = start; i < end; i++)
            {
                if (spec[i] is 'c' or 'l' or 'r') { alignments.Add(spec[i] == 'c' ? "center" : spec[i] == 'r' ? "right" : "left"); if (previousAlign) columnLines.Add("none"); previousAlign = true; }
                else if (spec[i] is '|' or ':' && previousAlign) { columnLines.Add(spec[i] == '|' ? "solid" : "dashed"); previousAlign = false; }
            }
            if (align || cases || cd || env.EndsWith("matrix", StringComparison.Ordinal) || env.EndsWith("matrix*", StringComparison.Ordinal))
            {
                string defaultAlign = alignments.Count == 1 ? alignments[0] : "center";
                int forcedColumns=env is "alignedat" or "alignat" or "alignat*"?(int)matrix.Value*2:matrix.ColumnCount;
                alignments.Clear(); for (int c = 0; c < (cases ? 2 : forcedColumns); c++) alignments.Add(cases ? "left" : align ? c % 2 == 0 ? "right" : "left" : defaultAlign);
            }
            if (alignments.Count == 0) for (int c = 0; c < matrix.ColumnCount; c++) alignments.Add(align ? c % 2 == 0 ? "right" : "left" : cases ? "left" : "center");
            Ref(table).ColumnAlign = env is "smallmatrix" or "equation" or "equation*" ? null : string.Join(' ', alignments);
            if (columnLines.Any(v => v != "none")) Ref(table).ColumnLines = string.Join(' ', columnLines);
            Ref(table).ColumnSpacing = env is "alignedat" or "alignat" or "alignat*" || gather ? "0em" : align ? string.Join(' ', Enumerable.Range(1, Math.Max(0, matrix.ColumnCount - 1)).Select(c => c % 2 == 1 ? "0em" : "1em")) : cd ? "0.5em" : env == "smallmatrix" ? "0.2778em" : "1em";
            var rowLines = new List<string>(); int rowCount = 0;
            for (int row = matrix.FirstChild; row >= 0; row = semantic[row].NextSibling)
            { if (rowCount++ == 0) { if (semantic[row].RuleAbove != 0) notation += "left "; } else rowLines.Add(semantic[row].RuleAbove > 0 ? "solid" : semantic[row].RuleAbove < 0 ? "dashed" : "none"); }
            if (rowLines.Any(v => v != "none")) Ref(table).RowLines = string.Join(' ', rowLines);
            if (matrix.RuleBelow != 0) notation += "right ";
            if (notation.Length > 0) table = Add(new("menclose") { First = table, Notation = notation.Trim() });
            return table;
        }
        bool IsNumber(int index) => Ref(index).Tag == "mn" || Ref(index).Tag == "mi" && Ref(index).Text == ".";
        bool Merge(int left, int right)
        {
            var a = Ref(left); var b = Ref(right);
            if (a.Tag == "mtext" && b.Tag == "mtext" && a.Variant == b.Variant || a.Tag == "mn" && IsNumber(right) || a.Tag == "mi" && a.Text == "." && b.Tag == "mn")
            {
                var buffer = Ref(left).Buffer ?? new StringBuilder(a.Text);
                if (b.Buffer is not null) buffer.Append(b.Buffer); else buffer.Append(b.Text);
                Ref(left).Buffer = buffer; Ref(left).Text = null;
                if (b.Tag == "mn") Ref(left).Tag = "mn"; return true;
            }
            return false;
        }
        }
        finally
        {
            ArrayPool<int>.Shared.Return(mapped);
            ArrayPool<MathStyle>.Shared.Return(styles);
            ArrayPool<byte>.Shared.Return(sizes);
            ArrayPool<string?>.Shared.Return(colors, clearArray: true);
            ArrayPool<int>.Shared.Return(parents);
        }
    }
    private static void Attribute(StringBuilder output,string name,string value)
    {
        output.Append(' ').Append(name).Append("=\"");
        AppendEscaped(output,value,true);
        output.Append('"');
    }
    private static void AppendText(StringBuilder output,string? text)
    {
        if(!string.IsNullOrEmpty(text))AppendEscaped(output,text,false);
    }
    private static void AppendEscaped(StringBuilder output,string text,bool attribute)
    {
        int start=0;
        for(int i=0;i<text.Length;i++)
        {
            string? replacement=text[i] switch
            {
                '&'=>"&amp;",'<'=>"&lt;",'>'=>"&gt;",'"' when attribute=>"&quot;",
                '\r'=>"&#xD;",'\n'=>"&#xA;",'\t'=>"&#x9;",_=>null
            };
            if(replacement is null)continue;
            if(i>start)output.Append(text,start,i-start);
            output.Append(replacement);start=i+1;
        }
        if(start<text.Length)output.Append(text,start,text.Length-start);
    }
    private static string Em(double value) => value.ToString("0.####", CultureInfo.InvariantCulture) + "em";
    private static int FenceCodePoint(int codePoint) => codePoint switch { '|' => 0x2223, 0x2016 => 0x2225, _ => codePoint };
    private static int AccentCodePoint(string label, int fallback = 0) => label switch
    {
        "\\overline" or "\\underline" => 0x203e,
        "\\widehat" => '^', "\\widecheck" => 0x02c7, "\\widetilde" or "\\utilde" or "\\tilde" => '~',
        "\\^"=>0x02c6,
        "\\overrightarrow" or "\\underrightarrow" or "\\xrightarrow" => 0x2192,
        "\\overleftarrow" or "\\underleftarrow" or "\\xleftarrow" => 0x2190,
        "\\overleftrightarrow" or "\\underleftrightarrow" or "\\xleftrightarrow" => 0x2194,
        "\\Overrightarrow" or "\\xRightarrow" => 0x21d2, "\\xLeftarrow" => 0x21d0, "\\xLeftrightarrow" => 0x21d4,
        "\\overleftharpoon" or "\\xleftharpoonup" => 0x21bc, "\\overrightharpoon" or "\\xrightharpoonup" => 0x21c0,
        "\\xleftharpoondown" => 0x21bd, "\\xrightharpoondown" => 0x21c1,
        "\\xrightleftharpoons" or "\\xrightequilibrium" => 0x21cc, "\\xleftrightharpoons" or "\\xleftequilibrium" => 0x21cb,
        "\\\\cdrightarrow" => 0x2192, "\\\\cdleftarrow" => 0x2190, "\\\\cdlongequal" => '=',
        "\\xhookleftarrow" => 0x21a9, "\\xhookrightarrow" => 0x21aa, "\\xmapsto" => 0x21a6,
        "\\xtwoheadleftarrow" => 0x219e, "\\xtwoheadrightarrow" => 0x21a0, "\\xlongequal" => '=',
        "\\xtofrom" or "\\xrightleftarrows" => 0x21c4,
        "\\overbrace" => 0x23de, "\\underbrace" => 0x23df, "\\overbracket" => 0x23b4, "\\underbracket" => 0x23b5,
        "\\overgroup" => 0x23e0, "\\undergroup" => 0x23e1,
        "\\overlinesegment" or "\\underlinesegment" => 0x23af,
        _ => fallback == 0 ? 0x005e : fallback,
    };
    private static string? Variant(in SemanticNode node)
    {
        if(node.OriginalCodePoint>0)return node.OriginalCodePoint>=0x1d7ce?null:node.IsMathOrd&&!node.TextMode?null:node.TextMode?null:"normal";
        var value = node.Font switch
        {
            MathFont.MathRoman => HasMainMetric(node.CodePoint) ? "normal" : node.IsMathOrd && !node.TextMode ? "italic" : "normal", MathFont.MathItalic => "italic", MathFont.MathBold => "bold", MathFont.MathBoldItalic => node.IsMathOrd ? "bold-italic" : "bold", MathFont.MainBoldItalic => "bold-italic", MathFont.MathSansBoldItalic => "sans-serif-bold-italic",
            MathFont.MathSans => "sans-serif", MathFont.MathSansBold => "bold-sans-serif", MathFont.MathSansItalic => "sans-serif-italic", MathFont.MathTypewriter => "monospace",
            MathFont.AMS => node.SymbolFont == MathFont.AMS ? node.IsMathOrd && !node.TextMode ? "italic" : "normal" : "double-struck", MathFont.Caligraphic or MathFont.Script => "script", MathFont.Fraktur => "fraktur",
            _ => node.IsMathOrd ? "italic" : "normal",
        };
        var defaultValue = !node.IsMathOrd&&(node.TextMode || node.CodePoint is >= '0' and <= '9') ? "normal" : "italic";
        if (node.Atom != MathAtom.Ord) return node.Atom == MathAtom.Bin && node.Font == MathFont.MathBoldItalic ? "bold-italic" : null;
        return value == defaultValue ? null : value;
    }
    private static bool HasMainMetric(int codePoint)
    {
        if (FallbackMetricData.Find(8,codePoint,false,out _)) return true;
        var font = FontData.Fonts[8]; int lower = font.MetricStart, upper = lower + font.MetricCount - 1;
        while (lower <= upper)
        {
            int middle = lower + (upper - lower) / 2, current = FontData.TexMetrics[middle].CodePoint;
            if (current == codePoint) return true; if (current < codePoint) lower = middle + 1; else upper = middle - 1;
        }
        return false;
    }
    private sealed class MmlBuffer : IDisposable
    {
        private MmlNode[] _items;
        internal MmlBuffer(int capacity) => _items = ArrayPool<MmlNode>.Shared.Rent(Math.Max(4, capacity));
        internal int Count { get; private set; }
        internal int Add(MmlNode node)
        {
            if (Count == _items.Length)
            {
                MmlNode[] larger = ArrayPool<MmlNode>.Shared.Rent(_items.Length * 2);
                Array.Copy(_items, larger, Count);
                ArrayPool<MmlNode>.Shared.Return(_items, clearArray: true);
                _items = larger;
            }
            _items[Count] = node;
            return Count++;
        }
        internal ref MmlNode this[int index] => ref _items[index];
        public void Dispose()
        {
            ArrayPool<MmlNode>.Shared.Return(_items, clearArray: true);
            _items = [];
        }
    }
    private struct MmlNode(string tag, string? text = null, string? variant = null)
    {
        internal string Tag = tag;
        internal string? Text = text, Variant = variant, Width;
        internal string? LineThickness, Fence, Stretchy, MinSize, MaxSize, LSpace, RSpace, Accent, AccentUnder;
        internal string? RowSpacing, ColumnSpacing, ColumnAlign, ColumnLines, RowLines, Notation;
        internal string? MathColor, MathBackground, MathSize, Height, Depth, VOffset, CssStyle, ClassName;
        internal string? LineBreak;
        internal StringBuilder? Buffer;
        internal int First = -1, Next = -1;
        internal bool Separator, NoStretch, SuppressSpacing, LargeOpFalse, ScriptLevelOnly;
        internal MathStyle Style;
    }
}
