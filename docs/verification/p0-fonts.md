# P0-03 font verification

Run on 2026-10-19. This closes the data and browser calibration part of P0-03;
native private-font drawing is tracked with the native probe. These are P0
calibration samples, not formula-engine compatibility fixtures.

## Reproduction

After the exact upstream acquisition in `tools/reference/`:

```powershell
python tools/VelociTex.DataGeneration/generate_fonts.py --upstream artifacts/upstream/npm/package
python tools/VelociTex.DataGeneration/generate_fonts.py --upstream artifacts/upstream/npm/package --check
node tools/reference/font-calibration.mjs
python tools/VelociTex.DataGeneration/verify_browser_fonts.py
```

The two generation runs completed with exit code 0, with the second comparing
all committed output bytes. Python was 3.13.2, using only the standard library.
No development Python, Node, browser, or font parsing dependency is added to
the shipped library. Full hashes are in
`docs/reference/font-generation-0.19.0.json` and
`docs/reference/font-browser-calibration-0.19.0.json`.

## Results

| Check | Actual result |
| --- | --- |
| Original lock | All 20 TTF size and SHA-256 values match; 513,664 bytes |
| Names and license | All 20 declare OFL-1.1 in name ID 13 and expose name ID 14; all 12 RFNs match the lock |
| Unicode cmap | 2,195 entries; supported Unicode subtables agree on glyph IDs and point inside `maxp` |
| Glyph metadata | 2,255 glyphs with advances, bearings and ink bounds extracted from sfnt tables |
| TeX metrics | 2,038 entries, five original values each; exact source SHA-256 verified against the git-blob manifest |
| Deterministic output | `--check` passed for font bytes, JSON, C# tables, license and summary |
| Negative checks | Changed TTF byte and changed metrics source were rejected before output; executable static-metrics syntax was rejected |
| C# consumer data | `FontData.g.cs` compiled in a temporary `net10.0` library, with zero C# errors; restore reported NU1900 because the online vulnerability feed was unreachable, so that audit is unverified |
| Browser private fonts | All 20 explicit TTF faces loaded; no system-font lookup is used for the single-glyph samples |
| Independent advance comparison | At 40px, largest canvas versus decoded TTF advance error is 0.0000140381px across 20 samples |
| Exact CSS | Host 20px → `.katex` 24.2px, scale 1.21; Main x width 10.5625px → 12.78125px |
| Baseline | Raw and CSS x share baseline 1557.671875px; delta 0 |
| Italic correction | `f` emits a 0.1076em right margin; scripts, fraction, sum and integral examples also rendered |
| Visual review | Actual `artifacts/fonts/font-calibration.png` opened and reviewed: 20 rows and all eight formula probes visible, baseline and 0.25em axis guides visible |

Browser was Chrome 155.0.8059.84, binary SHA-256
`6aa5d599bbc3a0b3842af9f44b18fd29989eb3cc21b4b3ec7489e12f36e9962e`;
the complete authoritative binary hash is in the development toolchain lock
and browser calibration manifest. The screenshot uses DPR 1 and 1200px viewport
width. Reproduction currently requires the same locally pinned browser binary;
it does not claim that an automated browser download completed.

## Differences that must survive into P1

There are 1,484 TeX-width versus TTF-advance differences larger than 0.00001em.
Many are ordinary 1/1000em TTF quantization, while some represent structural
differences: AMS combining U+0302/U+0303 have zero TTF advance but TeX width
2.33334em; Size2 U+222D has advance 1.592em and TeX width 0.55556em. The full
per-entry comparison is `artifacts/fonts/advance-comparison.json`. TeX metrics,
TTF advances, glyph bounds, CSS scaling, and italic correction remain separate
data. No source value was adjusted to force agreement.

There are no source TeX metric tables for `Caligraphic-Bold` or `Fraktur-Bold`,
although the fonts ship. Typewriter metric entries U+0302, U+0303, U+0308 have
no direct cmap glyph. These are reported, and no replacement glyph ID or metric
is invented. P1 must apply source font-selection and special-symbol rules;
the extracted inventory alone is not a rendering strategy.

The full OFL body and original font copyright/RFN declarations are shipped in
`licenses/KaTeX-FONTS-OFL.txt`, independently of code licenses. The raw font
bytes and names remain unchanged. The official OFL snapshot and extraction
provenance are documented in the generator README.

## Evidence and limits

- Committed: 20 unchanged TTFs; generated `FontData.g.cs` and `font-data.json`;
  checksummed generation and browser summary manifests; complete OFL text.
- Local raw artifacts: `artifacts/fonts/font-verification.json`,
  `advance-comparison.json`, `browser-calibration.json`,
  `font-calibration.html`, `font-calibration.png`, `generator-negative-checks.json`.
- Not established by this check: complete formula compatibility, native glyph
  rasterization, native baseline hosts, DPI/theme acceptance, old-CPU speed,
  AOT startup, or the first-version latency budget. Those require their own
  real probes and must not be inferred from browser calibration.
