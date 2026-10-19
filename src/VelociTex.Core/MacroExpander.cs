namespace VelociTex;

internal static class MacroExpander
{
    private static readonly Dictionary<string, MacroDefinition> BuiltIns = CreateBuiltIns();
    private static readonly string CellScopeMarker = new('\0', 1);

    private static Dictionary<string, MacroDefinition> CreateBuiltIns()
    {
        var result = new Dictionary<string, MacroDefinition>(StringComparer.Ordinal);
        foreach (var (name, body) in SymbolData.Macros)
        {
            var tokens = new List<LexToken>(); MathLexer.Tokenize(body, tokens, 65_536, default);
            result[name] = new(tokens.ToArray(), CountArguments(tokens), true);
        }
        return result;
    }

    internal static void Expand(string source, in MathOptions options, MathWorkspace w, CancellationToken cancellationToken)
    {
        foreach (var pair in BuiltIns) w.Definitions.Add(pair.Key, pair.Value);
        options.Session?.CopyTo(w.Definitions);
        ValidatePersistentBudget(w.Definitions, options);
        long macroSourceUnits = 0, macroTokenCount = 0;
        if (options.Macros is not null) foreach (var pair in options.Macros)
        {
            cancellationToken.ThrowIfCancellationRequested();
            if (pair.Key is null || pair.Value is null) throw new CompileFailure(new(MathDiagnosticCode.InvalidMacroDefinition, new(0, 0), "Macro names and replacements cannot be null."));
            macroSourceUnits += pair.Key.Length + pair.Value.Length;
            if (macroSourceUnits > options.MaxInputLength) throw new CompileFailure(new(MathDiagnosticCode.InputLimit, new(0, 0), "Aggregate option macro input budget exceeded."));
            ValidateName(pair.Key, new(0, 0));
            var tokens = new List<LexToken>(); MathLexer.Tokenize(pair.Value, tokens, options.MaxTokens, cancellationToken, new(0, 0));
            macroTokenCount += tokens.Count;
            if (macroTokenCount > options.MaxTokens) throw new CompileFailure(new(MathDiagnosticCode.TokenLimit, new(0, 0), "Aggregate option macro token budget exceeded."));
            w.Definitions[pair.Key] = new(Compact(tokens, new(0, 0), options.MaxTokens), CountArguments(tokens));
        }
        ValidatePersistentBudget(w.Definitions, options);
        MathLexer.Tokenize(source, w.Pending, options.MaxTokens, cancellationToken);
        w.Pending.Reverse();
        var expansions = 0;
        Span<(int Start, int Length)> arguments = stackalloc (int, int)[9];
        while (w.Pending.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var token = Pop(w);
            if (ReferenceEquals(token.Source, CellScopeMarker) && token.Length == 0) { OpenScope(w, options, token, 3); continue; }
            MacroDefinition macro = default;
            var isMacro = token.Text.Length > 1 && token.Text[0] == '\\' && w.Definitions.GetAlternateLookup<ReadOnlySpan<char>>().TryGetValue(token.Text, out macro);
            var customOverride = isMacro && !macro.BuiltIn;
            if (!customOverride && (token.Is("\\def") || token.Is("\\gdef"))) { Define(w, options, token.Is("\\gdef"), token, cancellationToken); continue; }
            if (!customOverride && (token.Is("\\newcommand") || token.Is("\\renewcommand") || token.Is("\\providecommand")))
            { NewCommand(w, options, token, cancellationToken); continue; }
            if (!customOverride && (token.Is("\\begingroup") || token.Is("\\endgroup")))
            {
                if (token.Is("\\begingroup")) OpenScope(w, options, token, 4);
                else { if (w.ScopeKinds.Count == 0 || w.ScopeKinds[^1] != 4) Fail(MathDiagnosticCode.UnexpectedGroupEnd, token, "Unmatched endgroup."); CloseScope(w); }
            }
            if (token.Is("{")) OpenScope(w, options, token, 1);
            else if (token.Is("}") && w.Scopes.Count != 0 && w.ScopeKinds[^1] == 1) CloseScope(w);
            if (isMacro && !(macro.BuiltIn && IsDirectSpacing(token.Text)))
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                if (macro.Delimiters is not null)
                {
                    foreach (var prefix in macro.Delimiters[0])
                        if (w.Pending.Count == 0 || !Pop(w).Text.SequenceEqual(prefix.Text)) Fail(MathDiagnosticCode.MissingArgument, token, "Macro invocation does not match its parameter prefix.");
                }
                for (var a = 0; a < macro.Arguments; a++) arguments[a] = macro.Delimiters is not null && macro.Delimiters[a + 1].Length != 0 ? ConsumeDelimitedArgument(w, options, token, macro.Delimiters[a + 1], cancellationToken) : ConsumeArgument(w, options, token, cancellationToken);
                for (var i = macro.Body.Length - 1; i >= 0; i--)
                {
                    var part = macro.Body[i];
                    if (i > 0 && macro.Body[i - 1].Is("#") && part.Is("#")) { Push(w, part with { Origin = token.Origin }, options.MaxTokens); i--; }
                    else if (IsParameterReference(macro.Body, i))
                    {
                        var index = part.Text[0] - '1';
                        if (index >= macro.Arguments) Fail(MathDiagnosticCode.InvalidMacroDefinition, token, "Macro parameter is outside its declared arity.");
                        var (start, length) = arguments[index];
                        for (var j = length - 1; j >= 0; j--) Push(w, w.Arguments[start + j], options.MaxTokens);
                        i--;
                    }
                    else Push(w, part with { Origin = token.Origin }, options.MaxTokens);
                }
                continue;
            }
            if (token.Is("\\begin"))
            {
                OpenScope(w, options, token, 2);
                // Insert a scope event after the literal environment-name group.
                // It owns no source text and is never exposed to the parser.
                var depth = 0;
                for (var i = w.Pending.Count - 1; i >= 0; i--)
                {
                    if (w.Pending[i].Is("{")) depth++;
                    else if (w.Pending[i].Is("}") && --depth == 0)
                    { if (w.Pending.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Pending token budget exceeded."); w.Pending.Insert(i, new(CellScopeMarker, 0, 0, token.Origin)); break; }
                }
            }
            else if (token.Is("\\end"))
            { if (w.ScopeKinds.Count != 0 && w.ScopeKinds[^1] == 3) CloseScope(w); if (w.ScopeKinds.Count != 0 && w.ScopeKinds[^1] == 2) CloseScope(w); }
            else if ((token.Is("&") || token.Is("\\\\") || token.Is("\\cr")) && w.ScopeKinds.Count != 0 && w.ScopeKinds[^1] == 3)
            { CloseScope(w); OpenScope(w, options, token, 3); }
            if (w.Expanded.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Expanded token budget exceeded.");
            w.Expanded.Add(token);
        }
        if (w.ScopeKinds.Contains((byte)4)) throw new CompileFailure(new(MathDiagnosticCode.UnclosedGroup, new(source.Length, 0), "Unclosed begingroup."));
        ValidatePersistentBudget(w.Definitions, options);
    }

    private static void OpenScope(MathWorkspace w, in MathOptions options, LexToken token, byte kind)
    { if (w.Scopes.Count >= options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token, "Group nesting budget exceeded."); w.Scopes.Add(w.Undo.Count); w.ScopeKinds.Add(kind); }
    private static void CloseScope(MathWorkspace w)
    {
        var marker = w.Scopes[^1]; w.Scopes.RemoveAt(w.Scopes.Count - 1); w.ScopeKinds.RemoveAt(w.ScopeKinds.Count - 1);
        for (var i = w.Undo.Count - 1; i >= marker; i--)
        { var undo = w.Undo[i]; if (undo.Existed) w.Definitions[undo.Name] = undo.Previous; else w.Definitions.Remove(undo.Name); }
        w.Undo.RemoveRange(marker, w.Undo.Count - marker);
    }

    // These stable built-in string macros have direct equivalent P1 opcodes.
    // User overrides retain ordinary macro behavior.
    private static bool IsDirectSpacing(ReadOnlySpan<char> name) => name is "\\," or "\\:" or "\\;" or "\\!" or "\\quad" or "\\qquad" or "\\operatorname" or "\\liminf" or "\\limsup" or "\\bm";

    private static void ValidatePersistentBudget(Dictionary<string, MacroDefinition> definitions, in MathOptions options)
    {
        long units = 0, tokens = 0, names = 0;
        foreach (var (name, definition) in definitions)
        {
            if (definition.BuiltIn) continue;
            units += name.Length; names++; tokens += definition.Body.Length;
            foreach (var token in definition.Body) units += token.Length;
            if (definition.Delimiters is not null) foreach (var delimiter in definition.Delimiters)
            { tokens += delimiter.Length; foreach (var token in delimiter) units += token.Length; }
            if (units > options.MaxInputLength) throw new CompileFailure(new(MathDiagnosticCode.InputLimit, new(0, 0), "Persistent macro source budget exceeded."));
            if (tokens > options.MaxTokens || names > options.MaxTokens) throw new CompileFailure(new(MathDiagnosticCode.TokenLimit, new(0, 0), "Persistent macro token budget exceeded."));
        }
    }

    private static void Define(MathWorkspace w, in MathOptions options, bool global, LexToken command, CancellationToken cancellationToken)
    {
        SkipSpaces(w);
        if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected a macro name.");
        var nameToken = Pop(w); var name = nameToken.Text.ToString(); ValidateName(name, nameToken.Origin);
        var count = 0; var delimiters = new List<List<LexToken>> { new() }; LexToken? insert = null;
        while (w.Pending.Count != 0 && !w.Pending[^1].Is("{"))
        {
            var hash = Pop(w);
            if (!hash.Is("#")) { delimiters[count].Add(hash); continue; }
            if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, hash, "Expected a macro parameter number.");
            if (w.Pending[^1].Is("{")) { insert = w.Pending[^1]; delimiters[count].Add(insert.Value); break; }
            var number = Pop(w);
            if (number.Text.Length != 1 || number.Text[0] != '1' + count || ++count > 9) Fail(MathDiagnosticCode.InvalidMacroDefinition, number, "Expected consecutive parameters #1 through #9.");
            delimiters.Add(new());
        }
        var body = ReadDefinitionBody(w, options, command, cancellationToken);
        if (insert is not null) body.Add(insert.Value);
        if (CountArguments(body) > count) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Replacement uses an undeclared macro parameter.");
        LexToken[][]? compactDelimiters = null;
        if (delimiters.Any(d => d.Count != 0))
        { compactDelimiters = new LexToken[delimiters.Count][]; for (var i = 0; i < delimiters.Count; i++) compactDelimiters[i] = Compact(delimiters[i], command.Origin, options.MaxTokens); }
        Assign(w, name, new(Compact(body, command.Origin, options.MaxTokens), count, Delimiters: compactDelimiters), global);
    }
    private static void NewCommand(MathWorkspace w, in MathOptions options, LexToken command, CancellationToken cancellationToken)
    {
        SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected a macro name.");
        var nameToken = Pop(w); var braced = nameToken.Is("{");
        if (braced) { SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected a macro name."); nameToken = Pop(w); SkipSpaces(w); if (w.Pending.Count == 0 || !Pop(w).Is("}")) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected closing brace after macro name."); }
        var name = nameToken.Text.ToString(); ValidateName(name, nameToken.Origin); SkipSpaces(w);
        var count = 0;
        if (w.Pending.Count != 0 && w.Pending[^1].Is("["))
        { Pop(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected arity."); var arity = Pop(w); if (arity.Text.Length != 1 || arity.Text[0] is < '0' or > '9') Fail(MathDiagnosticCode.InvalidMacroDefinition, arity, "Macro arity must be 0 through 9."); count = arity.Text[0] - '0'; if (w.Pending.Count == 0 || !Pop(w).Is("]")) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected closing arity bracket."); }
        var body = ReadDefinitionBody(w, options, command, cancellationToken);
        if (CountArguments(body) > count) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Replacement uses an undeclared macro parameter.");
        var exists = w.Definitions.ContainsKey(name) || Array.BinarySearch(SymbolData.KnownCommands, name, StringComparer.Ordinal) >= 0;
        if (command.Is("\\newcommand") && exists || command.Is("\\renewcommand") && !exists) Fail(MathDiagnosticCode.InvalidMacroDefinition, nameToken, "Macro declaration conflicts with its existing definition.");
        if (!command.Is("\\providecommand") || !exists) Assign(w, name, new(Compact(body, command.Origin, options.MaxTokens), count), false);
    }
    private static List<LexToken> ReadDefinitionBody(MathWorkspace w, in MathOptions options, LexToken command, CancellationToken cancellationToken)
    {
        SkipSpaces(w); if (w.Pending.Count == 0 || !Pop(w).Is("{")) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected braced macro replacement.");
        var body = new List<LexToken>(); var depth = 1;
        while (w.Pending.Count != 0)
        { cancellationToken.ThrowIfCancellationRequested(); var token = Pop(w); if (token.Is("{")) { if (++depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token, "Macro replacement nesting budget exceeded."); } else if (token.Is("}") && --depth == 0) return body; if (body.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Macro replacement token budget exceeded."); body.Add(token); }
        Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Unclosed macro replacement."); return body;
    }
    private static (int, int) ConsumeArgument(MathWorkspace w, in MathOptions options, LexToken command, CancellationToken cancellationToken)
    {
        SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, command, "Missing macro argument.");
        var start = w.Arguments.Count; var token = Pop(w);
        if (!token.Is("{")) { if (token.Is("}")) Fail(MathDiagnosticCode.MissingArgument, token, "Missing macro argument."); if (w.Arguments.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Macro argument token budget exceeded."); w.Arguments.Add(token); return (start, 1); }
        var depth = 1;
        while (w.Pending.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested(); token = Pop(w);
            if (token.Is("{")) { if (++depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token, "Macro argument nesting budget exceeded."); }
            else if (token.Is("}") && --depth == 0) return (start, w.Arguments.Count - start);
            if (w.Arguments.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Macro argument token budget exceeded."); w.Arguments.Add(token);
        }
        Fail(MathDiagnosticCode.UnclosedGroup, command, "Unclosed macro argument."); return default;
    }
    private static (int, int) ConsumeDelimitedArgument(MathWorkspace w, in MathOptions options, LexToken command, LexToken[] delimiter, CancellationToken cancellationToken)
    {
        var start = w.Arguments.Count; var depth = 0; var match = 0;
        while (w.Pending.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested(); var token = Pop(w);
            if (w.Arguments.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Delimited argument token budget exceeded.");
            w.Arguments.Add(token);
            if (token.Is("{")) { if (++depth > options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token, "Delimited argument nesting budget exceeded."); }
            else if (token.Is("}") && --depth < 0) Fail(MathDiagnosticCode.UnexpectedGroupEnd, token, "Unexpected closing brace in a delimited macro argument.");
            if ((depth == 0 || depth == 1 && delimiter[match].Is("{")) && token.Text.SequenceEqual(delimiter[match].Text))
            {
                if (++match == delimiter.Length)
                {
                    w.Arguments.RemoveRange(w.Arguments.Count - match, match); var length = w.Arguments.Count - start;
                    if (length >= 2 && w.Arguments[start].Is("{") && w.Arguments[^1].Is("}")) return (start + 1, length - 2);
                    return (start, length);
                }
            }
            else match = 0;
        }
        Fail(MathDiagnosticCode.MissingArgument, command, "Missing delimited macro argument terminator."); return default;
    }
    private static void Assign(MathWorkspace w, string name, MacroDefinition value, bool global)
    {
        if (!global && w.Scopes.Count != 0) { var exists = w.Definitions.TryGetValue(name, out var old); w.Undo.Add(new(name, exists, old)); }
        if (global)
        {
            for (var i = w.Undo.Count - 1; i >= 0; i--) if (w.Undo[i].Name == name)
            { w.Undo.RemoveAt(i); for (var s = 0; s < w.Scopes.Count; s++) if (w.Scopes[s] > i) w.Scopes[s]--; }
        }
        w.Definitions[name] = value;
    }
    private static int CountArguments(IReadOnlyList<LexToken> body)
    {
        var count = 0;
        for (var i = 0; i < body.Count; i++)
        {
            if (!body[i].Is("#") || i + 1 >= body.Count) continue;
            if (body[i + 1].Is("#")) { i++; continue; }
            if (body[i + 1].Text.Length == 1 && body[i + 1].Text[0] is >= '1' and <= '9') count = Math.Max(count, body[++i].Text[0] - '0');
        }
        return count;
    }
    private static bool IsParameterReference(LexToken[] body, int index)
    {
        if (index == 0 || body[index].Text.Length != 1 || body[index].Text[0] is < '1' or > '9') return false;
        var hashes = 0; for (var i = index - 1; i >= 0 && body[i].Is("#"); i--) hashes++; return (hashes & 1) != 0;
    }
    private static LexToken[] Compact(IReadOnlyList<LexToken> body, SourceRange origin, int maximum)
    {
        // A persistent session must not pin the complete formula for a tiny definition.
        var text = new System.Text.StringBuilder();
        foreach (var token in body)
        {
            text.Append(token.Text);
            if (token.Text.Length > 1 && token.Text[0] == '\\' && token.Text[1] is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or '@') text.Append(' ');
        }
        var compact = new List<LexToken>(body.Count); MathLexer.Tokenize(text.ToString(), compact, maximum, default, origin); return compact.ToArray();
    }
    private static void ValidateName(string name, SourceRange range)
    { if (name.Length < 2 || name[0] != '\\') throw new CompileFailure(new(MathDiagnosticCode.InvalidMacroDefinition, range, "Macro names must start with a backslash.")); }
    private static void SkipSpaces(MathWorkspace w) { while (w.Pending.Count != 0 && char.IsWhiteSpace(w.Pending[^1].Text[0])) Pop(w); }
    private static LexToken Pop(MathWorkspace w) { var token = w.Pending[^1]; w.Pending.RemoveAt(w.Pending.Count - 1); return token; }
    private static void Push(MathWorkspace w, LexToken token, int max) { if (w.Pending.Count >= max) Fail(MathDiagnosticCode.TokenLimit, token, "Pending token budget exceeded."); w.Pending.Add(token); }
    private static void Fail(MathDiagnosticCode code, LexToken token, string message) => throw new CompileFailure(new(code, token.Origin, message));
}
