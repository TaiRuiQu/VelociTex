using System.Text;
namespace VelociTex;

/// <summary>Presentation MathML projected by the native engine directly from retained semantics.</summary>
public static class MathMlWriter
{
    /// <summary>Writes presentation MathML, optionally retaining the exact TeX source annotation.</summary>
    public static string Write(MathDocument document, bool includeAnnotation = true)
    { ArgumentNullException.ThrowIfNull(document); return NativeInterop.MathMl(document.NativeHandle, includeAnnotation); }
    /// <summary>Writes UTF-8 presentation MathML while leaving the caller-owned stream open.</summary>
    public static void Write(MathDocument document, Stream output, bool includeAnnotation = true)
    { ArgumentNullException.ThrowIfNull(output); string text = Write(document, includeAnnotation); using var writer = new StreamWriter(output, new UTF8Encoding(false), 4096, leaveOpen: true); writer.Write(text); }
}
