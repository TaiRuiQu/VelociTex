using System.Text;
using VelociTex;

internal static class P4ParserAssertions
{
    internal static int Run()
    {
        var checks = 0;
        void Require(bool condition, string message) { checks++; if (!condition) throw new InvalidOperationException("P4 parser assertion: " + message); }
        SemanticNode[] Parse(string source, bool display = false)
        {
            var options = new MathOptions { DisplayMode = display, StrictMode = MathStrictMode.Ignore };
            using var workspace = new MathWorkspace(); Require(workspace.Enter(), "private workspace acquired");
            try { MacroExpander.Expand(source, options, workspace, default); MathParser.Parse(source, options, workspace, default); return workspace.Nodes.ToArray(); }
            finally { workspace.Leave(); }
        }
        string Symbols(string source)
        {
            var text = new StringBuilder(); foreach (var node in Parse(source)) if (node.Kind == NodeKind.Symbol) text.Append(char.ConvertFromUtf32(node.CodePoint)); return text.ToString();
        }
        Require(Symbols(@"\def\a{x}\edef\b{\a}\def\a{y}\b") == "x", "edef expands when declared");
        Require(Symbols(@"\def\a{x}\edef\b{\noexpand\a}\def\a{y}\b") == "y", "edef noexpand protects one expansion only");
        Require(Symbols(@"\def\a{x}\let\b=\a\def\a{y}\b") == "x", "let snapshots macro meaning");
        Require(Symbols(@"\def\a{x}{\global\def\a{y}}\a") == "y", "global prefix retains assignment effects");
        Require(Symbols(@"\@firstoftwo{a}{\undefinedP4}+\@secondoftwo{\undefinedP4}{d}") == "a+d", "unselected callback arguments never expand");
        Require(Symbols(@"\TextOrMath{text}{math}+\text{\TextOrMath{text}{math}}") == "math+text", "TextOrMath respects text argument boundaries");
        Require(Symbols(@"\def\arg{\TextOrMath{T}{M}}\text\arg") == "T", "mode callback inside a macro argument inherits the text mode");
        Require(Symbols(@"\text{a $\TextOrMath{T}{M}$ b}") == "a M b", "mode callback switches back to math within text dollars");
        Require(Symbols(@"\text{a \(\TextOrMath{T}{M}\) b}") == "a M b", "mode callback switches back to math within parenthesized math");
        Require(Symbols(@"\def a{x}a") == "x", "literal active-character macro definitions follow locked semantics");
        Require(Symbols(@"\def\n{2}\newcommand\f[\n]{#1+#2}\f xy") == "x+y", "newcommand argument count expands macros");
        Require(Symbols(@"\char65+\char'101+\char""41+\char`A") == "A+A+A+A", "character-code bases match locked core");
        Require(Parse(@"f'(x)+g''(x)").Count(n => n.CodePoint == 0x2032) == 3, "prime syntax retains actual prime glyphs");
        Require(Parse(@"x'^2").Count(n => n.Kind == NodeKind.Script) == 1, "explicit superscript joins preceding primes");
        Require(Parse("x²₁").Count(n => n.Kind == NodeKind.Script) == 1, "Unicode scripts share the same base");
        Require(Parse("á").Any(n => n.Kind == NodeKind.Accent && n.Text == "\\acute"), "generated Unicode decomposition uses locked accent command");
        Require(Parse("i\u0302").Any(n => n.Kind == NodeKind.Symbol && n.CodePoint == 0x131), "combining accents replace i with dotless i");
        Require(Parse(@"\verb|a_b^c\%|").Single(n => n.Kind == NodeKind.Verb).Text == @"a_b^c\%", "verbatim content bypasses math lexing and comments");
        Require(Parse(@"\textsf{\textbf{a}}").Single(n => n.Kind == NodeKind.Symbol).Font == MathFont.MathSansBold, "text weight preserves family");
        Require(Parse(@"\textbf{\textit{a}}").Single(n => n.Kind == NodeKind.Symbol).Font == MathFont.MainBoldItalic, "text bold italic selects the Main text face");
        Require(Parse(@"\textit{\emph{a}}").Single(n => n.Kind == NodeKind.Symbol).Font == MathFont.MathRoman, "emph toggles inherited italic shape");
        Require(Parse(@"\Braket{a|b||c}").Count(n => n.Kind == NodeKind.Delimiter && n.Value == 0) == 2, "Braket transforms all middle separators");
        Require(Parse(@"\Set{x|x>0|y}").Count(n => n.Kind == NodeKind.Delimiter && n.Value == 0) == 1, "Set transforms only the first separator");
        var tagged = Parse(@"\begin{align}x&=1\\y&=2\tag{B}\end{align}", true);
        Require(tagged.Count(n => n.Kind == NodeKind.MatrixRow && n.SecondChild >= 0) == 2, "align retains independent row tags");
        Require(Parse(@"\begin{CD}A@>f>>B\\@VgVV@VVhV\\C@>>k>D\end{CD}", true).Count(n => n.Kind == NodeKind.Box && n.Text == "cdparent") == 2, "CD vertical arrows retain their two label parents");
        var engine = new MathEngine(); using var engineWorkspace = new MathWorkspace(); var session = new MacroSession();
        bool Compile(string source, MathOptions options, out MathDocument? doc, out MathDiagnostic diagnostic) => engine.TryCompile(source, options, engineWorkspace, default, out doc, out diagnostic);
        var sessionOptions = new MathOptions { Session = session, StrictMode = MathStrictMode.Ignore };
        Require(Compile(@"\def\localOnly{x}", sessionOptions, out _, out _), "default formula-local definition succeeds");
        Require(!Compile(@"\localOnly", sessionOptions, out _, out var localError) && localError.Code == MathDiagnosticCode.UnsupportedCommand, "default formula-local definition does not leak to a session");
        Require(Compile(@"\def\globalGroupMacro{x}", sessionOptions with { GlobalGroup = true }, out _, out _), "globalGroup retains top-level definitions");
        Require(Compile(@"\globalGroupMacro", sessionOptions, out var sessionDocument, out _) && sessionDocument!.Glyphs.Span[0].CodePoint == 'x', "explicit session sees globalGroup definition");
        Require(Compile("é", new MathOptions { StrictMode = MathStrictMode.Warn }, out var warned, out _) && warned!.Warnings.Span.ToArray().Any(w => w.Code == "unicodeTextInMathMode"), "successful result independently retains strict warnings");
        Require(!Compile("é", new MathOptions { StrictMode = MathStrictMode.Error }, out _, out var strictError) && strictError.Code == MathDiagnosticCode.StrictViolation, "strict error rejects a non-TeX math accent");
        Require(Compile("é", new MathOptions { StrictMode = MathStrictMode.Ignore }, out var ignored, out _) && ignored!.Warnings.Length == 0, "strict ignore emits no warnings");
        Require(Compile(@"x^\relax2", new MathOptions(), out _, out _), "relax is skipped while scanning a script argument");
        Require(Compile(@"\kern + 1 em x", new MathOptions(), out var spacedDimension, out _) && spacedDimension!.LogicalSize.Width > 24, "unbraced dimensions permit source whitespace after a sign");
        Require(Compile(@"\genfrac{x}{y}{0pt}{}{a}{b}", new MathOptions(), out _, out _), "non-delimiter generalized fraction arguments normalize to null delimiters");
        Require(!Compile(@"\genfrac{\frac}{x}{0pt}{}{a}{b}", new MathOptions(), out _, out _), "generalized fraction delimiter arguments are fully parsed and validated");
        Require(Compile(@"\genfrac{}{}{}{2x}{a}{b}", new MathOptions(), out _, out _), "generalized fraction style uses the first text argument character");
        Require(Compile(@"\begin{array}{c:r}a&b\\c&d\end{array}", new MathOptions(), out _, out _), "dashed array separators are available");
        var stretchNodes = Parse(@"\def\arraystretch{2}\begin{matrix}x\\y\end{matrix}");
        Require(stretchNodes.Single(n => n.Kind == NodeKind.Matrix).Value2 == 2, "array stretch snapshots the macro at environment entry");
        Require(Compile(@"\color{red}{x}", sessionOptions with { GlobalGroup = true, ColorIsTextColor = true }, out _, out _), "globalGroup can retain color compatibility alias");
        Require(Compile(@"\color{red}{x}+y", sessionOptions, out var colorAlias, out _) && colorAlias!.PaintStates.Length >= 2, "later explicit session inherits the retained color alias");
        var strictCalls = 0;
        Require(Compile("é", new MathOptions { StrictMode = MathStrictMode.Error, StrictHandler = warning => { strictCalls++; return MathStrictMode.Ignore; } }, out var callbackDocument, out _) && strictCalls == 1 && callbackDocument!.Options.StrictHandler is null, "strict callback selects behavior and is detached from the document");
        try { Compile("é", new MathOptions { StrictHandler = _ => throw new NotSupportedException("strict callback sentinel") }, out _, out _); Require(false, "reported callback exception should propagate"); }
        catch (NotSupportedException) { Require(true, "reported callback exception propagates after workspace cleanup"); }
        Require(Compile("x+1", new MathOptions(), out _, out _), "workspace recovers after a caller strict callback exception");
        Require(Compile(@"x\\y", new MathOptions { DisplayMode = true, StrictHandler = _ => throw new NotSupportedException("strict behavioral sentinel") }, out _, out _), "behavioral strict callback exception selects strict behavior without throwing");
        Require(Compile(@"\overset{!}{x+y}", new MathOptions(), out var compoundOverset, out _) && compoundOverset!.Glyphs.Length == 4, "overset retains every glyph in a compound base");
        Require(Compile(@"\mathop{x+y}_i", new MathOptions(), out var compoundOperator, out _) && compoundOperator!.Glyphs.Length == 4, "mathop retains a compound body and side script");
        foreach (var invisibleSource in new[] { @"\phantom{xyz}", @"\hphantom{x}", @"\vphantom{x}", @"\color{transparent}x", @"\textcolor{red}{\phantom{xyz}}" })
        {
            Require(Compile(invisibleSource, new MathOptions(), out var invisible, out _), "invisible formula compiles: " + invisibleSource);
            Require(invisible!.PaintCommands.Length == 0 && invisible.PaintStates.Length > 0 && invisible.InkBounds.Width == 0 && invisible.InkBounds.Height == 0, "invisible formula has explicit empty ordered painting and empty ink: " + invisibleSource);
        }
        foreach (var invalidArgument in new[] { @"\frac\sqrt12", @"\frac\frac12ab", @"\hat\frac12", @"\sqrt[3]\frac12" })
            Require(!Compile(invalidArgument, new MathOptions(), out _, out var boundaryError) && boundaryError.IsError, "ordinary one-token argument does not consume following command arguments: " + invalidArgument);
        foreach (var primitiveArgument in new[] { @"\sqrt\frac12", @"x^\frac12", @"\mathop\frac12", @"\mathord\frac12" })
            Require(Compile(primitiveArgument, new MathOptions(), out _, out _), "primitive argument retains its locked unbraced command semantics: " + primitiveArgument);
        return checks;
    }
}
