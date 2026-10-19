# Font data generation

These are development tools. They require Python 3.10+ with only its standard
library; library consumers require none of these tools. The P0 run used Python
3.13.2. First acquire the exact upstream inputs using `tools/reference/`.

```powershell
python tools/VelociTex.DataGeneration/generate_fonts.py --upstream artifacts/upstream/npm/package
python tools/VelociTex.DataGeneration/generate_fonts.py --upstream artifacts/upstream/npm/package --check
node tools/reference/font-calibration.mjs
python tools/VelociTex.DataGeneration/verify_browser_fonts.py
```

`--check` compares the committed fonts, generated JSON, C# tables, license, and
summary byte for byte. It may refresh ignored calibration artifacts. A TTF whose
size or SHA-256 differs from the original lock fails before output is written.
The metrics source hash is independently checked against the exact git-blob
manifest produced by upstream acquisition. No installed font or external font
parser is involved. The static metrics parser accepts only objects, arrays,
strings, and numbers; executable JavaScript fails.

`font-data.json` preserves all TTF name records, Unicode cmap entries, glyph IDs,
horizontal advances, left side bearings, glyph bounds, ascenders, descenders,
and the five original TeX metrics. `FontData.g.cs` is the compact C# consumer
table: a descriptor gives each font's ranges in the glyph and TeX metric arrays.
Code points are sorted within each font, allowing a later binary search. Units
are explicit: TTF values use `unitsPerEm`; TeX values use em and are ordered
depth, height, italic correction, skew, width. These tables are data, not a
layout engine.

The TeX width and TrueType advance deliberately remain separate. Combining
accents, multi-integral glyphs, CSS italic correction, and source-selected
special symbols require layout rules; changing the metrics to match advance
would lose the original semantics. Upstream has 20 TTFs and only 18 TeX metric
font tables. `Caligraphic-Bold` and `Fraktur-Bold` have no generated TeX table.
Typewriter metric entries U+0302, U+0303 and U+0308 have no direct cmap entry.
The generator reports these facts without making a replacement glyph or
inventing metrics. P1 font selection must implement upstream selection rules.

The original name ID 13 license declarations include the copyright and Reserved
Font Names. The full OFL body is vendored separately as `OFL-1.1.txt` from the
[official OFL text](https://openfontlicense.org/documents/OFL.txt), retrieved
2026-10-19. The official download SHA-256 was
`1d361a8f8e8ce6e68457dcd93fb56e162e6baa3bbb7e7573a290d44399f6b57e`.
The generic leading copyright placeholder is omitted; the original TTF
copyright and RFN declarations precede the unchanged license body in the
generated `licenses/KaTeX-FONTS-OFL.txt`. Its input and output hashes are in the
generation manifest. No font bytes or Reserved Font Names are modified.

The browser probe renders every private TTF, shows baseline and math-axis
guides, compares exact KaTeX CSS scaling, and samples italic correction,
fractions, scripts, sums, and an integral. Its result is checked independently
against TTF advances. Browser layout and its platform rasterizer remain
reference evidence; the probe does not validate native rendering or engine
compatibility.
