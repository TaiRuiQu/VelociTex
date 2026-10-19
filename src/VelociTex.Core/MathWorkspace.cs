using System.Text;

namespace VelociTex;

/// <summary>Call-exclusive reusable scratch buffers. A failed call never leaves borrowed result data.</summary>
public sealed class MathWorkspace : IDisposable
{
    private int _busy;
    internal readonly List<LexToken> Pending = new(256);
    internal readonly List<LexToken> Expanded = new(256);
    internal readonly List<LexToken> Arguments = new(128);
    internal readonly List<SemanticNode> Nodes = new(256);
    internal readonly List<ParseTask> Tasks = new(128);
    internal readonly Dictionary<string, MacroDefinition> Definitions = new(StringComparer.Ordinal);
    internal readonly List<MacroUndo> Undo = new(32);
    internal readonly List<int> Scopes = new(32);
    internal readonly List<byte> ScopeKinds = new(32);
    internal readonly List<bool> ExpansionModes = new(16);
    internal readonly List<MathWarning> Warnings = new(8);
    internal bool Enter() => Interlocked.CompareExchange(ref _busy, 1, 0) == 0;
    internal void Leave()
    {
        Pending.Clear(); Expanded.Clear(); Arguments.Clear(); Nodes.Clear(); Tasks.Clear();
        Definitions.Clear(); Undo.Clear(); Scopes.Clear(); ScopeKinds.Clear(); ExpansionModes.Clear(); Warnings.Clear();
        // Large requests cannot permanently retain their high-water buffers.
        Trim(Pending, 4096); Trim(Expanded, 4096); Trim(Arguments, 4096); Trim(Nodes, 4096); Trim(Tasks, 1024);
        Trim(Undo, 256); Trim(Scopes, 128); Trim(ScopeKinds, 128); Trim(ExpansionModes, 128); Trim(Warnings, 128);
        if (Definitions.EnsureCapacity(0) > 1024) Definitions.TrimExcess(1024);
        Volatile.Write(ref _busy, 0);
    }
    private static void Trim<T>(List<T> list, int retained) { if (list.Capacity > retained) list.Capacity = retained; }
    /// <summary>Releases retained scratch capacity; the workspace may subsequently be reused.</summary>
    public void Dispose()
    {
        if (!Enter()) throw new InvalidOperationException("Cannot dispose a workspace during compilation.");
        try
        {
            Pending.Clear(); Pending.Capacity = 0; Expanded.Clear(); Expanded.Capacity = 0;
            Arguments.Clear(); Arguments.Capacity = 0; Nodes.Clear(); Nodes.Capacity = 0;
            Tasks.Clear(); Tasks.Capacity = 0; Definitions.Clear(); Definitions.TrimExcess();
            Undo.Clear(); Undo.Capacity = 0; Scopes.Clear(); Scopes.Capacity = 0; ScopeKinds.Clear(); ScopeKinds.Capacity = 0; ExpansionModes.Clear(); ExpansionModes.Capacity = 0; Warnings.Clear(); Warnings.Capacity = 0;
        }
        finally { Volatile.Write(ref _busy, 0); }
    }
}

/// <summary>Serial, explicit macro sharing. Successful calls commit; errors and cancellation roll back.</summary>
public sealed class MacroSession
{
    /// <summary>Creates an isolated empty serial macro session.</summary>
    public MacroSession() { }
    private int _busy;
    private Dictionary<string, MacroDefinition> _definitions = new(StringComparer.Ordinal);
    internal bool Enter() => Interlocked.CompareExchange(ref _busy, 1, 0) == 0;
    internal void CopyTo(Dictionary<string, MacroDefinition> destination)
    {
        foreach (var pair in _definitions) destination[pair.Key] = pair.Value;
    }
    internal void Commit(Dictionary<string, MacroDefinition> source)
    {
        var committed = new Dictionary<string, MacroDefinition>(StringComparer.Ordinal);
        foreach (var pair in source) if (!pair.Value.BuiltIn) committed.Add(pair.Key, pair.Value);
        _definitions = committed;
    }
    internal void Leave() => Volatile.Write(ref _busy, 0);
}

internal readonly record struct LexToken(string Source, int Start, int Length, SourceRange Origin, bool NoExpand = false, bool TreatAsRelax = false, double InternalArrayStretch = 0)
{
    internal ReadOnlySpan<char> Text => Source.AsSpan(Start, Length);
    internal bool Is(ReadOnlySpan<char> text) => Text.SequenceEqual(text);
}
internal readonly record struct MacroDefinition(LexToken[] Body, int Arguments, bool BuiltIn = false, LexToken[][]? Delimiters = null, byte Callback = 0);
internal readonly record struct MacroUndo(string Name, bool Existed, MacroDefinition Previous);
internal enum ParseTaskKind { Sequence, AttachSequence, Argument, Atom, SetChild, Return, FinishDelimited, MatrixNextCell, NormalizeChild, FinishClass, FinishTag, FinishTopTag, ReturnInternal, GenfracAfterDelimiters, AppendPrimeArgument, WrapHBoxArgument }
internal readonly record struct ParseTask(ParseTaskKind Kind, int Node = -1, int Last = -1, int Previous = -1, int Slot = 0, bool Closing = false, bool Text = false, MathFont Font = MathFont.Default, int Depth = 0, byte Stop = 0, byte TextFamily = 0, bool TextBold = false, bool TextItalic = false, int Limit = int.MaxValue, bool PrimitiveArgument = false);

internal sealed class CompileFailure(MathDiagnostic diagnostic) : Exception(diagnostic.Message)
{
    internal MathDiagnostic Diagnostic { get; } = diagnostic;
}

internal static class MathLexer
{
    internal static void Tokenize(string source, List<LexToken> output, int maxTokens, CancellationToken cancellationToken, SourceRange? origin = null, MathStrictMode strictMode = MathStrictMode.Ignore, List<MathWarning>? warnings = null, Func<MathWarning, MathStrictMode>? strictHandler = null)
    {
        for (var i = 0; i < source.Length;)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var start = i;
            var c = source[i++];
            if (c == '%')
            {
                while (i < source.Length && source[i] != '\n') i++;
                if (i < source.Length) i++;
                else
                {
                    var range = origin ?? new(start, source.Length - start);
                    var warning = new MathWarning("commentAtEnd", range, "Comment has no terminating newline."); var behavior = strictHandler?.Invoke(warning) ?? strictMode;
                    if (behavior == MathStrictMode.Error) throw new CompileFailure(new(MathDiagnosticCode.StrictViolation, range, "Unterminated comment at the end of the formula. [commentAtEnd]"));
                    if (behavior == MathStrictMode.Warn && warnings is not null && warnings.Count < Math.Min(maxTokens, 1024)) warnings.Add(warning);
                }
                continue;
            }
            if (c == '\\' && source.AsSpan(start).StartsWith("\\verb", StringComparison.Ordinal) && start + 5 < source.Length && !IsLetter(source[start + 5]))
            {
                var delimiter = start + 5; if (source[delimiter] == '*') delimiter++;
                if (delimiter >= source.Length) throw new CompileFailure(new(MathDiagnosticCode.UnclosedGroup, origin ?? new(start, source.Length - start), "Verbatim text requires a matching delimiter."));
                var end = delimiter + 1;
                while (end < source.Length && source[end] != source[delimiter] && source[end] is not ('\r' or '\n')) end++;
                if (end == source.Length || source[end] != source[delimiter]) throw new CompileFailure(new(MathDiagnosticCode.UnclosedGroup, origin ?? new(start, end - start), "Verbatim text ended before its matching delimiter."));
                i = end + 1;
            }
            else if (c == '\\' && i < source.Length)
            {
                if (char.IsWhiteSpace(source[i]))
                {
                    while (i < source.Length && source[i] is ' ' or '\r' or '\t') i++;
                    if (i < source.Length && source[i] == '\n') i++;
                    while (i < source.Length && char.IsWhiteSpace(source[i])) i++;
                    if (output.Count >= maxTokens) throw new CompileFailure(new(MathDiagnosticCode.TokenLimit, origin ?? new(start, i - start), "Token budget exceeded."));
                    output.Add(new("\\ ", 0, 2, origin ?? new(start, i - start))); continue;
                }
                if (IsLetter(source[i])) { while (i < source.Length && IsLetter(source[i])) i++; }
                else if (char.IsHighSurrogate(source[i]) && i + 1 < source.Length && char.IsLowSurrogate(source[i + 1])) i += 2;
                else i++;
            }
            else if (char.IsWhiteSpace(c)) { while (i < source.Length && char.IsWhiteSpace(source[i])) i++; }
            else if (char.IsSurrogate(c))
            {
                if (char.IsHighSurrogate(c) && i < source.Length && char.IsLowSurrogate(source[i])) i++;
                else throw new CompileFailure(new(MathDiagnosticCode.UnknownSymbol, origin ?? new(start, 1), "Unpaired UTF-16 surrogate."));
            }
            if (c != '\\' && !char.IsWhiteSpace(c)) while (i < source.Length && source[i] is >= '\u0300' and <= '\u036f') i++;
            if (output.Count >= maxTokens) throw new CompileFailure(new(MathDiagnosticCode.TokenLimit, origin ?? new(start, i - start), "Token budget exceeded."));
            output.Add(new(source, start, i - start, origin ?? new(start, i - start)));
            if (c == '\\' && i > start + 1 && IsLetter(source[start + 1])) while (i < source.Length && char.IsWhiteSpace(source[i])) i++;
        }
    }
    private static bool IsLetter(char c) => c is >= 'a' and <= 'z' or >= 'A' and <= 'Z' or '@';
}
