using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;

namespace VelociTex.NativeMath;

/// <summary>Optional native fast path for LaTeX to presentation MathML.</summary>
/// <remarks>This adapter is deliberately separate from the Core package. It returns
/// <see langword="false"/> for unsupported input so callers can fall back to the
/// full managed engine. Output is presentation MathML without a source annotation.</remarks>
public static class NativeMathMl
{
    private const string LibraryName = "VelociTex.NativeMath";
    private const int InitialOutputBytes = 64 * 1024;
    private static readonly Lazy<bool> Available = new(Probe);
    private static readonly DllImportResolver Resolver = Resolve;

    static NativeMathMl()
    {
        try
        { NativeLibrary.SetDllImportResolver(typeof(NativeMathMl).Assembly, Resolver); }
        catch (InvalidOperationException) { }
    }

    /// <summary>True when the native library was found and its version export answered.</summary>
    public static bool IsAvailable => Available.Value;

    /// <summary>Renders a supported formula without source validation fallback.</summary>
    public static bool TryRender(string latex, bool displayMode, out string mathml)
    {
        ArgumentNullException.ThrowIfNull(latex);
        mathml = string.Empty;
        if (!IsAvailable)
            return false;
        int maxBytes = Encoding.UTF8.GetMaxByteCount(latex.Length);
        byte[]? rentedInput = null;
        Span<byte> input = maxBytes <= 2048 ? stackalloc byte[maxBytes] : (rentedInput = System.Buffers.ArrayPool<byte>.Shared.Rent(maxBytes));
        try
        {
            int inputLength = Encoding.UTF8.GetBytes(latex, input);
            byte[] rentedOutput = System.Buffers.ArrayPool<byte>.Shared.Rent(InitialOutputBytes);
            Span<byte> output = rentedOutput;
            try
            {
                int size;
                unsafe
                {
                    fixed (byte* inputPtr = input)
                    fixed (byte* outputPtr = output)
                        size = vt_mathml2(inputPtr, inputLength, outputPtr, output.Length, displayMode ? 1 : 0);
                }
                if (size < 0)
                    return false;
                if (size > output.Length)
                {
                    System.Buffers.ArrayPool<byte>.Shared.Return(rentedOutput);
                    rentedOutput = System.Buffers.ArrayPool<byte>.Shared.Rent(size);
                    output = rentedOutput;
                    unsafe
                    {
                        fixed (byte* inputPtr = input)
                        fixed (byte* outputPtr = output)
                            size = vt_mathml2(inputPtr, inputLength, outputPtr, output.Length, displayMode ? 1 : 0);
                    }
                    if (size < 0 || size > output.Length)
                        return false;
                }
                mathml = Encoding.UTF8.GetString(output[..size]);
                return true;
            }
            finally { System.Buffers.ArrayPool<byte>.Shared.Return(rentedOutput); }
        }
        finally { if (rentedInput is not null) System.Buffers.ArrayPool<byte>.Shared.Return(rentedInput); }
    }

    private static bool Probe()
    {
        try
        {
            unsafe
            {
                byte[] input = "x"u8.ToArray();
                byte[] output = new byte[256];
                fixed (byte* inputPtr = input)
                fixed (byte* outputPtr = output)
                    return vt_mathml2(inputPtr, input.Length, outputPtr, output.Length, 1) > 0;
            }
        }
        catch (DllNotFoundException) { return false; }
        catch (EntryPointNotFoundException) { return false; }
        catch (BadImageFormatException) { return false; }
    }

    private static IntPtr Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath)
    {
        if (!string.Equals(libraryName, LibraryName, StringComparison.Ordinal))
            return IntPtr.Zero;
        string fileName = OperatingSystem.IsWindows() ? "VelociTex.NativeMath.dll" :
            OperatingSystem.IsMacOS() ? "libVelociTex.NativeMath.dylib" : "libVelociTex.NativeMath.so";
        foreach (string directory in Candidates())
        {
            if (NativeLibrary.TryLoad(Path.Combine(directory, fileName), out IntPtr handle))
                return handle;
        }
        return IntPtr.Zero;
    }

    private static IEnumerable<string> Candidates()
    {
        string? configured = Environment.GetEnvironmentVariable("VELOCITEX_NATIVE_DIR");
        if (!string.IsNullOrWhiteSpace(configured))
            yield return configured;
        yield return AppContext.BaseDirectory;
        string? current = AppContext.BaseDirectory;
        for (int i = 0; i < 6 && current is not null; i++)
        {
            yield return Path.Combine(current, "artifacts", "native");
            current = Path.GetDirectoryName(current.TrimEnd(Path.DirectorySeparatorChar));
        }
    }

    [DllImport(LibraryName, EntryPoint = "vt_mathml2", CallingConvention = CallingConvention.Cdecl)]
    private static extern unsafe int vt_mathml2(byte* input, int inputLength, byte* output, int outputCapacity, int displayMode);
}
