using System.Text;
using System.Xml.Linq;
using VelociTex;

internal static class CoreEdgeAssertions
{
    internal static int Run()
    {
        var engine = new MathEngine();
        using var workspace = new MathWorkspace();
        var options = new MathOptions();
        int checks = 0;
        void Require(bool condition, string message)
        { checks++; if (!condition) throw new InvalidOperationException("Core edge assertion: " + message); }
        MathDocument Good(string source, MathOptions? alternate = null)
        {
            var selected = alternate ?? options;
            Require(engine.TryCompile(source, selected, workspace, default, out var doc, out var error), source + ": " + error);
            Require(doc!.Source == source && !error.IsError, "source retained on success");
            return doc;
        }
        void Bad(string source, MathDiagnosticCode? expected = null, MathOptions? alternate = null)
        {
            var selected = alternate ?? options;
            Require(!engine.TryCompile(source, selected, workspace, default, out var doc, out var error), "invalid source rejected: " + source);
            Require(doc is null && error.IsError && (expected is null || error.Code == expected), "structured diagnostic: " + error);
            Require(error.Range.Start >= 0 && error.Range.Length >= 0 && error.Range.Start + error.Range.Length <= source.Length, "diagnostic in original source");
            int end = error.Range.Start + error.Range.Length;
            Require(error.Range.Start == 0 || error.Range.Start == source.Length || !char.IsLowSurrogate(source[error.Range.Start]) || !char.IsHighSurrogate(source[error.Range.Start - 1]), "range starts at scalar boundary");
            Require(end == 0 || end == source.Length || !char.IsLowSurrogate(source[end]) || !char.IsHighSurrogate(source[end - 1]), "range ends at scalar boundary");
        }
        string Glyphs(MathDocument doc)
        { var text = new StringBuilder(); foreach (var glyph in doc.Glyphs.Span) text.Append(char.ConvertFromUtf32(glyph.CodePoint)); return text.ToString(); }

        Require(Glyphs(Good("\\def\\edge{x}{\\def\\edge{y}\\edge}\\edge")) == "yx", "nested local definition restores parent");
        Require(Glyphs(Good("\\def\\edge{x}{\\gdef\\edge{y}}\\edge")) == "y", "global definition survives nested scope");
        Require(Glyphs(Good("\\newcommand{\\edge}[2]{{#1}+{#2}}\\edge{x}{y}")) == "x+y", "newcommand arity");
        Require(Glyphs(Good("\\renewcommand\\alpha{x}\\alpha")) == "x", "registered symbols count as defined commands");
        Require(Glyphs(Good("\\providecommand\\alpha{x}\\alpha")) == "α", "providecommand preserves existing symbol");
        Bad("\\newcommand\\frac{x}", MathDiagnosticCode.InvalidMacroDefinition);
        Bad("\\renewcommand\\edge{x}", MathDiagnosticCode.InvalidMacroDefinition);
        Bad("\\def\\edge#2{x}", MathDiagnosticCode.InvalidMacroDefinition);
        Good("\\def\\edge#1{#2}");
        Good("\\newcommand\\edge[1]{#2}");
        Bad("\\def\\edge#1{#2}\\edge x", MathDiagnosticCode.InvalidMacroDefinition);
        Require(Glyphs(Good("\\def\\quad{x}\\quad")) == "x", "direct spacing opcode permits user overrides");
        Require(Glyphs(Good("\\def\\def{x}\\def")) == "x", "definition primitive permits user overrides");
        Require(Glyphs(Good("\\replace", options with { Macros = new Dictionary<string, string> { ["\\replace"] = "\\alpha b" } })) == "αb", "control word separation survives macro compaction");
        Require(Glyphs(Good("\\def\\edge{\\alpha b}\\edge")) == "αb", "definition compaction preserves token boundaries");
        Good("\\sqrt[3]{x}");
        Good("\\sum_i");
        Bad("😀", MathDiagnosticCode.UnknownSymbol);
        Bad("x\ud800y", MathDiagnosticCode.UnknownSymbol);
        Bad("x\udc00y", MathDiagnosticCode.UnknownSymbol);
        Bad("x\\😀", MathDiagnosticCode.UnsupportedCommand);
        Require(Glyphs(Good("x% ignored \\bad\n+y")) == "x+y", "comments do not introduce tokens");
        Good("{{{{x}}}}", options with { MaxNesting = 4 });
        Bad("{{{{{x}}}}}", MathDiagnosticCode.NestingLimit, options with { MaxNesting = 4 });
        Bad("x", MathDiagnosticCode.InputLimit, options with { MaxInputLength = 10, Macros = new Dictionary<string, string> { ["\\many"] = new('x', 20) } });
        Bad("x", MathDiagnosticCode.TokenLimit, options with { MaxTokens = 4, Macros = new Dictionary<string, string> { ["\\a"] = "xxx", ["\\b"] = "yyy" } });
        Bad("\\double{x}", MathDiagnosticCode.TokenLimit, options with { MaxTokens = 16, Macros = new Dictionary<string, string> { ["\\double"] = "#1#1#1#1#1#1#1#1#1#1" } });
        var session = new MacroSession();
        var shared = options with { Session = session };
        Good("\\gdef\\edge{\\alpha b}", shared);
        Require(Glyphs(Good("\\edge", shared)) == "αb", "persistent definition owns compact source");
        Bad("\\gdef\\edge{y}\\unsupported", MathDiagnosticCode.UnsupportedCommand, shared);
        Require(Glyphs(Good("\\edge", shared)) == "αb", "failed transaction leaves prior definition");
        using (var cancelled = new CancellationTokenSource())
        { cancelled.Cancel(); Require(!engine.TryCompile("\\gdef\\edge{z}", shared, workspace, cancelled.Token, out _, out var error) && error.Code == MathDiagnosticCode.Cancelled, "cancelled session transaction"); }
        Require(Glyphs(Good("\\edge", shared)) == "αb", "cancelled transaction leaves prior definition");
        Require(MathMlWriter.Write(Good(new string('1', 2000))).Contains(new string('1', 2000), StringComparison.Ordinal), "long numeric run serializes linearly");

        // Deterministic grammar stress is extra lifecycle coverage, not phase fixture coverage.
        var random = new Random(0x19_00_01);
        string Expression(int depth)
        {
            string[] atoms = ["x", "y", "2", "f", "\\alpha", "\\Gamma"];
            if (depth == 0)
                return atoms[random.Next(atoms.Length)];
            return random.Next(8) switch
            {
                0 => "{" + Expression(depth - 1) + "+" + Expression(depth - 1) + "}",
                1 => "{" + Expression(depth - 1) + "}^{" + Expression(depth - 1) + "}",
                2 => "{" + Expression(depth - 1) + "}_{" + Expression(depth - 1) + "}",
                3 => "\\frac{" + Expression(depth - 1) + "}{" + Expression(depth - 1) + "}",
                4 => "\\sqrt{" + Expression(depth - 1) + "}",
                5 => "\\mathrm{" + Expression(depth - 1) + "}",
                6 => "(" + Expression(depth - 1) + ")=" + Expression(depth - 1),
                _ => atoms[random.Next(atoms.Length)],
            };
        }
        for (int i = 0; i < 512; i++)
        {
            string source = Expression(3);
            var first = Good(source);
            string mathml = MathMlWriter.Write(first);
            Require(XElement.Parse(mathml).Name.LocalName == "math", "valid namespace XML from deterministic grammar");
            Bad(source + "}");
            var repeated = Good(source);
            Require(first.LogicalSize == repeated.LogicalSize && first.Baseline == repeated.Baseline && mathml == MathMlWriter.Write(repeated), "recovery preserves deterministic geometry and semantics");
        }
        return checks;
    }
}
