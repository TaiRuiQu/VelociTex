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
        var position = 0; var result = -1; var delimiterDepth = 0; var pendingArrayStretch = 0d;
        var tokenCount = w.Expanded.Count; var parseEnd = tokenCount;
        while (w.Tasks.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var task = w.Tasks[^1]; w.Tasks.RemoveAt(w.Tasks.Count - 1);
            parseEnd = Math.Min(tokenCount, task.Limit);
            while (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0) pendingArrayStretch = w.Expanded[position++].InternalArrayStretch;
            if (task.Depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, CurrentRange(), "Syntax nesting budget exceeded.");
            switch (task.Kind)
            {
                case ParseTaskKind.Return: result = task.Node; break;
                case ParseTaskKind.ReturnInternal: result = -1; break;
                case ParseTaskKind.AppendPrimeArgument:
                    Node(task.Node).NextSibling = result; break;
                case ParseTaskKind.WrapHBoxArgument:
                    Node(task.Node).FirstChild = result >= 0 && Node(result).Kind == NodeKind.Row ? result : Add(w, options, new(NodeKind.Row, result >= 0 ? Node(result).Range : CurrentRange()) { FirstChild = result, Value = 1, TextMode = true });
                    break;
                case ParseTaskKind.GenfracAfterDelimiters:
                    var leftArgument = NormalizedSingle(Node(task.Last).FirstChild); var rightArgument = NormalizedSingle(Node(task.Previous).FirstChild);
                    if (leftArgument >= 0 && Node(leftArgument).Kind == NodeKind.Symbol && Node(leftArgument).Atom == MathAtom.Open) Node(task.Node).LeftDelimiter = Node(leftArgument).CodePoint;
                    if (rightArgument >= 0 && Node(rightArgument).Kind == NodeKind.Symbol && Node(rightArgument).Atom == MathAtom.Close) Node(task.Node).RightDelimiter = Node(rightArgument).CodePoint;
                    SkipMathSpaces(false); var blankThickness = false; TexLength barLength;
                    if (position < parseEnd && w.Expanded[position].Is("{")) { var raw = ReadRawGroup(); blankThickness = raw.Length == 0; barLength = ParseLength(raw, CurrentRange()); }
                    else barLength = ReadLength();
                    Node(task.Node).HasBar = blankThickness || barLength.Number > 0; Node(task.Node).Length1 = barLength; Node(task.Node).Value = blankThickness ? 0 : LengthEm(barLength); Node(task.Node).Text = blankThickness ? null : "\\genfrac";
                    SkipMathSpaces(false); var rawStyle = position < parseEnd && w.Expanded[position].Is("{") ? ReadRawGroup() : position < parseEnd ? w.Expanded[position++].Text.ToString() : "";
                    var outerFraction = task.Node;
                    if (rawStyle.Length > 0 && rawStyle[0] is >= '0' and <= '3') outerFraction = Add(w, options, new(NodeKind.Style, Node(task.Node).Range) { FirstChild = task.Node, Style = (MathStyle)(rawStyle[0] - '0'), Font = task.Font });
                    Push(w, new(ParseTaskKind.Return, outerFraction)); PushArgument(task.Node, 1, task with { Text = false }); PushArgument(task.Node, 0, task with { Text = false }); break;
                case ParseTaskKind.FinishTag:
                    Node(task.Node).FirstChild = result >= 0 && Node(result).Kind == NodeKind.Row ? Node(result).FirstChild : result;
                    break;
                case ParseTaskKind.FinishTopTag:
                    Node(task.Node).SecondChild = result; result = Add(w, options, new(NodeKind.Row, Node(task.Node).Range)); break;
                case ParseTaskKind.NormalizeChild:
                    var argument = Node(task.Node).FirstChild;
                    if (argument >= 0 && Node(argument).Kind == NodeKind.Row && Node(argument).Value == 1)
                    { Node(task.Node).FirstChild = Node(argument).FirstChild; }
                    result = task.Node; break;
                case ParseTaskKind.FinishClass:
                    if (Node(task.Node).Text is "\\overset" or "\\underset" or "\\stackrel")
                    {
                        var shifted = Node(task.Node).FirstChild; var basis = Node(task.Node).SecondChild;
                        var op = Add(w, options, new(NodeKind.Operator, Node(task.Node).Range) { FirstChild = Unwrap(basis), Limits = true, AlwaysLimits = true, Text = "\\mathop", Atom = MathAtom.Op, Value = Node(task.Node).Text == "\\stackrel" ? 0 : 1 });
                        var scripts = Add(w, options, new(NodeKind.Script, Node(task.Node).Range) { FirstChild = op, SecondChild = Node(task.Node).Text == "\\underset" ? -1 : shifted, ThirdChild = Node(task.Node).Text == "\\underset" ? shifted : -1 });
                        Node(task.Node).Atom = Node(task.Node).Text == "\\stackrel" ? MathAtom.Rel : BinRel(basis);
                        Node(task.Node).FirstChild = scripts; Node(task.Node).SecondChild = -1;
                    }
                    else if (Node(task.Node).Text == "\\@binrel")
                    { Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild); Node(task.Node).FirstChild = Unwrap(Node(task.Node).SecondChild); Node(task.Node).SecondChild = -1; }
                    else if (Node(task.Node).Text is "\\boldsymbol" or "\\bm" or "\\pmb") Node(task.Node).Atom = BinRel(Node(task.Node).FirstChild);
                    result = task.Node; break;
                case ParseTaskKind.FinishDelimited:
                    if (position >= parseEnd || !w.Expanded[position].Is("\\right")) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "A left delimiter requires a matching right delimiter.");
                    position++; Node(task.Node).RightDelimiter = ReadDelimiter(); delimiterDepth--; result = task.Node; break;
                case ParseTaskKind.MatrixNextCell:
                    SkipMathSpaces(false);
                    if (position >= parseEnd) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed math environment.");
                    if (w.Expanded[position].Is("&"))
                    {
                        position++; AddMatrixCell(task.Node, task.Last, task.Previous, task.Slot + 1, task); break;
                    }
                    Node(task.Last).ColumnCount = task.Slot + 1;
                    Node(task.Node).ColumnCount = Math.Max(Node(task.Node).ColumnCount, task.Slot + 1);
                    CompleteMatrixRow(task.Node, task.Last);
                    if (w.Expanded[position].Is("\\\\") || w.Expanded[position].Is("\\cr"))
                    {
                        position++; SkipMathSpaces(false);
                        if (position < parseEnd && w.Expanded[position].Is("[")) { var gap = ParseLength(ReadRawBracket(), CurrentRange()); Node(task.Last).Length1 = gap; Node(task.Last).Value = LengthEm(gap); }
                        SkipMathSpaces(false); var rules = ReadRules();
                        if (position < parseEnd && w.Expanded[position].Is("\\end")) { Node(task.Node).RuleBelow = rules; FinishEnvironment(task.Node); result = task.Node; break; }
                        var nextRow = Add(w, options, new(NodeKind.MatrixRow, CurrentRange()) { RuleAbove = rules }); Node(task.Last).NextSibling = nextRow;
                        AddMatrixCell(task.Node, nextRow, -1, 0, task); break;
                    }
                    if (!w.Expanded[position].Is("\\end")) Fail(MathDiagnosticCode.UnexpectedToken, CurrentRange(), "Expected a column separator, row separator, or environment end.");
                    FinishEnvironment(task.Node); result = task.Node; break;
                case ParseTaskKind.SetChild:
                    ref var parent = ref Node(task.Node);
                    if (task.Slot == 0) parent.FirstChild = result; else if (task.Slot == 1) parent.SecondChild = result; else if (task.Slot == 2) parent.ThirdChild = result; else parent.FourthChild = result;
                    break;
                case ParseTaskKind.AttachSequence:
                    if (result < 0) { Push(w, task with { Kind = ParseTaskKind.Sequence }); break; }
                    if (task.Last < 0) Node(task.Node).FirstChild = result; else Node(task.Last).NextSibling = result;
                    Push(w, task with { Kind = ParseTaskKind.Sequence, Last = result, Previous = task.Last });
                    break;
                case ParseTaskKind.Sequence:
                    SkipMathSpaces(task.Text);
                    if (position < parseEnd && w.Expanded[position].InternalArrayStretch != 0)
                    { pendingArrayStretch = w.Expanded[position++].InternalArrayStretch; Push(w, task); break; }
                    if (position < parseEnd && w.Expanded[position].Is("\\relax")) { position++; Push(w, task); break; }
                    if (position < parseEnd && (task.Stop == 2 && w.Expanded[position].Is("]") || task.Stop == 3 && w.Expanded[position].Is("\\right") || task.Stop == 5 && w.Expanded[position].Is("\\endgroup") || task.Stop == 6 && w.Expanded[position].Is("$") || task.Stop == 7 && w.Expanded[position].Is("\\)") || task.Stop == 4 && (w.Expanded[position].Is("&") || w.Expanded[position].Is("\\\\") || w.Expanded[position].Is("\\cr") || w.Expanded[position].Is("\\end"))))
                    { if (task.Stop is 2 or 5 or 6 or 7) position++; if (task.Text) FormLigatures(task.Node); result = task.Node; break; }
                    if (position == parseEnd || w.Expanded[position].Is("}"))
                    {
                        if (task.Stop != 0) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed delimiter, optional argument, or environment.");
                        if (position == parseEnd && task.Closing) Fail(MathDiagnosticCode.UnclosedGroup, Node(task.Node).Range, "Unclosed group.");
                        if (position < parseEnd)
                        {
                            if (!task.Closing) Fail(MathDiagnosticCode.UnexpectedGroupEnd, CurrentRange(), "Unexpected closing brace.");
                            var closing = w.Expanded[position++].Origin; var old = Node(task.Node).Range;
                            Node(task.Node).Range = new(old.Start, Math.Max(old.Length, closing.Start + closing.Length - old.Start));
                        }
                        if (task.Text) FormLigatures(task.Node); result = task.Node; break;
                    }
                    var next = w.Expanded[position];
                    var unicodeScript = !task.Text && next.Text[0] != '\\' ? UnicodeData.Script(Rune.GetRuneAt(next.Source, next.Start).Value, out _) : null;
                    if (unicodeScript is not null)
                    {
                        var scalar = Rune.GetRuneAt(next.Source, next.Start).Value; UnicodeData.Script(scalar, out var subscript);
                        var script = task.Last;
                        if (script < 0) { var empty = Add(w, options, new(NodeKind.Row, next.Origin)); script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = empty }); Node(task.Node).FirstChild = script; }
                        else if (Node(script).Kind != NodeKind.Script) { script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = task.Last }); if (task.Previous < 0) Node(task.Node).FirstChild = script; else Node(task.Previous).NextSibling = script; }
                        if ((subscript ? Node(script).ThirdChild : Node(script).SecondChild) >= 0) Fail(subscript ? MathDiagnosticCode.DoubleSubscript : MathDiagnosticCode.DoubleSuperscript, next.Origin, "Duplicate Unicode script.");
                        var row = Add(w, options, new(NodeKind.Row, next.Origin) { Value = 1 }); var last = -1;
                        while (position < parseEnd)
                        {
                            var part = w.Expanded[position]; if (part.Text[0] == '\\') break;
                            var mapped = UnicodeData.Script(Rune.GetRuneAt(part.Source, part.Start).Value, out var isSub);
                            if (mapped is null || isSub != subscript) break;
                            var value = AddSymbol(mapped, part.Origin, task); if (last < 0) Node(row).FirstChild = value; else Node(last).NextSibling = value; last = value; position++;
                        }
                        if (subscript) Node(script).ThirdChild = row; else Node(script).SecondChild = row;
                        Push(w, task with { Last = script }); break;
                    }
                    if (!task.Text && next.Is("'"))
                    {
                        var script = task.Last;
                        if (script < 0) { var empty = Add(w, options, new(NodeKind.Row, next.Origin)); script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = empty }); Node(task.Node).FirstChild = script; }
                        else if (Node(script).Kind != NodeKind.Script)
                        { script = Add(w, options, new(NodeKind.Script, next.Origin) { FirstChild = task.Last }); if (task.Previous < 0) Node(task.Node).FirstChild = script; else Node(task.Previous).NextSibling = script; }
                        if (Node(script).SecondChild >= 0) Fail(MathDiagnosticCode.DoubleSuperscript, next.Origin, "A base cannot have both an explicit superscript and a prime superscript.");
                        var primes = Add(w, options, new(NodeKind.Row, next.Origin) { Value = 1 }); var previousPrime = -1;
                        while (position < parseEnd && w.Expanded[position].Is("'"))
                        { var primeToken = w.Expanded[position++]; var prime = Add(w, options, new(NodeKind.Symbol, primeToken.Origin) { CodePoint = 0x2032, Font = MathFont.Default }); if (previousPrime < 0) Node(primes).FirstChild = prime; else Node(previousPrime).NextSibling = prime; previousPrime = prime; }
                        Node(script).SecondChild = primes; Push(w, task with { Last = script });
                        if (position < parseEnd && w.Expanded[position].Is("^"))
                        {
                            position++;
                            Push(w, task with { Kind = ParseTaskKind.AppendPrimeArgument, Node = previousPrime }); Push(w, task with { Kind = ParseTaskKind.Argument, Stop = 0, Closing = false, Depth = task.Depth + 1, PrimitiveArgument = true, Slot = -1 });
                        }
                        break;
                    }
                    if (!task.Text && next.Text is "\\limits" or "\\nolimits" or "\\displaylimits")
                    {
                        position++; var op = task.Last;
                        if (op >= 0 && Node(op).Kind == NodeKind.Script) op = Node(op).FirstChild;
                        if (op < 0 || Node(op).Kind != NodeKind.Operator) Fail(MathDiagnosticCode.UnexpectedToken, next.Origin, "Limit modifiers must follow an operator.");
                        if (Node(op).Text != "\\operatorname" || Node(op).Text2 == "star")
                        { Node(op).AlwaysLimits = next.Is("\\limits"); Node(op).NeverLimits = next.Is("\\nolimits"); if (next.Is("\\displaylimits")) Node(op).Limits = true; }
                        Push(w, task); break;
                    }
                    if (!task.Text && next.Text is "\\over" or "\\atop" or "\\choose" or "\\brace" or "\\brack" or "\\above")
                    {
                        if (task.Slot == 1) Fail(MathDiagnosticCode.UnexpectedToken, next.Origin, "Only one infix fraction is allowed in a group.");
                        position++; var numerator = Add(w, options, new(NodeKind.Row, Node(task.Node).Range) { FirstChild = Node(task.Node).FirstChild, Font = task.Font });
                        var denominator = Add(w, options, new(NodeKind.Row, next.Origin) { Font = task.Font });
                        var fraction = Add(w, options, new(NodeKind.Fraction, next.Origin) { FirstChild = numerator, SecondChild = denominator, HasBar = next.Is("\\over"), LeftDelimiter = next.Is("\\choose") ? '(' : next.Is("\\brace") ? '{' : next.Is("\\brack") ? '[' : 0, RightDelimiter = next.Is("\\choose") ? ')' : next.Is("\\brace") ? '}' : next.Is("\\brack") ? ']' : 0 });
                        if (next.Is("\\above")) { var length = ReadLength(); Node(fraction).Length1 = length; Node(fraction).Value = LengthEm(length); Node(fraction).HasBar = length.Number > 0; Node(fraction).Text = "\\above"; }
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
                        Push(w, task with { Kind = ParseTaskKind.Argument, Depth = task.Depth + 1, Stop = 0, Closing = false, Slot = -1, PrimitiveArgument = true });
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
                    var sizing = 0; var legacyFont = task.Font;
                    if (TrySizing(next.Text, out sizing) || TryLegacyFont(next.Text, out legacyFont) || next.Is("\\color") && !options.ColorIsTextColor)
                    {
                        position++;
                        var kind = next.Is("\\color") ? NodeKind.Color : sizing > 0 ? NodeKind.Sizing : NodeKind.Font;
                        var wrapper = Add(w, options, new(kind, next.Origin) { Value = sizing, Font = sizing > 0 ? task.Font : legacyFont, Text = next.Text.ToString(), TextMode = task.Text });
                        if (kind == NodeKind.Color) Node(wrapper).Text2 = ReadColor();
                        var inner = Add(w, options, new(NodeKind.Row, next.Origin) { Font = kind == NodeKind.Font ? legacyFont : task.Font, TextMode = task.Text }); Node(wrapper).FirstChild = inner;
                        if (task.Last < 0) Node(task.Node).FirstChild = wrapper; else Node(task.Last).NextSibling = wrapper;
                        Push(w, new(ParseTaskKind.Return, task.Node)); Push(w, task with { Node = inner, Last = -1, Previous = -1, Font = kind == NodeKind.Font ? legacyFont : task.Font, Depth = task.Depth + 1 }); break;
                    }
                    Push(w, task with { Kind = ParseTaskKind.AttachSequence });
                    Push(w, task with { Kind = ParseTaskKind.Atom });
                    break;
                case ParseTaskKind.Argument:
                    SkipMathSpaces(false);
                    if (task.Slot == -1) while (position < parseEnd && w.Expanded[position].Is("\\relax")) { position++; SkipMathSpaces(false); }
                    if (position == parseEnd || w.Expanded[position].Is("}") || w.Expanded[position].Is("^") || w.Expanded[position].Is("_"))
                        Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Missing command or script argument.");
                    Push(w, task with { Kind = ParseTaskKind.Atom, Limit = !task.PrimitiveArgument && !w.Expanded[position].Is("{") ? Math.Min(task.Limit, position + 1) : task.Limit }); break;
                case ParseTaskKind.Atom:
                    if (position == parseEnd) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Missing argument.");
                    var token = w.Expanded[position++];
                    if (!token.NoExpand && token.Text.Length == 1 && IsPlainAtomCharacter(token.Text[0]) &&
                        FindSymbol(token.Text, task.Text, out var plainDefinition) && plainDefinition.CodePoint >= 0 &&
                        plainDefinition.Group is not ("op-token" or "accent-token"))
                    {
                        result = Add(w, options, new(NodeKind.Symbol, token.Origin)
                        {
                            CodePoint = plainDefinition.CodePoint, FontIdOverride = -1, Atom = plainDefinition.Atom,
                            Font = task.Font == MathFont.Default ? plainDefinition.Font : task.Font,
                            SymbolFont = plainDefinition.Font, TextMode = task.Text, IsMathOrd = plainDefinition.IsMathOrd
                        });
                        break;
                    }
                    if (FindCommand(token.Text, out var commandRule))
                    {
                        if (task.Text && !commandRule.AllowedInText || !task.Text && !commandRule.AllowedInMath) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Command is not allowed in this input mode.");
                        if (task.Slot == -1 && !commandRule.AllowedInArgument) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "This command requires a braced script argument.");
                    }
                    if (token.Text.StartsWith("\\verb", StringComparison.Ordinal) && token.Length > 5)
                    {
                        var content = token.Text[5..]; var starred = content[0] == '*'; if (starred) content = content[1..];
                        result = Add(w, options, new(NodeKind.Verb, token.Origin) { Text = content[1..^1].ToString(), Value = starred ? 1 : 0, TextMode = true, Font = MathFont.MathTypewriter }); break;
                    }
                    if (token.Is("\\@char"))
                    {
                        var number = ReadRawGroup();
                        if (!int.TryParse(number, CultureInfo.InvariantCulture, out var scalar) || !Rune.IsValid(scalar)) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Character code must be a valid Unicode scalar.");
                        var character = char.ConvertFromUtf32(scalar);
                        if (FindSymbol(character, task.Text, out var characterDefinition)) result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = scalar, Font = task.Font, SymbolFont = characterDefinition.Font, TextMode = task.Text, IsMathOrd = false });
                        else result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = scalar, Font = task.Font, TextMode = task.Text });
                        break;
                    }
                    if (token.Is("\\tag") || token.Is("\\tag@literal") || token.Is("\\tag@paren"))
                    {
                        if (!options.DisplayMode) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Equation tags require display mode.");
                        SkipMathSpaces(false); var starred = token.Is("\\tag@literal") || position < parseEnd && w.Expanded[position].Is("*"); if (position < parseEnd && w.Expanded[position].Is("*")) position++;
                        var target = root;
                        for (var i = w.Tasks.Count - 1; i >= 0; i--) if (w.Tasks[i].Kind == ParseTaskKind.MatrixNextCell) { target = w.Tasks[i].Last; break; }
                        if (Node(target).SecondChild >= 0) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Multiple tags in one equation row.");
                        var tag = Add(w, options, new(NodeKind.Text, token.Origin) { Font = MathFont.MathRoman, TextMode = true, Text = "\\text" });
                        if (starred) { Node(target).SecondChild = tag; Push(w, task with { Kind = ParseTaskKind.ReturnInternal }); PushArgument(tag, 0, task with { Text = true, Font = MathFont.MathRoman }); }
                        else
                        {
                            var body = Add(w, options, new(NodeKind.Row, token.Origin) { TextMode = true }); Node(tag).FirstChild = body;
                            var left = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = '(', Font = MathFont.MathRoman, TextMode = true }); Node(body).FirstChild = left;
                            var tagArgument = Add(w, options, new(NodeKind.Row, token.Origin) { TextMode = true, Font = MathFont.MathRoman, Value = 1 }); Node(left).NextSibling = tagArgument;
                            var right = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = ')', Font = MathFont.MathRoman, TextMode = true }); Node(tagArgument).NextSibling = right;
                            Node(target).SecondChild = tag;
                            Push(w, task with { Kind = ParseTaskKind.ReturnInternal }); Push(w, task with { Kind = ParseTaskKind.FinishTag, Node = tagArgument }); Push(w, task with { Kind = ParseTaskKind.Argument, Text = true, Font = MathFont.MathRoman, Depth = task.Depth + 1, Stop = 0, Closing = false });
                        }
                        break;
                    }
                    if (token.Is("\\nonumber") || token.Is("\\notag"))
                    {
                        for (var i = w.Tasks.Count - 1; i >= 0; i--) if (w.Tasks[i].Kind == ParseTaskKind.MatrixNextCell) { Node(w.Tasks[i].Last).NeverLimits = true; break; }
                        result = -1; break;
                    }
                    if (token.Is("\\relax")) { result = Add(w, options, new(NodeKind.Row, token.Origin)); break; }
                    if (token.Text is "\\nobreak" or "\\allowbreak")
                    { result = Add(w, options, new(NodeKind.Space, token.Origin) { Text = token.Text.ToString() }); break; }
                    if (token.Is("\\\\"))
                    {
                        SkipMathSpaces(false); var gap = position < parseEnd && w.Expanded[position].Is("[") ? ParseLength(ReadRawBracket(), token.Origin) : default;
                        var newLine = !options.DisplayMode || !Strict("newLineInDisplayMode", token.Origin, "A line break in display mode is not a LaTeX line break.", false);
                        result = Add(w, options, new(NodeKind.Space, token.Origin) { Text = "\\\\", Length1 = gap, Value = newLine ? 1 : 0, TextMode = task.Text }); break;
                    }
                    if (token.Is("\\mathchoice") || token.Is("\\html@mathml"))
                    {
                        var choice = Add(w, options, new(token.Is("\\mathchoice") ? NodeKind.Choice : NodeKind.MathMlChoice, token.Origin) { Font = task.Font, TextMode = task.Text, Text = token.Text.ToString() });
                        Push(w, new(ParseTaskKind.Return, choice));
                        if (token.Is("\\mathchoice")) { PushArgument(choice, 3, task, true); PushArgument(choice, 2, task, true); }
                        PushArgument(choice, 1, task, token.Is("\\mathchoice")); PushArgument(choice, 0, task, token.Is("\\mathchoice")); break;
                    }
                    if (token.Is("\\textcolor") || token.Is("\\color") && options.ColorIsTextColor)
                    {
                        var color = ReadColor(); var wrapper = Add(w, options, new(NodeKind.Color, token.Origin) { Text = token.Text.ToString(), Text2 = color, Font = task.Font, TextMode = task.Text });
                        Push(w, task with { Kind = ParseTaskKind.NormalizeChild, Node = wrapper }); PushArgument(wrapper, 0, task); break;
                    }
                    if (token.Is("\\rule"))
                    {
                        SkipMathSpaces(false); var shift = position < parseEnd && w.Expanded[position].Is("[") ? ParseLength(ReadRawBracket(), token.Origin) : default;
                        result = Add(w, options, new(NodeKind.Rule, token.Origin) { Text = "\\rule", Length1 = ReadLength(), Length2 = ReadLength(), Length3 = shift, TextMode = task.Text }); break;
                    }
                    if (token.Text is "\\kern" or "\\mkern" or "\\hskip" or "\\mskip")
                    {
                        var dimension = ReadLength();
                        if ((token.Text is "\\mkern" or "\\mskip") != (dimension.Unit == TexUnit.Mu)) Strict("mathVsTextUnits", token.Origin, "Math and text spacing units use different TeX commands.");
                        result = Add(w, options, new(NodeKind.Space, token.Origin) { Text = token.Text.ToString(), Length1 = dimension, Font = task.Font, TextMode = task.Text }); break;
                    }
                    if (token.Text is "\\phantom" or "\\vphantom" or "\\smash" or "\\mathllap" or "\\mathrlap" or "\\mathclap" or "\\raisebox" or "\\vcenter" or "\\pmb" or "\\reflectbox" or "\\mathreflectbox" or "\\hbox")
                    {
                        var wrapper = Add(w, options, new(NodeKind.Box, token.Origin) { Text = token.Text.ToString(), Font = task.Font, TextMode = task.Text });
                        if (token.Is("\\raisebox")) Node(wrapper).Length1 = ReadLength();
                        if (token.Is("\\smash")) { SkipMathSpaces(false); Node(wrapper).Alignment = position < parseEnd && w.Expanded[position].Is("[") ? ReadRawBracket() : "tb"; }
                        Push(w, task with { Kind = token.Is("\\phantom") || token.Is("\\hbox") ? ParseTaskKind.NormalizeChild : token.Is("\\pmb") ? ParseTaskKind.FinishClass : ParseTaskKind.Return, Node = wrapper });
                        var textArgument = token.Text is "\\raisebox" or "\\reflectbox" or "\\hbox";
                        if (token.Text is "\\raisebox" or "\\reflectbox") PushHBoxArgument(wrapper, task);
                        else PushArgument(wrapper, 0, task with { Text = textArgument || task.Text, Font = textArgument ? MathFont.MathRoman : task.Font }); break;
                    }
                    if (token.Text is "\\colorbox" or "\\fcolorbox" or "\\fbox" or "\\cancel" or "\\bcancel" or "\\xcancel" or "\\phase" or "\\sout" or "\\angl")
                    {
                        if (token.Is("\\sout") && !task.Text) Strict("mathVsSout", token.Origin, "Text strikeout used in math mode.");
                        var wrapper = Add(w, options, new(NodeKind.Enclose, token.Origin) { Text = token.Text.ToString(), Font = task.Font, TextMode = task.Text });
                        if (token.Is("\\fcolorbox")) Node(wrapper).Text3 = ReadColor();
                        if (token.Is("\\colorbox") || token.Is("\\fcolorbox")) Node(wrapper).Text2 = ReadColor();
                        var textArgument = token.Text is "\\colorbox" or "\\fcolorbox" or "\\fbox" or "\\angl";
                        Push(w, new(ParseTaskKind.Return, wrapper)); if (textArgument) PushHBoxArgument(wrapper, task); else PushArgument(wrapper, 0, task); break;
                    }
                    if (TryClass(token.Text, out var atomClass) || token.Text is "\\@binrel" or "\\overset" or "\\underset" or "\\stackrel")
                    {
                        var wrapper = Add(w, options, new(NodeKind.Class, token.Origin) { Text = token.Text.ToString(), Atom = atomClass, Font = task.Font, TextMode = task.Text });
                        Push(w, task with { Kind = ParseTaskKind.FinishClass, Node = wrapper });
                        if (token.Text is "\\@binrel" or "\\overset" or "\\underset" or "\\stackrel") PushArgument(wrapper, 1, task);
                        PushArgument(wrapper, 0, task, token.Text is "\\mathord" or "\\mathbin" or "\\mathrel" or "\\mathopen" or "\\mathclose" or "\\mathpunct" or "\\mathinner"); break;
                    }
                    if (token.Text is "\\boldsymbol" or "\\bm")
                    {
                        var wrapper = Add(w, options, new(NodeKind.Class, token.Origin) { Text = token.Text.ToString(), Font = MathFont.MathBoldItalic });
                        Push(w, task with { Kind = ParseTaskKind.FinishClass, Node = wrapper }); PushArgument(wrapper, 0, task with { Font = MathFont.MathBoldItalic }); break;
                    }
                    if (task.Text && (token.Is("$") || token.Is("\\(")))
                    {
                        var wrapper = Add(w, options, new(NodeKind.Style, token.Origin) { Style = MathStyle.Text, Font = MathFont.Default, TextMode = true });
                        var body = Add(w, options, new(NodeKind.Row, token.Origin)); Node(wrapper).FirstChild = body;
                        Push(w, new(ParseTaskKind.Return, wrapper)); Push(w, task with { Kind = ParseTaskKind.Sequence, Node = body, Last = -1, Previous = -1, Stop = token.Is("$") ? (byte)6 : (byte)7, Closing = false, Text = false, Font = MathFont.Default, Depth = task.Depth + 1 }); break;
                    }
                    if (token.Is("{") || token.Is("\\begingroup"))
                    {
                        var semisimple = token.Is("\\begingroup");
                        var row = Add(w, options, new(NodeKind.Row, token.Origin) { Value = semisimple ? 2 : 1, TextMode = task.Text, Font = task.Font });
                        Push(w, task with { Kind = ParseTaskKind.Sequence, Node = row, Last = -1, Previous = -1, Closing = !semisimple, Stop = semisimple ? (byte)5 : (byte)0, Slot = 0, Depth = task.Depth + 1 });
                        break;
                    }
                    if (token.Text is "\\frac" or "\\dfrac" or "\\tfrac" or "\\cfrac" or "\\binom" or "\\dbinom" or "\\tbinom" or "\\\\atopfrac" or "\\\\bracefrac" or "\\\\brackfrac")
                    {
                        if (task.Text) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Fractions are only supported in math mode.");
                        var binom = token.Text.EndsWith("binom", StringComparison.Ordinal); var noBar = binom || token.Text.StartsWith("\\\\", StringComparison.Ordinal);
                        var fraction = Add(w, options, new(NodeKind.Fraction, token.Origin) { Font = task.Font, HasBar = !noBar, Text = token.Is("\\cfrac") ? "\\cfrac" : null, LeftDelimiter = binom ? '(' : token.Is("\\\\bracefrac") ? '{' : token.Is("\\\\brackfrac") ? '[' : 0, RightDelimiter = binom ? ')' : token.Is("\\\\bracefrac") ? '}' : token.Is("\\\\brackfrac") ? ']' : 0 }); var outer = fraction;
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
                        if (position < parseEnd && w.Expanded[position].Is("[")) { position++; index = Add(w, options, new(NodeKind.Row, token.Origin) { Font = task.Font }); Node(radical).SecondChild = index; }
                        Push(w, new(ParseTaskKind.Return, radical)); PushArgument(radical, 0, task, index < 0);
                        if (index >= 0) Push(w, task with { Kind = ParseTaskKind.Sequence, Node = index, Last = -1, Previous = -1, Closing = false, Stop = 2, Slot = 0, Depth = task.Depth + 1 });
                        break;
                    }
                    if (token.Is("\\genfrac"))
                    {
                        var fraction = Add(w, options, new(NodeKind.Fraction, token.Origin) { Font = task.Font });
                        var leftHolder = Add(w, options, new(NodeKind.Row, token.Origin)); var rightHolder = Add(w, options, new(NodeKind.Row, token.Origin));
                        Push(w, task with { Kind = ParseTaskKind.GenfracAfterDelimiters, Node = fraction, Last = leftHolder, Previous = rightHolder });
                        PushArgument(rightHolder, 0, task with { Text = false }); PushArgument(leftHolder, 0, task with { Text = false }); break;
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
                        if (environment is "equation" or "equation*" or "align" or "align*" or "alignat" or "alignat*" or "gather" or "gather*" or "CD" && !options.DisplayMode) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "This environment requires display mode.");
                        if (!IsEnvironment(environment)) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Math environment is not implemented: " + environment);
                        var fixedStretch = environment is "cases" or "dcases" or "rcases" or "drcases" or "smallmatrix" or "subarray" or "CD";
                        if (!fixedStretch && pendingArrayStretch != 0 && (!double.IsFinite(pendingArrayStretch) || pendingArrayStretch <= 0 || pendingArrayStretch > 1000)) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Invalid or excessive array stretch factor.");
                        if (fixedStretch) pendingArrayStretch = 0;
                        var alignment = environment is "array" or "darray" or "subarray" ? ReadRawGroup() : environment.EndsWith('*') && position < parseEnd && w.Expanded[position].Is("[") ? ReadRawBracket() : "c";
                        foreach (var a in alignment) if (a is not ('l' or 'c' or 'r' or '|' or ':')) Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "Array column specification supports l, c, r, and vertical rules.");
                        if (alignment.Length == 0) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Expected a nonempty column specification.");
                        var matrix = Add(w, options, new(NodeKind.Matrix, token.Origin) { Text = environment, Alignment = alignment, Font = task.Font, Value2 = pendingArrayStretch }); pendingArrayStretch = 0;
                        var envBase = environment.TrimEnd('*');
                        Node(matrix).LeftDelimiter = envBase switch { "pmatrix" => '(', "bmatrix" => '[', "Bmatrix" or "cases" or "dcases" => '{', "vmatrix" => '|', "Vmatrix" => 0x2016, _ => 0 };
                        Node(matrix).RightDelimiter = envBase switch { "pmatrix" => ')', "bmatrix" => ']', "Bmatrix" or "rcases" or "drcases" => '}', "vmatrix" => '|', "Vmatrix" => 0x2016, _ => 0 };
                        if (environment is "alignedat" or "alignat" or "alignat*")
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
                        if (token.Is("\\operatorname") && position < parseEnd && w.Expanded[position].Is("*")) { position++; starred = true; }
                        var op = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = token.Is("\\mathop") ? "\\mathop" : "\\operatorname", Text2 = starred ? "star" : null, Limits = starred, Atom = MathAtom.Op, Font = task.Font });
                        Push(w, new(ParseTaskKind.Return, op)); PushArgument(op, 0, task with { Font = token.Is("\\mathop") ? task.Font : MathFont.MathRoman }, token.Is("\\mathop")); break;
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
                    var unicodeOperator = token.Text.Length == 1 ? token.Text[0] switch { '∏' => "\\prod", '∐' => "\\coprod", '∑' => "\\sum", '⋀' => "\\bigwedge", '⋁' => "\\bigvee", '⋂' => "\\bigcap", '⋃' => "\\bigcup", '⨀' => "\\bigodot", '⨁' => "\\bigoplus", '⨂' => "\\bigotimes", '⨄' => "\\biguplus", '⨆' => "\\bigsqcup", '∫' => "\\int", '∬' => "\\iint", '∭' => "\\iiint", '∮' => "\\oint", '∯' => "\\oiint", '∰' => "\\oiiint", _ => null } : null;
                    if (unicodeOperator is not null && FindSymbol(unicodeOperator, false, out var opDefinition))
                    { result = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = unicodeOperator, CodePoint = opDefinition.CodePoint, Atom = MathAtom.Op, Limits = opDefinition.CodePoint is not (>= 0x222b and <= 0x2233), Font = task.Font }); break; }
                    if (TryAccent(token.Text, out var accentCodePoint))
                    {
                        var textAccent = token.Text is "\\'" or "\\`" or "\\^" or "\\~" or "\\=" or "\\u" or "\\." or "\\\"" or "\\c" or "\\r" or "\\H" or "\\v" or "\\textcircled";
                        if (textAccent && !task.Text) Strict("mathVsTextAccents", token.Origin, "Text accent used in math mode.");
                        var accent = Add(w, options, new(NodeKind.Accent, token.Origin) { Text = token.Text.ToString(), CodePoint = accentCodePoint, Font = task.Font, TextMode = textAccent || task.Text });
                        Push(w, new(ParseTaskKind.Return, accent)); PushArgument(accent, 0, task, textAccent); break;
                    }
                    if (IsExtensibleArrow(token.Text))
                    {
                        var arrow = Add(w, options, new(NodeKind.Accent, token.Origin) { Text = token.Text.ToString(), Atom = MathAtom.Rel, Font = task.Font });
                        SkipMathSpaces(false); var below = -1;
                        if (position < parseEnd && w.Expanded[position].Is("[")) { position++; below = Add(w, options, new(NodeKind.Row, token.Origin) { Font = task.Font }); Node(arrow).SecondChild = below; }
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
                        var family = task.TextFamily; var bold = task.TextBold; var italic = task.TextItalic;
                        if (token.Text is "\\textrm" or "\\textnormal") family = 0;
                        else if (token.Is("\\textsf")) family = 1; else if (token.Is("\\texttt")) family = 2;
                        if (token.Is("\\textbf")) bold = true; else if (token.Is("\\textmd")) bold = false;
                        if (token.Is("\\textit")) italic = true; else if (token.Is("\\textup")) italic = false; else if (token.Is("\\emph")) italic = !italic;
                        textFont = family == 2 ? MathFont.MathTypewriter : family == 1 ? bold && italic ? MathFont.MathSansBoldItalic : bold ? MathFont.MathSansBold : italic ? MathFont.MathSansItalic : MathFont.MathSans : bold && italic ? MathFont.MainBoldItalic : bold ? MathFont.MathBold : italic ? MathFont.MathItalic : MathFont.MathRoman;
                        if (token.Is("\\text")) textFont = task.Font;
                        var wrapper = Add(w, options, new(NodeKind.Text, token.Origin) { Font = textFont, TextMode = true, Text = token.Text.ToString() });
                        Push(w, new(ParseTaskKind.Return, wrapper)); PushArgument(wrapper, 0, task with { Font = textFont, Text = true, TextFamily = family, TextBold = bold, TextItalic = italic }); break;
                    }
                    if (token.Is("\\,") || token.Is("\\:") || token.Is("\\;") || token.Is("\\!") || token.Is("\\quad") || token.Is("\\qquad"))
                    {
                        var width = token.Is("\\,") ? 3d / 18 : token.Is("\\:") ? 4d / 18 : token.Is("\\;") ? 5d / 18 : token.Is("\\!") ? -3d / 18 : token.Is("\\quad") ? 1 : 2;
                        if (task.Text) width = token.Is("\\,") ? .1667 : token.Is("\\:") ? .2222 : token.Is("\\;") ? .2777 : token.Is("\\!") ? -.1667 : width;
                        result = Add(w, options, new(NodeKind.Space, token.Origin) { Value = width, TextMode = task.Text, Font = task.Font }); break;
                    }
                    if (char.IsWhiteSpace(token.Text[0]) && task.Text)
                    { result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = 32, Font = task.Font, TextMode = true }); break; }
                    if (token.Is("}") || token.Is("^") || token.Is("_") || token.Is("&") || token.Is("#") || token.Is("$")) Fail(MathDiagnosticCode.UnexpectedToken, token.Origin, "Unexpected TeX special token.");
                    if (token.Text[0] != '\\' && (token.Text.Length > 1 || token.Text[0] >= 0x80))
                    {
                        var scalar = Rune.GetRuneAt(token.Source, token.Start).Value;
                        var expansion = !FindSymbol(char.ConvertFromUtf32(scalar), task.Text, out _) ? UnicodeData.Decompose(scalar) : null;
                        if (expansion is not null && !task.Text) Strict("unicodeTextInMathMode", token.Origin, "Accented Unicode text used in math mode.");
                        var combined = expansion is null ? token.Text.ToString() : expansion + token.Text[Rune.GetRuneAt(token.Source, token.Start).Utf16SequenceLength..].ToString();
                        var baseLength = Rune.GetRuneAt(combined, 0).Utf16SequenceLength;
                        if (baseLength < combined.Length && combined[baseLength] is >= '\u0300' and <= '\u036f')
                        {
                            var baseText = combined[..baseLength]; if (baseText == "i") baseText = "ı"; else if (baseText == "j") baseText = "ȷ";
                            var basis = AddSymbol(baseText, token.Origin, task);
                            foreach (var mark in combined.AsSpan(baseLength))
                            {
                                var command = UnicodeData.Accent(mark, task.Text);
                                var point = 0;
                                if (command is null || !TryAccent(command, out point)) Fail(MathDiagnosticCode.UnknownSymbol, token.Origin, "Unsupported combining accent.");
                                basis = Add(w, options, new(NodeKind.Accent, token.Origin) { FirstChild = basis, Text = command, CodePoint = point, TextMode = task.Text, Font = task.Font });
                            }
                            result = basis; break;
                        }
                    }
                    if (!FindSymbol(token.Text, task.Text, out var definition))
                    {
                        if (token.Text[0] >= 0x80 && options.TextShaper is not null)
                        { if (!task.Text) Strict("unicodeTextInMathMode", token.Origin, "Unicode text used in math mode."); result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = Rune.GetRuneAt(token.Source, token.Start).Value, Font = task.Font, TextMode = true }); break; }
                        Fail(token.Text[0] == '\\' ? MathDiagnosticCode.UnsupportedCommand : MathDiagnosticCode.UnknownSymbol, token.Origin, token.Text[0] == '\\' ? "Command is not implemented." : "Symbol requires an available font or an explicit text shaper.");
                    }
                    if (definition.CodePoint < 0 || definition.Group == "accent-token") Fail(MathDiagnosticCode.UnsupportedCommand, token.Origin, "This symbol requires an unsupported construction rule.");
                    if (definition.Group == "op-token")
                    {
                        result = Add(w, options, new(NodeKind.Operator, token.Origin) { Text = token.Text.ToString(), CodePoint = definition.CodePoint, Atom = MathAtom.Op, Limits = token.Is("\\intop") || token.Is("\\smallint") || definition.CodePoint is not (>= 0x222b and <= 0x2233), Font = task.Font }); break;
                    }
                    var wide = token.Text[0] == '\ud835' && token.Text.Length >= 2 ? Rune.GetRuneAt(token.Source, token.Start).Value : 0;
                    var wideFont = wide is >= 0x1d400 and < 0x1d6a4 ? ((wide - 0x1d400) / 26) switch { 0 or 1 => 5, 2 or 3 => 10, 4 or 5 => 6, 6 => 14, 10 or 11 or 14 or 15 => 4, 12 or 13 => 0, 16 or 17 => 13, 18 or 19 => 11, 20 or 21 => 12, 24 or 25 => 19, _ => -1 } : wide is >= 0x1d7ce and <= 0x1d7ff ? ((wide - 0x1d7ce) / 10) switch { 0 => 5, 2 => 13, 3 => 11, 4 => 19, _ => -1 } : -1;
                    result = Add(w, options, new(NodeKind.Symbol, token.Origin) { CodePoint = definition.CodePoint, OriginalCodePoint = wide, FontIdOverride = wideFont, Atom = definition.Atom, Font = task.Font == MathFont.Default ? definition.Font : task.Font, SymbolFont = definition.Font, TextMode = task.Text, IsMathOrd = definition.IsMathOrd });
                    break;
            }
        }
        var topTag = Node(root).SecondChild;
        if (topTag >= 0) { Node(root).SecondChild = -1; return Add(w, options, new(NodeKind.Tag, new(0, source.Length)) { FirstChild = root, SecondChild = topTag }); }
        return root;

        ref SemanticNode Node(int index) => ref CollectionsMarshal.AsSpan(w.Nodes)[index];
        bool Strict(string code, SourceRange range, string message, bool report = true)
        {
            var behavior = settings.StrictMode;
            if (settings.StrictHandler is not null)
            {
                if (report) behavior = settings.StrictHandler(new(code, range, message));
                else { try { behavior = settings.StrictHandler(new(code, range, message)); } catch { behavior = MathStrictMode.Error; } }
            }
            if (behavior == MathStrictMode.Ignore) return false;
            if (behavior == MathStrictMode.Error) { if (report) Fail(MathDiagnosticCode.StrictViolation, range, message + " [" + code + "]"); return true; }
            if (w.Warnings.Count < Math.Min(settings.MaxTokens, 1024)) w.Warnings.Add(new(code, range, message));
            return false;
        }
        int AddSymbol(string name, SourceRange range, ParseTask context)
        {
            if (FindSymbol(name, context.Text, out var definition))
                return Add(w, settings, new(NodeKind.Symbol, range) { CodePoint = definition.CodePoint, Atom = definition.Atom, Font = context.Font == MathFont.Default ? definition.Font : context.Font, SymbolFont = definition.Font, TextMode = context.Text, IsMathOrd = definition.IsMathOrd });
            var scalar = Rune.GetRuneAt(name, 0).Value;
            if (scalar >= 0x80 && settings.TextShaper is not null)
                return Add(w, settings, new(NodeKind.Symbol, range) { CodePoint = scalar, Font = context.Font, TextMode = true });
            Fail(MathDiagnosticCode.UnknownSymbol, range, "Unicode base symbol has no available font."); return -1;
        }
        void FormLigatures(int row)
        {
            for (var child = Node(row).FirstChild; child >= 0; child = Node(child).NextSibling)
            {
                if (Node(child).Kind != NodeKind.Symbol || Node(child).Font == MathFont.MathTypewriter) continue;
                var next = Node(child).NextSibling; if (next < 0 || Node(next).Kind != NodeKind.Symbol) continue;
                var cp = Node(child).CodePoint;
                if (cp == '-' && Node(next).CodePoint == '-')
                {
                    var after = Node(next).NextSibling; var third = after >= 0 && Node(after).Kind == NodeKind.Symbol && Node(after).CodePoint == '-';
                    Node(child).CodePoint = third ? 0x2014 : 0x2013; Node(child).NextSibling = third ? Node(after).NextSibling : Node(next).NextSibling;
                }
                else if (cp is '\'' or '`' or 0x2018 or 0x2019 && Node(next).CodePoint == cp)
                { Node(child).CodePoint = cp is '\'' or 0x2019 ? 0x201d : 0x201c; Node(child).NextSibling = Node(next).NextSibling; }
            }
        }
        int Unwrap(int index) => index >= 0 && Node(index).Kind == NodeKind.Row && Node(index).Value == 1 ? Node(index).FirstChild : index;
        int NormalizedSingle(int index) => index >= 0 && Node(index).Kind == NodeKind.Row ? Node(index).FirstChild >= 0 && Node(Node(index).FirstChild).NextSibling < 0 ? Node(index).FirstChild : -1 : index;
        MathAtom BinRel(int index)
        {
            var child = Unwrap(index); if (child >= 0 && Node(child).Atom is MathAtom.Bin or MathAtom.Rel) return Node(child).Atom; return MathAtom.Ord;
        }
        SourceRange CurrentRange() => position < parseEnd ? w.Expanded[position].Origin : new(source.Length, 0);
        void SkipMathSpaces(bool text) { if (!text) while (position < parseEnd && w.Expanded[position].Length > 0 && char.IsWhiteSpace(w.Expanded[position].Text[0])) position++; }
        void PushArgument(int parent, int slot, ParseTask context, bool primitive = false)
        { Push(w, context with { Kind = ParseTaskKind.SetChild, Node = parent, Slot = slot }); Push(w, context with { Kind = ParseTaskKind.Argument, Depth = context.Depth + 1, Stop = 0, Closing = false, Slot = primitive ? -1 : 0, PrimitiveArgument = primitive }); }
        void PushHBoxArgument(int parent, ParseTask context)
        {
            var style = Add(w, settings, new(NodeKind.Style, Node(parent).Range) { Style = MathStyle.Text, Font = MathFont.MathRoman, TextMode = true }); Node(parent).FirstChild = style;
            Push(w, context with { Kind = ParseTaskKind.WrapHBoxArgument, Node = style });
            Push(w, context with { Kind = ParseTaskKind.Argument, Depth = context.Depth + 1, Stop = 0, Closing = false, Slot = 0, PrimitiveArgument = false, Text = true, Font = MathFont.MathRoman });
        }
        string ReadRawGroup()
        {
            SkipMathSpaces(false);
            if (position >= parseEnd || !w.Expanded[position++].Is("{")) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected a braced command argument.");
            var builder = new StringBuilder(); var nesting = 1;
            while (position < parseEnd)
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
            if (position >= parseEnd || !w.Expanded[position++].Is("[")) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected an optional argument.");
            var builder = new StringBuilder(); var braces = 0;
            while (position < parseEnd)
            { var token = w.Expanded[position++]; if (token.Is("]") && braces == 0) return builder.ToString(); if (token.Is("{")) braces++; else if (token.Is("}") && --braces < 0) Fail(MathDiagnosticCode.UnexpectedGroupEnd, token.Origin, "Unexpected optional argument closing brace."); builder.Append(token.Text); }
            Fail(MathDiagnosticCode.UnclosedGroup, CurrentRange(), "Unclosed optional argument."); return "";
        }
        int ReadDelimiter()
        {
            SkipMathSpaces(false); if (position >= parseEnd) Fail(MathDiagnosticCode.MissingArgument, CurrentRange(), "Expected a delimiter.");
            var token = w.Expanded[position++]; return DelimiterFromText(token.Text, token.Origin);
        }
        string ReadColor()
        {
            var text = ReadRawGroup().Trim();
            var valid = text.StartsWith('#') ? text.Length is 4 or 5 or 7 or 9 && text.AsSpan(1).IndexOfAnyExcept("0123456789abcdefABCDEF") < 0 : text.Length == 6 && text.All(Uri.IsHexDigit) || text.Length > 0 && text.All(char.IsAsciiLetter);
            if (!valid) Fail(MathDiagnosticCode.UnexpectedToken, CurrentRange(), "Invalid TeX color token.");
            if (text.Length == 6 && text.All(Uri.IsHexDigit)) text = "#" + text;
            return text;
        }
        TexLength ReadLength()
        {
            SkipMathSpaces(false);
            if (position < parseEnd && w.Expanded[position].Is("{")) return ParseLength(ReadRawGroup(), CurrentRange());
            var value = new StringBuilder(); var range = CurrentRange();
            while (position < parseEnd)
            {
                var part = w.Expanded[position].Text;
                if (part.Length > 0 && char.IsWhiteSpace(part[0])) { value.Append(' '); position++; continue; }
                if (part.Length != 1 || !(char.IsDigit(part[0]) || part[0] is '.' or '+' or '-')) break;
                value.Append(part); position++;
            }
            SkipMathSpaces(false);
            for (var unitLetter = 0; unitLetter < 2 && position < parseEnd; unitLetter++)
            { var part = w.Expanded[position].Text; if (part.Length != 1 || !char.IsAsciiLetter(part[0])) break; value.Append(part); position++; }
            SkipMathSpaces(false);
            return ParseLength(value.ToString(), range);
        }
        int ReadRules()
        {
            SkipMathSpaces(false); var count = 0;
            while (position < parseEnd && (w.Expanded[position].Is("\\hline") || w.Expanded[position].Is("\\hdashline")))
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
            if (Node(matrix).Text == "CD") TransformCommutativeDiagram(matrix);
        }
        void TransformCommutativeDiagram(int matrix)
        {
            var rowNumber = 0; var finalRow = -1;
            for (var row = Node(matrix).FirstChild; row >= 0; row = Node(row).NextSibling, rowNumber++)
            {
                finalRow = row;
                var values = new List<int>();
                for (var cell = Node(row).FirstChild; cell >= 0; cell = Node(cell).NextSibling)
                {
                    var style = Node(cell).FirstChild; var body = Node(style).FirstChild;
                    for (var part = Node(body).FirstChild; part >= 0; part = Node(part).NextSibling) values.Add(part);
                }
                Node(row).FirstChild = -1; var lastCell = -1; var contents = new List<int>(); var column = 0;
                for (var i = 0; i < values.Count; i++)
                {
                    if (!IsCharacter(values[i], '@')) { contents.Add(values[i]); continue; }
                    AppendCell(contents); contents.Clear();
                    if (++i >= values.Count || Node(values[i]).Kind != NodeKind.Symbol) Fail(MathDiagnosticCode.UnexpectedToken, Node(row).Range, "CD arrow requires one of <>AV=|. after @.");
                    var direction = Node(values[i]).CodePoint; if (direction == 0x2223) direction = '|'; var labels = new[] { new List<int>(), new List<int>() };
                    if (direction is '<' or '>' or 'A' or 'V')
                    {
                        for (var label = 0; label < 2; label++)
                        {
                            var closed = false;
                            while (++i < values.Count)
                            { if (IsCharacter(values[i], direction)) { closed = true; break; } if (IsCharacter(values[i], '@')) Fail(MathDiagnosticCode.UnexpectedToken, Node(values[i]).Range, "CD arrow label is missing its terminator."); labels[label].Add(values[i]); }
                            if (!closed) Fail(MathDiagnosticCode.UnclosedGroup, Node(row).Range, "CD arrow label is missing its terminator.");
                        }
                    }
                    else if (direction is not ('=' or '|' or '.')) Fail(MathDiagnosticCode.UnexpectedToken, Node(values[i]).Range, "CD arrow direction is unavailable.");
                    var above = MakeRow(labels[0]); var below = MakeRow(labels[1]); int arrow;
                    if (direction is '<' or '>' or '=') arrow = Add(w, settings, new(NodeKind.Accent, Node(row).Range) { FirstChild = direction == '=' ? -1 : above, SecondChild = direction == '=' ? -1 : below, Atom = MathAtom.Rel, Text = direction == '<' ? "\\\\cdleftarrow" : direction == '>' ? "\\\\cdrightarrow" : "\\\\cdlongequal" });
                    else if (direction == '.') arrow = Add(w, settings, new(NodeKind.Space, Node(row).Range) { Text = "cdnull", Value = 1d / 3 });
                    else if (direction == '|') arrow = Add(w, settings, new(NodeKind.Delimiter, Node(row).Range) { CodePoint = 0x2016, Value = 2 });
                    else
                    {
                        var left = Add(w, settings, new(NodeKind.Box, Node(row).Range) { Text = "cdleft", FirstChild = above });
                        var shaft = Add(w, settings, new(NodeKind.Delimiter, Node(row).Range) { CodePoint = direction == 'A' ? 0x2191 : direction == 'V' ? 0x2193 : 0x2016, Value = 2 });
                        var right = Add(w, settings, new(NodeKind.Box, Node(row).Range) { Text = "cdright", FirstChild = below });
                        Node(left).NextSibling = shaft; Node(shaft).NextSibling = right;
                        var body = Add(w, settings, new(NodeKind.Row, Node(row).Range) { FirstChild = left, Value = 1 });
                        arrow = Add(w, settings, new(NodeKind.Box, Node(row).Range) { Text = "cdparent", FirstChild = body });
                    }
                    AppendCell([arrow]);
                }
                if ((rowNumber & 1) == 0) AppendCell(contents);
                else if (Node(row).FirstChild >= 0) { Node(row).FirstChild = Node(Node(row).FirstChild).NextSibling; column--; }
                Node(row).ColumnCount = column; Node(matrix).ColumnCount = Math.Max(Node(matrix).ColumnCount, column);

                bool IsCharacter(int index, int codePoint) => Node(index).Kind == NodeKind.Symbol && Node(index).CodePoint == codePoint;
                int MakeRow(List<int> parts)
                {
                    var body = Add(w, settings, new(NodeKind.Row, Node(row).Range) { Value = 1 }); var last = -1;
                    foreach (var part in parts) { Node(part).NextSibling = -1; if (last < 0) Node(body).FirstChild = part; else Node(last).NextSibling = part; last = part; }
                    return body;
                }
                void AppendCell(List<int> parts)
                {
                    var body = MakeRow(parts); var style = Add(w, settings, new(NodeKind.Style, Node(row).Range) { FirstChild = body, Style = MathStyle.Display });
                    var cell = Add(w, settings, new(NodeKind.MatrixCell, Node(row).Range) { FirstChild = style });
                    if (lastCell < 0) Node(row).FirstChild = cell; else Node(lastCell).NextSibling = cell; lastCell = cell; column++;
                }
            }
            if (finalRow >= 0) Node(finalRow).NextSibling = Add(w, settings, new(NodeKind.MatrixRow, Node(matrix).Range));
        }
        void CompleteMatrixRow(int matrix, int row)
        {
            var env = Node(matrix).Text;
            if (Node(row).SecondChild >= 0 || Node(row).NeverLimits || env is not ("equation" or "align" or "alignat" or "gather")) return;
            var tag = Add(w, settings, new(NodeKind.Text, Node(row).Range) { Font = MathFont.MathRoman, TextMode = true, Text = "\\text" });
            var body = Add(w, settings, new(NodeKind.Row, Node(row).Range) { TextMode = true }); Node(tag).FirstChild = body; var last = -1;
            var number = settings.EquationNumber; for (var previous = Node(matrix).FirstChild; previous >= 0 && previous != row; previous = Node(previous).NextSibling) number++;
            foreach (var character in "(" + number.ToString(CultureInfo.InvariantCulture) + ")")
            { var symbol = Add(w, settings, new(NodeKind.Symbol, Node(row).Range) { CodePoint = character, TextMode = true, Font = MathFont.MathRoman }); if (last < 0) Node(body).FirstChild = symbol; else Node(last).NextSibling = symbol; last = symbol; }
            Node(row).SecondChild = tag; Node(row).AlwaysLimits = true;
        }
        void AddMatrixCell(int matrix, int row, int previous, int column, ParseTask context)
        {
            if (context.Depth + 1 > settings.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, CurrentRange(), "Environment nesting budget exceeded.");
            var cell = Add(w, settings, new(NodeKind.MatrixCell, CurrentRange()));
            if (previous < 0) Node(row).FirstChild = cell; else Node(previous).NextSibling = cell;
            var env = Node(matrix).Text!;
            var style = env is "smallmatrix" or "subarray" ? MathStyle.Script : env is "aligned" or "alignedat" or "align" or "align*" or "alignat" or "alignat*" or "gather" or "gather*" or "equation" or "equation*" or "CD" or "gathered" or "split" or "darray" or "dcases" or "drcases" ? MathStyle.Display : MathStyle.Text;
            var wrapper = Add(w, settings, new(NodeKind.Style, CurrentRange()) { Style = style, Font = context.Font }); Node(cell).FirstChild = wrapper;
            var body = Add(w, settings, new(NodeKind.Row, CurrentRange()) { Font = context.Font }); Node(wrapper).FirstChild = body;
            var leading = -1;
            if ((column & 1) != 0 && env is "aligned" or "alignedat" or "align" or "align*" or "alignat" or "alignat*" or "split")
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
        var map = text ? TextSymbolMap : MathSymbolMap;
        return map.GetAlternateLookup<ReadOnlySpan<char>>().TryGetValue(name, out definition);
    }
    private static bool FindCommand(ReadOnlySpan<char> name, out CommandDefinition definition)
        => CommandMap.GetAlternateLookup<ReadOnlySpan<char>>().TryGetValue(name, out definition);
    private static readonly Dictionary<string, SymbolDefinition> MathSymbolMap = BuildSymbolMap(false);
    private static readonly Dictionary<string, SymbolDefinition> TextSymbolMap = BuildSymbolMap(true);
    private static readonly Dictionary<string, CommandDefinition> CommandMap = BuildCommandMap();
    private static Dictionary<string, SymbolDefinition> BuildSymbolMap(bool text)
    {
        var map = new Dictionary<string, SymbolDefinition>(StringComparer.Ordinal);
        foreach (var symbol in SymbolData.Symbols) if (symbol.TextMode == text) map.TryAdd(symbol.Name, symbol);
        return map;
    }
    private static Dictionary<string, CommandDefinition> BuildCommandMap()
    {
        var map = new Dictionary<string, CommandDefinition>(StringComparer.Ordinal);
        foreach (var command in CommandData.Definitions) map.TryAdd(command.Name, command);
        return map;
    }
    private static bool TryStyle(ReadOnlySpan<char> name, out MathStyle style)
    {
        style = name switch { "\\displaystyle" => MathStyle.Display, "\\textstyle" => MathStyle.Text, "\\scriptstyle" => MathStyle.Script, "\\scriptscriptstyle" => MathStyle.ScriptScript, _ => (MathStyle)(-1) }; return (int)style >= 0;
    }
    private static bool IsPlainAtomCharacter(char value) => value is > '\u0020' and < '\u007f' and not ('\\' or '{' or '}' or '^' or '_' or '&' or '\'' or '$' or '#');
    private static int DelimiterFromText(ReadOnlySpan<char> text, SourceRange range)
    {
        text = text.Trim();
        if (text.Length == 0 || text.SequenceEqual(".")) return 0;
        var cp = text switch
        {
            "(" or "\\lparen" => '(', ")" or "\\rparen" => ')', "[" or "\\lbrack" => '[', "]" or "\\rbrack" => ']',
            "\\{" or "\\lbrace" => '{', "\\}" or "\\rbrace" => '}', "|" or "\\vert" or "\\lvert" or "\\rvert" => '|',
            "\\|" or "\\Vert" or "\\lVert" or "\\rVert" or "‖" => 0x2016,
            "\\langle" or "<" or "\\lt" or "⟨" => 0x27e8, "\\rangle" or ">" or "\\gt" or "⟩" => 0x27e9, "⟮" or "\\lgroup" => 0x27ee, "⟯" or "\\rgroup" => 0x27ef,
            "⎰" or "\\lmoustache" => 0x23b0, "⎱" or "\\rmoustache" => 0x23b1,
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
        => LengthEm(ParseLength(text, range));
    private static TexLength ParseLength(string text, SourceRange range)
    {
        var span = text.AsSpan().Trim(); if (span.Length == 0) return new(0, TexUnit.Pt);
        var compact = new string(span.ToArray().Where(c => !char.IsWhiteSpace(c)).ToArray()); span = compact.AsSpan(); var split = 0;
        while (split < span.Length && (char.IsDigit(span[split]) || span[split] is '.' or '+' or '-')) split++;
        var number = 0d;
        if (split == 0 || !double.TryParse(span[..split], NumberStyles.Float, CultureInfo.InvariantCulture, out number) || !double.IsFinite(number)) Fail(MathDiagnosticCode.UnexpectedToken, range, "Expected a finite TeX dimension.");
        var unit = span[split..].Trim() switch { "em" => TexUnit.Em, "ex" => TexUnit.Ex, "mu" => TexUnit.Mu, "pt" => TexUnit.Pt, "bp" => TexUnit.Bp, "pc" => TexUnit.Pc, "in" => TexUnit.In, "cm" => TexUnit.Cm, "mm" => TexUnit.Mm, "dd" => TexUnit.Dd, "cc" => TexUnit.Cc, "nd" => TexUnit.Nd, "nc" => TexUnit.Nc, "sp" => TexUnit.Sp, "px" => TexUnit.Px, _ => (TexUnit)(-1) };
        if ((int)unit < 0 || Math.Abs(number) > 1e8) Fail(MathDiagnosticCode.UnexpectedToken, range, "Unsupported or excessive TeX dimension."); return new(number, unit);
    }
    private static double LengthEm(TexLength length) => length.Number * (length.Unit switch { TexUnit.Em => 1d, TexUnit.Ex => .431, TexUnit.Mu => 1d / 18, TexUnit.Pt => .1, TexUnit.Bp => .100375, TexUnit.Pc => 1.2, TexUnit.In => 7.227, TexUnit.Cm => 7.227 / 2.54, TexUnit.Mm => 7.227 / 25.4, TexUnit.Dd => .1070001, TexUnit.Cc => 1.284001, TexUnit.Nd => .106978, TexUnit.Nc => 1.283736, TexUnit.Sp => .1 / 65536, _ => 1 });
    private static bool TrySizing(ReadOnlySpan<char> name, out int size)
    { size = name switch { "\\tiny" => 1, "\\sixptsize" => 2, "\\scriptsize" => 3, "\\footnotesize" => 4, "\\small" => 5, "\\normalsize" => 6, "\\large" => 7, "\\Large" => 8, "\\LARGE" => 9, "\\huge" => 10, "\\Huge" => 11, _ => 0 }; return size != 0; }
    private static bool TryLegacyFont(ReadOnlySpan<char> name, out MathFont font)
    { font = name switch { "\\rm" => MathFont.MathRoman, "\\sf" => MathFont.MathSans, "\\tt" => MathFont.MathTypewriter, "\\bf" => MathFont.MathBold, "\\it" => MathFont.MathItalic, "\\cal" => MathFont.Caligraphic, _ => (MathFont)(-1) }; return (int)font >= 0; }
    private static bool TryClass(ReadOnlySpan<char> name, out MathAtom atom)
    { atom = name switch { "\\mathord" => MathAtom.Ord, "\\mathbin" => MathAtom.Bin, "\\mathrel" => MathAtom.Rel, "\\mathopen" => MathAtom.Open, "\\mathclose" => MathAtom.Close, "\\mathpunct" => MathAtom.Punct, "\\mathinner" => MathAtom.Inner, _ => (MathAtom)(-1) }; return (int)atom >= 0; }
    private static bool IsEnvironment(string name) => name is "matrix" or "pmatrix" or "bmatrix" or "Bmatrix" or "vmatrix" or "Vmatrix" or "matrix*" or "pmatrix*" or "bmatrix*" or "Bmatrix*" or "vmatrix*" or "Vmatrix*" or "array" or "darray" or "smallmatrix" or "subarray" or "cases" or "dcases" or "rcases" or "drcases" or "aligned" or "alignedat" or "gathered" or "split" or "align" or "align*" or "alignat" or "alignat*" or "gather" or "gather*" or "equation" or "equation*" or "CD";
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
            "\\acute" or "\\'" => 0x2ca, "\\grave" or "\\`" => 0x2cb, "\\ddot" or "\\\"" => 0xa8, "\\tilde" or "\\widetilde" => '~', "\\~" => 0x2dc, "\\bar" or "\\=" => 0x2c9, "\\breve" or "\\u" => 0x2d8, "\\check" or "\\widecheck" or "\\v" => 0x2c7, "\\hat" or "\\widehat" or "\\^" => '^', "\\vec" => 0x20d7, "\\dot" or "\\." => 0x2d9, "\\mathring" or "\\r" => 0x2da, "\\H" => 0x2dd, "\\c" => 0xb8, "\\textcircled" => 0x25ef,
            "\\overline" or "\\underline" or "\\overbrace" or "\\underbrace" or "\\overbracket" or "\\underbracket" or "\\utilde" or "\\overrightarrow" or "\\overleftarrow" or "\\Overrightarrow" or "\\overleftrightarrow" or "\\underleftrightarrow" or "\\underleftarrow" or "\\underrightarrow" or "\\overgroup" or "\\undergroup" or "\\overlinesegment" or "\\underlinesegment" or "\\overleftharpoon" or "\\overrightharpoon" => 0,
            _ => -1,
        }; return codePoint >= 0;
    }
    private static bool IsExtensibleArrow(ReadOnlySpan<char> name) => name is "\\xleftarrow" or "\\xrightarrow" or "\\xLeftarrow" or "\\xRightarrow" or "\\xleftrightarrow" or "\\xLeftrightarrow" or "\\xhookleftarrow" or "\\xhookrightarrow" or "\\xmapsto" or "\\xleftharpoonup" or "\\xleftharpoondown" or "\\xrightharpoonup" or "\\xrightharpoondown" or "\\xrightleftharpoons" or "\\xleftrightharpoons" or "\\xtwoheadleftarrow" or "\\xtwoheadrightarrow" or "\\xlongequal" or "\\xtofrom" or "\\xrightleftarrows" or "\\xrightequilibrium" or "\\xleftequilibrium";
    private static bool TryFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\mathrm" => MathFont.MathRoman, "\\mathit" => MathFont.MathItalic, "\\mathbf" or "\\bold" => MathFont.MathBold, "\\mathnormal" => MathFont.MathNormal, "\\mathsf" => MathFont.MathSans, "\\mathsfit" => MathFont.MathSansItalic, "\\mathtt" => MathFont.MathTypewriter, "\\mathbb" or "\\Bbb" => MathFont.AMS, "\\mathcal" => MathFont.Caligraphic, "\\mathfrak" or "\\frak" => MathFont.Fraktur, "\\mathscr" => MathFont.Script, "\\boldsymbol" or "\\bm" => MathFont.MathBoldItalic, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
    private static bool TryTextFont(ReadOnlySpan<char> name, out MathFont font)
    {
        font = name switch { "\\text" or "\\textrm" or "\\textnormal" or "\\textmd" or "\\textup" => MathFont.MathRoman, "\\textbf" => MathFont.MathBold, "\\textit" or "\\emph" => MathFont.MathItalic, "\\textsf" => MathFont.MathSans, "\\texttt" => MathFont.MathTypewriter, _ => (MathFont)(-1) }; return (int)font >= 0;
    }
}
