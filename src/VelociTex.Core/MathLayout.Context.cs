namespace VelociTex;

internal static partial class MathLayout
{
    // A compact value context replaces upstream's cloned Options objects.
    // Text size and math style are separate: \Huge's scripts are 20.74pt,
    // while normal-size scripts are 7pt. Style alone cannot encode that.
    private readonly record struct LayoutContext(byte Style, byte TextSize = 6)
    {
        internal int Size => Style < 4 ? TextSize : SizeStyleMap[(TextSize - 1) * 3 + (Style >= 6 ? 2 : 1)];
        internal double Scale => SizeMultipliers[Size - 1];
        internal int Metrics => Size >= 5 ? 0 : Size >= 3 ? 1 : 2;
        internal bool Tight => Style >= 4;
        internal LayoutContext WithStyle(byte style) => new(style, TextSize);
        internal LayoutContext WithSize(byte size) => new((byte)(Style < 2 ? 0 : Style & 1 | 2), size);
        public static implicit operator byte(LayoutContext context) => context.Style;
    }
    private static ReadOnlySpan<byte> SizeStyleMap =>
    [
        1,1,1, 2,1,1, 3,1,1, 4,2,1, 5,2,1, 6,3,1,
        7,4,2, 8,6,3, 9,7,6, 10,8,7, 11,10,9,
    ];
    private static ReadOnlySpan<double> SizeMultipliers => [.5,.6,.7,.8,.9,1,1.2,1.44,1.728,2.074,2.488];
    private static double Scale(LayoutContext context) => context.Scale;
    private static int MetricIndex(LayoutContext context) => context.Metrics;
    private static double Length(TexLength value, LayoutContext context, in MathOptions options)
    {
        double textScale = SizeMultipliers[context.TextSize - 1];
        int textMetric = context.TextSize >= 5 ? 0 : context.TextSize >= 3 ? 1 : 2;
        double em = value.Unit switch
        {
            TexUnit.Em => MathConstants.quad(textMetric) * textScale,
            TexUnit.Ex => MathConstants.xHeight(textMetric) * textScale,
            TexUnit.Mu => MathConstants.quad(context.Metrics) * context.Scale / 18,
            TexUnit.Pt => .1, TexUnit.Bp or TexUnit.Px => .1 * 803 / 800, TexUnit.Pc => 1.2,
            TexUnit.In => 7.227, TexUnit.Cm => 722.7 / 254, TexUnit.Mm => 722.7 / 2540,
            TexUnit.Dd => .1 * 1238 / 1157, TexUnit.Cc => .1 * 14856 / 1157,
            TexUnit.Nd => .1 * 685 / 642, TexUnit.Nc => .1 * 1370 / 107, TexUnit.Sp => .1 / 65536,
            _ => throw new InvalidOperationException("Unknown retained TeX length unit."),
        };
        return Math.Min(value.Number * em, options.MaxSize * context.Scale);
    }
    internal static double MathMlLength(TexLength value, MathStyle style, byte textSize, in MathOptions options)
    { LayoutContext context = new((byte)((int)style * 2), textSize == 0 ? (byte)6 : textSize); return Length(value,context,options)/context.Scale; }
    internal static double MathMlSizeScale(int size) => SizeMultipliers[size - 1];
    private static MathTransform Then(MathTransform local, MathTransform outer) => new(
        local.M11 * outer.M11 + local.M12 * outer.M21,
        local.M11 * outer.M12 + local.M12 * outer.M22,
        local.M21 * outer.M11 + local.M22 * outer.M21,
        local.M21 * outer.M12 + local.M22 * outer.M22,
        local.Dx * outer.M11 + local.Dy * outer.M21 + outer.Dx,
        local.Dx * outer.M12 + local.Dy * outer.M22 + outer.Dy);
}
