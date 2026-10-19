# Local release-candidate gates

Package creation is a reversible local preparation step. It does not publish a
release, prove compatibility or satisfy hardware acceptance by itself.

As of 2026-10-19, local `1.0.0-rc.1` packages and isolated consumers are prepared
from P4 commit `1b0c77774418fd349c36b8b4cd0c89a135f70fba`. The [P5 record](verification/p5-2026-10-19.md)
and its checksum manifest define this frozen candidate. Formal 1.0 exit remains
pending the real-machine and audio gates in item 7; no external publication is
authorized. The candidate also retains a documented non-blocking XML-comment
blemish that must be cleaned up before a stable release.

1. Freeze the exact source revision, SDK, centrally managed dependencies and
   JIT/AOT lock files. Keep KaTeX fixed at 0.19.0 and the original font hashes.
2. Finish applicable semantic, geometric, MathML, native and raster checks for
   the complete included registration denominator. Preserve every failing or
   diagnostic result. The user's later instruction accepts antialiasing
   differences when typesetting is correct; raw pixel-threshold failures remain
   informational, while semantics, layout, missing glyphs, clipping and colors
   remain mandatory. Keep remaining work explicit in the phase record.
3. Run the real WinUI application and inspect automated screenshots after UI
   changes. Record independent keyboard, clipboard and UI Automation evidence;
   basic providers are not a screen-reader audio test.
4. Pack the three local packages and portable symbols with `pack-local.ps1
   -Locked -VerifyDeterminism -Version <candidate>`. Inspect every payload,
   licenses, XML documentation, source map and original embedded font hash.
5. Restore each candidate into fresh consumers with isolated package folders,
   centrally pinned versions and reviewed version-specific locks. Compile and
   run both JIT and Native AOT. Verify actual nupkg/DLL bytes, all outputs,
   contextual text, resource ownership, errors, cancellation and cache bounds.
6. Record deterministic decompressed payload equality separately from ZIP
   timestamps and whole-archive hashes. Verify the SourceLink JSON in the actual
   portable PDB. An unpublished local source revision must be described as
   unavailable remotely, even if the source-map syntax is correct.
7. Record actual old-CPU results, memory pressure, device reconstruction, actual
   monitor DPI changes and downstream rollback acceptance. Unexecuted checks
   remain unverified; modern hardware does not satisfy an old-hardware gate.
8. Freeze the candidate package hashes and reproduction commands. Keep the local
   preview/candidate label until all release gates are met. External publication,
   signing and pushing commits require explicit user authorization.
