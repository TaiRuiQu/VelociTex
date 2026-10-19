# Public API export sample

This Windows x64 console sample compiles five complete formulas and exports PNG,
self-contained SVG and MathML. It includes integrals, matrices, a named operator,
an accent and CJK text using actual system-font outlines. It uses public APIs
only. The development project references the repository projects; the clean
package verification below copies the exact same `Program.cs` into a fresh
consumer with only centrally versioned `PackageReference` entries.

From the repository root, with the pinned .NET SDK:

```powershell
dotnet run --project samples/VelociTex.Export.Sample -c Release -p:PublishAot=false -p:Platform=x64 -- artifacts/export-sample --expect-jit
```

After building the local preview feed, verify actual package consumption:

```powershell
pwsh ./tools/Verify-PackageConsumer.ps1
```

The verifier creates a unique consumer and isolated package directory under
`artifacts/p2/package-consumer`, restores both JIT and Native AOT using the
reviewed locks in `package-locks`, then compiles and runs both. It verifies the
restored and JIT-loaded DLL bytes against the actual nupkg entries, all 20
original font hashes, source-retaining MathML, 15 identical JIT/AOT exports,
bounded caching, cancellation, error recovery, macro rollback and ownership.
The generated project has no project references. `-UpdateLocks` is reserved for
intentional dependency changes; inspect and commit both resulting lock files.

Reports and all 15 exported artifacts are retained per runtime. The report names
the installed fallback font. CJK scalar fallback does not promise contextual
shaping of complex scripts. These five consumer formulas do not replace the
full compatibility corpus or old-hardware acceptance.
