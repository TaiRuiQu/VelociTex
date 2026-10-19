using System.Runtime.InteropServices;
using System.Text;
using System.Globalization;

namespace VelociTex;

internal static class MathParser
{
    internal static int Parse(string source, in MathOptions options, MathWorkspace w, CancellationToken cancellationToken)
    {
        var settings = options;
        var root = Add(w, options, new(NodeKind.Row, new(0, source.Length)));
        Push(w, new(ParseTaskKind.Sequence, root));
        var position = 0; var result = -1; var delimiterDepth = 0;
        while (w.Tasks.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var task = w.Tasks[^1]; w.Tasks.RemoveAt(w.Tasks.Count - 1);
            if (task.Depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, CurrentRange(), "Syntax nesting budget exceeded.");
            switch (task.Kind)
            {
                case ParseTaskKind.Return: result = task.Node; break;
                case ParseTaskKind.FinishDelimited:
                    if (position >= w.Expanded.Count || !w.Expanded[position].Is("\\right")) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "A left delimiter requires a matching right delimiter.");
                    position++; Node(task.Node).RightDelimiter = ReadDelimiter(); delimiterDepth--; result = task.Node; break;
                case ParseTaskKind.MatrixNextCell:
                    SkipMathSpaces(false);
                    if (position >= w.Expanded.Count) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed math environment.");
                    if (w.Expanded[position].Is("&"))
                    {
                        position++; AddMatrixCell(task.Node, task.Last, task.Previous, task.Slot + 1, task); break;
                    }
                    Node(task.Last).ColumnCount = task.Slot + 1;
                    Node(task.Node).ColumnCount = Math.Max(Node(task.Node).ColumnCount, task.Slot + 1);
                    if (w.Expanded[position].Is("\\\\") || w.Expanded[position].Is("\\cr"))
                    {
                        position++; SkipMathSpaces(false);
                        if (position < w.Expanded.Count && w.Expanded[position].Is("[")) Node(task.Last).Value = ReadDimension(ReadRawBracket(), CurrentRange());
                        SkipMathSpaces(false); var rules = ReadRules();
                        if (position < w.Expanded.Count && w.Expanded[position].Is("\\end")) { Node(task.Node).RuleBelow = rules; FinishEnvironment(task.Node); result = task.Node; break; }
                        var nextRow = Add(w, options, new(NodeKind.MatrixRow, CurrentRange()) { RuleAbove = rules }); Node(task.Last).NextSibling = nextRow;
                        AddMatrixCell(task.Node, nextRow, -1, 0, task); break;
                    }
                    if (!w.Expanded[position].Is("\\end")) Fail(MathDiagnosticCode.UnexpectedToken, CurrentRange(), "Expected a column separator, row separator, or environment end.");
                    FinishEnvironment(task.Node); result = task.Node; break;
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
                    if (position < w.Expanded.Count && (task.Stop == 2 && w.Expanded[position].Is("]") || task.Stop == 3 && w.Expanded[position].Is("\\right") || task.Stop == 5 && w.Expanded[position].Is("\\endgroup") || task.Stop == 4 && (w.Expanded[position].Is("&") || w.Expanded[position].Is("\\\\") || w.Expanded[position].Is("\\cr") || w.Expanded[position].Is("\\end"))))
                    { if (task.Stop is 2 or 5) position++; result = task.Node; break; }
                    if (position == w.Expanded.Count || w.Expanded[position].Is("}"))
                    {
                        if (task.Stop != 0) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed delimiter, optional argument, or environment.");
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
                    if (!task.Text && next.Text is "\\limits" or "\\nolimits" or "\\displaylimits")
                    {
                        position++; var op = task.Last;
                        if (op >= 0 && Node(op).Kind == NodeKind.Script) op = Node(op).FirstChild;
                        if (op < 0 || Node(op).Kind != NodeKind.Operator) Fail(MathDiagnosticCode.UnexpectedToken, next.Origin, "Limit modifiers must follow an operator.");
                        Node(op).AlwaysLimits = next.Is("\\limits"); Node(op).NeverLimits = next.Is("\\nolimits");
                        if (next.Is("\\displaylimits")) Node(op).Limits = true;
                        Push(w, task); break;
                    }
                    if (!task.Text && next.Text is "\\over" or "\\atop" or "\\choose" or "\\brace" or "\\brack")
                    {
                        if (task.Slot == 1) Fail(MathDiagnosticCode.UnexpectedToken, next.Origin, "Only one infix fraction is allowed in a group.");
                        position++; var numerator = Add(w, options, new(NodeKind.Row, Node(task.Node).Range) { FirstChild = Node(task.Node).FirstChild, Font = task.Font });
                        var denominator = Add(w, options, new(NodeKind.Row, next.Origin) { Font = task.Font });
                        var fraction = Add(w, options, new(NodeKind.Fraction, next.Origin) { FirstChild = numerator, SecondChild = denominator, HasBar = next.Is("\\over"), LeftDelimiter = next.Is("\\choose") ? '(' : next.Is("\\brace") ? '{' : next.Is("\\brack") ? '[' : 0, RightDelimiter = next.Is("\\choose") ? ')' : next.Is("\\brace") ? '}' : next.Is("\\brack") ? ']' : 0 });
                        Node(task.Node).FirstChild = fraction;
                        Push(w, new(ParseTaskKind.Return, task.Node)); Push(w, task with { Node = denominator, Last = -1, Previous = -1, Slot = 1 }); break;
                    }
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
                        Push(w, task with { Kind = ParseTaskKind.Argument, Depth = task.Depth + 1, Stop = 0, Closing = false });
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
                    if (token.Is("{") || token.Is("\\begingroup"))
                    {
                        var semisimple = token.Is("\\begingroup");
                        var row = Add(w, options, new(NodeKind.Row, token.Origin) { Value = semisimple ? 2 : 1, TextMode = task.Text, Font = task.Font });
                        Push(w, task with { Kind = ParseTaskKind.Sequence, Node = row, Last = -1, Previous = -1, Closing = !semisimple, Stop = semisimple ? (byte)5 : (byte)0, Slot = 0, Depth = task.Depth + 1 });
                        break;
                    }
                    if (token.Text is "\\frac" or "\\dfrac" or "\\tfrac" or "\\cfrac" or "\\binom" or "\\dbinom" or "\\tbinom")
                    {
                        if (task.Text) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Fractions are only supported in math mode.");
                        var binom = token.Text.EndsWith("binom", StringComparison.Ordinal);
                        var fraction = Add(w, options, new(NodeKind.Fraction, token.Origin) { Font = task.Font, HasBar = !binom, Text = token.Is("\\cfrac") ? "\\cfrac" : null, LeftDelimiter = binom ? '(' : 0, RightDelimiter = binom ? ')' : 0 }); var outer = fraction;
                        if (token.Text[1] is 'd' or 't' || token.Is("\\cfrac")) outer = Add(w, options, new(NodeKind.Style, token.Origin) { FirstChild = fraction, Style = token.Text[1] == 't' ? MathStyle.Text : MathStyle.Display, Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, outer));
                        PushArgument(fraction, 1, task); PushArgument(fraction, 0, task); break;
                    }
                    if (token.Is("\\sqrt"))
                    {
                        if (task.Text) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Radicals are only supported in math mode.");
                        var radical = Add(w, options, new(NodeKind.Radical, token.Origin) { Font = task.Font });
                        SkipMathSpaces(false);
                        var index = -1;
                        if (position < w.Expanded.Count && w.Expanded[position].Is("[")) { position++; index = Add(w, options, new(NodeKind.Row, token.Origin) { Font = task.Font }); Node(radical).SecondChild = index; }
                        Push(w, new(ParseTaskKind.Return, radical)); PushArgument(radical, 0, task);
                        if (index >= 0) Push(w, task with { Kind = ParseTaskKind.Sequence, Node = index, Last = -1, Previous = -1, Closing = false, Stop = 2, Slot = 0, Depth = task.Depth + 1 });
                        break;
                    }
                    if (token.Is("\\genfrac"))
                    {
                        var left = DelimiterFromText(ReadRawGroup(), token.Origin); var right = DelimiterFromText(ReadRawGroup(), token.Origin);
                        var thickness = ReadRawGroup(); var size = ReadRawGroup();
                        var bar = thickness.Length == 0 ? 0 : ReadDimension(thickness, token.Origin);
                        var fraction = Add(w, options, new(NodeKind.Fraction, token.Origin) { Font = task.Font, HasBar = thickness.Length == 0 || bar > 0, Value = bar, LeftDelimiter = left, RightDelimiter = right }); var outer = fraction;
                        if (size.Length != 0)
                        {
                            if (size.Length != 1 || size[0] is < '0' or > '3') Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Generalized fraction style must be empty or 0 through 3.");
                            outer = Add(w, options, new(NodeKind.Style, token.Origin) { FirstChild = fraction, Style = (MathStyle)(size[0] - '0'), Font = task.Font });
                        }
                        Push(w, new(ParseTaskKind.Return, outer)); PushArgument(fraction, 1, task); PushArgument(fraction, 0, task); break;
                    }
                    if (token.Is("\\left"))
                    {
                        var left = ReadDelimiter(); var wrapper = Add(w, options, new(NodeKind.Delimited, token.Origin) { LeftDelimiter = left, Atom = MathAtom.Inner, Font = task.Font });
                        var body = Add(w, options, new(NodeKind.Row, token.Origin) { Font = task.Font }); Node(wrapper).FirstChild = body; delimiterDepth++;
                        Push(w, task with { Kind = ParseTaskKind.FinishDelimited, Node = wrapper });
                        Push(w, task with { Kind = ParseTaskKind.Sequence, Node = body, Last = -1, Previous = -1, Closing = false, Stop = 3, Slot = 0, Depth = task.Depth + 1 }); break;
                    }
                    var delimiterSize = 0; var delimiterAtom = MathAtom.Ord;
                    if (token.Is("\\middle") || TryDelimiterSize(token.Text, out delimiterSize, out delimiterAtom))
                    {
                        if (token.Is("\\middle") && delimiterDepth == 0) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "A middle delimiter requires a left/right group.");
                        var cp = ReadDelimiter(); result = Add(w, options, new(NodeKind.Delimiter, token.Origin) { CodePoint = cp, Value = token.Is("\\middle") ? 0 : delimiterSize, Atom = token.Is("\\middle") ? MathAtom.Rel : delimiterAtom, Font = task.Font }); break;
                    }
                    if (token.Is("\\begin"))
                    {
                        var environment = ReadRawGroup();
                        if (!IsEnvironment(environment)) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Math environment is not implemented: " + environment);
                        var alignment = environment is "array" or "darray" or "subarray" ? ReadRawGroup() : environment.EndsWith('*') && position < w.Expanded.Count && w.Expanded[position].Is("[") ? ReadRawBracket() : "c";
                        foreach (var a in alignment) if (a is not ('l' or 'c' or 'r' or '|')) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Array column specification supports l, c, r, and vertical rules.");
                        if (alignment.Length == 0) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Expected a nonempty column specification.");
                        var matrix = Add(w, options, new(NodeKind.Matrix, token.Origin) { Text = environment, Alignment = alignment, Font = task.Font });
                        var envBase = environment.TrimEnd('*');
                        Node(matrix).LeftDelimiter = envBase switch { "pmatrix" => '(', "bmatrix" => '[', "Bmatrix" or "cases" or "dcases" => '{', "vmatrix" => '|', "Vmatrix" => 0x2016, _ => 0 };
                        Node(matrix).RightDelimiter = envBase switch { "pmatrix" => ')', "bmatrix" => ']', "Bmatrix" or "rcases" or "drcases" => '}', "vmatrix" => '|', "Vmatrix" => 0x2016, _ => 0 };
                        if (environment == "alignedat")
                        {
                            var pairs = ReadRawGroup(); if (!int.TryParse(pairs, CultureInfo.InvariantCulture, out var pairCount) || pairCount < 1 || pairCount > options.MaxNodes / 2) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Alignedat requires a positive bounded column pair count.");
                            Node(matrix).Value = pairCount;
                        }
                        var rules = ReadRules(); var row = Add(w, options, new(NodeKind.MatrixRow, token.Origin) { RuleAbove = rules }); Node(matrix).FirstChild = row;
                        AddMatrixCell(matrix, row, -1, 0, task); break;
                    }
                    if (token.Text is "\\operatorname" or "\\operatorname@" or "\\operatornamewithlimits" or "\\mathop")
                    {
                        SkipMathSpaces(false); var starred = token.Is("\\operatornamewithlimits");
                        if (token.Is("\\operatorname") && position < w.Expanded.Count && w.Expanded[position].Is("*")) { position++; starred = true; }
                        var op = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = token.Is("\\mathop") ? "\\mathop" : "\\operatorname", Limits = starred, Atom = MathAtom.Op, Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, op)); PushArgument(op, 0, task with { Font = token.Is("\\mathop") ? task.Font : MathFont.MathRoman }); break;
                    }
                    if (TryNamedOperator(token.Text, out var operatorText, out var operatorLimits))
                    {
                        var op = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = token.Text.ToString(), Limits = operatorLimits, Atom = MathAtom.Op, Font = task.Font });
                        var row = Add(w, options, new(NodeKind.Row, token.Origin) { Font = MathFont.MathRoman }); Node(op).FirstChild = row; var previous = -1;
                        foreach (var c in operatorText)
                        {
                            var letter = Add(w, options, c == ' ' ? new(NodeKind.Space, token.Origin) { Value = 3d / 18 } : new(NodeKind.Symbol, token.Origin) { CodePoint = c, Font = MathFont.MathRoman, SymbolFont = MathFont.Default, IsMathOrd = true });
                            if (previous < 0) Node(row).FirstChild = letter; else Node(previous).NextSibling = letter; previous = letter;
                        }
                        result = op; break;
                    }
                    if (TryAccent(token.Text, out var accentCodePoint))
                    {
                        var accent = Add(w, options, new(NodeKind.Accent, token.Origin) { Text = token.Text.ToString(), CodePoint = accentCodePoint, Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, accent)); PushArgument(accent, 0, task); break;
                    }
                    if (IsExtensibleArrow(token.Text))
                    {
                        var arrow = Add(w, options, new(NodeKind.Accent, token.Origin) { Text = token.Text.ToString(), Atom = MathAtom.Rel, Font = task.Font });
                        SkipMathSpaces(false); var below = -1;
                        if (position < w.Expanded.Count && w.Expanded[position].Is("[")) { position++; below = Add(w, options, new(NodeKind.Row, token.Origin) { Font = task.Font }); Node(arrow).SecondChild = below; }
                        Push(w, new(ParseTaskKind.Return, arrow)); PushArgument(arrow, 0, task);
                        if (below >= 0) Push(w, task with { Kind = ParseTaskKind.Sequence, Node = below, Last = -1, Previous = -1, Closing = false, Stop = 2, Slot = 0, Depth = task.Depth + 1 });
                        break;
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
                    {
                        if (task.Text && token.Text[0] != '\\' && options.TextShaper is not null)
                        { result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = Rune.GetRuneAt(token.Source, token.Start).Value, Font = task.Font, TextMode = true }); break; }
                        Fail(token.Text[0] == '\\' ? MathDiagnosticCode.UnsupportedCommand : MathDiagnosticCode.UnknownSymbol, token.Origin, token.Text[0] == '\\' ? "Command is not implemented." : "Symbol requires an available font or an explicit text shaper.");
                    }
                    if (definition.CodePoint < 0 || definition.Group == "accent-token") Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "This symbol requires an unsupported construction rule.");
                    if (definition.Group == "op-token")
                    {
                        result = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = token.Text.ToString(), CodePoint = definition.CodePoint, Atom = MathAtom.Op, Limits = token.Is("\\intop") || token.Is("\\smallint") || definition.CodePoint is not (>= 0x222b and <= 0x2233), Font = task.Font }); break;
                    }
                    result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = definition.CodePoint, Atom = definition.Atom, Font = task.Font == MathFont.Default ? definition.Font : task.Font, SymbolFont = definition.Font, TextMode = task.Text, IsMathOrd = definition.IsMathOrd });
                    break;
            }
        }
        return root;

        ref SemanticNode Node(int index) => ref CollectionsMarshal.AsSpan(w.Nodes)[index];
        SourceRange CurrentRange() => position < w.Expanded.Count ? w.Expanded[position].Origin : new(source.Length, 0);
        void SkipMathSpaces(bool text) { if (!text) while (position < w.Expanded.Count && char.IsWhiteSpace(w.Expanded[position].Text[0])) position++; }
        void PushArgument(int parent, int slot, ParseTask context)
        { Push(w, context with { Kind = ParseTaskKind.SetChild, Node = parent, Slot = slot }); Push(w, context with { Kind = ParseTaskKind.Argument, Depth = context.Depth + 1, Stop = 0, Closing = false }); }
        string ReadRawGroup()
        {
            SkipMathSpaces(false);
            if (position >= w.Expanded.Count || !w.Expanded[position++].Is("{")) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected a braced command argument.");
            var builder = new StringBuilder(); var nesting = 1;
            while (position < w.Expanded.Count)
            {
                cancellationToken.ThrowIfCancellationRequested(); var token = w.Expanded[position++];
                if (token.Is("{")) { if (++nesting > settings.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token.Origin, "Argument nesting budget exceeded."); }
                else if (token.Is("}") && --nesting == 0) return builder.ToString();
                builder.Append(token.Text);
            }
            Fail(MathDiagnosticCode.UnclosedGroup, CurrentRange(), "Unclosed braced command argument."); return "";
        }
        string ReadRawBracket()
        {
            SkipMathSpaces(false);
            if (position >= w.Expanded.Count || !w.Expanded[position++].Is("[")) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected an optional argument.");
            var builder = new StringBuilder();
            while (position < w.Expanded.Count)
            { var token = w.Expanded[position++]; if (token.Is("]")) return builder.ToString(); if (token.Is("{")) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Grouped raw optional arguments are not supported."); builder.Append(token.Text); }
            Fail(MathDiagnosticCode.UnclosedGroup, CurrentRange(), "Unclosed optional argument."); return "";
        }
        int ReadDelimiter()
        {
            SkipMathSpaces(false); if (position >= w.Expanded.Count) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected a delimiter.");
            var token = w.Expanded[position++]; return DelimiterFromText(token.Text, token.Origin);
        }
        int ReadRules()
        {
            SkipMathSpaces(false); var count = 0;
            while (position < w.Expanded.Count && (w.Expanded[position].Is("\\hline") || w.Expanded[position].Is("\\hdashline")))
            {
                var dashed = w.Expanded[position++].Is("\\hdashline");
                if (count != 0 && (count < 0) != dashed) Fail(MathDiagnosticCode.UnsupportedCommand, CurrentRange(), "Mixed adjacent solid and dashed array rules are not implemented.");
                count += dashed ? -1 : 1; SkipMathSpaces(false);
            }
            return count;
        }
        void FinishEnvironment(int matrix)
        {
            position++; var name = ReadRawGroup();
            if (name != Node(matrix).Text) Fail(MathDiagnosticCode.UnexpectedToken, Node(matrix).Range, "Environment end does not match begin.");
            var expected = Node(matrix).Alignment!.Count(c => c is 'l' or 'c' or 'r');
            if (Node(matrix).Text is "array" or "darray" or "subarray" && Node(matrix).ColumnCount > expected) Fail(MathDiagnosticCode.UnexpectedToken, Node(matrix).Range, "Array contains more cells than its column specification.");
            if (Node(matrix).Text is "cases" or "dcases" or "rcases" or "drcases" && Node(matrix).ColumnCount > 2) Fail(MathDiagnosticCode.UnexpectedToken, Node(matrix).Range, "Cases environments have at most two columns.");
            if (Node(matrix).Text == "alignedat" && Node(matrix).ColumnCount > Node(matrix).Value * 2) Fail(MathDiagnosticCode.UnexpectedToken, Node(matrix).Range, "Alignedat contains more cells than its column pair count.");
        }
        void AddMatrixCell(int matrix, int row, int previous, int column, ParseTask context)
        {
            if (context.Depth + 1 > settings.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, CurrentRange(), "Environment nesting budget exceeded.");
            var cell = Add(w, settings, new(NodeKind.MatrixCell, CurrentRange()));
            if (previous < 0) Node(row).FirstChild = cell; else Node(previous).NextSibling = cell;
            var env = Node(matrix).Text!;
            var style = env is "smallmatrix" or "subarray" ? MathStyle.Script : env is "aligned" or "alignedat" or "gathered" or "split" or "darray" or "dcases" or "drcases" ? MathStyle.Display : MathStyle.Text;
            var wrapper = Add(w, settings, new(NodeKind.Style, CurrentRange()) { Style = style, Font = context.Font }); Node(cell).FirstChild = wrapper;
            var body = Add(w, settings, new(NodeKind.Row, CurrentRange()) { Font = context.Font }); Node(wrapper).FirstChild = body;
            var leading = -1;
            if ((column & 1) != 0 && env is "aligned" or "alignedat" or "split")
            { leading = Add(w, settings, new(NodeKind.Row, CurrentRange()) { Value = 1, Font = context.Font }); Node(body).FirstChild = leading; }
            Push(w, context with { Kind = ParseTaskKind.MatrixNextCell, Node = matrix, Last = row, Previous = cell, Slot = column });
            Push(w, context with { Kind = ParseTaskKind.Sequence, Node = body, Last = leading, Previous = -1, Slot = 0, Closing = false, Stop = 4, Depth = context.Depth + 1 });
        }
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
    private static int DelimiterFromText(ReadOnlySpan<char> text, SourceRange range)
    {
        text = text.Trim();
        if (text.Length == 0 || text.SequenceEqual(".")) return 0;
        var cp = text switch
        {
            "(" or "\\lparen" => '(', ")" or "\\rparen" => ')', "[" or "\\lbrack" => '[', "]" or "\\rbrack" => ']',
            "\\{" or "\\lbrace" => '{', "\\}" or "\\rbrace" => '}', "|" or "\\vert" or "\\lvert" or "\\rvert" => '|',
            "\\|" or "\\Vert" or "\\lVert" or "\\rVert" or "‖" => 0x2016,
            "\\langle" or "<" or "⟨" => 0x27e8, "\\rangle" or ">" or "⟩" => 0x27e9,
            "\\lfloor" or "⌊" => 0x230a, "\\rfloor" or "⌋" => 0x230b, "\\lceil" or "⌈" => 0x2308, "\\rceil" or "⌉" => 0x2309,
            "/" => '/', "\\backslash" => '\\', "\\uparrow" or "↑" => 0x2191, "\\downarrow" or "↓" => 0x2193, "\\updownarrow" or "↕" => 0x2195,
            "\\Uparrow" or "⇑" => 0x21d1, "\\Downarrow" or "⇓" => 0x21d3, "\\Updownarrow" or "⇕" => 0x21d5,
            _ => -1,
        };
        if (cp < 0) Fail(MathDiagnosticCode.UnexpectedToken, range, "Expected a valid math delimiter."); return cp;
    }
    private static bool TryDelimiterSize(ReadOnlySpan<char> name, out int size, out MathAtom atom)
    {
        size = 0; atom = MathAtom.Ord;
        if (name.Length < 4 || name[0] != '\\') return false;
        var stem = name;
        if (name[^1] is 'l' or 'r' or 'm') { atom = name[^1] == 'l' ? MathAtom.Open : name[^1] == 'r' ? MathAtom.Close : MathAtom.Rel; stem = name[..^1]; }
        size = stem switch { "\\big" => 1, "\\Big" => 2, "\\bigg" => 3, "\\Bigg" => 4, _ => 0 }; return size != 0;
    }
    private static double ReadDimension(string text, SourceRange range)
    {
        var span = text.AsSpan().Trim(); var split = 0;
        while (split < span.Length && (char.IsDigit(span[split]) || span[split] is '.' or '+' or '-')) split++;
        var number = 0d;
        if (split == 0 || !double.TryParse(span[..split], NumberStyles.Float, CultureInfo.InvariantCulture, out number) || !double.IsFinite(number)) Fail(MathDiagnosticCode.UnexpectedToken, range, "Expected a finite TeX dimension.");
        var factor = span[split..].Trim() switch { "em" => 1d, "ex" => .431, "mu" => 1d / 18, "pt" => .1, "bp" => .100375, "pc" => 1.2, "in" => 7.227, "cm" => 7.227 / 2.54, "mm" => 7.227 / 25.4, "dd" => .1070001, "cc" => 1.284001, "nd" => .106978, "nc" => 1.283736, "sp" => .1 / 65536, _ => double.NaN };
        if (!double.IsFinite(factor) || Math.Abs(number * factor) > 1000) Fail(MathDiagnosticCode.UnexpectedToken, range, "Unsupported or excessive TeX dimension."); return number * factor;
    }
    private static bool IsEnvironment(string name) => name is "matrix" or "pmatrix" or "bmatrix" or "Bmatrix" or "vmatrix" or "Vmatrix" or "matrix*" or "pmatrix*" or "bmatrix*" or "Bmatrix*" or "vmatrix*" or "Vmatrix*" or "array" or "darray" or "smallmatrix" or "subarray" or "cases" or "dcases" or "rcases" or "drcases" or "aligned" or "alignedat" or "gathered" or "split";
    private static bool TryNamedOperator(ReadOnlySpan<char> name, out string text, out bool limits)
    {
        limits = name is "\\det" or "\\gcd" or "\\inf" or "\\lim" or "\\max" or "\\min" or "\\Pr" or "\\sup" or "\\liminf" or "\\limsup";
        var recognized = limits || name is "\\arcsin" or "\\arccos" or "\\arctan" or "\\arctg" or "\\arcctg" or "\\arg" or "\\ch" or "\\cos" or "\\cosec" or "\\cosh" or "\\cot" or "\\cotg" or "\\coth" or "\\csc" or "\\ctg" or "\\cth" or "\\deg" or "\\dim" or "\\exp" or "\\hom" or "\\ker" or "\\lg" or "\\ln" or "\\log" or "\\sec" or "\\sin" or "\\sinh" or "\\sh" or "\\tan" or "\\tanh" or "\\tg" or "\\th";
        text = !recognized ? "" : name is "\\liminf" ? "lim inf" : name is "\\limsup" ? "lim sup" : name[1..].ToString(); return recognized;
    }
    private static bool TryAccent(ReadOnlySpan<char> name, out int codePoint)
    {
        codePoint = name switch
        {
            "\\acute" => 0x2ca, "\\grave" => 0x2cb, "\\ddot" => 0xa8, "\\tilde" or "\\widetilde" => '~', "\\bar" => 0x2c9, "\\breve" => 0x2d8, "\\check" or "\\widecheck" => 0x2c7, "\\hat" or "\\widehat" => '^', "\\vec" => 0x20d7, "\\dot" => 0x2d9, "\\mathring" => 0x2da,
            "\\overline" or "\\underline" or "\\overbrace" or "\\underbrace" or "\\overrightarrow" or "\\overleftarrow" or "\\Overrightarrow" or "\\overleftrightarrow" or "\\underleftrightarrow" or "\\underleftarrow" or "\\underrightarrow" or "\\overgroup" or "\\undergroup" or "\\overlinesegment" or "\\underlinesegment" or "\\overleftharpoon" or "\\overrightharpoon" => 0,
            _ => -1,
        }; return codePoint >= 0;
    }
    private static bool IsExtensibleArrow(ReadOnlySpan<char> name) => name is "\\xleftarrow" or "\\xrightarrow" or "\\xLeftarrow" or "\\xRightarrow" or "\\xleftrightarrow" or "\\xLeftrightarrow" or "\\xhookleftarrow" or "\\xhookrightarrow" or "\\xmapsto" or "\\xleftharpoonup" or "\\xleftharpoondown" or "\\xrightharpoonup" or "\\xrightharpoondown" or "\\xrightleftharpoons" or "\\xleftrightharpoons" or "\\xtwoheadleftarrow" or "\\xtwoheadrightarrow";
    private static bool TryFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\mathrm" => MathFont.MathRoman, "\\mathit" => MathFont.MathItalic, "\\mathbf" or "\\bold" => MathFont.MathBold, "\\mathnormal" => MathFont.MathNormal, "\\mathsf" => MathFont.MathSans, "\\mathsfit" => MathFont.MathSansItalic, "\\mathtt" => MathFont.MathTypewriter, "\\mathbb" or "\\Bbb" => MathFont.AMS, "\\mathcal" => MathFont.Caligraphic, "\\mathfrak" or "\\frak" => MathFont.Fraktur, "\\mathscr" => MathFont.Script, "\\boldsymbol" or "\\bm" => MathFont.MathBoldItalic, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
    private static bool TryTextFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\text" or "\\textrm" or "\\textnormal" => MathFont.MathRoman, "\\textbf" => MathFont.MathBold, "\\textit" => MathFont.MathItalic, "\\textsf" => MathFont.MathSans, "\\texttt" => MathFont.MathTypewriter, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
}
