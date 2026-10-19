using System.Globalization;
using System.Runtime.InteropServices;
using System.Text;
using System.Xml;

namespace VelociTex;

/// <summary>Presentation MathML projected directly from retained semantics, independently of drawing commands.</summary>
public static class MathMlWriter
{
    public static string Write(MathDocument document, bool includeAnnotation = true)
    {
        ArgumentNullException.ThrowIfNull(document);
        var output = new StringBuilder();
        using var writer = XmlWriter.Create(output, new XmlWriterSettings { OmitXmlDeclaration = true, ConformanceLevel = ConformanceLevel.Fragment });
        Write(document, writer, includeAnnotation); writer.Flush(); return output.ToString();
    }

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
        var nodes = new List<MmlNode>(semantic.Length + 2);
        var stack = new List<(int Node, bool Visit)> { (document.Root, false) };
        while (stack.Count != 0)
        {
            var (index, visit) = stack[^1]; stack.RemoveAt(stack.Count - 1);
            var node = semantic[index];
            if (!visit)
            {
                stack.Add((index, true));
                if (node.Kind == NodeKind.Row)
                { for (var child = node.FirstChild; child >= 0; child = semantic[child].NextSibling) stack.Add((child, false)); }
                else
                {
                    if (node.FirstChild >= 0) stack.Add((node.FirstChild, false));
                    if (node.SecondChild >= 0) stack.Add((node.SecondChild, false));
                    if (node.ThirdChild >= 0) stack.Add((node.ThirdChild, false));
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
                case NodeKind.Text:
                    mapped[index] = mapped[node.FirstChild]; break;
                case NodeKind.Style:
                    var styleChild = mapped[node.FirstChild];
                    if (semantic[node.FirstChild].Kind == NodeKind.Row && semantic[node.FirstChild].Value == 0 && Ref(styleChild).Tag == "mrow") styleChild = Ref(styleChild).First;
                    mapped[index] = Add(new("mstyle") { First = styleChild, Style = node.Style }); break;
                case NodeKind.Fraction:
                    var numerator = mapped[node.FirstChild]; var denominator = mapped[node.SecondChild];
                    Ref(numerator).Next = denominator;
                    mapped[index] = Add(new("mfrac") { First = numerator }); break;
                case NodeKind.Radical:
                    mapped[index] = Add(new("msqrt") { First = mapped[node.FirstChild] }); break;
                case NodeKind.Script:
                    var @base = mapped[node.FirstChild]; var scriptLast = @base;
                    if (node.ThirdChild >= 0) { var sub = mapped[node.ThirdChild]; Ref(scriptLast).Next = sub; scriptLast = sub; }
                    if (node.SecondChild >= 0) Ref(scriptLast).Next = mapped[node.SecondChild];
                    mapped[index] = Add(new(node.SecondChild < 0 ? "msub" : node.ThirdChild < 0 ? "msup" : "msubsup") { First = @base }); break;
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
            writer.WriteStartElement(node.Tag, ns);
            if (node.Variant is not null) writer.WriteAttributeString("mathvariant", node.Variant);
            if (node.Separator) writer.WriteAttributeString("separator", "true");
            if (node.NoStretch) writer.WriteAttributeString("stretchy", "false");
            if (node.SuppressSpacing) { writer.WriteAttributeString("lspace", "0em"); writer.WriteAttributeString("rspace", "0em"); }
            if (node.Width is not null) writer.WriteAttributeString("width", node.Width);
            if (node.Tag == "mstyle")
            {
                writer.WriteAttributeString("scriptlevel", node.Style is MathStyle.Display or MathStyle.Text ? "0" : node.Style == MathStyle.Script ? "1" : "2");
                writer.WriteAttributeString("displaystyle", node.Style == MathStyle.Display ? "true" : "false");
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
    private static string? Variant(in SemanticNode node)
    {
        var value = node.Font switch
        {
            MathFont.MathRoman => HasMainMetric(node.CodePoint) ? "normal" : node.IsMathOrd && !node.TextMode ? "italic" : "normal", MathFont.MathItalic => "italic", MathFont.MathBold => "bold", MathFont.MathBoldItalic => node.IsMathOrd ? "bold-italic" : "bold",
            MathFont.MathSans => "sans-serif", MathFont.MathSansBold => "bold-sans-serif", MathFont.MathSansItalic => "sans-serif-italic", MathFont.MathTypewriter => "monospace", _ => node.IsMathOrd && !node.TextMode ? "italic" : "normal",
        };
        var defaultValue = node.TextMode || node.CodePoint is >= '0' and <= '9' ? "normal" : "italic";
        if (node.Atom != MathAtom.Ord) return node.Atom == MathAtom.Bin && value == "bold-italic" ? value : null;
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
        internal StringBuilder? Buffer;
        internal int First = -1, Next = -1;
        internal bool Separator, NoStretch, SuppressSpacing;
        internal MathStyle Style;
    }
}
