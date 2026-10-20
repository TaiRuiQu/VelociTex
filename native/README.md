# VelociTex native fast MathML parser

`VelociTex.NativeMath.cpp` is a bounded, allocation-light LaTeX parser that
serializes presentation MathML directly. It has no AST, no C# runtime and no
dependency on the managed engine. It is the pre-render pipeline for the native
P/Invoke path and the standalone WASM binding.

## Build

Windows (Visual Studio 2026 BuildTools x64):

```powershell
./native/build-native.ps1          # artifacts/native/VelociTex.NativeMath.dll
./native/build-wasm.ps1            # artifacts/wasm/fastmath/fastmath.{js,wasm}
```

The Emscripten script uses the compiler/cache bundled with the .NET 10
`wasm-tools` workload (`Microsoft.NET.Runtime.Emscripten.3.1.56.*`). The native
script uses `vcvars64.bat` from the installed BuildTools.

## ABI

```c
int vt_mathml2(const char* utf8, int length, char* output, int capacity, int display);
int vt_mathml(const char* utf8, int length, char* output, int capacity);
int vt_mathml_supported(const char* utf8, int length);
const char* vt_version(void);
```

`vt_mathml2` returns the required byte count, or a negative value when the input
is unsupported/invalid. The output is not NUL-terminated. Call again with a
larger buffer when the returned count exceeds `capacity`. Output is UTF-8
presentation MathML with no source annotation. One `thread_local` buffer is
reused per thread.

## Supported subset

Atoms, digits, groups, scripts, `\frac`, `\sqrt`, `\text`, `\mathrm`/`\mathbf`/
`\mathit`/`\mathsf`/`\mathtt`, common Greek letters/operators/relations,
`\left`/`\right`, `aligned` and matrix environments, accents, `\phantom`,
`\boxed`, spacing commands and large operators. Inputs are bounded to 16 KB,
64 nesting levels and 4,096 nodes. Anything else returns unsupported and the
caller must use the full C# engine.

## Measured

Native AOT, 180,000 calls per process, five processes under shared-VM load: mean
**3.10–3.78 µs**, P50 2.5–3.0 µs, P95 6.3–6.5 µs, 916 managed bytes/call. An
earlier idle run measured 2.40–2.61 µs. KaTeX 0.19.0 MathML mean is 53.527 µs,
so the conservative worst run is **14.2x**. The Emscripten build is used by
`npm/velocitex` and measured separately in
`docs/verification/performance-cpp-2026-10-20.md`.

## Fallback

`NativeMathMl.TryRender` returns `false` rather than a partial result.
`FastMathEngine.TryCompileToMathMl` retries through
`MathEngine.TryCompileToMathMl` for unsupported input or non-default options.
The fast path is not a replacement for the full engine in correctness-sensitive
byte-for-byte comparisons.
