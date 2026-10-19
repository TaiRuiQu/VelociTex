using VelociTex;

internal static class ParserConstructionAssertions
{
    internal static int Run()
    {
        var checks = 0;
        void Require(bool condition, string message) { checks++; if (!condition) throw new InvalidOperationException("Parser construction assertion: " + message); }
        SemanticNode[] Parse(string source)
        {
            using var workspace = new MathWorkspace();
            var options = new MathOptions();
            Require(workspace.Enter(), "private workspace acquired");
            try { MacroExpander.Expand(source, options, workspace, default); MathParser.Parse(source, options, workspace, default); return workspace.Nodes.ToArray(); }
            finally { workspace.Leave(); }
        }
        SemanticNode One(string source, NodeKind kind) => Parse(source).Single(n => n.Kind == kind);
        void Bad(string source, MathDiagnosticCode code)
        {
            try { Parse(source); Require(false, "source must be rejected: " + source); }
            catch (CompileFailure failure) { Require(failure.Diagnostic.Code == code, "expected diagnostic: " + source + " -> " + failure.Diagnostic); }
        }
        string Symbols(string source) => string.Concat(Parse(source).Where(n => n.Kind == NodeKind.Symbol).Select(n => char.ConvertFromUtf32(n.CodePoint)));

        Require(One("\\sum", NodeKind.Operator).Limits, "sum defaults to display limits");
        Require(!One("\\int", NodeKind.Operator).Limits, "integral defaults to side scripts");
        Require(One("\\int\\limits_0^1", NodeKind.Operator).AlwaysLimits, "explicit limits retained");
        Require(One("\\sum\\nolimits_i", NodeKind.Operator).NeverLimits, "explicit nolimits retained");
        Require(One("\\operatorname*{argmax}_x", NodeKind.Operator).Limits, "starred operator name retains limits");
        Require(One("\\operatorname{rank}", NodeKind.Operator).Text == "\\operatorname", "operator name retains semantic source kind");
        Require(!One("\\binom nk", NodeKind.Fraction).HasBar, "binomial suppresses fraction bar");
        var generalized = One("\\genfrac{[}{]}{1pt}{0}{a}{b}", NodeKind.Fraction);
        Require(generalized.LeftDelimiter == '[' && generalized.RightDelimiter == ']' && generalized.HasBar && Math.Abs(generalized.Value - .1) < 1e-12, "generalized fraction keeps delimiters and dimension");
        Require(One("{n\\choose k}", NodeKind.Fraction).LeftDelimiter == '(', "infix choose becomes compact fraction");
        Require(!One("{a\\atop b}", NodeKind.Fraction).HasBar, "infix atop suppresses bar");
        Require(One("\\cfrac1x", NodeKind.Fraction).Text == "\\cfrac", "continued fraction marker retained");
        Require(One("\\sqrt[3]{x}", NodeKind.Radical).SecondChild >= 0, "radical index retained");
        var delimited = One("\\left\\{a\\middle|b\\right.", NodeKind.Delimited);
        Require(delimited.LeftDelimiter == '{' && delimited.RightDelimiter == 0, "null right delimiter retained");
        Require(One("\\left(a\\middle|b\\right)", NodeKind.Delimiter).Value == 0, "middle is deferred to enclosing delimiter height");
        Require(One("\\Biggl\\langle", NodeKind.Delimiter).Value == 4, "explicit delimiter size retained");
        Require(One("\\hat x", NodeKind.Accent).CodePoint == '^', "ordinary accent keeps glyph");
        Require(One("A\\xrightarrow[g]{f}B", NodeKind.Accent).SecondChild >= 0, "extensible arrow lower label retained");
        var matrix = One("\\begin{array}{|c|r|}\\hline a&b\\\\[4pt]\\hdashline c&d\\\\\\hline\\end{array}", NodeKind.Matrix);
        Require(matrix.ColumnCount == 2 && matrix.Alignment == "|c|r|" && matrix.RuleBelow == 1, "array column and bottom rule metadata retained");
        var rows = Parse("\\begin{array}{cc}\\hline a&b\\\\[4pt]\\hdashline c&d\\end{array}").Where(n => n.Kind == NodeKind.MatrixRow).ToArray();
        Require(rows.Length == 2 && rows[0].RuleAbove == 1 && Math.Abs(rows[0].Value - .4) < 1e-12 && rows[1].RuleAbove == -1, "array row gaps and solid/dashed lines retained");
        Require(One("\\begin{alignedat}{2}a&=b&c&=d\\end{alignedat}", NodeKind.Matrix).Value == 2, "alignedat pair count retained");
        var aligned = Parse("\\begin{aligned}a&=b\\end{aligned}");
        var alignedCells = aligned.Select((node, index) => (node, index)).Where(p => p.node.Kind == NodeKind.MatrixCell).ToArray();
        var oddBody = aligned[aligned[alignedCells[1].index].FirstChild].FirstChild;
        var oddLeading = aligned[aligned[oddBody].FirstChild];
        Require(oddLeading.Kind == NodeKind.Row && oddLeading.Value == 1 && oddLeading.FirstChild == -1, "odd aligned cell retains its leading ordinary empty group");
        Require(Symbols("\\def\\a{x}\\begin{matrix}\\def\\a{y}\\a&\\a\\\\\\a\\end{matrix}\\a") == "yxxx", "array cells and environment restore local macro definitions");
        Require(Symbols("\\def\\a{x}\\begin{matrix}\\gdef\\a{y}\\a&\\a\\end{matrix}\\a") == "yyy", "global definitions survive cell scopes");
        Require(Symbols("\\def\\a{x}\\begingroup\\def\\a{y}\\a\\endgroup\\a") == "yx", "explicit macro groups restore their parent");
        Require(Symbols("\\def\\a#1{\\def\\b##1{##1+#1}\\b x}\\a y") == "x+y", "nested macro replacement escapes a parameter hash");
        Require(Symbols("\\def\\a(#1,#2){#1+#2}\\a(x,y)") == "x+y", "delimited parameters preserve literal prefix and separators");
        Require(Symbols("\\def\\a#1ab#2;{#1+#2}\\a xaby;") == "x+y", "multi-token parameter terminators are consumed");
        Require(Symbols("\\def\\a#1;{#1}\\a{a;b};") == "a;b", "delimiters inside braces do not terminate an argument");
        Require(Symbols("\\def\\a#1;{x#1}\\a;") == "x", "delimited arguments may be empty");
        Require(Symbols("\\def\\a#{x}\\a{y}") == "xy", "final parameter hash inserts an opening brace into replacement");

        Bad("x\\limits_1", MathDiagnosticCode.UnexpectedToken);
        Bad("\\left x\\right)", MathDiagnosticCode.UnexpectedToken);
        Bad("x\\middle|y", MathDiagnosticCode.UnexpectedToken);
        Bad("{a\\over b\\over c}", MathDiagnosticCode.UnexpectedToken);
        Bad("\\begin{array}{c}a&b\\end{array}", MathDiagnosticCode.UnexpectedToken);
        Bad("\\begin{matrix}x\\end{array}", MathDiagnosticCode.UnexpectedToken);
        Bad("\\begingroup x", MathDiagnosticCode.UnclosedGroup);
        Bad("\\endgroup", MathDiagnosticCode.UnexpectedGroupEnd);
        Bad("\\def\\a(#1){#1}\\a[x]", MathDiagnosticCode.MissingArgument);
        return checks;
    }
}
