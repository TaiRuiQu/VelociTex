using System.Runtime.InteropServices;
using Microsoft.UI.Composition;
using VelociTex.Rendering.Direct2D;

namespace VelociTex.WinUI;

// A page typically displays many MathViews on one UI thread. The DirectWrite
// faces, WARP device and CompositionGraphicsDevice are formula-independent and
// expensive; share them until the last attached view releases the pool.
internal static unsafe class MathViewSharedResources
{
    private static WindowsMathRenderer? _renderer;
    private static nint _device;
    private static CompositionGraphicsDevice? _graphics;
    private static int _users;
    private static int _threadId = -1;

    internal static (WindowsMathRenderer Renderer, CompositionGraphicsDevice Graphics) Acquire(Compositor compositor)
    {
        int thread = Environment.CurrentManagedThreadId;
        if (_renderer is not null && thread != _threadId) throw new InvalidOperationException("MathView resources are bound to the UI thread that created them.");
        if (_renderer is null)
        {
            WindowsMathRenderer renderer = new();
            nint device = 0;
            CompositionGraphicsDevice? graphics = null;
            nint compositorPointer = 0, interop = 0, graphicsPointer = 0;
            try
            {
                device = WindowsMathRenderer.CreateWarpD2DDevice();
                compositorPointer = WinRT.MarshalInspectable<Compositor>.FromManaged(compositor);
                Guid iid = new("FAB19398-6D19-4D8A-B752-8F096C396069");
                Check(Marshal.QueryInterface(compositorPointer, in iid, out interop));
                Check(((delegate* unmanaged[Stdcall]<nint, nint, nint*, int>)(*(nint**)interop)[3])(interop, device, &graphicsPointer));
                graphics = WinRT.MarshalInspectable<CompositionGraphicsDevice>.FromAbi(graphicsPointer);
            }
            catch
            {
                graphics?.Dispose();
                if (device != 0) { nint value = device; Release(ref value); }
                renderer.Dispose();
                throw;
            }
            finally
            {
                if (graphicsPointer != 0) { nint value = graphicsPointer; Release(ref value); }
                if (interop != 0) { nint value = interop; Release(ref value); }
                if (compositorPointer != 0) { nint value = compositorPointer; Release(ref value); }
            }
            _renderer = renderer; _device = device; _graphics = graphics; _threadId = thread;
        }
        _users++;
        return (_renderer, _graphics!);
    }

    internal static void Release()
    {
        if (_users == 0) return;
        if (--_users != 0) return;
        _graphics?.Dispose(); _graphics = null;
        _renderer?.Dispose(); _renderer = null;
        if (_device != 0) { nint value = _device; Release(ref value); }
        _device = 0; _threadId = -1;
    }

    private static void Check(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
    private static void Release(ref nint pointer) { if (pointer != 0) { Marshal.Release(pointer); pointer = 0; } }
}
