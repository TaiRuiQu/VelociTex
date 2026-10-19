using System.Runtime.InteropServices;
using System.Text;

namespace VelociTex;

internal static class MathParser
{
    internal static int Parse(string source, in MathOptions options, MathWorkspace w, CancellationToken cancellationToken)
    {
        var root = Add(w, options, new(NodeKind.Row, new(0, source.Length)));
        Push(w, new(ParseTaskKind.Sequence, root));
        var position = 0; var result = -1;
        while (w.Tasks.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var task = w.Tasks[^1]; w.Tasks.RemoveAt(w.Tasks.Count - 1);
            if (task.Depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, CurrentRange(), "Syntax nesting budget exceeded.");
            switch (task.Kind)
            {
                case ParseTaskKind.Return: result = task.Node; break;
                case ParseTaskKind.SetChild:
                    ref var parent = ref Node(task.Node);
                    if (task.Slot == 0) parent.FirstChild = result; else if (task.Slot == 1) parent.SecondChild = result; else parent.ThirdChild = result;
                    break;
                case ParseTaskKind.AttachSequence:
                    if (task.Last < 0) Node(task.Node).FirstChild = result; else Node(task.Last).NextSibling = result;
                    Push(w, task with { Kind = ParseTaskKind.Sequence, Last = result, Previous = task.Last });
                    break;
                case ParseTaskKind.Sequence:
                    SkipMathSpaces(task.Text);
                    if (position == w.Expanded.Count || w.Expanded[position].Is("}"))
                    {
                        if (position == w.Expanded.Count && task.Closing) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed group.");
                        if (position < w.Expanded.Count)
                        {
                            if (!task.Closing) Fail(MathDiagnosticCode.UnexpectedGroupEnd, CurrentRange(), "Unexpected closing brace.");
                            var closing = w.Expanded[position++].Origin; var old = Node(task.Node).Range;
                            Node(task.Node).Range = new(old.Start, Math.Max(old.Length, closing.Start + closing.Length - old.Start));
                        }
                        result = task.Node; break;
                    }
                    var next = w.Expanded[position];
                    if (!task.Text && (next.Is("^") || next.Is("_")))
                    {
                        position++; var superscript = next.Is("^"); var script = task.Last;
                        if (script < 0)
                        {
                            var empty = Add(w, options, new(NodeKind.Row, next.Origin));
                            script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = empty, Font = task.Font });
                            Node(task.Node).FirstChild = script;
                        }
                        else if (Node(script).Kind != NodeKind.Script)
                        {
                            script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = task.Last, Font = task.Font });
                            if (task.Previous < 0) Node(task.Node).FirstChild = script; else Node(task.Previous).NextSibling = script;
                        }
                        if ((superscript ? Node(script).SecondChild : Node(script).ThirdChild) != -1)
                            Fail(superscript ? MathDiagnosticCode.DoubleSuperscript : MathDiagnosticCode.DoubleSubscript, next.Origin, "A base cannot have two scripts of the same kind.");
                        Push(w, task with { Last = script });
                        Push(w, task with { Kind = ParseTaskKind.SetChild, Node = script, Slot = superscript ? 1 : 2 });
                        Push(w, task with { Kind = ParseTaskKind.Argument, Depth = task.Depth + 1 });
                        break;
                    }
                    if (TryStyle(next.Text, out var style))
                    {
                        position++; var wrapper = Add(w, options, new(NodeKind.Style, next.Origin) { Style = style, Font = task.Font, TextMode = task.Text });
                        var inner = Add(w, options, new(NodeKind.Row, next.Origin) { Font = task.Font, TextMode = task.Text }); Node(wrapper).FirstChild = inner;
                        if (task.Last < 0) Node(task.Node).FirstChild = wrapper; else Node(task.Last).NextSibling = wrapper;
                        Push(w, new(ParseTaskKind.Return, task.Node)); Push(w, task with { Node = inner, Last = -1, Previous = -1, Depth = task.Depth + 1 });
                        break;
                    }
                    Push(w, task with { Kind = ParseTaskKind.AttachSequence });
                    Push(w, task with { Kind = ParseTaskKind.Atom });
                    break;
                case ParseTaskKind.Argument:
                    SkipMathSpaces(false);
                    if (position == w.Expanded.Count || w.Expanded[position].Is("}") || w.Expanded[position].Is("^") || w.Expanded[position].Is("_"))
                        Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Missing command or script argument.");
                    Push(w, task with { Kind = ParseTaskKind.Atom }); break;
                case ParseTaskKind.Atom:
                    if (position == w.Expanded.Count) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Missing argument.");
                    var token = w.Expanded[position++];
                    if (token.Is("{"))
                    {
                        var row = Add(w, options, new(NodeKind.Row, token.Origin) { Value = 1, TextMode = task.Text, Font = task.Font });
                        Push(w, task with { Kind = ParseTaskKind.Sequence, Node = row, Last = -1, Previous = -1, Closing = true, Depth = task.Depth + 1 });
                        break;
                    }
                    if (token.Is("\\frac") || token.Is("\\dfrac") || token.Is("\\tfrac"))
                    {
                        if (task.Text) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Fractions are only supported in math mode.");
                        var fraction = Add(w, options, new(NodeKind.Fraction, token.Origin) { Font = task.Font }); var outer = fraction;
                        if (!token.Is("\\frac")) outer = Add(w, options, new(NodeKind.Style, token.Origin) { FirstChild = fraction, Style = token.Is("\\dfrac") ? MathStyle.Display : MathStyle.Text, Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, outer));
                        PushArgument(fraction, 1, task); PushArgument(fraction, 0, task); break;
                    }
                    if (token.Is("\\sqrt"))
                    {
                        if (task.Text) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Radicals are only supported in math mode.");
                        if (position < w.Expanded.Count && w.Expanded[position].Is("[")) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Indexed radicals are not implemented in P1.");
                        var radical = Add(w, options, new(NodeKind.Radical, token.Origin) { Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, radical)); PushArgument(radical, 0, task); break;
                    }
                    if (TryFont(token.Text, out var font))
                    {
                        var wrapper = Add(w, options, new(NodeKind.Font, token.Origin) { Font = font, TextMode = task.Text });
                        Push(w, new(ParseTaskKind.Return, wrapper)); PushArgument(wrapper, 0, task with { Font = font }); break;
                    }
                    if (TryTextFont(token.Text, out var textFont))
                    {
                        var wrapper = Add(w, options, new(NodeKind.Text, token.Origin) { Font = textFont, TextMode = true });
                        Push(w, new(ParseTaskKind.Return, wrapper)); PushArgument(wrapper, 0, task with { Font = textFont, Text = true }); break;
                    }
                    if (token.Is("\\,") || token.Is("\\:") || token.Is("\\;") || token.Is("\\!") || token.Is("\\quad") || token.Is("\\qquad"))
                    {
                        var width = token.Is("\\,") ? 3d / 18 : token.Is("\\:") ? 4d / 18 : token.Is("\\;") ? 5d / 18 : token.Is("\\!") ? -3d / 18 : token.Is("\\quad") ? 1 : 2;
                        result = Add(w, options, new(NodeKind.Space, token.Origin) { Value = width, TextMode = task.Text, Font = task.Font }); break;
                    }
                    if (char.IsWhiteSpace(token.Text[0]) && task.Text)
                    { result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = 32, Font = task.Font, TextMode = true }); break; }
                    if (token.Is("}") || token.Is("^") || token.Is("_") || token.Is("&") || token.Is("#") || token.Is("$")) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Unexpected TeX special token.");
                    if (!FindSymbol(token.Text, task.Text, out var definition))
                        Fail(token.Text[0] == '\\' ? MathDiagnosticCode.UnsupportedCommand : MathDiagnosticCode.UnknownSymbol, token.Origin, token.Text[0] == '\\' ? "Command is not implemented in P1." : "Symbol is unavailable in the P1 font and rule subset.");
                    if (definition.CodePoint < 0 || definition.Group is "accent-token" or "op-token") Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "This symbol requires a P2 construction rule.");
                    result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = definition.CodePoint, Atom = definition.Atom, Font = task.Font == MathFont.Default ? definition.Font : task.Font, SymbolFont = definition.Font, TextMode = task.Text, IsMathOrd = definition.IsMathOrd });
                    break;
            }
        }
        return root;

        ref SemanticNode Node(int index) => ref CollectionsMarshal.AsSpan(w.Nodes)[index];
        SourceRange CurrentRange() => position < w.Expanded.Count ? w.Expanded[position].Origin : new(source.Length, 0);
        void SkipMathSpaces(bool text) { if (!text) while (position < w.Expanded.Count && char.IsWhiteSpace(w.Expanded[position].Text[0])) position++; }
        void PushArgument(int parent, int slot, ParseTask context)
        { Push(w, context with { Kind = ParseTaskKind.SetChild, Node = parent, Slot = slot }); Push(w, context with { Kind = ParseTaskKind.Argument, Depth = context.Depth + 1 }); }
    }
    private static int Add(MathWorkspace w, in MathOptions options, SemanticNode node)
    { if (w.Nodes.Count >= options.MaxNodes) Fail(MathDiagnosticCode.NodeLimit, node.Range, "Semantic node budget exceeded."); var index = w.Nodes.Count; w.Nodes.Add(node); return index; }
    private static void Push(MathWorkspace w, ParseTask task) => w.Tasks.Add(task);
    private static void Fail(MathDiagnosticCode code, SourceRange range, string message) => throw new CompileFailure(new(code, range, message));
    private static bool FindSymbol(ReadOnlySpan<char> name, bool text, out SymbolDefinition definition)
    {
        var lower = 0; var upper = SymbolData.Symbols.Length - 1;
        while (lower <= upper)
        {
            var middle = lower + (upper - lower) / 2; var value = SymbolData.Symbols[middle];
            var compare = text.CompareTo(value.TextMode); if (compare == 0) compare = name.CompareTo(value.Name.AsSpan(), StringComparison.Ordinal);
            if (compare == 0) { definition = value; return true; } if (compare < 0) upper = middle - 1; else lower = middle + 1;
        }
        definition = default; return false;
    }
    private static bool TryStyle(ReadOnlySpan<char> name, out MathStyle style)
    {
        style = name switch { "\\displaystyle" => MathStyle.Display, "\\textstyle" => MathStyle.Text, "\\scriptstyle" => MathStyle.Script, "\\scriptscriptstyle" => MathStyle.ScriptScript, _ => (MathStyle)(-1) }; return (int)style >= 0;
    }
    private static bool TryFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\mathrm" => MathFont.MathRoman, "\\mathit" => MathFont.MathItalic, "\\mathbf" => MathFont.MathBold, "\\mathnormal" => MathFont.MathNormal, "\\mathsf" => MathFont.MathSans, "\\mathsfit" => MathFont.MathSansItalic, "\\mathtt" => MathFont.MathTypewriter, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
    private static bool TryTextFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\text" or "\\textrm" or "\\textnormal" => MathFont.MathRoman, "\\textbf" => MathFont.MathBold, "\\textit" => MathFont.MathItalic, "\\textsf" => MathFont.MathSans, "\\texttt" => MathFont.MathTypewriter, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
}
