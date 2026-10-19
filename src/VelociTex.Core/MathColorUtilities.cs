namespace VelociTex;

internal static class MathColorUtilities
{
    internal static bool TryParse(string? text, out MathColor result)
    {
        result = default;
        if (string.IsNullOrEmpty(text)) return false;
        if (text[0] == '#')
        {
            ReadOnlySpan<char> hex = text.AsSpan(1);
            if (hex.Length is not (3 or 4 or 6 or 8)) return false;
            uint rgba = 0;
            foreach (char c in hex)
            {
                int digit = c is >= '0' and <= '9' ? c - '0' : c is >= 'a' and <= 'f' ? c - 'a' + 10 : c is >= 'A' and <= 'F' ? c - 'A' + 10 : -1;
                if (digit < 0) return false; rgba = rgba << 4 | (uint)digit;
            }
            if (hex.Length <= 4)
            {
                byte alpha = hex.Length == 4 ? (byte)((rgba & 15) * 17) : (byte)255;
                if (hex.Length == 4) rgba >>= 4;
                result = new((byte)(((rgba >> 8) & 15) * 17), (byte)(((rgba >> 4) & 15) * 17), (byte)((rgba & 15) * 17), alpha);
            }
            else
            {
                byte alpha = hex.Length == 8 ? (byte)rgba : (byte)255;
                if (hex.Length == 8) rgba >>= 8;
                result = new((byte)(rgba >> 16), (byte)(rgba >> 8), (byte)rgba, alpha);
            }
            return true;
        }
        // This BCL value type contains the CSS named-color table and performs
        // no platform drawing or font work. Unknown CSS identifiers inherit
        // the surrounding color, exactly as the browser declaration does.
        if (text.Equals("rebeccapurple", StringComparison.OrdinalIgnoreCase)) { result = new(102, 51, 153); return true; }
        string normalized = text.Replace("grey", "gray", StringComparison.OrdinalIgnoreCase);
        System.Drawing.Color named = System.Drawing.Color.FromName(normalized);
        if (!named.IsKnownColor || named.IsSystemColor) return false;
        result = new(named.R, named.G, named.B, named.A); return true;
    }
}
