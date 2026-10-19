using System.Runtime.InteropServices;
using Windows.Graphics.Capture;
using Windows.Graphics.DirectX;
using Windows.Graphics.DirectX.Direct3D11;
using Windows.Graphics.Imaging;
using Windows.Storage;
using WinRT;

namespace VelociTex.WinUI.Sample;

// Captures the actual composited HWND, including the external Composition visual.
// RenderTargetBitmap omits that visual and cannot verify this probe.
internal static partial class WindowCapture
{
    public static async Task CaptureAsync(nint hwnd, string path)
    {
        using var capture = new CaptureResources(hwnd);
        var received = new TaskCompletionSource<byte[]>(TaskCreationOptions.RunContinuationsAsynchronously);
        int width = 0, height = 0;
        using var frames = Direct3D11CaptureFramePool.CreateFreeThreaded(capture.ProjectedDevice, DirectXPixelFormat.B8G8R8A8UIntNormalized, 2, capture.Item.Size);
        frames.FrameArrived += (pool, _) =>
        {
            if (received.Task.IsCompleted) return;
            try
            {
                using var frame = pool.TryGetNextFrame();
                if (frame is null) return;
                width = frame.ContentSize.Width; height = frame.ContentSize.Height;
                received.TrySetResult(capture.Read(frame.Surface, width, height));
            }
            catch (Exception error) { received.TrySetException(error); }
        };
        using var session = frames.CreateCaptureSession(capture.Item);
        session.IsCursorCaptureEnabled = false;
        session.StartCapture();
        byte[] pixels = await received.Task.WaitAsync(TimeSpan.FromSeconds(8));
        var folder = await StorageFolder.GetFolderFromPathAsync(Path.GetDirectoryName(path)!);
        var file = await folder.CreateFileAsync(Path.GetFileName(path), CreationCollisionOption.ReplaceExisting);
        using var stream = await file.OpenAsync(FileAccessMode.ReadWrite);
        var encoder = await BitmapEncoder.CreateAsync(BitmapEncoder.PngEncoderId, stream);
        encoder.SetPixelData(BitmapPixelFormat.Bgra8, BitmapAlphaMode.Premultiplied, (uint)width, (uint)height, 96, 96, pixels);
        await encoder.FlushAsync();
    }

    private sealed unsafe class CaptureResources : IDisposable
    {
        private nint device, context;
        private IDirect3DDevice? projectedDevice;
        public IDirect3DDevice ProjectedDevice => projectedDevice ?? throw new ObjectDisposedException(nameof(CaptureResources));
        public GraphicsCaptureItem Item { get; }
        public CaptureResources(nint hwnd)
        {
            try
            {
            nint createdDevice = 0, createdContext = 0;
            Check(D3D11CreateDevice(0, 5, 0, 0x20, 0, 0, 7, &createdDevice, null, &createdContext));
            device = createdDevice; context = createdContext;
            nint dxgi = 0, projected = 0;
            try
            {
                Guid iid = new("54EC77FA-1377-44E6-8C32-88FD5F44C84C");
                Check(Marshal.QueryInterface(device, in iid, out dxgi));
                Check(CreateDirect3D11DeviceFromDXGIDevice(dxgi, &projected));
                projectedDevice = MarshalInterface<IDirect3DDevice>.FromAbi(projected);
            }
            finally { Free(ref projected); Free(ref dxgi); }
            nint activation = 0, item = 0, className = 0;
            try
            {
                const string name = "Windows.Graphics.Capture.GraphicsCaptureItem";
                fixed (char* chars = name) Check(WindowsCreateString(chars, (uint)name.Length, &className));
                Guid interopId = new("3628E81B-3CAC-4C60-B7F4-23CE0E0C3356");
                Check(RoGetActivationFactory(className, &interopId, &activation));
                Guid itemId = new("79C3F95B-31F7-4EC2-A464-632EF5D30760");
                Check(((delegate* unmanaged[Stdcall]<nint, nint, Guid*, nint*, int>)(*(nint**)activation)[3])(activation, hwnd, &itemId, &item));
                Item = MarshalInspectable<GraphicsCaptureItem>.FromAbi(item);
            }
            finally { Free(ref item); Free(ref activation); if (className != 0) WindowsDeleteString(className); }
            }
            catch { Dispose(); throw; }
        }

        public byte[] Read(IDirect3DSurface surface, int width, int height)
        {
            nint surfacePointer = MarshalInterface<IDirect3DSurface>.FromManaged(surface);
            nint access = 0, texture = 0, staging = 0;
            try
            {
                Guid accessId = new("A9B3D012-3DF2-4EE3-B8D1-8695F457D3C1");
                Check(Marshal.QueryInterface(surfacePointer, in accessId, out access));
                Guid textureId = new("6F15AAF2-D208-4E89-9AB4-489535D34F9C");
                Check(((delegate* unmanaged[Stdcall]<nint, Guid*, nint*, int>)(*(nint**)access)[3])(access, &textureId, &texture));
                TextureDesc desc;
                ((delegate* unmanaged[Stdcall]<nint, TextureDesc*, void>)(*(nint**)texture)[10])(texture, &desc);
                desc.Usage = 3; desc.BindFlags = 0; desc.CpuAccess = 0x20000; desc.MiscFlags = 0;
                Check(((delegate* unmanaged[Stdcall]<nint, TextureDesc*, void*, nint*, int>)(*(nint**)device)[5])(device, &desc, null, &staging));
                ((delegate* unmanaged[Stdcall]<nint, nint, nint, void>)(*(nint**)context)[47])(context, staging, texture);
                Mapped mapped;
                Check(((delegate* unmanaged[Stdcall]<nint, nint, uint, uint, uint, Mapped*, int>)(*(nint**)context)[14])(context, staging, 0, 1, 0, &mapped));
                try
                {
                    var bytes = new byte[checked(width * height * 4)];
                    for (int row = 0; row < height; row++) Marshal.Copy(mapped.Data + (nint)(row * mapped.RowPitch), bytes, row * width * 4, width * 4);
                    return bytes;
                }
                finally { ((delegate* unmanaged[Stdcall]<nint, nint, uint, void>)(*(nint**)context)[15])(context, staging, 0); }
            }
            finally { Free(ref staging); Free(ref texture); Free(ref access); Free(ref surfacePointer); }
        }
        public void Dispose() { projectedDevice?.Dispose(); projectedDevice = null; Free(ref context); Free(ref device); }
    }

    private static void Check(int hr) => Marshal.ThrowExceptionForHR(hr);
    private static void Free(ref nint pointer) { if (pointer != 0) { Marshal.Release(pointer); pointer = 0; } }
    [StructLayout(LayoutKind.Sequential)] private struct TextureDesc { public uint Width, Height, MipLevels, ArraySize, Format, SampleCount, SampleQuality, Usage, BindFlags, CpuAccess, MiscFlags; }
    [StructLayout(LayoutKind.Sequential)] private struct Mapped { public nint Data; public uint RowPitch, DepthPitch; }
    [LibraryImport("d3d11.dll")] private static unsafe partial int D3D11CreateDevice(nint adapter, uint driverType, nint software, uint flags, nint levels, uint levelCount, uint sdk, nint* device, uint* level, nint* context);
    [LibraryImport("d3d11.dll")] private static unsafe partial int CreateDirect3D11DeviceFromDXGIDevice(nint dxgi, nint* result);
    [LibraryImport("combase.dll")] private static unsafe partial int WindowsCreateString(char* value, uint length, nint* result);
    [LibraryImport("combase.dll")] private static partial int WindowsDeleteString(nint value);
    [LibraryImport("combase.dll")] private static unsafe partial int RoGetActivationFactory(nint name, Guid* iid, nint* result);
}
