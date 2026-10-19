using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace VelociTex.Rendering.Direct2D;

// Deliberately small, Windows SDK ABI binding. No runtime-generated COM stubs.
internal static unsafe partial class NativeMethods
{
    [LibraryImport("ole32.dll")] internal static partial int CoInitializeEx(nint reserved, uint flags);
    [LibraryImport("ole32.dll")] internal static partial void CoUninitialize();
    [LibraryImport("ole32.dll")] internal static partial int CreateStreamOnHGlobal(nint memory,int deleteOnRelease,nint* stream);
    [LibraryImport("ole32.dll")] internal static partial int CoCreateInstance(Guid* clsid, nint outer, uint context, Guid* iid, nint* result);
    [LibraryImport("dwrite.dll")] internal static partial int DWriteCreateFactory(uint factoryType, Guid* iid, nint* factory);
    [LibraryImport("d2d1.dll")] internal static partial int D2D1CreateFactory(uint factoryType, Guid* iid, nint options, nint* factory);
    [LibraryImport("d2d1.dll")] internal static partial int D2D1CreateDevice(nint dxgiDevice, nint properties, nint* device);
    [LibraryImport("d3d11.dll")] internal static partial int D3D11CreateDevice(nint adapter, uint driverType, nint software, uint flags, nint levels, uint levelCount, uint sdkVersion, nint* device, uint* level, nint* context);

    internal static nint Slot(nint instance, int slot) => (*(nint**)instance)[slot];
    internal static void Check(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
    internal static void Release(ref nint value)
    {
        nint item = value;
        value = 0;
        if (item != 0) ((delegate* unmanaged[Stdcall]<nint, uint>)Slot(item, 2))(item);
    }
    internal static nint Query(nint value, Guid iid)
    {
        nint result = 0;
        Check(((delegate* unmanaged[Stdcall]<nint, Guid*, nint*, int>)Slot(value, 0))(value, &iid, &result));
        return result;
    }
    internal static void BeginDraw(nint target) => ((delegate* unmanaged[Stdcall]<nint, void>)Slot(target, 48))(target);
    internal static void EndDraw(nint target) => Check(((delegate* unmanaged[Stdcall]<nint, ulong*, ulong*, int>)Slot(target, 49))(target, null, null));
    internal static void Clear(nint target) => ((delegate* unmanaged[Stdcall]<nint, Color*, void>)Slot(target, 47))(target, null);
    internal static void SetDpi(nint target, float dpi) => ((delegate* unmanaged[Stdcall]<nint, float, float, void>)Slot(target, 51))(target, dpi, dpi);

    [StructLayout(LayoutKind.Sequential)] internal struct Point(float x, float y) { internal float X = x, Y = y; }
    [StructLayout(LayoutKind.Sequential)] internal struct Rect(float left, float top, float right, float bottom) { internal float Left = left, Top = top, Right = right, Bottom = bottom; }
    [StructLayout(LayoutKind.Sequential)] internal struct Color(float r, float g, float b, float a = 1) { internal float R = r, G = g, B = b, A = a; }
    [StructLayout(LayoutKind.Sequential)] internal struct Matrix(float dx, float dy) { internal float M11 = 1, M12 = 0, M21 = 0, M22 = 1, Dx = dx, Dy = dy; }
    [StructLayout(LayoutKind.Sequential)] internal struct SizeU(uint width, uint height) { internal uint Width = width, Height = height; }
    [StructLayout(LayoutKind.Sequential)] internal struct PixelFormat(uint format, uint alphaMode) { internal uint Format = format, AlphaMode = alphaMode; }
    [StructLayout(LayoutKind.Sequential)] internal struct TargetProperties { internal uint Type; internal PixelFormat PixelFormat; internal float DpiX, DpiY; internal uint Usage, MinimumLevel; }
    [StructLayout(LayoutKind.Sequential)] internal struct BitmapProperties { internal PixelFormat PixelFormat; internal float DpiX, DpiY; internal uint Options; internal nint ColorContext; }
    [StructLayout(LayoutKind.Sequential)] internal struct MappedRect { internal uint Pitch; internal byte* Bits; }
    [StructLayout(LayoutKind.Sequential)] internal struct Bezier { internal Point P1, P2, P3; }
    [StructLayout(LayoutKind.Sequential)] internal struct GlyphRun
    {
        internal nint FontFace;
        internal float EmSize;
        internal uint GlyphCount;
        internal ushort* GlyphIndices;
        internal float* GlyphAdvances;
        internal nint GlyphOffsets;
        internal int IsSideways;
        internal uint BidiLevel;
    }
    [StructLayout(LayoutKind.Sequential)] internal struct GlyphMetrics
    {
        internal int LeftSideBearing;
        internal uint AdvanceWidth;
        internal int RightSideBearing, TopSideBearing;
        internal uint AdvanceHeight;
        internal int BottomSideBearing, VerticalOriginY;
    }
}
