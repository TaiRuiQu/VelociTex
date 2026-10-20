using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.ExceptionServices;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;

namespace VelociTex;

internal static unsafe partial class NativeInterop
{
    private const string Library = "VelociTex.Core.Native";
    static NativeInterop()
    {
        // Source-tree test runs keep the native build under artifacts/core-native,
        // while NuGet places it under runtimes/win-x64/native. Resolve both without
        // requiring callers to modify PATH or load the DLL manually.
        try
        { NativeLibrary.SetDllImportResolver(typeof(NativeInterop).Assembly, ResolveLibrary); }
        catch (InvalidOperationException) { }
    }

    private static nint ResolveLibrary(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, Library, StringComparison.Ordinal))
            return 0;
        string fileName = OperatingSystem.IsWindows() ? "VelociTex.Core.Native.dll" :
            OperatingSystem.IsMacOS() ? "libVelociTex.Core.Native.dylib" : "libVelociTex.Core.Native.so";
        string? configured = Environment.GetEnvironmentVariable("VELOCITEX_NATIVE_DIR");
        if (!string.IsNullOrWhiteSpace(configured) && TryLoad(configured, fileName, out nint handle))
            return handle;
        if (TryLoad(AppContext.BaseDirectory, fileName, out handle))
            return handle;

        string? current = AppContext.BaseDirectory;
        // Test and sample outputs are nested several directories below the
        // repository root; keep walking until the filesystem root.
        for (int i = 0; i < 12 && current is not null; i++)
        {
            string candidate = System.IO.Path.Combine(current, "artifacts", "core-native");
            if (TryLoad(candidate, fileName, out handle))
                return handle;
            current = Directory.GetParent(current.TrimEnd(System.IO.Path.DirectorySeparatorChar, System.IO.Path.AltDirectorySeparatorChar))?.FullName;
        }
        return 0;
    }

    private static bool TryLoad(string directory, string fileName, out nint handle)
        => NativeLibrary.TryLoad(System.IO.Path.Combine(directory, fileName), out handle);

    [StructLayout(LayoutKind.Sequential)] internal struct String16 { internal char* Data; internal int Length; internal readonly string Value => Length == 0 ? string.Empty : new(Data, 0, Length); internal readonly string? OptionalValue => Data == null ? null : Value; }
    [StructLayout(LayoutKind.Sequential)] internal struct String8 { internal byte* Data; internal int Length; internal readonly string Value => Encoding.UTF8.GetString(new ReadOnlySpan<byte>(Data, Length)); }
    [StructLayout(LayoutKind.Sequential)] internal struct Diagnostic { internal int Code; internal SourceRange Range; internal String16 Message; internal readonly MathDiagnostic Value => new((MathDiagnosticCode)Code, Range, Message.Value); }
    [StructLayout(LayoutKind.Sequential)] internal struct Warning { internal String16 Code; internal SourceRange Range; internal String16 Message; internal readonly MathWarning Value => new(Code.Value, Range, Message.Value); }
    [StructLayout(LayoutKind.Sequential)] internal struct Macro { internal String16 Name, Body; }
    [StructLayout(LayoutKind.Sequential)] internal struct Path { internal MathPathSegment* Segments; internal int SegmentCount; internal double X, Y, ScaleX, ScaleY; internal MathBounds ClipBounds; internal readonly MathPath Value => new(new ReadOnlySpan<MathPathSegment>(Segments, SegmentCount).ToArray(), X, Y, ScaleX, ScaleY, ClipBounds); }
    [StructLayout(LayoutKind.Sequential)] internal struct Shape { internal double Advance; internal MathPathSegment* Segments; internal int SegmentCount; internal MathBounds InkBounds; internal String16 FontFamily; }
    [StructLayout(LayoutKind.Sequential)] internal struct RunShape { internal double Advance; internal Path* Paths; internal int PathCount; internal MathBounds InkBounds; }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Options
    {
        internal uint StructSize; internal double FontSize;
        internal int DisplayMode, MaxInputLength, MaxNodes, MaxNesting, MaxExpansion, MaxTokens;
        internal double MaxSize, MinRuleThickness;
        internal int ColorIsTextColor, GlobalGroup, Leqno, Fleqn;
        internal double DisplayWidth; internal int EquationNumber, StrictMode;
        internal Macro* Macros; internal int MacroCount; internal nint Session, CallbackContext;
        internal delegate* unmanaged[Cdecl]<nint, Warning*, int> StrictHandler;
        internal delegate* unmanaged[Cdecl]<nint, int, int, Shape*, int> ShapeHandler;
        internal delegate* unmanaged[Cdecl]<nint, String16, int, RunShape*, int> RunHandler;
        internal int* CancellationFlag;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct State { internal int HasColor; internal byte R, G, B, A; internal MathTransform Transform; internal readonly MathPaintState Value => new(HasColor != 0 ? new MathColor(R, G, B, A) : null, Transform); }
    [StructLayout(LayoutKind.Sequential)] internal struct TextGlyph { internal int CodePoint; internal double X, Y, FontSize; internal String16 FontFamily; internal readonly MathTextGlyph Value => new(CodePoint, X, Y, FontSize, FontFamily.Value); }
    [StructLayout(LayoutKind.Sequential)] internal struct TextRun { internal String16 Text; internal double X, Y, FontSize; internal int FirstPath, PathCount; internal double Advance; internal readonly MathTextRun Value => new(Text.Value, X, Y, FontSize, FirstPath, PathCount, Advance); }
    [StructLayout(LayoutKind.Sequential)] internal struct Length { internal double Number; internal int Unit; internal readonly TexLength Value => new(Number, (TexUnit)Unit); }
    [StructLayout(LayoutKind.Sequential)]
    internal struct Node
    {
        internal int Kind; internal SourceRange Range;
        internal int FirstChild, NextSibling, SecondChild, ThirdChild, FourthChild, CodePoint, OriginalCodePoint, FontIdOverride;
        internal int Atom, Font, SymbolFont, Style, TextMode; internal double Value, Value2, Value3;
        internal String16 Text, Alignment, Text2, Text3; internal Length Length1, Length2, Length3;
        internal int LeftDelimiter, RightDelimiter, ColumnCount, RuleAbove, RuleBelow, HasBar, Limits, AlwaysLimits, NeverLimits, IsMathOrd;
        internal readonly SemanticNode ToManaged() => new((NodeKind)Kind, Range) { FirstChild = FirstChild, NextSibling = NextSibling, SecondChild = SecondChild, ThirdChild = ThirdChild, FourthChild = FourthChild, CodePoint = CodePoint, OriginalCodePoint = OriginalCodePoint, FontIdOverride = FontIdOverride, Atom = (MathAtom)Atom, Font = (MathFont)Font, SymbolFont = (MathFont)SymbolFont, Style = (MathStyle)Style, TextMode = TextMode != 0, Value = Value, Value2 = Value2, Value3 = Value3, Text = Text.OptionalValue, Alignment = Alignment.OptionalValue, Text2 = Text2.OptionalValue, Text3 = Text3.OptionalValue, Length1 = Length1.Value, Length2 = Length2.Value, Length3 = Length3.Value, LeftDelimiter = LeftDelimiter, RightDelimiter = RightDelimiter, ColumnCount = ColumnCount, RuleAbove = RuleAbove, RuleBelow = RuleBelow, HasBar = HasBar != 0, Limits = Limits != 0, AlwaysLimits = AlwaysLimits != 0, NeverLimits = NeverLimits != 0, IsMathOrd = IsMathOrd != 0 };
    }
    [StructLayout(LayoutKind.Sequential)]
    internal struct View
    {
        internal double Width, Height, Baseline; internal MathBounds InkBounds;
        internal MathGlyph* Glyphs; internal int GlyphCount; internal MathRule* Rules; internal int RuleCount;
        internal MathPaintCommand* PaintCommands; internal int PaintCount;
        internal int PathCount, StateCount, TextGlyphCount, TextRunCount, WarningCount, NodeCount, Root;
    }
    [LibraryImport(Library, EntryPoint = "vt_core_workspace_create")] internal static partial nint WorkspaceCreate();
    [LibraryImport(Library, EntryPoint = "vt_core_workspace_clear")] internal static partial int WorkspaceClear(nint workspace);
    [LibraryImport(Library, EntryPoint = "vt_core_workspace_destroy")] internal static partial void WorkspaceDestroy(nint workspace);
    [LibraryImport(Library, EntryPoint = "vt_core_session_create")] internal static partial nint SessionCreate();
    [LibraryImport(Library, EntryPoint = "vt_core_session_destroy")] internal static partial void SessionDestroy(nint session);
    [LibraryImport(Library, EntryPoint = "vt_core_compile")] internal static partial int Compile(nint workspace, String16 source, Options* options, int withLayout, out nint result, out Diagnostic diagnostic);
    [LibraryImport(Library, EntryPoint = "vt_core_present")] internal static partial int Present(nint workspace, String16 source, Options* options, int renderErrors, uint errorRgba, out nint result, out Diagnostic diagnostic, out int errorFallback);
    [LibraryImport(Library, EntryPoint = "vt_core_result_destroy")] internal static partial void ResultDestroy(nint result);
    [LibraryImport(Library, EntryPoint = "vt_core_result_view")] internal static partial int ResultView(nint result, out View view);
    [LibraryImport(Library, EntryPoint = "vt_core_result_path")] internal static partial int ResultPath(nint result, int index, out Path path);
    [LibraryImport(Library, EntryPoint = "vt_core_result_state")] internal static partial int ResultState(nint result, int index, out State state);
    [LibraryImport(Library, EntryPoint = "vt_core_result_text_glyph")] internal static partial int ResultTextGlyph(nint result, int index, out TextGlyph glyph);
    [LibraryImport(Library, EntryPoint = "vt_core_result_text_run")] internal static partial int ResultTextRun(nint result, int index, out TextRun run);
    [LibraryImport(Library, EntryPoint = "vt_core_result_warning")] internal static partial int ResultWarning(nint result, int index, out Warning warning);
    [LibraryImport(Library, EntryPoint = "vt_core_result_node")] internal static partial int ResultNode(nint result, int index, out Node node);
    [LibraryImport(Library, EntryPoint = "vt_core_result_mathml")] internal static partial int ResultMathMl(nint result, int annotation, out String8 output);
    [LibraryImport(Library, EntryPoint = "vt_core_parse_color")] internal static partial int ParseColor(String16 text, out uint rgba);

    internal static void Check(int status)
    {
        if (status == 0)
            return;
        if (status == 4)
            throw new OutOfMemoryException("The native formula engine could not allocate memory.");
        throw new InvalidOperationException($"The native formula engine returned ABI status {status}.");
    }
    internal static string MathMl(NativeResultHandle handle, bool annotation)
    {
        Check(ResultMathMl(handle.DangerousGetHandle(), annotation ? 1 : 0, out var value));
        string result = value.Value;
        GC.KeepAlive(handle);
        return result;
    }
    internal static SemanticNode[] Nodes(NativeResultHandle handle, in View view)
    {
        var nodes = new SemanticNode[view.NodeCount];
        for (int i = 0; i < nodes.Length; i++)
        { Check(ResultNode(handle.DangerousGetHandle(), i, out var node)); nodes[i] = node.ToManaged(); }
        GC.KeepAlive(handle);
        return nodes;
    }
    internal static MathWarning[] Warnings(NativeResultHandle handle, in View view)
    {
        var values = new MathWarning[view.WarningCount];
        for (int i = 0; i < values.Length; i++)
        { Check(ResultWarning(handle.DangerousGetHandle(), i, out var value)); values[i] = value.Value; }
        GC.KeepAlive(handle);
        return values;
    }
    internal static LayoutOutput Layout(NativeResultHandle handle, in View view)
    {
        var glyphs = new ReadOnlySpan<MathGlyph>(view.Glyphs, view.GlyphCount).ToArray();
        var rules = new ReadOnlySpan<MathRule>(view.Rules, view.RuleCount).ToArray();
        var commands = new ReadOnlySpan<MathPaintCommand>(view.PaintCommands, view.PaintCount).ToArray();
        var paths = new MathPath[view.PathCount];
        for (int i = 0; i < paths.Length; i++)
        { Check(ResultPath(handle.DangerousGetHandle(), i, out var value)); paths[i] = value.Value; }
        var states = new MathPaintState[view.StateCount];
        for (int i = 0; i < states.Length; i++)
        { Check(ResultState(handle.DangerousGetHandle(), i, out var value)); states[i] = value.Value; }
        var textGlyphs = new MathTextGlyph[view.TextGlyphCount];
        for (int i = 0; i < textGlyphs.Length; i++)
        { Check(ResultTextGlyph(handle.DangerousGetHandle(), i, out var value)); textGlyphs[i] = value.Value; }
        var runs = new MathTextRun[view.TextRunCount];
        for (int i = 0; i < runs.Length; i++)
        { Check(ResultTextRun(handle.DangerousGetHandle(), i, out var value)); runs[i] = value.Value; }
        GC.KeepAlive(handle);
        return new(glyphs, rules, paths, new(view.Width, view.Height), view.Baseline, view.InkBounds, textGlyphs, commands, states, runs);
    }

    internal sealed class Invocation : IDisposable
    {
        internal Options Options;
        private readonly List<GCHandle> _pins = [];
        private readonly MathOptions _managed;
        private readonly Callbacks? _callbacks;
        private GCHandle _callbackHandle;
        private readonly CancellationState? _cancellation;
        internal Invocation(in MathOptions options, CancellationToken token)
        {
            _managed = options;
            try
            {
                Options = new() { StructSize = (uint)sizeof(Options), FontSize = options.FontSize, DisplayMode = options.DisplayMode ? 1 : 0, MaxInputLength = options.MaxInputLength, MaxNodes = options.MaxNodes, MaxNesting = options.MaxNesting, MaxExpansion = options.MaxExpansion, MaxTokens = options.MaxTokens, MaxSize = options.MaxSize, MinRuleThickness = options.MinRuleThickness, ColorIsTextColor = options.ColorIsTextColor ? 1 : 0, GlobalGroup = options.GlobalGroup ? 1 : 0, Leqno = options.Leqno ? 1 : 0, Fleqn = options.Fleqn ? 1 : 0, DisplayWidth = options.DisplayWidth, EquationNumber = options.EquationNumber, StrictMode = (int)options.StrictMode, Session = options.Session?.NativeHandle.DangerousGetHandle() ?? 0 };
                if (options.Macros is { Count: > 0 } macros)
                {
                    var entries = new Macro[macros.Count];
                    int index = 0;
                    foreach (var pair in macros)
                        entries[index++] = new() { Name = PinString(pair.Key), Body = PinString(pair.Value) };
                    Options.Macros = (Macro*)Pin(entries);
                    Options.MacroCount = entries.Length;
                }
                if (options.StrictHandler is not null || options.TextShaper is not null)
                {
                    _callbacks = new(options);
                    _callbackHandle = GCHandle.Alloc(_callbacks);
                    Options.CallbackContext = GCHandle.ToIntPtr(_callbackHandle);
                    if (options.StrictHandler is not null)
                        Options.StrictHandler = &StrictCallback;
                    if (options.TextShaper is not null)
                        Options.ShapeHandler = &ShapeCallback;
                    if (options.TextShaper is IMathTextRunShaper)
                        Options.RunHandler = &RunCallback;
                }
                if (token.CanBeCanceled)
                { _cancellation = new(token); Options.CancellationFlag = _cancellation.Flag; }
            }
            catch { Dispose(); throw; }
        }
        private void* Pin(object value) { var pin = GCHandle.Alloc(value, GCHandleType.Pinned); _pins.Add(pin); return (void*)pin.AddrOfPinnedObject(); }
        private String16 PinString(string value) { ArgumentNullException.ThrowIfNull(value); return new() { Data = (char*)Pin(value), Length = value.Length }; }
        internal void RethrowCallback() => _callbacks?.Exception?.Throw();
        public void Dispose() { _cancellation?.Dispose(); _callbacks?.Dispose(); if (_callbackHandle.IsAllocated) _callbackHandle.Free(); foreach (var pin in _pins) pin.Free(); _pins.Clear(); GC.KeepAlive(_managed.Session); GC.KeepAlive(_managed.TextShaper); }
    }
    private sealed class CancellationState : IDisposable
    {
        internal readonly int* Flag;
        private readonly CancellationTokenRegistration _registration;
        internal CancellationState(CancellationToken token)
        {
            Flag = (int*)NativeMemory.AllocZeroed((nuint)sizeof(int));
            if (Flag == null)
                throw new OutOfMemoryException();
            try
            { _registration = token.UnsafeRegister(static state => { var value = (CancellationState)state!; Volatile.Write(ref *value.Flag, 1); }, this); }
            catch { NativeMemory.Free(Flag); throw; }
        }
        public void Dispose() { _registration.Dispose(); NativeMemory.Free(Flag); }
    }
    private sealed class Callbacks : IDisposable
    {
        internal readonly MathOptions Options; internal ExceptionDispatchInfo? Exception;
        private readonly List<GCHandle> _pins = [];
        internal Callbacks(MathOptions options) => Options = options;
        internal void ClearPins() { foreach (var pin in _pins) pin.Free(); _pins.Clear(); }
        internal void* Pin(object value) { var pin = GCHandle.Alloc(value, GCHandleType.Pinned); _pins.Add(pin); return (void*)pin.AddrOfPinnedObject(); }
        internal MathPathSegment* Segments(ReadOnlyMemory<MathPathSegment> memory) { var array = memory.ToArray(); return (MathPathSegment*)Pin(array); }
        internal String16 String(string value) => new() { Data = (char*)Pin(value), Length = value.Length };
        public void Dispose() => ClearPins();
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int StrictCallback(nint context, Warning* warning)
    {
        var state = (Callbacks)GCHandle.FromIntPtr(context).Target!;
        try
        { return (int)state.Options.StrictHandler!(warning->Value); }
        catch (Exception e) { state.Exception = ExceptionDispatchInfo.Capture(e); return -1; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int ShapeCallback(nint context, int codePoint, int font, Shape* shape)
    {
        var state = (Callbacks)GCHandle.FromIntPtr(context).Target!;
        try
        { state.ClearPins(); if (!state.Options.TextShaper!.TryShape(codePoint, (MathFont)font, out var value)) return 0; *shape = new() { Advance = value.Advance, Segments = state.Segments(value.Segments), SegmentCount = value.Segments.Length, InkBounds = value.InkBounds, FontFamily = state.String(value.FontFamily) }; return 1; }
        catch (Exception e) { state.Exception = ExceptionDispatchInfo.Capture(e); return -1; }
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvCdecl)])]
    private static int RunCallback(nint context, String16 text, int font, RunShape* shape)
    {
        var state = (Callbacks)GCHandle.FromIntPtr(context).Target!;
        try
        {
            state.ClearPins();
            if (!((IMathTextRunShaper)state.Options.TextShaper!).TryShapeRun(text.Value, (MathFont)font, out var value))
                return 0;
            var paths = new Path[value.Paths.Length];
            for (int i = 0; i < paths.Length; i++)
            { var p = value.Paths.Span[i]; paths[i] = new() { Segments = state.Segments(p.Segments), SegmentCount = p.Segments.Length, X = p.X, Y = p.Y, ScaleX = p.ScaleX, ScaleY = p.ScaleY, ClipBounds = p.ClipBounds }; }
            *shape = new() { Advance = value.Advance, Paths = (Path*)state.Pin(paths), PathCount = paths.Length, InkBounds = value.InkBounds };
            return 1;
        }
        catch (Exception e) { state.Exception = ExceptionDispatchInfo.Capture(e); return -1; }
    }
}
internal sealed class NativeWorkspaceHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    internal NativeWorkspaceHandle() : base(true) { SetHandle(NativeInterop.WorkspaceCreate()); if (IsInvalid) throw new OutOfMemoryException(); }
    protected override bool ReleaseHandle() { NativeInterop.WorkspaceDestroy(handle); return true; }
}
internal sealed class NativeSessionHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    internal NativeSessionHandle() : base(true) { SetHandle(NativeInterop.SessionCreate()); if (IsInvalid) throw new OutOfMemoryException(); }
    protected override bool ReleaseHandle() { NativeInterop.SessionDestroy(handle); return true; }
}
internal sealed class NativeResultHandle : SafeHandleZeroOrMinusOneIsInvalid
{
    internal NativeResultHandle(nint value) : base(true) => SetHandle(value);
    protected override bool ReleaseHandle() { NativeInterop.ResultDestroy(handle); return true; }
}
