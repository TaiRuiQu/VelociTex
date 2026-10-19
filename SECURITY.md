# Input and resource boundaries

VelociTex compiles one complete formula. Treat TeX source, macro dictionaries,
font availability, export paths and caller callbacks as inputs owned by the
host. Compilation does not execute JavaScript, a browser, Node.js, Typst,
external resource requests, file commands or caller-provided TeX callback code.
The Windows text shaper calls the installed DirectWrite font system.

The self-contained WinUI host inherits Windows App SDK framework payloads,
including WebView2 loader/projection DLLs. MathView never creates a WebView or
uses those binaries to compile or draw formulas. The headless Core/Direct2D
consumer does not require that WinUI framework payload. Package and runtime
claims must distinguish the library path from incidental host SDK distribution.

`MathOptions` bounds UTF-16 input, expanded tokens, semantic nodes, nesting and
macro expansion. Workspaces release transient state after success, ordinary
errors, cancellation and unexpected caller exceptions. Large scratch buffers
are trimmed. Set application-specific quotas and cancellation before accepting
untrusted requests. Compilation is synchronous; cancellation is cooperative.
The host must also bound simultaneous requests and pending work.

Result caches have count and estimated managed-byte limits. Sessions, custom
macros, stateful shaping or strict callbacks and definition-bearing formulas
bypass the result cache. A cache byte estimate does not measure CLR heap size,
native memory or GPU memory. A single workspace or macro session is serial;
concurrent calls need independent workspaces.

PNG exports reject dimensions above 8,192 pixels per side or 4,194,304 pixels in
total. Input and export budgets are separate. Native renderers and preparations
are confined to their creating thread and must be disposed there. Caller-owned
streams remain open. Caller-owned font directories are preserved; the default
renderer releases its native faces before deleting its private extracted fonts.

Compilation failures carry source ranges and stable diagnostic codes. Hosts
should retain the original source and avoid showing filesystem paths, stack
traces or internal logs to untrusted clients. `CompileForPresentation` can draw
ordinary syntax failures as literal colored source; it does not retry cancelled,
busy or resource-exhausted requests as another unbounded formula. Caller-supplied
strict handlers and text-shaping providers are ordinary .NET code: only install
trusted implementations and follow their threading contracts.

No local package command publishes to a remote feed. A remote release, signing
identity and public vulnerability-reporting contact require a separate release
decision. This document does not assert a security audit or a warranty. A source
URL or SourceLink map does not prove that an unpublished local commit is
available on the remote repository.
