namespace VelociTex;

internal static class MacroExpander
{
    private static readonly Dictionary<string, MacroDefinition> BuiltIns = CreateBuiltIns();
    private static readonly Dictionary<string, MacroDefinition>.AlternateLookup<ReadOnlySpan<char>> BuiltInLookup = BuiltIns.GetAlternateLookup<ReadOnlySpan<char>>();
    private static readonly string CellScopeMarker = new('\0', 1);
    private static readonly string TextModeMarker = new('\0', 2), RestoreModeMarker = new('\0', 3);
    private static readonly string BraketScopeEndMarker = new('\0', 4);

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
        MathLexer.Tokenize(source, w.Pending, options.MaxTokens, cancellationToken, strictMode: options.StrictMode, warnings: w.Warnings, strictHandler: options.StrictHandler);
        w.Pending.Reverse();
        if (!options.GlobalGroup) OpenScope(w, options, new(source, 0, 0, new(0, 0)), 6);
        if (options.ColorIsTextColor)
        { const string alias = "\\textcolor"; Assign(w, "\\color", new([new(alias, 0, alias.Length, new(0, 0))], 0), false); }
        var textMode = false;
        var mathSwitches = new List<byte>();
        var expansions = 0;
        Span<(int Start, int Length)> arguments = stackalloc (int, int)[9];
        while (w.Pending.Count != 0)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var token = Pop(w);
            if (ReferenceEquals(token.Source, CellScopeMarker) && token.Length == 0) { OpenScope(w, options, token, 3); continue; }
            if (ReferenceEquals(token.Source, BraketScopeEndMarker) && token.Length == 0) { CloseScope(w); continue; }
            if (token.Length == 0 && ReferenceEquals(token.Source, TextModeMarker)) { w.ExpansionModes.Add(textMode); textMode = true; continue; }
            if (token.Length == 0 && ReferenceEquals(token.Source, RestoreModeMarker)) { textMode = w.ExpansionModes[^1]; w.ExpansionModes.RemoveAt(w.ExpansionModes.Count - 1); continue; }
            MacroDefinition macro = default;
            var isMacro = !token.NoExpand && token.Text.Length > 0 && TryGetDefinition(w, token.Text, out macro);
            if (token.Length != 0 && !isMacro && !token.TreatAsRelax && token.Text[0] != '\\' &&
                !token.Is("$") && !token.Is("{") && !token.Is("}") && !token.Is("&"))
            {
                if (w.Expanded.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Expanded token budget exceeded.");
                w.Expanded.Add(token);
                continue;
            }
            if (token.Is("$") && (textMode || mathSwitches.Count > 0 && mathSwitches[^1] == 1))
            { if (textMode) { mathSwitches.Add(1); textMode = false; } else { mathSwitches.RemoveAt(mathSwitches.Count - 1); textMode = true; } }
            else if (token.Is("\\(") && textMode) { mathSwitches.Add(2); textMode = false; }
            else if (token.Is("\\)") && mathSwitches.Count > 0 && mathSwitches[^1] == 2) { mathSwitches.RemoveAt(mathSwitches.Count - 1); textMode = true; }
            var customOverride = isMacro && !macro.BuiltIn;
            if (isMacro && macro.Callback is >= 1 and <= 4)
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                var doubled = macro.Callback is 2 or 4;
                if (!doubled && macro.Delimiters is { Length: > 0 } && macro.Delimiters[0].Length > 0 && w.Pending.Count > 0 && w.Pending[^1].Is("|")) { Pop(w); doubled = true; }
                if (macro.Callback is 3 or 4)
                {
                    RestoreBraketDefinition(w, "|"); RestoreBraketDefinition(w, "\\|");
                }
                var chosen = doubled && macro.Delimiters is { Length: > 0 } ? macro.Delimiters[0] : macro.Body;
                for (var i = chosen.Length - 1; i >= 0; i--) Push(w, chosen[i] with { Origin = token.Origin }, options.MaxTokens);
                continue;
            }
            if (!customOverride && token.Text is "\\bra@ket" or "\\bra@set")
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                var ranges = new (int Start, int Length)[5]; for (var i = 0; i < 5; i++) ranges[i] = ConsumeArgument(w, options, token, cancellationToken);
                LexToken[] Part(int index) { var (start, length) = ranges[index]; return w.Arguments.GetRange(start, length).ToArray(); }
                var left = Part(0); var middle = Part(1); var middleDouble = Part(2); var right = Part(3); var body = Part(4);
                OpenScope(w, options, token, 5);
                var one = token.Is("\\bra@set");
                Assign(w, "|", new(middle, 0, Delimiters: [middleDouble], Callback: one ? (byte)3 : (byte)1), false);
                if (middleDouble.Length > 0) Assign(w, "\\|", new(middleDouble, 0, Delimiters: [middleDouble], Callback: one ? (byte)4 : (byte)2), false);
                Push(w, new(BraketScopeEndMarker, 0, 0, token.Origin), options.MaxTokens);
                foreach (var parts in new[] { right, body, left }) for (var i = parts.Length - 1; i >= 0; i--) Push(w, parts[i], options.MaxTokens);
                continue;
            }
            if (token.TreatAsRelax) { PushExpansion(w, "\\relax", token, options, cancellationToken); continue; }
            if (!token.NoExpand && !customOverride && token.Text is "\\noexpand" or "\\expandafter")
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Expansion control requires a following token.");
                var deferred = Pop(w);
                if (token.Is("\\noexpand"))
                { var expandable = IsExpandable(w, deferred.Text); Push(w, deferred with { NoExpand = true, TreatAsRelax = expandable }, options.MaxTokens); }
                else
                {
                    if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Expandafter requires two following tokens.");
                    ExpandOne(w, options, ref expansions, cancellationToken); Push(w, deferred, options.MaxTokens);
                }
                continue;
            }
            if (!customOverride && token.Text is "\\global" or "\\long")
            {
                SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Macro prefix requires an assignment.");
                var next = Pop(w); var text = token.Is("\\long") ? next.Text.ToString() : next.Text switch { "\\def" or "\\gdef" => "\\gdef", "\\edef" or "\\xdef" => "\\xdef", "\\let" => "\\\\globallet", "\\futurelet" => "\\\\globalfuture", "\\global" => "\\global", "\\long" => "\\global", _ => "" };
                if (text.Length == 0) Fail(MathDiagnosticCode.UnexpectedToken, next, "Invalid assignment after a macro prefix.");
                Push(w, new(text, 0, text.Length, next.Origin), options.MaxTokens); continue;
            }
            if (!customOverride && token.Text is "\\let" or "\\\\globallet" or "\\futurelet" or "\\\\globalfuture")
            {
                SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, token, "Let requires a control sequence.");
                var target = Pop(w); var name = target.Text.ToString(); ValidateName(name, target.Origin); SkipSpaces(w);
                LexToken? middle = null;
                if (token.Is("\\futurelet") || token.Is("\\\\globalfuture")) { if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Futurelet requires two tokens."); middle = Pop(w); }
                else if (w.Pending.Count > 0 && w.Pending[^1].Is("=")) { Pop(w); SkipSpaces(w); }
                if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Let requires a right-hand token.");
                var rhs = Pop(w); var value = TryGetDefinition(w, rhs.Text, out var existing) ? existing with { BuiltIn = false } : new MacroDefinition([rhs with { NoExpand = true }], 0);
                Assign(w, name, value, token.Is("\\\\globallet") || token.Is("\\\\globalfuture"));
                if (middle is not null) { Push(w, rhs, options.MaxTokens); Push(w, middle.Value, options.MaxTokens); }
                continue;
            }
            if (!customOverride && token.Text is "\\@firstoftwo" or "\\@secondoftwo" or "\\@ifnextchar" or "\\TextOrMath")
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                var count = token.Is("\\@ifnextchar") ? 3 : 2;
                for (var a = 0; a < count; a++) arguments[a] = ConsumeArgument(w, options, token, cancellationToken);
                var selected = token.Is("\\@secondoftwo") || token.Is("\\TextOrMath") && !textMode ? 1 : 0;
                if (count == 3) { SkipSpaces(w); var first = arguments[0]; selected = first.Length == 1 && w.Pending.Count > 0 && w.Pending[^1].Text.SequenceEqual(w.Arguments[first.Start].Text) ? 1 : 2; }
                var (start, length) = arguments[selected]; for (var i = length - 1; i >= 0; i--) Push(w, w.Arguments[start + i], options.MaxTokens); continue;
            }
            if (!customOverride && token.Is("\\char"))
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                var code = ReadCharacter(w, token); PushExpansion(w, "\\@char{" + code.ToString(System.Globalization.CultureInfo.InvariantCulture) + "}", token, options, cancellationToken); continue;
            }
            if (!customOverride && token.Text is "\\message" or "\\errmessage" or "\\show")
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                if (token.Is("\\show")) { if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Show requires a token."); var shown = Pop(w); System.Diagnostics.Trace.WriteLine(shown.Text.ToString()); }
                else { var (start, length) = ConsumeArgument(w, options, token, cancellationToken); var message = new System.Text.StringBuilder(); for (var i = 0; i < length; i++) message.Append(w.Arguments[start + i].Text); System.Diagnostics.Trace.WriteLine(message.ToString()); }
                continue;
            }
            if (!customOverride && token.Text is "\\dots" or "\\dotso" or "\\dotsc" or "\\cdots")
            {
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                if (token.Is("\\dots") && w.Pending.Count != 0) ExpandOne(w, options, ref expansions, cancellationToken);
                var nextText = w.Pending.Count == 0 ? ReadOnlySpan<char>.Empty : w.Pending[^1].Text;
                if (token.Is("\\dots"))
                {
                    var centered = nextText is "+" or "=" or "<" or ">" or "-" or "*" or ":" or "\\DOTSB" or "\\coprod" or "\\bigvee" or "\\bigwedge" or "\\biguplus" or "\\bigcap" or "\\bigcup" or "\\prod" or "\\sum" or "\\bigotimes" or "\\bigoplus" or "\\bigodot" or "\\bigsqcup" or "\\And" or "\\mathbin" or "\\mathrel" or "\\xrightarrow" or "\\xleftarrow" || nextText.StartsWith("\\not", StringComparison.Ordinal);
                    if (!centered)
                    {
                        foreach (var symbol in SymbolData.Symbols) if (!symbol.TextMode && symbol.Name.AsSpan().SequenceEqual(nextText) && symbol.Atom is MathAtom.Bin or MathAtom.Rel) { centered = true; break; }
                    }
                    var nextIntegral = nextText is "\\DOTSI" or "\\int" or "\\oint" or "\\iint" or "\\iiint" or "\\iiiint" or "\\idotsint";
                    PushExpansion(w, nextIntegral ? "\\dotsi" : centered ? "\\dotsb" : nextText is "\\DOTSX" ? "\\dotsx" : "\\dotso", token, options, cancellationToken); continue;
                }
                var extra = nextText is ")" or "]" or "\\rbrack" or "\\}" or "\\rbrace" or "\\rangle" or "\\rceil" or "\\rfloor" or "\\rgroup" or "\\rmoustache" or "\\right" or "\\bigr" or "\\biggr" or "\\Bigr" or "\\Biggr" or "$" or ";" or "." or ",";
                if (token.Is("\\dotsc") && nextText.SequenceEqual(",")) extra = false;
                PushExpansion(w, (token.Is("\\cdots") ? "\\@cdots" : "\\ldots") + (extra ? "\\," : ""), token, options, cancellationToken); continue;
            }
            if (!customOverride && token.Text is "\\def" or "\\gdef" or "\\edef" or "\\xdef") { Define(w, options, token.Is("\\gdef") || token.Is("\\xdef"), token, ref expansions, cancellationToken); continue; }
            if (!customOverride && (token.Is("\\newcommand") || token.Is("\\renewcommand") || token.Is("\\providecommand")))
            { NewCommand(w, options, token, ref expansions, cancellationToken); continue; }
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
                    if (macro.Arguments > 0 && i > 0 && macro.Body[i - 1].Is("#") && part.Is("#")) { Push(w, part with { Origin = token.Origin }, options.MaxTokens); i--; }
                    else if (macro.Arguments > 0 && IsParameterReference(macro.Body, i))
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
                if (TryGetDefinition(w, "\\arraystretch", out var stretchDefinition))
                {
                    var expanded = ExpandDefinition(w, options, stretchDefinition.Body.ToList(), ref expansions, cancellationToken); var text = new System.Text.StringBuilder(); foreach (var part in expanded) text.Append(part.Text);
                    if (!double.TryParse(text.ToString(), System.Globalization.NumberStyles.Float, System.Globalization.CultureInfo.InvariantCulture, out var stretch)) stretch = double.NaN;
                    if (w.Expanded.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Expanded token budget exceeded.");
                    w.Expanded.Add(token with { Length = 0, InternalArrayStretch = stretch == 0 ? double.NaN : stretch });
                }
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
            if (IsTextArgumentCommand(token.Text)) MarkTextArgument(w, token, options);
            else if (token.Text is "\\raisebox" or "\\colorbox" or "\\fcolorbox") MarkTextArgument(w, token, options, token.Is("\\fcolorbox") ? 2 : 1);
            if (w.Expanded.Count >= options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Expanded token budget exceeded.");
            w.Expanded.Add(token);
        }
        if (w.ScopeKinds.Contains((byte)4)) throw new CompileFailure(new(MathDiagnosticCode.UnclosedGroup, new(source.Length, 0), "Unclosed begingroup."));
        if (w.ScopeKinds.Count == 1 && w.ScopeKinds[0] == 6) CloseScope(w);
        ValidatePersistentBudget(w.Definitions, options);
    }

    private static void OpenScope(MathWorkspace w, in MathOptions options, LexToken token, byte kind)
    { if (kind is 1 or 4 && w.ScopeKinds.Count(k => k is 1 or 4) >= options.MaxNesting) Fail(MathDiagnosticCode.NestingLimit, token, "Group nesting budget exceeded."); w.Scopes.Add(w.Undo.Count); w.ScopeKinds.Add(kind); }
    private static void CloseScope(MathWorkspace w)
    {
        var marker = w.Scopes[^1]; w.Scopes.RemoveAt(w.Scopes.Count - 1); w.ScopeKinds.RemoveAt(w.ScopeKinds.Count - 1);
        for (var i = w.Undo.Count - 1; i >= marker; i--)
        { var undo = w.Undo[i]; if (undo.Existed) w.Definitions[undo.Name] = undo.Previous; else w.Definitions.Remove(undo.Name); }
        w.Undo.RemoveRange(marker, w.Undo.Count - marker);
    }
    private static void RestoreBraketDefinition(MathWorkspace w, string name)
    {
        for (var i = w.Undo.Count - 1; i >= 0; i--)
        {
            var undo = w.Undo[i]; if (undo.Name != name) continue;
            if (undo.Existed) w.Definitions[name] = undo.Previous; else w.Definitions.Remove(name); return;
        }
    }
    private static void PushExpansion(MathWorkspace w, string value, LexToken origin, in MathOptions options, CancellationToken cancellationToken)
    {
        var parts = new List<LexToken>(); MathLexer.Tokenize(value, parts, options.MaxTokens, cancellationToken, origin.Origin, options.StrictMode, w.Warnings, options.StrictHandler);
        for (var i = parts.Count - 1; i >= 0; i--) Push(w, parts[i], options.MaxTokens);
    }
    private static bool IsExpandable(MathWorkspace w, ReadOnlySpan<char> name)
    {
        if (TryGetDefinition(w, name, out _)) return true;
        if (name is "\\noexpand" or "\\expandafter" or "\\@firstoftwo" or "\\@secondoftwo" or "\\@ifnextchar" or "\\TextOrMath" or "\\char" or "\\message" or "\\errmessage" or "\\show" or "\\dots" or "\\dotso" or "\\dotsc" or "\\cdots" or "\\bra@ket" or "\\bra@set" or "\\tag@literal") return true;
        foreach (var command in CommandData.Definitions) if (!command.Primitive && name.SequenceEqual(command.Name)) return true;
        return false;
    }
    private static void ExpandOne(MathWorkspace w, in MathOptions options, ref int expansions, CancellationToken cancellationToken)
    {
        if (w.Pending.Count == 0) return;
        var token = Pop(w);
        if (token.NoExpand) { Push(w, token, options.MaxTokens); return; }
        if (token.Is("\\expandafter"))
        {
            var deferred = new List<LexToken>();
            while (token.Is("\\expandafter"))
            {
                if (w.Pending.Count < 2) Fail(MathDiagnosticCode.MissingArgument, token, "Expandafter requires two tokens.");
                if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
                deferred.Add(Pop(w)); token = Pop(w);
            }
            Push(w, token, options.MaxTokens); ExpandOne(w, options, ref expansions, cancellationToken);
            for (var i = deferred.Count - 1; i >= 0; i--) Push(w, deferred[i], options.MaxTokens); return;
        }
        if (token.Is("\\noexpand"))
        { if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, token, "Noexpand requires a token."); Push(w, Pop(w) with { NoExpand = true }, options.MaxTokens); return; }
        if (!TryGetDefinition(w, token.Text, out var macro)) { Push(w, token, options.MaxTokens); return; }
        if (++expansions > options.MaxExpansion) Fail(MathDiagnosticCode.ExpansionLimit, token, "Macro expansion budget exceeded.");
        Span<(int Start, int Length)> args = stackalloc (int, int)[9];
        if (macro.Delimiters is not null) foreach (var prefix in macro.Delimiters[0]) if (w.Pending.Count == 0 || !Pop(w).Text.SequenceEqual(prefix.Text)) Fail(MathDiagnosticCode.MissingArgument, token, "Macro invocation does not match its prefix.");
        for (var a = 0; a < macro.Arguments; a++) args[a] = macro.Delimiters is not null && macro.Delimiters[a + 1].Length > 0 ? ConsumeDelimitedArgument(w, options, token, macro.Delimiters[a + 1], cancellationToken) : ConsumeArgument(w, options, token, cancellationToken);
        for (var i = macro.Body.Length - 1; i >= 0; i--)
        {
            var part = macro.Body[i];
            if (macro.Arguments > 0 && i > 0 && macro.Body[i - 1].Is("#") && part.Is("#")) { Push(w, part with { Origin = token.Origin }, options.MaxTokens); i--; }
            else if (macro.Arguments > 0 && IsParameterReference(macro.Body, i))
            { var index = part.Text[0] - '1'; if (index >= macro.Arguments) Fail(MathDiagnosticCode.InvalidMacroDefinition, token, "Macro argument outside declared arity."); var (start, length) = args[index]; for (var j = length - 1; j >= 0; j--) Push(w, w.Arguments[start + j], options.MaxTokens); i--; }
            else Push(w, part with { Origin = token.Origin }, options.MaxTokens);
        }
    }
    private static List<LexToken> ExpandDefinition(MathWorkspace w, in MathOptions options, List<LexToken> body, ref int expansions, CancellationToken cancellationToken)
    {
        var boundary = w.Pending.Count; for (var i = body.Count - 1; i >= 0; i--) Push(w, body[i], options.MaxTokens);
        var result = new List<LexToken>(body.Count);
        while (w.Pending.Count > boundary)
        {
            cancellationToken.ThrowIfCancellationRequested(); var token = w.Pending[^1];
            if (!token.NoExpand && (token.Is("\\noexpand") || HasDefinition(w, token.Text))) { ExpandOne(w, options, ref expansions, cancellationToken); continue; }
            result.Add(Pop(w) with { NoExpand = false, TreatAsRelax = false }); if (result.Count > options.MaxTokens) Fail(MathDiagnosticCode.TokenLimit, token, "Expanded definition token budget exceeded.");
        }
        if (result.Count > options.MaxExpansion - expansions) throw new CompileFailure(new(MathDiagnosticCode.ExpansionLimit, body.Count > 0 ? body[0].Origin : new(0, 0), "Expanded definition output budget exceeded."));
        expansions += result.Count;
        return result;
    }
    private static int ReadCharacter(MathWorkspace w, LexToken command)
    {
        if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, command, "Character command requires a code.");
        var token = Pop(w);
        if (token.Is("`"))
        {
            if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, command, "Backtick character command requires a character.");
            token = Pop(w); var text = token.Text; if (text[0] == '\\') text = text[1..];
            if (text.Length == 0) Fail(MathDiagnosticCode.UnexpectedToken, token, "Expected a character after backtick.");
            return System.Text.Rune.GetRuneAt(text.ToString(), 0).Value;
        }
        var radix = token.Is("'") ? 8 : token.Is("\"") ? 16 : 10;
        if (radix != 10) { if (w.Pending.Count == 0) Fail(MathDiagnosticCode.MissingArgument, command, "Character command requires digits."); token = Pop(w); }
        var value = Digit(token.Text, radix); if (value < 0) Fail(MathDiagnosticCode.UnexpectedToken, token, "Invalid character-code digit.");
        while (w.Pending.Count != 0)
        {
            var next = Digit(w.Pending[^1].Text, radix); if (next < 0) break;
            if (value > (0x10ffff - next) / radix) Fail(MathDiagnosticCode.UnexpectedToken, command, "Character code is outside Unicode.");
            value = value * radix + next; Pop(w);
        }
        if (!System.Text.Rune.IsValid(value)) Fail(MathDiagnosticCode.UnexpectedToken, command, "Character code is not a Unicode scalar."); return value;
        static int Digit(ReadOnlySpan<char> text, int radix)
        { if (text.Length != 1) return -1; var value = text[0] is >= '0' and <= '9' ? text[0] - '0' : text[0] is >= 'a' and <= 'f' ? text[0] - 'a' + 10 : text[0] is >= 'A' and <= 'F' ? text[0] - 'A' + 10 : -1; return value >= 0 && value < radix ? value : -1; }
    }
    private static bool IsTextArgumentCommand(ReadOnlySpan<char> name) => name is "\\text" or "\\textrm" or "\\textnormal" or "\\textbf" or "\\textmd" or "\\textit" or "\\textup" or "\\textsf" or "\\texttt" or "\\emph" or "\\hbox" or "\\reflectbox" or "\\fbox" or "\\angl";
    private static void MarkTextArgument(MathWorkspace w, LexToken command, in MathOptions options, int precedingArguments = 0)
    {
        var begin = w.Pending.Count - 1; while (begin >= 0 && char.IsWhiteSpace(w.Pending[begin].Text[0])) begin--;
        while (precedingArguments-- > 0)
        {
            if (begin < 0) return;
            if (w.Pending[begin].Is("{"))
            { var depth = 1; begin--; while (begin >= 0) { if (w.Pending[begin].Is("{")) depth++; else if (w.Pending[begin].Is("}") && --depth == 0) { begin--; break; } begin--; } }
            else begin--;
            while (begin >= 0 && char.IsWhiteSpace(w.Pending[begin].Text[0])) begin--;
        }
        if (begin < 0) return;
        var end = begin;
        if (w.Pending[begin].Is("{"))
        {
            var depth = 1; for (end = begin - 1; end >= 0; end--) { if (w.Pending[end].Is("{")) depth++; else if (w.Pending[end].Is("}") && --depth == 0) break; }
            if (end < 0) return;
        }
        if (w.Pending.Count > options.MaxTokens - 2) Fail(MathDiagnosticCode.TokenLimit, command, "Text argument marker token budget exceeded.");
        w.Pending.Insert(begin + 1, new(TextModeMarker, 0, 0, command.Origin)); w.Pending.Insert(end, new(RestoreModeMarker, 0, 0, command.Origin));
    }

    // These stable built-in string macros have direct equivalent P1 opcodes.
    // User overrides retain ordinary macro behavior.
    private static bool IsDirectSpacing(ReadOnlySpan<char> name) => name is "\\," or "\\:" or "\\;" or "\\!" or "\\quad" or "\\qquad" or "\\operatorname" or "\\liminf" or "\\limsup" or "\\bm" or "\\tag" or "\\notag" or "\\nonumber";

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

    private static void Define(MathWorkspace w, in MathOptions options, bool global, LexToken command, ref int expansions, CancellationToken cancellationToken)
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
        if (command.Is("\\edef") || command.Is("\\xdef")) body = ExpandDefinition(w, options, body, ref expansions, cancellationToken);
        LexToken[][]? compactDelimiters = null;
        if (delimiters.Any(d => d.Count != 0))
        { compactDelimiters = new LexToken[delimiters.Count][]; for (var i = 0; i < delimiters.Count; i++) compactDelimiters[i] = Compact(delimiters[i], command.Origin, options.MaxTokens); }
        Assign(w, name, new(Compact(body, command.Origin, options.MaxTokens), count, Delimiters: compactDelimiters), global);
    }
    private static void NewCommand(MathWorkspace w, in MathOptions options, LexToken command, ref int expansions, CancellationToken cancellationToken)
    {
        SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected a macro name.");
        var nameToken = Pop(w); var braced = nameToken.Is("{");
        if (braced) { SkipSpaces(w); if (w.Pending.Count == 0) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected a macro name."); nameToken = Pop(w); SkipSpaces(w); if (w.Pending.Count == 0 || !Pop(w).Is("}")) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Expected closing brace after macro name."); }
        var name = nameToken.Text.ToString(); ValidateName(name, nameToken.Origin); SkipSpaces(w);
        var count = 0;
        if (w.Pending.Count != 0 && w.Pending[^1].Is("["))
        {
            Pop(w); var arity = new System.Text.StringBuilder();
            while (w.Pending.Count != 0 && !w.Pending[^1].Is("]"))
            {
                if (!w.Pending[^1].NoExpand && HasDefinition(w, w.Pending[^1].Text)) { ExpandOne(w, options, ref expansions, cancellationToken); continue; }
                arity.Append(Pop(w).Text);
            }
            if (w.Pending.Count == 0 || !Pop(w).Is("]") || !int.TryParse(arity.ToString().Trim(), out count) || count is < 0 or > 9) Fail(MathDiagnosticCode.InvalidMacroDefinition, command, "Macro arity must be a bounded integer from zero through nine.");
        }
        var body = ReadDefinitionBody(w, options, command, cancellationToken);
        var exists = HasDefinition(w, name) || Array.BinarySearch(SymbolData.KnownCommands, name, StringComparer.Ordinal) >= 0 || SymbolData.Symbols.Any(s => s.Name == name);
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
        var parameters = 0;
        for (var i = 0; i < body.Count; i++)
        {
            if (!body[i].Is("#") || i + 1 >= body.Count) continue;
            if (body[i + 1].Is("#")) { i++; continue; }
            if (body[i + 1].Text.Length == 1 && body[i + 1].Text[0] is >= '1' and <= '9') parameters |= 1 << (body[++i].Text[0] - '1');
        }
        var count = 0; while ((parameters & (1 << count)) != 0 && count < 9) count++; return count;
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
        var compact = text.ToString(); var tokens = new LexToken[body.Count]; var offset = 0;
        for (var i = 0; i < body.Count; i++)
        {
            var token = body[i]; tokens[i] = new(compact, offset, token.Length, origin, token.NoExpand, token.TreatAsRelax); offset += token.Length;
            if (token.Text.Length > 1 && token.Text[0] == '\\' && token.Text[1] is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or '@') offset++;
        }
        if (tokens.Length > maximum) throw new CompileFailure(new(MathDiagnosticCode.TokenLimit, origin, "Macro replacement token budget exceeded."));
        return tokens;
    }
    private static bool TryGetDefinition(MathWorkspace w,ReadOnlySpan<char> name,out MacroDefinition macro)
    {
        if(w.Definitions.GetAlternateLookup<ReadOnlySpan<char>>().TryGetValue(name,out macro))return true;
        // All single-character ASCII built-ins are active characters; '~' is
        // currently the only one. Ordinary letters, digits and operators can
        // skip the static table without changing semantics.
        if(name.Length==1&&name[0]<'\u0080'&&name[0]!='~')return false;
        return BuiltInLookup.TryGetValue(name,out macro);
    }
    private static bool HasDefinition(MathWorkspace w,ReadOnlySpan<char> name)=>TryGetDefinition(w,name,out _);
    private static void ValidateName(string name, SourceRange range)
    { if (string.IsNullOrEmpty(name) || name is "\\" or "{" or "}" or "$" or "&" or "#" or "^" or "_") throw new CompileFailure(new(MathDiagnosticCode.InvalidMacroDefinition, range, "Expected a valid control sequence or active character.")); }
    private static void SkipSpaces(MathWorkspace w) { while (w.Pending.Count != 0 && char.IsWhiteSpace(w.Pending[^1].Text[0])) Pop(w); }
    private static LexToken Pop(MathWorkspace w) { var token = w.Pending[^1]; w.Pending.RemoveAt(w.Pending.Count - 1); return token; }
    private static void Push(MathWorkspace w, LexToken token, int max) { if (w.Pending.Count >= max) Fail(MathDiagnosticCode.TokenLimit, token, "Pending token budget exceeded."); w.Pending.Add(token); }
    private static void Fail(MathDiagnosticCode code, LexToken token, string message) => throw new CompileFailure(new(code, token.Origin, message));
}
