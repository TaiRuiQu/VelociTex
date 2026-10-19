# oh-my-study Display Chain Migration

English | [中文](integration-oh-my-study.zh-CN.md)

Investigation date: 2026-10-19. No downstream source code was modified in this round. The paths and behaviors below are investigation-time snapshots; re-read the real source and the downstream AGENTS.md before migrating.

## 1. Current Status and Actual Replacement Points

| File / Type | Current Responsibility | Migration Action |
| --- | --- | --- |
| `src/OhMyStudy.App/Services/NativeQuestionPresenter.cs` | Native containers for questions, options, tables, images, sub-questions, and feedback | Retain domain and UI organization, replace dependent reading service |
| `src/OhMyStudy.App/Services/NativeReadingSession.cs` | Collect body text, batch call Typst, whole paragraph SVG cache, theme / DPI and copy | Split body host and single formula compilation / drawing service |
| `src/OhMyStudy.Documents/TypstDocumentService.Reading.cs` | Typeset body text and inline formulas together into SVG | Exit default chain in display scenarios; print calls continue retained |
| `src/OhMyStudy.Documents/LatexFormulaConverter.cs` | Bounded LaTeX / mhchem → Typst | Display math goes to VelociTex; the print conversion is temporarily retained |
| `src/OhMyStudy.Documents/DocumentText.cs` | Math delimiters, normalization, and body text scanning | Prioritize reuse, verify cannot rewrite math new library can recognize as legitimate |
| `src/OhMyStudy.App/Diagnostics`, `probes/OhMyStudy.Documents.Probe` | Actual host screenshots, reading and document regression | Add new chain use cases, retain same-condition old chain baseline |

The current implementation compiles a whole paragraph of text and formulas at once, and the SVG contains the body text. Dropping a "single formula emits SVG" API into the existing call site therefore does not automatically preserve paragraph line breaking or the inline baseline.

The latest investigation record lives in the downstream repository at `docs/verification/math-rendering.2026-10-19.md`. Its 381.44ms first creation, roughly 0.83/0.88ms cached numbers, and P95 9.54ms question switching are existing small-sample CPU and layout records; they cannot serve as a controlled multiple between this library and KaTeX.

## 2. Font Convention

The older downstream convention requires sans-serif interface fonts. In this round the user explicitly allows **math formulas to use the original KaTeX fonts**; body text stays sans-serif and the existing print rules keep applying. During migration, record this local exception in the downstream conventions and in the design record, so that a later Agent does not unify math fonts back to sans to satisfy the old rule.

The user did not authorize changing ordinary interface fonts to KaTeX fonts, nor did they ask for the print chain to be replaced in this round.

## 3. Target Call Flow

```text
Question content
  → Downstream DocumentText scans into prose / inline math / display math
  → Math fragments: VelociTex compile → immutable result / baseline / ink bounds
  → Downstream paragraph host: body text typesetting, formula inline object, line breaking / width
  → WinUI native drawing and interaction
```

The single-formula API does not receive the outer `$` or other body-text delimiters. Keep the scanning, escaping, explicit line breaks, and unclosed-original-text behavior of `$...$`, `$$...$$`, `\(...\)`, and `\[...\]`. Once the core support scope widens, body-text normalization must not first filter out commands through the old converter when the new library already supports them.

Downstream adapts VelociTex behind its own `IFormulaPresentationService` or an equivalent narrow interface. Question or Content domain types are never passed into the math library, and the question-bank model is never copied into it.

## 4. Inline Host and Body Text Boundary

P0 first verifies the real baseline, line height, and matrix behavior of the native RichTextBlock plus InlineUIContainer. The default bottom alignment of a generic UIElement is not the math baseline, and a visually similar Margin tweak is not a general solution.

If the WinUI host cannot express the required baseline stably, adopt DirectWrite paragraph layout with an inline object whose metrics return the formula width, height, and baseline, and whose draw callback consumes the same drawing result. DirectWrite's [GetMetrics](https://learn.microsoft.com/en-us/windows/win32/api/dwrite/nf-dwrite-idwriteinlineobject-getmetrics) supplies the baseline-related metrics for this model.

This is necessary downstream adaptation work, not a first-version Core body-text engine. When DirectWrite self-drawing is chosen, also handle whole-paragraph and per-formula copying, keyboard access, and basic semantics, and evaluate the existing selection behavior of ordinary body text: existing downstream features that must be kept cannot disappear without a record. Continuous selection across body text and formulas, selection inside a formula, and an editing caret remain out of this library's requirements scope.

If only standalone formulas are finished while inline paragraphs still fall short, that is allowed as an explicitly marked transition phase that keeps the old paragraph path; the display chain may only be called fully replaced once mixed layout also works.

## 5. Scheduling, Caching, and Presentation

- Compile in the background once a complete formula has arrived; reuse the engine, cap concurrency, and retain results by byte budget. Never create one XAML element per symbol.
- Downstream controls cancellation and the request generation, so a late result cannot overwrite a newer question, and one invalid formula in a batch does not affect the valid ones.
- Font size, math options, and macro context changes invalidate compiled results; a theme default foreground change only redraws, and a DPI change updates surfaces and mappings instead of forcing a re-parse.
- Macros are isolated per formula by default; when a document must share them, use an explicit session, and never let different users or messages share global definitions arbitrarily.
- Long display formulas keep their font size and scroll horizontally; inline line breaking belongs to the host, which must not split fractions or matrices. Tables are typeset under the existing content-width constraints.
- Do not emit PNG or serialized SVG during normal display just so WinUI can parse it; use the drawable result directly. The export API is only used when the user asks for an export.
- On errors or exceeded limits, keep the complete readable original text and make it copyable, with a short user-facing prompt. Diagnostic details stay in the logs; framework and stage information do not belong in the UI.

## 6. Interaction and Accessibility

MathView exposes a single-formula semantic object; the copy menu and keyboard shortcuts yield the original LaTeX, and the host can additionally offer whole-paragraph copying. At minimum, verify the AutomationPeer name, role, description, and focus behavior, and never read the original text and the MathML twice.

Downstream can supply short descriptions such as "integral formula"; when there is no description, expose the source text as the basic fallback. MathML exists as an independent output, so do not assume WinUI reads arbitrary MathML automatically.

## 7. Migration and Rollback Steps

1. Freeze the old-chain samples and the same-condition baseline, keeping source, build, and screenshot correlated.
2. Build a local pre-release package in this repository; downstream references it through centralized versions and lock files instead of an absolute path on a development machine.
3. Integrate standalone formulas and the math result cache, then integrate the verified paragraph host, covering every call site.
4. Switch between the new and old paths through internal configuration to review the same content; never show the engine name on normal product pages.
5. Run automatic screenshots of the real Release AOT application, plus functional and stress regressions, and inspect the default page and its surroundings.
6. Enable the new display path by default, and keep the old one only for a transition period whose verified scope is explicit.
7. Delete the display conversion, whole-paragraph SVG parsing, and display-specific caches that are no longer used, while keeping the Typst tooling and conversion logic still used for print / PDF.

Do not upgrade the database, publish an installer, or rewrite the print architecture as a side effect of packaging or display migration.

## 8. Downstream Completion Checks

- Body text, options, tables, sub-questions, feedback, and list previews all run through the new path.
- Fewer than 10 formulas per question and 50–100 per page, first view, repeat view, and fast question switching, with empty, invalid, and very long content, all pass.
- At minimum review the existing 1440×960, 720×960, and 460×960 scenarios, and add 100/125/150/200% DPI plus dark and light themes.
- Visually, the baseline, punctuation, line spacing, fraction and radical boundaries, scrolling, table width, and adjacent headings show no regression.
- Copy, keyboard, basic screen reading, and cancellation generations work; ordinary valid display starts no Typst subprocess.
- The existing print / PDF regression keeps passing; untested physical printing, system high contrast, and real multi-monitor DPI are listed explicitly.
- Every UI change is screenshotted automatically and actually inspected as the downstream requires; a successful build is not a substitute.
