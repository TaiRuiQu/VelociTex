using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Xml;

namespace VelociTex;

/// <summary>Presentation MathML projected directly from retained semantics, independently of drawing commands.</summary>
public static class MathMlWriter
{
    /// <summary>Writes presentation MathML from a compiled formula, optionally retaining the exact TeX source as an annotation.</summary>
    public static string Write(MathDocument document, bool includeAnnotation = true)
    {
        ArgumentNullException.ThrowIfNull(document);
        var output = new StringBuilder();
        using var writer = XmlWriter.Create(output, new XmlWriterSettings { OmitXmlDeclaration = true, ConformanceLevel = ConformanceLevel.Fragment });
        Write(document, writer, includeAnnotation); writer.Flush(); return output.ToString();
    }

    /// <summary>Writes UTF-8 presentation MathML while leaving the caller-owned output stream open.</summary>
    public static void Write(MathDocument document, Stream output, bool includeAnnotation = true)
    {
        ArgumentNullException.ThrowIfNull(output);
        using var writer = XmlWriter.Create(output, new XmlWriterSettings { OmitXmlDeclaration = true, Encoding = new UTF8Encoding(false), CloseOutput = false });
        Write(document, writer, includeAnnotation);
    }

    private static void Write(MathDocument document, XmlWriter writer, bool includeAnnotation)
    {
        ArgumentNullException.ThrowIfNull(document);
        var semantic = document.Nodes;
        var mapped = new int[semantic.Length];
        var styles = new MathStyle[semantic.Length];
        var parents = new int[semantic.Length];
        Array.Fill(parents, -1);
        styles[document.Root] = document.Options.DisplayMode ? MathStyle.Display : MathStyle.Text;
        var nodes = new List<MmlNode>(semantic.Length + 2);
        var stack = new List<(int Node, bool Visit)> { (document.Root, false) };
        while (stack.Count != 0)
        {
            var (index, visit) = stack[^1]; stack.RemoveAt(stack.Count - 1);
            var node = semantic[index];
            if (!visit)
            {
                stack.Add((index, true));
                var currentStyle = node.Kind == NodeKind.Style ? node.Style : styles[index];
                styles[index] = currentStyle;
                if (node.Kind is NodeKind.Row or NodeKind.Matrix or NodeKind.MatrixRow or NodeKind.MatrixCell)
                { for (var child = node.FirstChild; child >= 0; child = semantic[child].NextSibling) { parents[child] = index; styles[child] = currentStyle; stack.Add((child, false)); } }
                else
                {
                    if (node.FirstChild >= 0) { parents[node.FirstChild] = index; styles[node.FirstChild] = currentStyle; stack.Add((node.FirstChild, false)); }
                    if (node.SecondChild >= 0) { parents[node.SecondChild] = index; styles[node.SecondChild] = currentStyle; stack.Add((node.SecondChild, false)); }
                    if (node.ThirdChild >= 0) { parents[node.ThirdChild] = index; styles[node.ThirdChild] = currentStyle; stack.Add((node.ThirdChild, false)); }
                }
                continue;
            }
            switch (node.Kind)
            {
                case NodeKind.Symbol:
                    var tag = node.TextMode ? "mtext" : node.Atom != MathAtom.Ord ? "mo" : node.CodePoint is >= '0' and <= '9' ? "mn" : "mi";
                    var variant = Variant(node);
                    var value = char.ConvertFromUtf32(node.TextMode && node.CodePoint == 32 ? 160 : node.CodePoint);
                    mapped[index] = Add(new(tag, value, variant) { Separator = node.Atom == MathAtom.Punct, NoStretch = node.Atom is MathAtom.Open or MathAtom.Close });
                    break;
                case NodeKind.Space:
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
                    if (count == 1 && node.Value == 1 && Ref(first).Tag == "mo") Ref(first).SuppressSpacing = true;
                    mapped[index] = count == 1 && (index != document.Root || Ref(first).Tag == "mrow") ? first : Add(new("mrow") { First = first });
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
                    if (semantic[node.FirstChild].Kind == NodeKind.Row && semantic[node.FirstChild].Value == 0 && Ref(styleChild).Tag == "mrow" &&
                        (parents[index] < 0 || semantic[parents[index]].Kind != NodeKind.MatrixCell) &&
                        (semantic[node.FirstChild].FirstChild < 0 || semantic[semantic[node.FirstChild].FirstChild].NextSibling >= 0)) styleChild = Ref(styleChild).First;
                    mapped[index] = Add(new("mstyle") { First = styleChild, Style = node.Style }); break;
                case NodeKind.Fraction:
                    var numerator = mapped[node.FirstChild]; var denominator = mapped[node.SecondChild];
                    Ref(numerator).Next = denominator;
                    var fraction = Add(new("mfrac") { First = numerator, LineThickness = !node.HasBar ? "0px" : node.Value > 0 ? Em(node.Value) : null });
                    mapped[index] = WithFences(fraction, node.LeftDelimiter, node.RightDelimiter); break;
                case NodeKind.Radical:
                    var radicand = mapped[node.FirstChild];
                    if (node.SecondChild >= 0) Ref(radicand).Next = mapped[node.SecondChild];
                    mapped[index] = Add(new(node.SecondChild >= 0 ? "mroot" : "msqrt") { First = radicand }); break;
                case NodeKind.Script:
                    var @base = mapped[node.FirstChild]; var scriptLast = @base;
                    if (node.ThirdChild >= 0) { var sub = mapped[node.ThirdChild]; Ref(scriptLast).Next = sub; scriptLast = sub; }
                    if (node.SecondChild >= 0) Ref(scriptLast).Next = mapped[node.SecondChild];
                    var limits = semantic[node.FirstChild].Kind == NodeKind.Operator && !semantic[node.FirstChild].NeverLimits &&
                        (semantic[node.FirstChild].AlwaysLimits || semantic[node.FirstChild].Limits && styles[index] == MathStyle.Display);
                    var brace = semantic[node.FirstChild].Kind == NodeKind.Accent &&
                        (semantic[node.FirstChild].Text is "\\overbrace" or "\\overbracket" && node.ThirdChild < 0 || semantic[node.FirstChild].Text is "\\underbrace" or "\\underbracket" && node.SecondChild < 0);
                    mapped[index] = Add(new(limits || brace ? node.SecondChild < 0 ? "munder" : node.ThirdChild < 0 ? "mover" : "munderover" : node.SecondChild < 0 ? "msub" : node.ThirdChild < 0 ? "msup" : "msubsup") { First = @base }); break;
                case NodeKind.Operator:
                    if (node.CodePoint > 0)
                        mapped[index] = Add(new("mo", char.ConvertFromUtf32(node.CodePoint)) { LargeOpFalse = node.Text == "\\smallint" });
                    else if (node.Text == "\\mathop")
                        mapped[index] = Add(new("mo") { First = Unwrap(mapped[node.FirstChild]) });
                    else
                    {
                        var name = node.Text is "\\operatorname@" or "\\operatornamewithlimits" or "\\operatorname" or "\\operatorname*" or "\\liminf" or "\\limsup";
                        string? text = node.Text == "\\liminf" ? "lim\u2009inf" : node.Text == "\\limsup" ? "lim\u2009sup" : name ? TextContent(mapped[node.FirstChild]).Replace('\u2212', '-').Replace('\u2217', '*') : node.Text?.TrimStart('\\');
                        var identifier = Add(new("mi", text, name ? "normal" : null));
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
                    if (node.Text?.StartsWith("\\x", StringComparison.Ordinal) == true)
                    {
                        var arrow = Add(new("mo", char.ConvertFromUtf32(AccentCodePoint(node.Text))) { Stretchy = "true", MinSize = "3.0em" });
                        var upper = Add(new("mpadded") { First = mapped[node.FirstChild], Width = "+0.6em", LSpace = "0.3em" });
                        if (node.SecondChild >= 0)
                        { var lower = Add(new("mpadded") { First = mapped[node.SecondChild], Width = "+0.6em", LSpace = "0.3em" }); Ref(arrow).Next = lower; Ref(lower).Next = upper; }
                        else Ref(arrow).Next = upper;
                        mapped[index] = Add(new(node.SecondChild >= 0 ? "munderover" : "mover") { First = arrow });
                    }
                    else
                    {
                        bool under = node.Text?.StartsWith("\\under", StringComparison.Ordinal) == true || node.Text == "\\utilde";
                        bool horizontalBrace = node.Text is "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket";
                        bool stretchy = node.Text is not ("\\acute" or "\\grave" or "\\ddot" or "\\tilde" or "\\bar" or "\\breve" or "\\check" or "\\hat" or "\\vec" or "\\dot" or "\\mathring");
                        var accent = Add(new("mo", char.ConvertFromUtf32(AccentCodePoint(node.Text ?? "", node.CodePoint))) { Stretchy = stretchy ? "true" : null });
                        var body = mapped[node.FirstChild]; Ref(body).Next = accent;
                        mapped[index] = Add(new(under ? "munder" : "mover") { First = body, Accent = !under && !horizontalBrace ? "true" : null, AccentUnder = under && !horizontalBrace ? "true" : null });
                    }
                    break;
                case NodeKind.MatrixCell:
                    mapped[index] = Add(new("mtd") { First = mapped[node.FirstChild] }); break;
                case NodeKind.MatrixRow:
                    mapped[index] = Add(new("mtr") { First = LinkChildren(node.FirstChild) }); break;
                case NodeKind.Matrix:
                    var table = Add(new("mtable") { First = LinkChildren(node.FirstChild) });
                    table = SetTableAttributes(table, node);
                    if (node.Text is "smallmatrix" or "subarray") table = Add(new("mstyle") { First = table, ScriptLevelOnly = true });
                    mapped[index] = WithFences(table, node.LeftDelimiter, node.RightDelimiter); break;
            }
        }
        const string ns = "http://www.w3.org/1998/Math/MathML";
        writer.WriteStartElement("math", ns);
        if (document.Options.DisplayMode) writer.WriteAttributeString("display", "block");
        if (includeAnnotation) writer.WriteStartElement("semantics", ns);
        var tasks = new List<(int Node, bool Close)> { (mapped[document.Root], false) };
        while (tasks.Count != 0)
        {
            var (index, close) = tasks[^1]; tasks.RemoveAt(tasks.Count - 1);
            if (close) { writer.WriteEndElement(); continue; }
            var node = nodes[index];
            if (node.Next >= 0) tasks.Add((node.Next, false));
            if (node.Tag == "#text") { writer.WriteString(node.Text); continue; }
            if (node.Tag == "#fragment") { if (node.First >= 0) tasks.Add((node.First, false)); continue; }
            writer.WriteStartElement(node.Tag, ns);
            if (node.Variant is not null) writer.WriteAttributeString("mathvariant", node.Variant);
            if (node.Separator) writer.WriteAttributeString("separator", "true");
            if (node.NoStretch) writer.WriteAttributeString("stretchy", "false");
            if (node.SuppressSpacing) { writer.WriteAttributeString("lspace", "0em"); writer.WriteAttributeString("rspace", "0em"); }
            if (node.Width is not null) writer.WriteAttributeString("width", node.Width);
            if (node.LineThickness is not null) writer.WriteAttributeString("linethickness", node.LineThickness);
            if (node.Fence is not null) writer.WriteAttributeString("fence", node.Fence);
            if (node.Stretchy is not null) writer.WriteAttributeString("stretchy", node.Stretchy);
            if (node.MinSize is not null) writer.WriteAttributeString("minsize", node.MinSize);
            if (node.MaxSize is not null) writer.WriteAttributeString("maxsize", node.MaxSize);
            if (node.LSpace is not null) writer.WriteAttributeString("lspace", node.LSpace);
            if (node.RSpace is not null) writer.WriteAttributeString("rspace", node.RSpace);
            if (node.Accent is not null) writer.WriteAttributeString("accent", node.Accent);
            if (node.AccentUnder is not null) writer.WriteAttributeString("accentunder", node.AccentUnder);
            if (node.LargeOpFalse) writer.WriteAttributeString("largeop", "false");
            if (node.RowSpacing is not null) writer.WriteAttributeString("rowspacing", node.RowSpacing);
            if (node.ColumnSpacing is not null) writer.WriteAttributeString("columnspacing", node.ColumnSpacing);
            if (node.ColumnAlign is not null) writer.WriteAttributeString("columnalign", node.ColumnAlign);
            if (node.ColumnLines is not null) writer.WriteAttributeString("columnlines", node.ColumnLines);
            if (node.RowLines is not null) writer.WriteAttributeString("rowlines", node.RowLines);
            if (node.Notation is not null) writer.WriteAttributeString("notation", node.Notation);
            if (node.Tag == "mstyle")
            {
                writer.WriteAttributeString("scriptlevel", node.ScriptLevelOnly ? "1" : node.Style is MathStyle.Display or MathStyle.Text ? "0" : node.Style == MathStyle.Script ? "1" : "2");
                if (!node.ScriptLevelOnly) writer.WriteAttributeString("displaystyle", node.Style == MathStyle.Display ? "true" : "false");
            }
            if (node.Buffer is not null) writer.WriteString(node.Buffer.ToString()); else if (node.Text is not null) writer.WriteString(node.Text);
            tasks.Add((index, true));
            if (node.First >= 0) tasks.Add((node.First, false));
        }
        if (includeAnnotation)
        { writer.WriteStartElement("annotation", ns); writer.WriteAttributeString("encoding", "application/x-tex"); writer.WriteString(document.Source); writer.WriteEndElement(); writer.WriteEndElement(); }
        writer.WriteEndElement();

        int Add(MmlNode node) { var index = nodes.Count; nodes.Add(node); return index; }
        ref MmlNode Ref(int index) => ref CollectionsMarshal.AsSpan(nodes)[index];
        int Unwrap(int id) => Ref(id).Tag == "mrow" ? Ref(id).First : id;
        int LinkChildren(int first)
        { int result = -1, last = -1; for (int child = first; child >= 0; child = semantic[child].NextSibling) { if (result < 0) result = mapped[child]; else Ref(last).Next = mapped[child]; last = mapped[child]; } return result; }
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
            bool small = env is "smallmatrix" or "subarray", align = env is "aligned" or "alignedat" or "split", gather = env == "gathered", cases = env.Contains("cases", StringComparison.Ordinal);
            Ref(table).RowSpacing = Em(small ? .1 : cases ? .36 : align || gather ? .25 : .16);
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
            if (align || cases || env.EndsWith("matrix", StringComparison.Ordinal) || env.EndsWith("matrix*", StringComparison.Ordinal))
            {
                string defaultAlign = alignments.Count == 1 ? alignments[0] : "center";
                alignments.Clear(); for (int c = 0; c < (cases ? 2 : matrix.ColumnCount); c++) alignments.Add(cases ? "left" : align ? c % 2 == 0 ? "right" : "left" : defaultAlign);
            }
            if (alignments.Count == 0) for (int c = 0; c < matrix.ColumnCount; c++) alignments.Add(align ? c % 2 == 0 ? "right" : "left" : cases ? "left" : "center");
            Ref(table).ColumnAlign = env == "smallmatrix" ? null : string.Join(' ', alignments);
            if (columnLines.Any(v => v != "none")) Ref(table).ColumnLines = string.Join(' ', columnLines);
            Ref(table).ColumnSpacing = env == "alignedat" || gather ? "0em" : align ? string.Join(' ', Enumerable.Range(1, Math.Max(0, matrix.ColumnCount - 1)).Select(c => c % 2 == 1 || env != "aligned" ? "0em" : "1em")) : env == "smallmatrix" ? "0.2778em" : "1em";
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
    private static string Em(double value) => value.ToString("0.####", CultureInfo.InvariantCulture) + "em";
    private static int FenceCodePoint(int codePoint) => codePoint switch { '|' => 0x2223, 0x2016 => 0x2225, _ => codePoint };
    private static int AccentCodePoint(string label, int fallback = 0) => label switch
    {
        "\\overline" or "\\underline" => 0x203e,
        "\\widehat" => '^', "\\widecheck" => 0x02c7, "\\widetilde" or "\\utilde" or "\\tilde" => '~',
        "\\overrightarrow" or "\\underrightarrow" or "\\xrightarrow" => 0x2192,
        "\\overleftarrow" or "\\underleftarrow" or "\\xleftarrow" => 0x2190,
        "\\overleftrightarrow" or "\\underleftrightarrow" or "\\xleftrightarrow" => 0x2194,
        "\\Overrightarrow" or "\\xRightarrow" => 0x21d2, "\\xLeftarrow" => 0x21d0, "\\xLeftrightarrow" => 0x21d4,
        "\\overleftharpoon" or "\\xleftharpoonup" => 0x21bc, "\\overrightharpoon" or "\\xrightharpoonup" => 0x21c0,
        "\\xleftharpoondown" => 0x21bd, "\\xrightharpoondown" => 0x21c1,
        "\\xrightleftharpoons" or "\\xrightequilibrium" => 0x21cc, "\\xleftrightharpoons" or "\\xleftequilibrium" => 0x21cb,
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
        var value = node.Font switch
        {
            MathFont.MathRoman => HasMainMetric(node.CodePoint) ? "normal" : node.IsMathOrd && !node.TextMode ? "italic" : "normal", MathFont.MathItalic => "italic", MathFont.MathBold => "bold", MathFont.MathBoldItalic => node.IsMathOrd ? "bold-italic" : "bold",
            MathFont.MathSans => "sans-serif", MathFont.MathSansBold => "bold-sans-serif", MathFont.MathSansItalic => "sans-serif-italic", MathFont.MathTypewriter => "monospace",
            MathFont.AMS => "double-struck", MathFont.Caligraphic or MathFont.Script => "script", MathFont.Fraktur => "fraktur",
            _ => node.IsMathOrd && !node.TextMode ? "italic" : "normal",
        };
        var defaultValue = node.TextMode || node.CodePoint is >= '0' and <= '9' ? "normal" : "italic";
        if (node.Atom != MathAtom.Ord) return node.Atom == MathAtom.Bin && node.Font == MathFont.MathBoldItalic ? "bold-italic" : null;
        return value == defaultValue ? null : value;
    }
    private static bool HasMainMetric(int codePoint)
    {
        var font = FontData.Fonts[8]; int lower = font.MetricStart, upper = lower + font.MetricCount - 1;
        while (lower <= upper)
        {
            int middle = lower + (upper - lower) / 2, current = FontData.TexMetrics[middle].CodePoint;
            if (current == codePoint) return true; if (current < codePoint) lower = middle + 1; else upper = middle - 1;
        }
        return false;
    }
    private struct MmlNode(string tag, string? text = null, string? variant = null)
    {
        internal string Tag = tag;
        internal string? Text = text, Variant = variant, Width;
        internal string? LineThickness, Fence, Stretchy, MinSize, MaxSize, LSpace, RSpace, Accent, AccentUnder;
        internal string? RowSpacing, ColumnSpacing, ColumnAlign, ColumnLines, RowLines, Notation;
        internal StringBuilder? Buffer;
        internal int First = -1, Next = -1;
        internal bool Separator, NoStretch, SuppressSpacing, LargeOpFalse, ScriptLevelOnly;
        internal MathStyle Style;
    }
}
