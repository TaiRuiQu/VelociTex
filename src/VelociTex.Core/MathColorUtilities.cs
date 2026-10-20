namespace VelociTex;

internal static class MathColorUtilities
{
    internal static unsafe bool TryParse(string? text, out MathColor result)
    {
        result = default;
        if (string.IsNullOrEmpty(text))
            return false;
        fixed (char* p = text)
        { if (NativeInterop.ParseColor(new() { Data = p, Length = text.Length }, out uint rgba) == 0) return false; result = new((byte)(rgba >> 24), (byte)(rgba >> 16), (byte)(rgba >> 8), (byte)rgba); return true; }
    }
}
