# Requirements and Design Document Verification Record

English | [中文](design-review.2026-10-19.zh-CN.md)

Date: 2026-10-19. The current deliverables are requirements, design, implementation, and handover material only; this document does not indicate that a rendering engine exists.

## Executed Checks

| Check | Result |
| --- | --- |
| Local Markdown links, fence pairing, trailing whitespace | Pass |
| The two JSON files parse, fixture IDs are unique | Pass |
| KaTeX v0.19.0 GitHub tag | Resolves to `6ea2dc97188f0cc282e05a9690418ae7fe5457ba` |
| npm archive | Downloaded into memory, SHA-512 and SHA-256 recomputed, both match the lock data |
| 20 TTFs | Filenames, lengths, and SHA-256 all match; name table licenses and reserved names checked |
| 21 valid formula seeds | Using dist/katex.js 0.19.0 from the archive, renderToString produced HTML and MathML successfully |
| 5 invalid / macro loop seeds | All threw KaTeX parse errors as expected; the loop macro hit the expansion count limit |
| 3 out-of-scope seeds | Explicitly excluded and not counted as passing compatibility cases |

For the syntax check each formula used a fresh macro object, maxExpand=1000, the displayMode from the fixture configuration, output=htmlAndMathml, and throwOnError=true. The strict processor collects diagnostics and returns ignore, so this proves that the samples parse, not that strict mode is fully compatible. No differential run was performed on exported MathML because this library does not exist yet.

The valid formulas cover calculus, 3×3 matrices, determinants, Maxwell-style aligned equations, cases, array rules, stretchy arrows and braces, fonts, Chinese text, atom spacing, special boxes, macro scope, mathchoice, labels, and the core CD environments. They remain a seed set only and cannot stand in for the complete list and corpus required in P4.

Input data checksums:

| File | SHA-256 |
| --- | --- |
| `docs/examples/formulas.json` | `1061caae80c12af46a75b485de69305524eb6abb77ce1ccd4c135bab661b18a9` |
| `docs/reference/katex-0.19.0.lock.json` | `a742ad058752090b93db72ec9103d0c385d4a8cd34a35dae4ccf6088db5aa251` |

Verification only loaded the checksum-verified KaTeX bundle from temporary files and deleted it afterwards. No browser, Node dependency, upstream code, or font was added to this repository.

## Not Yet Executed

Native typesetting and screenshot comparison, the WinUI host visuals, PNG / SVG export, this library's MathML, AOT consumption, old-CPU latency, full-page performance, memory pressure, and downstream migration are all unexecuted and left to P0 and the following phases. There is no conclusion that this library meets 10ms or ≥10×.

The workspace was not yet a Git repository, so `git diff --check` was not run and no commit was produced. The documents were checked by file content, and no downstream source code was modified and no package published in this round.

## Later Document Change and Re-check

After the checks above, the document set was restructured so that English is the default version: the English files lost their `.en` suffix (`README.md`, `AGENTS.md`, `docs/*.md`, `docs/verification/design-review.2026-10-19.md`) and the Chinese versions became `*.zh-CN.md`. Cross-language links, wording, and terminology were updated accordingly.

Re-verified after that change, still on 2026-10-19:

| Check | Result |
| --- | --- |
| Local Markdown links across all `.md` files | Pass, no broken targets |
| Code fence pairing and trailing whitespace | Pass |
| SHA-256 of `docs/examples/formulas.json` and `docs/reference/katex-0.19.0.lock.json` | Unchanged, identical to the table above |
| Fixture IDs in `docs/examples/formulas.json` | 29 unique IDs: 21 valid, 5 invalid, 3 excluded |

Not re-executed: the npm archive digest check, the 20 TTF checksum and name table checks, and the KaTeX rendering run of the seed set, since no upstream or font data changed.
