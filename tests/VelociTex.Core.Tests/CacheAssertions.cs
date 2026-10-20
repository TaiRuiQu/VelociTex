using VelociTex;

internal static class CacheAssertions
{
    internal static int Run()
    {
        var count = 0;
        void Check(bool value, string description)
        { count++; if (!value) throw new InvalidOperationException("Cache: " + description); }
        MathEngine engine = new();
        using MathWorkspace workspace = new();
        MathOptions options = new();
        MathDocumentCache cache = new(32_768, 2);
        Check(cache.TryCompile(engine, "x+1", in options, workspace, default, out var retained, out _, out var hit) && !hit, "initial miss");
        Check(cache.TryCompile(engine, "x+1", in options, workspace, default, out var second, out _, out hit) && hit && ReferenceEquals(retained, second), "identity hit");
        MathOptions display = options with { DisplayMode = true };
        Check(cache.TryCompile(engine, "x+1", in display, workspace, default, out _, out _, out hit) && !hit, "options identity");
        Check(cache.TryCompile(engine, "y+1", in options, workspace, default, out _, out _, out hit) && !hit && cache.Count == 2, "item eviction");
        Check(cache.TryCompile(engine, "x+1", in options, workspace, default, out _, out _, out hit) && !hit, "least recent entry evicted");
        Check(cache.EstimatedRetainedBytes is > 0 and <= 32_768, "byte budget");
        using CancellationTokenSource cancellation = new();
        cancellation.Cancel();
        Check(!cache.TryCompile(engine, "x+1", in options, workspace, cancellation.Token, out var cancelled, out var diagnostic, out hit) && cancelled is null && diagnostic.Code == MathDiagnosticCode.Cancelled && !hit, "cancelled hit rejected");
        Check(!cache.TryCompile(engine, "x^", in options, workspace, default, out _, out _, out hit) && !hit, "errors not cached");
        for (int i = 0; i < 2; i++)
            Check(cache.TryCompile(engine, "\\def\\foo{x}\\foo", in options, workspace, default, out _, out _, out hit) && !hit, "definitions bypass");
        MathOptions macros = options with { Macros = new Dictionary<string, string> { ["\\foo"] = "x" } };
        for (int i = 0; i < 2; i++)
            Check(cache.TryCompile(engine, "\\foo", in macros, workspace, default, out _, out _, out hit) && !hit, "mutable macros bypass");
        MathOptions session = options with { Session = new() };
        for (int i = 0; i < 2; i++)
            Check(cache.TryCompile(engine, "x", in session, workspace, default, out _, out _, out hit) && !hit, "session bypass");
        MathOptions shaped = options with { TextShaper = new VelociTex.Rendering.Direct2D.WindowsMathTextShaper() };
        for (int i = 0; i < 2; i++)
            Check(cache.TryCompile(engine, "x", in shaped, workspace, default, out _, out _, out hit) && !hit, "external shaping state bypass");
        MathDocumentCache tiny = new(1, 100);
        Check(tiny.TryCompile(engine, "x", in options, workspace, default, out _, out _, out hit) && !hit && tiny.Count == 0 && tiny.EstimatedRetainedBytes == 0, "oversized result not retained");
        cache.Clear();
        Check(cache.Count == 0 && cache.EstimatedRetainedBytes == 0 && retained!.Glyphs.Length == 3, "clear retains caller ownership");
        Parallel.For(0, 64, i =>
        {
            using MathWorkspace isolated = new();
            MathOptions local = new();
            if (!cache.TryCompile(engine, "x_" + (i % 10), in local, isolated, default, out _, out _, out _))
                throw new InvalidOperationException("Concurrent cache compile");
        });
        Check(cache.Count <= 2 && cache.EstimatedRetainedBytes <= cache.MaximumBytes, "concurrent bounded retention");
        return count;
    }
}
