# Public API export sample

This Windows x64 console sample compiles nine complete formulas and exports PNG,
self-contained SVG and MathML. It includes integrals, matrices, a named operator,
an accent, explicit colors, transforms, invisible content and contextual text
using actual system-font outlines. It also exports literal-source error
presentation. It uses public APIs
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
`artifacts/p4/package-consumer`, restores both JIT and Native AOT using the
reviewed versioned locks in `package-locks/<version>`, then compiles and runs both. It verifies the
restored and JIT-loaded DLL bytes against the actual nupkg entries, all 20
original font hashes, source-retaining MathML, identical JIT/AOT exports,
bounded caching, cancellation, error recovery, macro rollback and ownership.
The generated project has no project references. `-UpdateLocks` is reserved for
intentional dependency changes; inspect and commit both resulting lock files.

Reports and all exported artifacts are retained per runtime. Actual contextual
run text and installed fallback font provenance remain available in documents.
These consumer formulas do not replace the full compatibility corpus or
old-hardware acceptance. The historical P2 source template and original locks
remain available for explicitly verifying `0.2.0-preview.1`.
