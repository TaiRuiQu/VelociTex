using System.Collections.Concurrent;

namespace VelociTex;

// Generated stretchy/radical geometry depends only on small integer sizes.
// Reusing the arrays lets layout and SVG serialization cache their formatted
// path data instead of regenerating and reformatting identical contours.
internal static class MathPathCache
{
    private const int Limit = 512;
    private static readonly ConcurrentDictionary<string, MathPathSegment[]> Named = new(StringComparer.Ordinal);
    private static readonly ConcurrentDictionary<(string Label, long Height), MathPathSegment[]> Tall = new();
    private static readonly ConcurrentDictionary<(int CodePoint, long Height), MathPathSegment[]> Inner = new();
    private static readonly ConcurrentDictionary<long, MathPathSegment[]> RadicalTall = new();
    private static readonly ConcurrentDictionary<(int Size, long Extra, long Height), MathPathSegment[]> RadicalRule = new();

    internal static MathPathSegment[] Get(string name)
    {
        if (Named.TryGetValue(name, out var cached)) return cached;
        if (Named.Count > Limit) Named.Clear();
        return Named.GetOrAdd(name, static value => StretchyPathData.Get(value));
    }
    internal static MathPathSegment[] TallPath(string label, long height)
    {
        if (Tall.TryGetValue((label, height), out var cached)) return cached;
        if (Tall.Count > Limit) Tall.Clear();
        return Tall.GetOrAdd((label, height), static key => StretchyPathData.Tall(key.Label, key.Height / 1000.0));
    }
    internal static MathPathSegment[] InnerPath(int codePoint, long height)
    {
        if (Inner.TryGetValue((codePoint, height), out var cached)) return cached;
        if (Inner.Count > Limit) Inner.Clear();
        return Inner.GetOrAdd((codePoint, height), static key => StretchyPathData.Inner(key.CodePoint, key.Height / 1000.0));
    }
    internal static MathPathSegment[] Radical(double viewBoxHeight)
    {
        long height = (long)Math.Round(viewBoxHeight);
        if (RadicalTall.TryGetValue(height, out var cached)) return cached;
        if (RadicalTall.Count > Limit) RadicalTall.Clear();
        return RadicalTall.GetOrAdd(height, static value => RadicalPathData.Tall(value));
    }
    internal static MathPathSegment[] RadicalWithRule(int size, double extraVinculum, double viewBoxHeight)
    {
        var key = (size, (long)Math.Round(extraVinculum * 1_000_000), (long)Math.Round(viewBoxHeight));
        if (RadicalRule.TryGetValue(key, out var cached)) return cached;
        if (RadicalRule.Count > Limit) RadicalRule.Clear();
        return RadicalRule.GetOrAdd(key, static value => RadicalPathData.WithRule(value.Size, value.Extra / 1_000_000.0, value.Height));
    }
}
