using System.Runtime.InteropServices;

namespace VelociTex.WinUI.Sample;

// Windows.Graphics.Capture requires an interactive capture consent that the
// automated session does not provide. PrintWindow with PW_RENDERFULLCONTENT is
// an independent GDI fallback for real HWND evidence; it captures the composed
// window surface including DirectComposition visuals on supported Windows.
internal static partial class GdiWindowCapture
{
    private const uint PW_RENDERFULLCONTENT = 2;
    private const uint DIB_RGB_COLORS = 0;
    private const uint BI_RGB = 0;

    internal static unsafe void Capture(nint hwnd, string path)
    {
        if (GetClientRect(hwnd, out RECT rect) == 0 || rect.Right <= rect.Left || rect.Bottom <= rect.Top) throw new InvalidOperationException("The window has no client area to capture.");
        int width = rect.Right - rect.Left, height = rect.Bottom - rect.Top;
        nint screen = GetDC(0), memory = CreateCompatibleDC(screen), bitmap = 0;
        try
        {
            BITMAPINFO info = new() { Header = new() { Size = 40, Width = width, Height = height, Planes = 1, BitCount = 32, Compression = BI_RGB } };
            bitmap = CreateDIBSection(memory, ref info, DIB_RGB_COLORS, out nint bits, 0, 0);
            if (bitmap == 0 || bits == 0) throw new InvalidOperationException("CreateDIBSection failed.");
            nint previous = SelectObject(memory, bitmap);
            try
            {
                if (PrintWindow(hwnd, memory, PW_RENDERFULLCONTENT) == 0) throw new InvalidOperationException("PrintWindow failed.");
                int stride = width * 4;
                using FileStream stream = File.Create(path);
                using BinaryWriter writer = new(stream);
                writer.Write((ushort)0x4D42); writer.Write(14 + 40 + stride * height); writer.Write((ushort)0); writer.Write((ushort)0); writer.Write(14 + 40);
                writer.Write(40); writer.Write(width); writer.Write(height); writer.Write((ushort)1); writer.Write((ushort)32);
                writer.Write(BI_RGB); writer.Write(stride * height); writer.Write(2835); writer.Write(2835); writer.Write(0); writer.Write(0);
                writer.Write(new ReadOnlySpan<byte>((void*)bits, stride * height));
            }
            finally { SelectObject(memory, previous); }
        }
        finally { if (bitmap != 0) DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(0, screen); }
    }

    [StructLayout(LayoutKind.Sequential)] private struct RECT { internal int Left, Top, Right, Bottom; }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAPINFOHEADER
    {
        internal uint Size; internal int Width, Height; internal ushort Planes, BitCount; internal uint Compression, SizeImage;
        internal int XPelsPerMeter, YPelsPerMeter; internal uint ClrUsed, ClrImportant;
    }
    [StructLayout(LayoutKind.Sequential)] private struct BITMAPINFO { internal BITMAPINFOHEADER Header; internal uint Colors; }

    [LibraryImport("user32.dll")] private static partial nint GetDC(nint hwnd);
    [LibraryImport("user32.dll")] private static partial int ReleaseDC(nint hwnd, nint dc);
    [LibraryImport("user32.dll")] private static partial int GetClientRect(nint hwnd, out RECT rect);
    [LibraryImport("user32.dll")] private static partial int PrintWindow(nint hwnd, nint dc, uint flags);
    [LibraryImport("gdi32.dll")] private static partial nint CreateCompatibleDC(nint dc);
    [LibraryImport("gdi32.dll")] private static partial int DeleteDC(nint dc);
    [LibraryImport("gdi32.dll")] private static partial nint CreateDIBSection(nint dc, ref BITMAPINFO info, uint usage, out nint bits, nint section, uint offset);
    [LibraryImport("gdi32.dll")] private static partial nint SelectObject(nint dc, nint value);
    [LibraryImport("gdi32.dll")] private static partial int DeleteObject(nint value);
}
