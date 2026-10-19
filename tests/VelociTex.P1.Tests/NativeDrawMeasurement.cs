using System.Runtime.InteropServices;
using VelociTex;

/// <summary>Test-only initialized native bitmap target; measures command submission and completed software drawing separately.</summary>
internal sealed unsafe partial class NativeDrawMeasurement : IDisposable
{
    private nint _wic, _bitmap, _factory;
    internal nint Target { get; private set; }
    internal double OffsetX { get; }
    internal double OffsetY { get; }
    internal NativeDrawMeasurement(MathDocument document)
    {
        var ink = document.InkBounds;
        OffsetX = 8 - Math.Min(0, ink.X); OffsetY = 8 - Math.Min(0, ink.Y);
        uint width = checked((uint)Math.Ceiling(Math.Max(document.LogicalSize.Width, ink.X + ink.Width) + OffsetX + 8));
        uint height = checked((uint)Math.Ceiling(Math.Max(document.LogicalSize.Height, ink.Y + ink.Height) + OffsetY + 8));
        Guid clsid = new("cacaf262-9370-4615-a13b-9f5539da4c0a"), iid = new("ec5ec8a9-c395-4314-9c77-54d7a935ff70"); nint wic = 0, bitmap = 0, factory = 0, target = 0;
        try
        {
            Check(CoCreateInstance(&clsid, 0, 1, &iid, &wic)); _wic = wic;
            Guid pixel = new("6fddc324-4e03-4bfe-b185-3d77768dc910");
            Check(((delegate* unmanaged[Stdcall]<nint, uint, uint, Guid*, uint, nint*, int>)Slot(wic, 17))(wic, width, height, &pixel, 2, &bitmap)); _bitmap = bitmap;
            iid = new("06152247-6f50-465a-9245-118bfd3b6007"); Check(D2D1CreateFactory(1, &iid, 0, &factory)); _factory = factory;
            TargetProperties properties = new() { Type = 1, Format = 87, Alpha = 1, DpiX = 96, DpiY = 96 };
            Check(((delegate* unmanaged[Stdcall]<nint, nint, TargetProperties*, nint*, int>)Slot(factory, 13))(factory, bitmap, &properties, &target)); Target = target;
        }
        catch { Dispose(); throw; }
    }
    internal void Begin() { ((delegate* unmanaged[Stdcall]<nint, void>)Slot(Target, 48))(Target); ((delegate* unmanaged[Stdcall]<nint, nint, void>)Slot(Target, 47))(Target, 0); }
    internal void End() => Check(((delegate* unmanaged[Stdcall]<nint, ulong*, ulong*, int>)Slot(Target, 49))(Target, null, null));
    public void Dispose() { nint target = Target; Target = 0; Release(ref target); Release(ref _factory); Release(ref _bitmap); Release(ref _wic); }
    private static nint Slot(nint value, int index) => (*(nint**)value)[index];
    private static void Check(int hr) { if (hr < 0) Marshal.ThrowExceptionForHR(hr); }
    private static void Release(ref nint value) { if (value == 0) return; nint old = value; value = 0; ((delegate* unmanaged[Stdcall]<nint, uint>)Slot(old, 2))(old); }
    [LibraryImport("ole32.dll")] private static partial int CoCreateInstance(Guid* clsid, nint outer, uint context, Guid* iid, nint* value);
    [LibraryImport("d2d1.dll")] private static partial int D2D1CreateFactory(uint type, Guid* iid, nint options, nint* value);
    [StructLayout(LayoutKind.Sequential)] private struct TargetProperties { internal uint Type, Format, Alpha; internal float DpiX, DpiY; internal uint Usage, MinimumLevel; }
}
