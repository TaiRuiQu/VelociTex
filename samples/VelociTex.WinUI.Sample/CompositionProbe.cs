using System.Numerics;
using System.Runtime.InteropServices;
using Microsoft.UI.Composition;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Hosting;
using VelociTex.Rendering.Direct2D;
using Microsoft.Graphics.DirectX;

namespace VelociTex.WinUI.Sample;

// P0 hard-coded drawing data only. This is not the MathView control or a parser.
internal sealed unsafe partial class CompositionProbe : Grid, IDisposable
{
    private NativeProbeRenderer? renderer;
    private CompositionGraphicsDevice? graphics;
    private CompositionDrawingSurface? surface;
    private CompositionSurfaceBrush? brush;
    private SpriteVisual? visual;
    private nint device;
    private double rasterScale = 1;
    public ProbeTheme Theme { get; set; }
    public bool IsParagraph { get; init; }
    public InlineParagraphMetrics ParagraphMetrics { get; private set; }

    public CompositionProbe()
    {
        Width = NativeProbeRenderer.Width;
        Height = NativeProbeRenderer.Height;
        Loaded += (_, _) => Rebuild();
        Unloaded += (_, _) => Release();
    }

    public void Rebuild(double scale = 1)
    {
        Release();
        rasterScale = scale;
        if (IsParagraph) Height = NativeInlineBaselineProbe.MeasureParagraph((float)Width).Height;
        try
        {
        renderer = new NativeProbeRenderer(Path.Combine(AppContext.BaseDirectory, "Fonts"));
        var compositor = ElementCompositionPreview.GetElementVisual(this).Compositor;
        device = NativeProbeRenderer.CreateWarpD2DDevice();
        nint compositorPointer = WinRT.MarshalInspectable<Compositor>.FromManaged(compositor);
        nint interop = 0, graphicsPointer = 0;
        try
        {
            Guid iid = new("FAB19398-6D19-4D8A-B752-8F096C396069");
            Check(Marshal.QueryInterface(compositorPointer, in iid, out interop));
            Check(((delegate* unmanaged[Stdcall]<nint, nint, nint*, int>)(*(nint**)interop)[3])(interop, device, &graphicsPointer));
            graphics = WinRT.MarshalInspectable<CompositionGraphicsDevice>.FromAbi(graphicsPointer);
        }
        finally
        {
            Free(ref graphicsPointer); Free(ref interop); Free(ref compositorPointer);
        }
        surface = graphics.CreateDrawingSurface(
            new Windows.Foundation.Size(Math.Ceiling(Width * scale), Math.Ceiling(Height * scale)),
            DirectXPixelFormat.B8G8R8A8UIntNormalized, DirectXAlphaMode.Premultiplied);
        brush = compositor.CreateSurfaceBrush(surface);
        visual = compositor.CreateSpriteVisual();
        visual.Size = new Vector2((float)Width, (float)Height);
        visual.Brush = brush;
        ElementCompositionPreview.SetElementChildVisual(this, visual);
        Redraw();
        }
        catch { Release(); throw; }
    }

    public void Redraw()
    {
        if (surface is null || renderer is null) return;
        nint surfacePointer = WinRT.MarshalInspectable<CompositionDrawingSurface>.FromManaged(surface);
        nint interop = 0, context = 0;
        bool begun = false;
        try
        {
            Guid iid = new("2D6355C2-AD57-4EAE-92E4-4C3EFF65D578");
            Check(Marshal.QueryInterface(surfacePointer, in iid, out interop));
            iid = new("E8F7FE7A-191C-466D-AD95-975678BDA998");
            Point offset;
            Check(((delegate* unmanaged[Stdcall]<nint, void*, Guid*, nint*, Point*, int>)(*(nint**)interop)[3])(interop, null, &iid, &context, &offset));
            begun = true;
            var table = *(nint**)context;
            // BeginDraw returns a shared atlas context: account for its update offset.
            ((delegate* unmanaged[Stdcall]<nint, float, float, void>)table[51])(context, (float)(96 * rasterScale), (float)(96 * rasterScale));
            Matrix matrix = new() { M11 = 1, M22 = 1, Dx = offset.X / (float)rasterScale, Dy = offset.Y / (float)rasterScale };
            ((delegate* unmanaged[Stdcall]<nint, Matrix*, void>)table[30])(context, &matrix);
            float* clear = stackalloc float[4];
            clear[0] = clear[1] = clear[2] = clear[3] = 0;
            ((delegate* unmanaged[Stdcall]<nint, float*, void>)table[47])(context, clear);
            if (IsParagraph) ParagraphMetrics = NativeInlineBaselineProbe.DrawParagraph(context, (float)Width, Theme);
            else renderer.Draw(context, Theme);
        }
        finally
        {
            Free(ref context);
            try
            {
                if (begun) Check(((delegate* unmanaged[Stdcall]<nint, int>)(*(nint**)interop)[4])(interop));
            }
            finally { Free(ref interop); Free(ref surfacePointer); }
        }
    }

    private void Release()
    {
        ElementCompositionPreview.SetElementChildVisual(this, null);
        visual?.Dispose(); visual = null;
        brush?.Dispose(); brush = null;
        surface?.Dispose(); surface = null;
        graphics?.Dispose(); graphics = null;
        renderer?.Dispose(); renderer = null;
        Free(ref device);
    }
    public void Dispose() => Release();
    private static void Check(int hr) => Marshal.ThrowExceptionForHR(hr);
    private static void Free(ref nint pointer) { if (pointer != 0) { Marshal.Release(pointer); pointer = 0; } }
    [StructLayout(LayoutKind.Sequential)] private struct Point { public int X, Y; }
    [StructLayout(LayoutKind.Sequential)] private struct Matrix { public float M11, M12, M21, M22, Dx, Dy; }
}
