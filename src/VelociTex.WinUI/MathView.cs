using System.Numerics;
using System.Runtime.InteropServices;
using Microsoft.Graphics.DirectX;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;
using Microsoft.UI.Xaml.Automation.Peers;
using Microsoft.UI.Xaml.Automation.Provider;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using Microsoft.UI.Xaml.Input;
using VelociTex.Rendering.Direct2D;
using Windows.ApplicationModel.DataTransfer;
using Windows.System;

namespace VelociTex.WinUI;

/// <summary>Displays one independently owned Core result on a native Composition surface.</summary>
public sealed unsafe partial class MathView : Grid, IDisposable
{
    // Register as object because MathDocument is a plain managed immutable type, not a WinRT runtime class.
    /// <summary>Identifies the precompiled <see cref="Result"/> binding.</summary>
    public static readonly DependencyProperty ResultProperty = DependencyProperty.Register(nameof(Result), typeof(object), typeof(MathView), new PropertyMetadata(null, OnResultChanged));
    /// <summary>Identifies the optional human-readable accessibility description.</summary>
    public static readonly DependencyProperty AccessibleDescriptionProperty = DependencyProperty.Register(nameof(AccessibleDescription), typeof(string), typeof(MathView), new PropertyMetadata(string.Empty, OnDescriptionChanged));
    private WindowsMathRenderer? _renderer;
    private PreparedMath? _prepared;
    private CompositionGraphicsDevice? _graphics;
    private CompositionDrawingSurface? _surface;
    private CompositionSurfaceBrush? _brush;
    private SpriteVisual? _visual;
    private XamlRoot? _observedRoot;
    private double _scale = 1;
    private int _surfacePixelWidth, _surfacePixelHeight;
    private MathDrawingColor? _color;
    private bool _disposed;
    /// <summary>Gets or binds an independently owned, precompiled formula. Null clears the display.</summary>
    public MathDocument? Result { get => (MathDocument?)GetValue(ResultProperty); set => SetValue(ResultProperty, value); }
    /// <summary>Gets the formula baseline from the top of this control, including ink padding, in DIP.</summary>
    public double BaselineDip => Result is null ? 0 : Result.Baseline + 4 - Math.Min(0, Result.InkBounds.Y);
    /// <summary>Gets or sets the drawing color. Initially follows the actual light or dark theme.</summary>
    public MathDrawingColor FormulaColor { get => _color ?? (ActualTheme == ElementTheme.Dark ? MathDrawingColor.White : MathDrawingColor.Black); set { _color = value; if (IsLoaded) Redraw(); } }
    /// <summary>Gets or binds a spoken description. The complete original source remains the read-only UIA value.</summary>
    public string AccessibleDescription { get => (string)GetValue(AccessibleDescriptionProperty); set => SetValue(AccessibleDescriptionProperty, value ?? string.Empty); }

    /// <summary>Creates a focusable native formula display with Control+C source copying.</summary>
    public MathView()
    {
        HorizontalAlignment = HorizontalAlignment.Left;
        IsTabStop = true;
        KeyboardAccelerator copy = new() { Key = VirtualKey.C, Modifiers = VirtualKeyModifiers.Control, ScopeOwner = this };
        copy.Invoked += (_, args) => { if (Result is not null) { CopySourceToClipboard(); args.Handled = true; } };
        KeyboardAccelerators.Add(copy);
        ActualThemeChanged += (_, _) => { if (_color is null && IsLoaded) Redraw(); };
        Loaded += OnLoaded;
        Unloaded += OnUnloaded;
    }
    /// <summary>Returns drawing color to automatic light/dark theme tracking.</summary>
    public void UseThemeColor() { _color = null; if (IsLoaded) Redraw(); }
    /// <summary>Copies the complete original formula to the system clipboard on the UI thread.</summary>
    /// <exception cref="InvalidOperationException">There is no compiled result to copy.</exception>
    public void CopySourceToClipboard()
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        MathDocument document = Result ?? throw new InvalidOperationException("There is no formula to copy.");
        DataPackage data = new();
        data.SetText(document.Source);
        Clipboard.SetContent(data);
    }
    private static void OnResultChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        MathView view = (MathView)sender;
        view.ResizeToResult();
        if (view.IsLoaded)
            view.Rebuild();
        if (FrameworkElementAutomationPeer.FromElement(view) is MathViewAutomationPeer peer)
            peer.ResultChanged(args.OldValue as MathDocument, args.NewValue as MathDocument);
    }
    private static void OnDescriptionChanged(DependencyObject sender, DependencyPropertyChangedEventArgs args)
    {
        MathView view = (MathView)sender;
        if (string.IsNullOrEmpty(AutomationProperties.GetName(view)) && FrameworkElementAutomationPeer.FromElement(view) is MathViewAutomationPeer peer)
        {
            string source = view.Result?.Source ?? string.Empty;
            string previous = string.IsNullOrEmpty(args.OldValue as string) ? source : (string)args.OldValue;
            string current = string.IsNullOrEmpty(args.NewValue as string) ? source : (string)args.NewValue;
            peer.RaisePropertyChangedEvent(AutomationElementIdentifiers.NameProperty, previous, current);
        }
    }
    private void ResizeToResult()
    {
        MathDocument? document = Result;
        if (document is null)
        { Width = Height = 0; return; }
        Width = Math.Max(document.LogicalSize.Width, document.InkBounds.X + document.InkBounds.Width) - Math.Min(0, document.InkBounds.X) + 8;
        Height = Math.Max(document.LogicalSize.Height, document.InkBounds.Y + document.InkBounds.Height) - Math.Min(0, document.InkBounds.Y) + 8;
    }
    private void OnLoaded(object sender, RoutedEventArgs args)
    {
        _observedRoot = XamlRoot;
        _observedRoot?.Changed += OnRootChanged;
        ResizeToResult();
        Rebuild();
    }
    private void OnUnloaded(object sender, RoutedEventArgs args)
    {
        _observedRoot?.Changed -= OnRootChanged;
        _observedRoot = null;
        Release();
    }
    private void OnRootChanged(XamlRoot sender, XamlRootChangedEventArgs args)
    {
        if (Math.Abs(sender.RasterizationScale - _scale) > .0001)
            Rebuild();
    }
    /// <summary>Rebuilds the displayed formula while retaining the expensive native devices.</summary>
    public void Rebuild(double? scaleOverride = null)
    {
        ObjectDisposedException.ThrowIf(_disposed, this);
        MathDocument? document = Result;
        if (document is null || !IsLoaded)
            return;
        _scale = scaleOverride ?? XamlRoot?.RasterizationScale ?? 1;
        if (!double.IsFinite(_scale) || _scale <= 0 || _scale > 8)
            throw new ArgumentOutOfRangeException(nameof(scaleOverride));
        double pixelWidth = Math.Ceiling(Width * _scale), pixelHeight = Math.Ceiling(Height * _scale);
        if (pixelWidth > 8192 || pixelHeight > 8192 || pixelWidth * pixelHeight > 4_194_304)
            throw new InvalidOperationException("The native surface exceeds the 8192 pixel side or 4 megapixel limit.");
        try
        {
            // Renderer, WARP device and graphics device are independent of one
            // formula. Reuse them and only recreate the fixed-size surface when
            // the logical size actually changes; this keeps result swaps out of
            // the font-extraction, device-creation and DPI paths.
            EnsureCore();
            EnsureSurface((int)pixelWidth, (int)pixelHeight);
            _prepared?.Dispose();
            _prepared = _renderer!.Prepare(document);
            _visual!.Size = new((float)Width, (float)Height);
            Redraw();
        }
        catch { Release(); throw; }
    }
    private void EnsureCore()
    {
        if (_renderer is not null)
            return;
        Compositor compositor = ElementCompositionPreview.GetElementVisual(this).Compositor;
        var shared = MathViewSharedResources.Acquire(compositor);
        _renderer = shared.Renderer;
        _graphics = shared.Graphics;
        try
        {
            _visual = compositor.CreateSpriteVisual();
            ElementCompositionPreview.SetElementChildVisual(this, _visual);
        }
        catch
        {
            _renderer = null;
            _graphics = null;
            MathViewSharedResources.Release();
            throw;
        }
    }
    private void EnsureSurface(int pixelWidth, int pixelHeight)
    {
        if (_surface is not null && _surfacePixelWidth == pixelWidth && _surfacePixelHeight == pixelHeight)
            return;
        Compositor compositor = ElementCompositionPreview.GetElementVisual(this).Compositor;
        CompositionDrawingSurface surface = _graphics!.CreateDrawingSurface(new Windows.Foundation.Size(pixelWidth, pixelHeight), DirectXPixelFormat.B8G8R8A8UIntNormalized, DirectXAlphaMode.Premultiplied);
        CompositionSurfaceBrush brush = compositor.CreateSurfaceBrush(surface);
        CompositionSurfaceBrush? oldBrush = _brush;
        CompositionDrawingSurface? oldSurface = _surface;
        _surface = surface;
        _brush = brush;
        _surfacePixelWidth = pixelWidth;
        _surfacePixelHeight = pixelHeight;
        _visual!.Brush = brush;
        oldBrush?.Dispose();
        oldSurface?.Dispose();
    }
    /// <summary>Redraws the retained result using the current color and surface density.</summary>
    public void Redraw()
    {
        if (_surface is null || _renderer is null || _prepared is null)
            return;
        nint surfacePointer = WinRT.MarshalInspectable<CompositionDrawingSurface>.FromManaged(_surface), interop = 0, context = 0;
        bool begun = false;
        try
        {
            Guid iid = new("2D6355C2-AD57-4EAE-92E4-4C3EFF65D578");
            Check(Marshal.QueryInterface(surfacePointer, in iid, out interop));
            iid = new("E8F7FE7A-191C-466D-AD95-975678BDA998");
            Point offset;
            Check(((delegate* unmanaged[Stdcall]<nint, void*, Guid*, nint*, Point*, int>)(*(nint**)interop)[3])(interop, null, &iid, &context, &offset));
            begun = true;
            nint* table = *(nint**)context;
            ((delegate* unmanaged[Stdcall]<nint, float, float, void>)table[51])(context, (float)(96 * _scale), (float)(96 * _scale));
            Matrix transform = new() { M11 = 1, M22 = 1, Dx = (float)(offset.X / _scale), Dy = (float)(offset.Y / _scale) };
            ((delegate* unmanaged[Stdcall]<nint, Matrix*, void>)table[30])(context, &transform);
            ((delegate* unmanaged[Stdcall]<nint, nint, void>)table[47])(context, 0);
            MathDocument document = _prepared.Document;
            _renderer.Draw(_prepared, context, FormulaColor, 4 - Math.Min(0, document.InkBounds.X), 4 - Math.Min(0, document.InkBounds.Y));
        }
        finally
        {
            Free(ref context);
            try
            { if (begun) Check(((delegate* unmanaged[Stdcall]<nint, int>)(*(nint**)interop)[4])(interop)); }
            finally { Free(ref interop); Free(ref surfacePointer); }
        }
    }
    /// <inheritdoc/>
    protected override AutomationPeer OnCreateAutomationPeer() => new MathViewAutomationPeer(this);
    private void Release()
    {
        ElementCompositionPreview.SetElementChildVisual(this, null);
        _visual?.Dispose();
        _visual = null;
        _brush?.Dispose();
        _brush = null;
        _surface?.Dispose();
        _surface = null;
        _graphics = null;
        _prepared?.Dispose();
        _prepared = null;
        if (_renderer is not null)
        { _renderer = null; MathViewSharedResources.Release(); }
        _surfacePixelWidth = _surfacePixelHeight = 0;
    }
    /// <summary>Releases owned native resources on the creating UI thread. Subsequent drawing is invalid.</summary>
    public void Dispose()
    {
        if (_disposed)
            return;
        _observedRoot?.Changed -= OnRootChanged;
        _observedRoot = null;
        Release();
        _disposed = true;
    }
    private static void Check(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
    private static void Free(ref nint pointer) { if (pointer != 0) { Marshal.Release(pointer); pointer = 0; } }
    [StructLayout(LayoutKind.Sequential)] private struct Point { internal int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct Matrix { internal float M11, M12, M21, M22, Dx, Dy; }
    private sealed partial class MathViewAutomationPeer(MathView owner) : FrameworkElementAutomationPeer(owner), IValueProvider
    {
        protected override string GetClassNameCore() => nameof(MathView);
        protected override AutomationControlType GetAutomationControlTypeCore() => AutomationControlType.Text;
        protected override string GetNameCore()
        {
            string explicitName = base.GetNameCore();
            return !string.IsNullOrEmpty(explicitName) ? explicitName : !string.IsNullOrEmpty(owner.AccessibleDescription) ? owner.AccessibleDescription : Value;
        }
        protected override string GetHelpTextCore() => "Read-only mathematical formula. Press Control+C to copy the complete TeX source.";
        protected override object GetPatternCore(PatternInterface patternInterface) => patternInterface == PatternInterface.Value ? this : base.GetPatternCore(patternInterface);
        public bool IsReadOnly => true;
        public string Value => owner.Result?.Source ?? string.Empty;
        public void SetValue(string value) => throw new InvalidOperationException("Formula results are read-only.");
        internal void ResultChanged(MathDocument? previous, MathDocument? current)
        {
            RaisePropertyChangedEvent(ValuePatternIdentifiers.ValueProperty, previous?.Source ?? string.Empty, current?.Source ?? string.Empty);
            if (string.IsNullOrEmpty(owner.AccessibleDescription) && string.IsNullOrEmpty(AutomationProperties.GetName(owner)))
                RaisePropertyChangedEvent(AutomationElementIdentifiers.NameProperty, previous?.Source ?? string.Empty, current?.Source ?? string.Empty);
        }
    }
}
