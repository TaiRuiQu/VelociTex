namespace VelociTex;

/// <summary>A thread-safe, bounded least-recently-used cache of independently owned formula results.</summary>
/// <remarks>Native preparations, images and GPU surfaces are not cached. Sessions, custom macros and
/// definition-bearing input bypass the cache. The byte charge is a conservative managed payload
/// estimate, not a measurement of the CLR heap or native memory. Custom text shaping providers
/// also bypass the cache because their external font state is not an immutable cache key.
/// Compilation occurs outside the lock.</remarks>
public sealed class MathDocumentCache
{
    private readonly object _gate = new();
    private readonly Dictionary<Key, LinkedListNode<Entry>> _items = new();
    private readonly LinkedList<Entry> _recency = new();
    private long _bytes;

    /// <summary>Creates a cache with both a byte budget and an item limit.</summary>
    /// <param name="maximumBytes">Maximum estimated managed payload retained; zero disables retention.</param>
    /// <param name="maximumItems">Maximum formula count retained; zero disables retention.</param>
    public MathDocumentCache(long maximumBytes = 16 * 1024 * 1024, int maximumItems = 256)
    {
        ArgumentOutOfRangeException.ThrowIfNegative(maximumBytes);
        ArgumentOutOfRangeException.ThrowIfNegative(maximumItems);
        MaximumBytes = maximumBytes; MaximumItems = maximumItems;
    }

    /// <summary>Gets the maximum estimated retained managed payload in bytes.</summary>
    public long MaximumBytes { get; }
    /// <summary>Gets the maximum number of retained formulas.</summary>
    public int MaximumItems { get; }
    /// <summary>Gets the current conservative managed payload charge.</summary>
    public long EstimatedRetainedBytes { get { lock (_gate) return _bytes; } }
    /// <summary>Gets the current retained formula count.</summary>
    public int Count { get { lock (_gate) return _items.Count; } }

    /// <summary>Compiles a complete formula or returns an immutable cached result.</summary>
    /// <param name="engine">Stateless compiler used on a miss.</param>
    /// <param name="latex">Complete source without host delimiters.</param>
    /// <param name="options">All compilation options participate in identity. Macro inputs bypass caching.</param>
    /// <param name="workspace">Call-exclusive scratch space used only on a miss.</param>
    /// <param name="cancellationToken">Cancellation is checked before a cache lookup and before return.</param>
    /// <param name="document">Independently owned successful result, or null on error.</param>
    /// <param name="diagnostic">Structured compilation or cancellation diagnostic.</param>
    /// <param name="cacheHit">True only when compilation was skipped.</param>
    public bool TryCompile(MathEngine engine, string latex, in MathOptions options, MathWorkspace workspace,
        CancellationToken cancellationToken, out MathDocument? document, out MathDiagnostic diagnostic, out bool cacheHit)
    {
        ArgumentNullException.ThrowIfNull(engine); ArgumentNullException.ThrowIfNull(latex); ArgumentNullException.ThrowIfNull(workspace);
        document = null; diagnostic = default; cacheHit = false;
        if (cancellationToken.IsCancellationRequested)
        { diagnostic = new(MathDiagnosticCode.Cancelled, new(0, latex.Length), "Compilation was cancelled."); return false; }
        var eligible = MaximumBytes > 0 && MaximumItems > 0 && options.Macros is null && options.Session is null && options.TextShaper is null && !ContainsDefinition(latex);
        var key = new Key(latex, options);
        if (eligible)
        {
            lock (_gate)
            {
                if (_items.TryGetValue(key, out var cached))
                {
                    _recency.Remove(cached); _recency.AddFirst(cached);
                    document = cached.Value.Document; cacheHit = true;
                }
            }
            if (cacheHit)
            {
                if (!cancellationToken.IsCancellationRequested) return true;
                document = null; cacheHit = false;
                diagnostic = new(MathDiagnosticCode.Cancelled, new(0, latex.Length), "Compilation was cancelled."); return false;
            }
        }
        if (!engine.TryCompile(latex, in options, workspace, cancellationToken, out document, out diagnostic)) return false;
        if (!eligible) return true;
        var bytes = Estimate(document!);
        if (bytes > MaximumBytes) return true;
        lock (_gate)
        {
            // Another independent workspace may have compiled the same formula meanwhile.
            if (_items.ContainsKey(key)) return true;
            while (_items.Count >= MaximumItems || _bytes > MaximumBytes - bytes)
            {
                var last = _recency.Last!; _items.Remove(last.Value.Key); _recency.RemoveLast(); _bytes -= last.Value.Bytes;
            }
            var node = _recency.AddFirst(new Entry(key, document!, bytes)); _items.Add(key, node); _bytes += bytes;
        }
        return true;
    }

    /// <summary>Releases all retained results. Results already returned to callers remain valid.</summary>
    public void Clear() { lock (_gate) { _items.Clear(); _recency.Clear(); _bytes = 0; } }

    private static bool ContainsDefinition(string source)
    {
        // Conservative bypass also covers escaped text and definitions in comments. Do not
        // execute macros while deciding eligibility or retain mutable user dictionaries as keys.
        ReadOnlySpan<string> names = ["\\def", "\\gdef", "\\edef", "\\xdef", "\\let", "\\futurelet", "\\global", "\\newcommand", "\\renewcommand", "\\providecommand"];
        foreach (var name in names) if (source.Contains(name, StringComparison.Ordinal)) return true;
        return false;
    }
    private static long Estimate(MathDocument document)
    {
        // Array headers, strings, keys, dictionary/LRU bookkeeping and the result object are
        // deliberately charged above their payloads. Shared static path data is charged per
        // result as a conservative upper bound; it is never omitted from the budget.
        long bytes = 512 + 2L * document.Source.Length + 160L * document.Nodes.Length +
            64L * document.Glyphs.Length + 96L * document.TextGlyphs.Length + 48L * document.Rules.Length + 128L * document.Paths.Length;
        foreach (var node in document.Nodes.AsSpan()) bytes += 64 + 2L * ((node.Text?.Length ?? 0) + (node.Alignment?.Length ?? 0));
        foreach (var path in document.Paths.Span) bytes += 64L * path.Segments.Length;
        return bytes;
    }
    private readonly record struct Key(string Source, MathOptions Options);
    private readonly record struct Entry(Key Key, MathDocument Document, long Bytes);
}
