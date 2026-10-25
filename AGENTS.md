# AGENTS.md

Guidance for coding agents working in this repository.

## What this project is

VelociTex typesets TeX formulas using **KaTeX 0.19.0** mathematical rules.

There is exactly **one formula parser and one renderer**: the C++ core in
`native/core`. Both public endpoints call it:

| Endpoint                | How it reaches the C++ core                                                 |
| ----------------------- | --------------------------------------------------------------------------- |
| .NET (`VelociTex.Core`) | `native/core/api.cpp` (C ABI) through `src/VelociTex.Core/NativeInterop.cs` |
| JS / browser (npm)      | `native/wasm/katex_wasm.cpp` compiled to `velocitex-core.wasm`              |

There is **no** JavaScript formula parser and **no** Rust formula parser in the
repository. Do not reintroduce one. The browser package loads the C++ core
directly; the .NET WASM runtime is only used for the legacy SVG contract.

## Ground rules

- **KaTeX 0.19.0 is the single source of truth.** The reference sources live in
  `tools/reference/node_modules/katex/src/`. When behaviour is unclear, read the
  TypeScript there and port it literally. Never invent semantics.
- **Output must be byte identical.** The HTML and MathML the core emits are
  compared against KaTeX byte for byte. Class order, attribute order, style
  key order and number formatting all matter. Use KaTeX's own `makeEm`
  rounding (`+(n.toFixed(4))` semantics), not `std::round(n * 10000)`.
- **One parser, one renderer.** Do not add a second implementation of any
  KaTeX behaviour in JavaScript, C# or Rust. If the core cannot express
  something, extend the core.
- **Never weaken a test to make it pass.** The corpora under `tests/native/` are
  the acceptance criteria.
- **A user macro never hides a built-in macro.** KaTeX keeps the built-in macro
  list and the user macros in one namespace: a macro the user did not define
  still resolves to the built-in one (`\limsup` stays expandable when the user
  defines `\foo`). The lookup in `native/core/macros.cpp` (`TryGetDefinition`)
  searches the user table first and the built-in table second.

## Layout

```
native/core/            the C++ core
  api.h / api.cpp       stable C ABI used by .NET
  parser.cpp            TeX parsing into the semantic tree
  types.hpp             semantic tree, options, diagnostics
  layout.cpp            drawing layout (glyphs, rules, paths) for native/SVG
  mathml.cpp            KaTeX-compatible MathML serializer
  html/                 KaTeX-compatible HTML/CSS renderer
    domtree.*           DOM tree and markup serialization
    options.*           Options, Style, font metrics
    build_common.*      makeSymbol/makeOrd/makeSpan/makeVList/tryCombineChars
    build_html.*        buildExpression/buildGroup and the group dispatch
    accent.cpp array.cpp enclose.cpp delimiter.cpp sqrt_image.cpp
  generated/            generated tables (symbols, SVG paths, HTML paths)
native/wasm/            standalone WebAssembly entry points for the npm package
  katex.pgo             the profile of the core that the wasm build reads
native/pgo/             the training program that collects the profile
native/CMakeLists.txt   the native build
src/                    the .NET solution projects
npm/velocitex/          the npm browser package
tests/native/           byte-for-byte parity harnesses (no browser needed)
tests/wasm/             browser acceptance and benchmark harnesses
tools/reference/        KaTeX sources, generators and the pinned toolchain
  generate-pgo-profile.mjs  collects the profile of the core
```

## Build and test

### Native core

```bash
cmake -S native -B /tmp/vtbuild -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/vtbuild -j8
```

### Byte-for-byte parity (fast feedback loop, no browser)

```bash
VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-html-parity.mjs --corpus all --limit 0

VELOCITEX_NATIVE_DIR=$PWD/native VELOCITEX_BUILD_DIR=/tmp/vtbuild \
  node tests/native/katex-mathml-parity.mjs --corpus all --limit 0
```

`--corpus` selects `curated` (hand written feature corpus),
`realworld` (1605 real world formulas), `workloads` (the frozen benchmark
workloads), `fixtures` (the repository's own `tests/fixtures/core.json`, 3710
registered symbols, functions, macros and environments) or `all`.

Current state: **HTML 5411/5411 and MathML 5411/5411** byte identical to KaTeX
0.19.0 across all four corpora. The browser gate
(`node tests/wasm/katex-browser-parity.mjs`) repeats 1651 of them inside
Chromium and also compares the MathML output.

The error path is byte identical as well: for a formula KaTeX rejects, the
`throwOnError: false` markup (the `katex-error` span with KaTeX's message, its
source range and the underlined context) matches KaTeX for all 49 fixtures that
KaTeX itself can render, and the reported source ranges match for 41/41. A
sweep over the whole command table of KaTeX — every command with an unclosed
brace, 582 cases — reports no difference either.

### WebAssembly

```powershell
./native/build-wasm-katex.ps1     # standalone velocitex-core.wasm for npm
./native/build-wasm-core.ps1      # static archive for the .NET WASM runtime
./build-wasm.ps1                  # full npm distribution
```

The WebAssembly toolchain is the Emscripten SDK that ships with the .NET
`wasm-tools` workload; the scripts locate it themselves.

The build of the standalone module reads the **profile of the core**
(`native/wasm/katex.pgo`). The profile is a build input: it is refreshed with

```bash
node tools/reference/generate-pgo-profile.mjs
```

when the core changes. The script needs a `clang` with the profile runtime and
an `llvm-profdata`; it takes them from `CLANG` and `LLVM_PROFDATA` or from the
`PATH`.

### Browser acceptance and benchmark

```bash
node tests/wasm/stage-core-dist.mjs                 # stage a minimal dist/
node tests/wasm/katex-browser-parity.mjs            # byte diff in Chromium
node tests/wasm/katex-browser-benchmark.mjs          # the 5x performance gate
node tests/wasm/katex-parity.mjs                    # WASM vs KaTeX, no browser
```

## Conventions

- C++: 2 space indent, `PascalCase` functions, `//` comments, `snake_case`
  members. `clang-format` owns the native sources; Prettier does not parse C++.
- C++: **compare a UTF-16 string against a literal with the `_u16` suffix**
  (`name == u"display"_u16`). A bare `u"..."` is converted with
  `std::u16string_view(const char16_t*)`, which asks
  `char_traits<char16_t>::length` for the length, and that is a loop over the
  characters. GCC has no constant folding pass for `char16_t`, so every
  comparison against a bare literal walks the literal at run time — this was
  36% of the parse stage before the suffix was introduced. The suffix builds
  the view with a length that is known at compile time. The same applies to
  `LexToken::Is`, `text_is`, `starts_with`, `ends_with`, `find` and `replace`.
  Clang folds the length of a literal on its own, so the suffix does not help
  the Emscripten build; it costs nothing there and it keeps the two builds
  comparable.
- C++: **a comparison of a short text against a literal takes the literal as an
  array, not as a view.** `same_text(a, "mord")` (`native/core/types.hpp`)
  compares the bytes with a length that is known at compile time, so it is a
  load and a compare. `a == "mord"` does not: `basic_string_view::compare`
  compares the **shorter** of the two lengths, and that value is only known at
  run time, so `memcmp` cannot be inlined and every comparison costs a call
  into the C library. The renderer asks the question for every atom of a
  formula — the class of a node decides the spacing of its neighbours and
  whether a binary operator is cancelled, and the tag of a MathML node decides
  how it is written — so the call is worth removing. **The `_u16` suffix hides
  the length from the compiler**, because it builds a view: a comparison
  against a UTF-16 literal therefore takes the raw literal (`u"matrix"`), not
  the suffixed one (`u"matrix"_u16`). The suffix stays the rule for every
  other use of a UTF-16 literal.
- C++: **on WebAssembly, a `memcmp` of a known length is a call too.** The
  rule above holds for the native build, where libstdc++ inlines the
  comparison of a few bytes. The WebAssembly target does not: its DataLayout
  does not allow an unaligned access, so LLVM skips the expansion of the
  intrinsic into loads and keeps the call. **A comparison that is written by
  hand is the only one that is free there**, which is why `same_text` reads
  the bytes itself, four at a time, instead of calling `memcmp`. The same
  holds for a helper that compares a name against a list of names: the
  literals have to arrive as arrays, so the helper is a variadic template
  (`oneof` in `native/core/mathml.cpp`) and the literals are bare.
- C++: **a task of the parser is copied into the stack once, and the edit is
  applied to the copy in place** (`PushEdited`, `native/core/parser.cpp`). A
  task is a plain structure without a move constructor, so a "move" copies it
  as well: building the copy with `changed()` and handing it over by value
  copied the task three times per transition, and the copy of the task was the
  hottest single line of the parser. A structure that is copied should be
  copied into its final home and edited there.
- C++: **the task of the parser is still copied twice per transition, and the
  copy out of the stack is the hottest line of the parser** (2026-10-25). The
  loop of `ParserState::Run` takes the task out of the stack into a local
  (`auto task = w.Tasks.back();`) and the successor is pushed back as a copy
  of that local, so a 72 byte structure moves twice per transition. The line
  with the copy out is the hottest line of the parser in the native profile
  (19% of it) and in the WebAssembly profile, and the parser is the largest
  function of the pipeline (12%). The successor of a task is **the task
  itself with a few fields changed**, so the copy out is not needed: the task
  can stay on the stack and be edited there.
  Two things make that harder than it looks, and both are worth knowing
  before touching it:
  - **The `Return` marker has to run below the continuation.** The marker is
    pushed first and the continuation last, so the stack ends up
    `[..., Return, continuation]` and the continuation is processed first. A
    task that stays on the stack cannot have a marker pushed below it, so
    those cases have to take the task out and put it back — the copy stays
    there, but only for those cases.
  - **An edit that is applied in place reads the task it writes.** The
    lambdas write `v.Field` and read `task.Field`, and `v` would be the task
    itself, so a lambda that reads a field after writing it would see the new
    value. There is one such lambda (`v.Last = result; v.Previous =
task.Last;`) and it has to read the old value first.
- C++: **the standard library of the reference toolchain (GCC 11) has no
  `operator+` for a string literal on the left of a `string_view`.** The
  concatenation of a view was added in C++20 and reached libstdc++ with GCC
  12, so `u"text " + view` does not compile with the GCC of the reference
  machine — while it does compile in the Emscripten build, which uses a newer
  libc++. Build a message with `JoinMessage` (`native/core/parser.cpp`) or with
  `std::u16string::append`, never with `+` on a literal.
- C++: **a diagnostic without a position uses `SourceRange{-1, 0}`.** KaTeX
  raises several violations without a token (`commentAtEnd`, `mathVsTextUnits`,
  `mathVsSout`, `mathVsTextAccents`, the expansion limit), and those errors
  carry no source range and no underlined context. The JavaScript and the .NET
  API report the start of such a diagnostic as -1.
- C++: **a class name must live as long as the render.** The class list of a
  node holds the names as views, so a name that comes from a temporary string
  (`"col-align-" + align`, a `std::vector<std::string>` on the stack) dangles
  and the renderer writes garbage into the class attribute. The names are
  therefore literals, entries of a table, or a copy in the arena
  (`CopyToNodeArena`). The build with `-DVELOCITEX_CHECK_CLASS_NAMES` keeps a
  copy of every name and compares it with the bytes of the view when the name
  is written, so a dangling name is reported instead of written out; the whole
  corpus (5411 cases) runs clean with it.
- C++: **a node of the arena owns no heap memory.** The nodes are owned by the
  arena and are never destructed, so a `std::string` inside a node never
  releases its buffer — the SVG path data used to leak that way and a page ran
  out of the fixed WebAssembly heap after about thirty thousand renders. The
  text of a node is an `ArenaText`, and `CopyText` (not `CopyShort`, which
  only covers the head and the tail of a text of up to forty-eight bytes)
  copies it.
- C++: **a copy whose length is known at compile time is inlined; a copy whose
  length is a run time value is a call into libc++, and in WebAssembly that
  call costs more than the copy.** `memcpy(dst, src, n)` with a constant `n`
  becomes a load and a store; with a variable `n` it becomes a cross module
  call that the compiler cannot inline. The renderer makes a few hundred of
  them per formula (every class name of a node, every opening tag, every
  fixed size struct that is passed by value). Use `detail::CopyBytes` and
  `detail::CopyShort` in `native/core/html/markup.hpp`, and pass the big
  structs (`ClassList`, `VListParams`) by rvalue reference. This is the one
  change that was **faster in the browser and neutral natively**, and it is
  invisible in a native profile.
- C++: **a loop over the bytes is a call into libc++ as well.** The rule above
  is about `memcpy` with a run time length, but a loop that copies the bytes
  one by one is the same thing after the optimiser has seen it: LLVM
  recognises the pattern and replaces it with a `memcpy` whose length is only
  known at run time. `detail::CopyShort` had such a loop for the texts of one
  to three bytes, and the markup writer makes a few hundred calls of that
  length per formula (the last digit of a fraction, a class name of three
  letters). The loop is now two explicit moves (a two byte copy and, for
  three bytes, one more byte), so no call is left. Measured: the module has
  **12 `__memcpy` call sites fewer**, the share of `__memcpy` in the
  WebAssembly profile falls from **3.09% to 2.60%**, and the Node paired A/B
  reads **+2.18% / +1.92%** (the null control of the same window reads
  ±0.7%). The native side is neutral, because libstdc++ inlines the copy of
  three bytes.
- C++: **do not build a fragment twice.** The opening tag of a node used to be
  assembled in a buffer of the stack and then copied into the output buffer, so
  every byte of the tag moved twice. It now goes straight into the output.
  The stack buffer was worth it when the output was a `std::string` (every
  `append` called `memmove`); with the `MarkupBuffer` it is not, because
  `Append` is a capacity check plus one copy. Measured: the native paired A/B
  reads +3.1% / +3.5%, and the browser core direct test reads -7.2% on the
  median round and -12.1% on the mean — the gain is in the tail, not in the
  average.
- C++: **the number of writes into the markup buffer was a multiplier — and it
  stopped being one.** An instrumented build counts **784 writes and 4,407 bytes
  per formula** (HTML and MathML together), and 92% of the fragments are eight
  bytes or shorter — the cost is the _call_, not the copy. Merging what can be
  merged (the space of a class name with the name, the colon of a style
  declaration with the key, the semicolon with the value, the whole `makeEm`
  result in one piece, the pieces of the opening tag) takes it to **625 writes,
  -20%**. Every piece of the markup costs one call into the buffer, so a new
  piece of markup should be written in as few calls as the byte order allows.
  **But that was measured against a `std::string` output.** With the
  `MarkupBuffer` a write is a capacity check plus an inlined copy, and the rule
  no longer holds: joining the class names into a buffer of the stack took the
  writes from 625 to **547 per formula (-12.4%)** and changed **no** time at all
  (native paired A/B -0.46%, the two Node orders contradict each other, the
  browser cannot see it). Do not spend a change on it again.
- C++: **the memory footprint of the DOM tree is not a cost.** A formula
  builds 261 nodes, the node is 240 bytes and the class list alone is 112 of
  them, so the tree of one render is **38 KB** — more than the L1 cache of the
  machine, and the natural hypothesis is that the cache misses of that walk
  are what `NodeArena::Allocate` (2.64% of the native profile) pays for.
  **The probe kills the hypothesis — natively.** 128 bytes of padding in
  `HtmlNode` make the node 368 bytes and the footprint **71 KB (+87%)**; the
  markup stays byte identical and the time does not move: the native paired
  A/B reads 99.39% (the padded build slower) in one order and 101.13% (the
  padded build faster) in the other, against a null control of 100.55% in the
  same window.
- C++: **the footprint of the DOM tree is a cost in the browser, even though it
  is not one natively.** The native probe above was run on the native build, and
  the browser is the target: **the same probe in the browser says the opposite.**
  The build with `-DVELOCITEX_PAD_NODES` (the flag is in
  `native/core/html/domtree.hpp`) pads the node by 128 bytes, and the four
  instance harness reads it **2.46% slower** in one order and **1.35% slower** in
  the other (the quietest window of the group), against a null control of
  exactly 1.0000. So the tree of a formula does not fit into the cache of the
  browser, and every byte of the node is paid twice: once when the node is
  built and once when it is walked.
  **The family of ideas that shrinks the node is therefore not dead** — it is
  dead _natively_, and alive in the browser. What is worth trying there:
  - The **class list is 112 bytes of the 240 bytes of the node** (and 64 of the
    192 bytes of the WebAssembly node, where a pointer is four bytes): twelve
    pointers and twelve lengths. A list of indices into a table of the names
    would take sixteen bytes.
  - The **children and the attributes of a node are two vectors**, 48 bytes of
    the node, and most nodes carry neither: a side table with an index would
    take eight.
    The measurement that decides each of them is the browser probe, not the
    native paired A/B: a change that is neutral natively can still be worth a
    percent in the browser.
- C++: **in WebAssembly, "is it a call into the runtime" matters more than
  "how much work is done".** Two rules follow from it, and both are worth more
  than the algorithm they replace:
  - **A `thread_local` whose type has a constructor or a destructor carries an
    initialization guard on every access.** In Emscripten that guard is a real
    call (`__tls_init`), and the reused state of the renderer is touched
    hundreds of times per formula (the node arena alone about 300 times). The
    state is therefore declared at **namespace scope** (`native/core/state.hpp`):
    in the WebAssembly build it is a plain static, which the module initializes
    before it runs, and in the native build it stays a `thread_local` because
    the .NET endpoint renders from several threads. The node arena and the
    allocation pool are additionally made **trivially constructible and
    trivially destructible** (no member initializers; a `thread_local` of such a
    type is zero initialized), so they need no guard at all. Measured: the
    WebAssembly build **+4.45%** (Node paired A/B), the browser core test
    **-5.2%** on the fastest round and **-4.0%** on the median round.
  - **The native build still pays for the guard, and that is a lead for the
    .NET endpoint.** In the WebAssembly build the reused state is a plain
    static, so there is no guard at all; in the native build it stays a
    `thread_local`, and the state that is not trivially destructible (the
    scratch key of the macro lookup, the retired buffers of the arena, the
    stacks of the MathML writer, the pool, the table of the font indices)
    carries the guard. The native profile attributes **0.99%** of the time to
    `__tls_init`, and the callers are spread over many functions, so the
    attribution may be skewed but the guard is paid on many accesses. Making
    those types trivially destructible is the same treatment the WebAssembly
    build got; it does not show up in the browser at all, because the guard
    does not exist there.
  - **An array that is never read past its length must not be zeroed.** The
    class list holds its names in a fixed array and only the first `count_` of
    them are ever read, but the array had a default initializer, so **every**
    class list of a formula paid for zeroing 108 bytes. The names are not
    initialized any more, and the class list therefore carries its own copy and
    move, which only carry the names it holds — copying the rest would read
    indeterminate values. The detector build
    (`-DVELOCITEX_CHECK_CLASS_NAMES`) reports no dangling name over the whole
    corpus. Measured: native **+0.86% ~ +1.54%**, the browser as above.
- C++: **a profile percentage is not the part you can take away.** The WASM
  profile shows `GetCharacterMetrics` at 1.9%, and the font name was hashed
  once per symbol — 21.7 hashes per formula. Resolving the font once (the
  metrics are then looked up by an index) removed **all** of those hashes and
  the per-delimiter `std::to_string` allocation, and the **time did not
  move**: native **+0.35% / +0.58%** (null +-0.6%), Node **+0.59% / +0.75%**
  (null +-0.7%). It is published because the work is deterministically gone
  and a font becomes a value instead of a string — not because it is fast.
  The lesson: a hot function in the profile is mostly the _work_ it does,
  and only a part of it is overhead. Measure the part, do not assume it.
- C++: **a length of the MathML output is formatted into the value pool, not
  into a string.** `FormatNumber` (`native/core/mathml.cpp`) writes the number
  into a buffer of the stack and the pool takes it from there, so a length
  costs no heap allocation. The old code built three or four `std::string`
  per call and then copied them into the pool. The core now makes **2.94 heap
  allocations per formula** (HTML and MathML together).
- PowerShell: **the Windows build of the core has two rules that the CMake build
  and the WebAssembly scripts do not enforce.** MSVC derives the object file
  name from the source file name and writes every object of the build into one
  directory, so two translation units with the same base name overwrite each
  other and the linker drops one of them without a word (LNK4042); the build
  refuses to start when the source list holds a duplicate. And MSVC resolves a
  quoted include next to the including file before it looks at the include
  search path, so the build passes `native/core` as the include root, the same
  root the CMake build and both WebAssembly scripts pass: a header under
  `native/core/html` is reachable as `html/markup.hpp` from every translation
  unit. A change to the source list has to keep the four lists in sync:
  `native/CMakeLists.txt`, `native/build-core.ps1`, `native/build-wasm-core.ps1`,
  `native/build-wasm-katex.ps1` and `tools/reference/generate-pgo-profile.mjs`.
- PowerShell: **`Start-Process` binds `-ArgumentList` to a `string[]`, so the
  list has to stay flat.** A nested array cannot be converted to a string and
  the call fails with "Cannot convert 'System.Object[]' to the type
  'System.String'". Concatenate the flags instead of nesting them:
  `$arguments = $optimization + @('-std=c++20', ...)`.
- PowerShell: **the writable Emscripten cache of the standalone module has to
  live on the drive of the SDK.** Emscripten builds the system libraries that
  the frozen cache does not carry into the cache directory and relates the
  sources of the frozen cache to that directory with a relative path;
  Windows refuses to relate two paths on different drives, and the
  repository is on another drive than the SDK on the build agent. The cache
  therefore goes beside the SDK, under the .NET root
  (`native/build-wasm-katex.ps1`).
- C#: the `vt_options` struct in `native/core/api.h` is mirrored by
  `NativeInterop.Options` in `src/VelociTex.Core/NativeInterop.cs`. The mirror
  is exact, including `StructSize`; change one, change the other.
- JavaScript: ESM, Prettier formatting, no runtime dependencies.
- Generated data under `native/core/generated/` and `src/VelociTex.Core/Generated/`
  is produced by the locked generators in `tools/reference/` and
  `tools/VelociTex.DataGeneration/`. Regenerate, never hand-edit. Note that the
  C++ and C# symbol tables are separate generated artifacts that must be
  updated together.

## Working on the HTML or MathML renderer

1. Read the KaTeX source for the function you are porting.
2. Implement it in the matching `native/core/html/*.cpp` (or `mathml.cpp`).
3. Add or extend the curated corpus in `tests/native/katex-html-corpus.json`.
4. Run the parity harness and confirm the count went up and nothing regressed.

Large modules are split per KaTeX file so that they can be worked on
independently; they are wired together through `native/core/html/module_api.hpp`.
Keep the module boundaries intact.

## Performance

The browser package is **faster** than KaTeX's JavaScript implementation, but it
does **not** reach the 5x requirement of the project brief. The harness still
asserts `--min-ratio 5` and therefore still fails; do not lower the gate to
make it green.

**The C++ core is about 5x faster than KaTeX when it runs natively.** The same
corpus (36 frozen formulas) split into the matching stages. Both engines are
measured with the same harness shape as the browser benchmark (25 warmup
rounds over the whole corpus, then 25 measured rounds, best round wins, same
machine, same run):

| Stage                    | KaTeX (JS) | native (gcc 11.4) | native (clang 20) | Ratio | VelociTex WASM (Node) |
| ------------------------ | ---------: | ----------------: | ----------------: | ----: | --------------------: |
| parse                    |     ~29 us |           ~6.3 us |           ~5.2 us |  5.6x |               ~5.7 us |
| HTML build + serialize   |    ~145 us |          ~18.3 us |          ~16.3 us |  8.9x |              ~26.3 us |
| MathML build + serialize |     ~24 us |           ~5.9 us |           ~5.9 us |  4.1x |              ~10.1 us |
| total (html + mathml)    |    ~215 us |          ~32.2 us |          ~29.3 us |  7.3x |              ~42.1 us |

The ratio has to be read with care: the stage numbers of the two engines come
from different harnesses, and on this 2 core reference machine a single round
is worth 10-20%. The reproducible statement is the one the harness prints:
KaTeX needs ~215 us per formula, the native core needs ~32.2 us (gcc) or
~29.3 us (clang), the WebAssembly build needs ~42 us and the browser build
50-65 us.
**The stage shares of the WebAssembly build were re-measured with the ablation
method** (switch a stage off with a compile-time flag and compare the two
builds alternately in one process): the HTML stage is ~65% of the pipeline,
the MathML stage ~35% and the parse ~12%. The mode subtraction
(`parse = mode1 + mode2 - mode0`) underestimates the parse, because mode 0
carries the HTML _and_ the MathML. See "The shape of the MathML writer" below.
The native columns and the WebAssembly column do not compare like for like
unless the same compiler is used; the compiler difference is measured below.

**Native changes are judged with a native paired A/B, not with the wall clock.**
The Node-side wall clock cannot resolve a 1% change of the core on the
reference machine (the interference lasts much longer than one measurement
window, so it hits one build and not the other). The native side can: build
two drivers from the two sources with the same compiler, alternate them over
groups of the corpus, time with `CLOCK_PROCESS_CPUTIME_ID` (process CPU time,
unaffected by other processes) and keep the minimum of every group. The null
control (the same binary against itself) reads **100.02%**, so a 1% change of
the core is measurable there. Warm the corpus up before the measurement window
(the symbol tables are built once per process) and reuse the workspace and the
document, exactly like the WebAssembly entry does.

**The stage shares, re-measured with the ablation method (2026-10-24).** Four
builds with compile-time flags (`-DVELOCITEX_ABLATE_MATHML`,
`-DVELOCITEX_ABLATE_HTML_SERIALIZE`, `-DVELOCITEX_ABLATE_HTML`) measured
alternately in one process: parse ~17-20%, HTML tree build ~40-44%, HTML
serialization ~16-21%, MathML ~17-24%. The HTML tree build is the bigger half
of the HTML stage. The WebAssembly tax per stage (WASM divided by native) is
parse 1.65x, HTML tree build 2.19x, **HTML serialization 2.55x**, MathML
1.44x: the tax sits in the part that turns the layout into markup, because
that part is all memory access and every access in WebAssembly is more
expensive than in the native build. **The bounds check is not the reason**
(see "The WebAssembly tax is not the bounds check" below).

**State the runtime of the KaTeX reference whenever a ratio is quoted.** The
same `katex.mjs` runs about 40% slower in Node 24 (232-288 us per formula)
than in Chromium (156-244 us per formula; the gate runs of the day read
153-156 us for the fastest round and 186-214 us for the median). The native
core at 27.5 us is therefore 8.4-10.5x against the Node reading and 5.7-6.9x
against the Chromium one.

**How the WebAssembly numbers are measured.** The three output modes of
`vt_render` (0 = HTML and MathML, 1 = HTML only, 2 = MathML only) all include
the parse, so `parse = mode1 + mode2 - mode0`. The measurement uses the CPU
time of the process (`process.cpuUsage()`), not the wall clock: the reference
machine has two busy cores and the wall clock moves by more than the
differences that are measured. Two further things matter: the two builds are
compared **in one process, alternately**, and the input buffer of the harness
must be large enough for every formula — a buffer that is too small writes
over the WebAssembly heap and makes the formulas fail, which is faster and
therefore looks like a win.

**Compare the two builds pairwise, and always run a null test.** On a loaded
machine the per-round ratio of two builds swings by +-40%, so the fastest
sample of each build is not a reliable estimator: the readings of the same
pair differed by 6% between runs. The estimator that holds up is the **median
of the per-round ratios** of a run of at least a thousand rounds, with the two
builds measured back to back inside every round. Its noise floor was +-0.6%
in the measurements of the previous round and +-0.15% in the quiet window of
the last one (a build against itself), so a difference below 1% is not a
result. The paired median of a thousand rounds agrees with the native
measurement, where the same change is visible in a single stage.

So the remaining gap is **not** in the algorithm: byte identical output does
not cost speed. The gap is the WebAssembly and browser boundary.

**A measurement that only exists in Node is not a result.** The paired A/B
protocol above runs in Node, and Node and the browser do not always agree:
the fixed linear memory (see the negative results below) is worth 56% in
Node and nothing in the browser, because the two V8 builds take different
paths for the bounds checks of a memory that may grow. Every change that is
published is therefore confirmed in the browser, which is the only place the
gate runs. A number that only the Node harness produces is a hypothesis.

**The browser gate cannot resolve a change of three percent.** A round of
the benchmark renders 36 formulas; the core needs about 2.2 ms for that and
`performance.now()` has a resolution of 100 us in Chromium, so a round of the
core is quantized in steps of about 4.5%. Three runs of the same build read
3.32x, 3.33x and 3.33x, and a change of three percent does not move the
number. A change of that size is measured with a **paired A/B of two variants
in one page**: the variants share the same WebAssembly instance (they import
the same module), the rounds measure them back to back in the order A,B,B,A,
and the median of the per-round ratios is the result.

**A change of one percent is measured in the browser too, with two modules in
one page.** The core test that measures one build per browser session lands
the two builds in different load windows, and a round of the core is
quantized in steps of about 1.1% (100 us on a 9 ms round), so the median of
the per-round ratios cannot resolve less than that: with two builds that
differ by one percent the median of the ratios sits on a step of the
quantization and reads 1.0000 or 1.0106 depending on the order. **The
estimator that resolves it is the quietest window** — the minimum over all the
rounds of each build — and the two orders of the pair have to agree.

The harness instantiates the two modules from their own bytes inside one page
(`WebAssembly.instantiate`), so the measurement is not disturbed by the
JavaScript layer of the package, and it calls the exports of the module
directly (`vt_alloc`, `vt_render`, `vt_result_len`, `vt_free_result`), the
way the package does. The two modules have their own memory, so they do not
share the state of the core. The rounds measure them back to back in the
order A,B,B,A, and the corpus is repeated inside the timed round (eight
times), so that a round is long enough for the quantization to be a small
part of it.

| pair (the second build is the new one) | quietest window | median of the per-round ratios |
| -------------------------------------- | --------------: | -----------------------------: |
| the task stack of the parser (batch 9) |    2.30%, 2.30% |                   2.22%, 2.17% |
| the same, with the order swapped       |    1.14%, 1.14% |                   0.00%, 0.00% |
| the same module twice (the null)       |           0.00% |                          0.00% |

The four readings agree that the new build is the faster one (1.1% to 2.3%),
which is what the Node paired A/B reads (+1.30% and +1.90%). The null control
reads exactly 1.0000, because the quantization puts the two minima on the
same step.

**The two module harness has a bias of about one percent, and the null
control of the previous paragraph does not show it.** The null above was read
with the corpus repeated eight times inside the timed round, and the two
minima landed on the same step of the quantization. With the corpus repeated
six times instead, the round is about 6.6 ms and the quantization is 1.5%,
and **the null of the two module harness reads -1.15%**: the second instance
of the page is about one percent faster than the first one. The order inside
the round does not cancel it — the harness alternates A,B,B,A and B,A,A,B from
one round to the next — so the cause is the **instance**, not the position:
the two modules share the code space of the engine, they are compiled at
different addresses, and the second one is a little luckier.

**The harness that measures the difference is the one with four instances.**
Two copies of each build, the first two instances are the group A and the
last two the group B, and the order inside the round alternates. Every group
holds one instance that was loaded early and one that was loaded late, so the
placement bias of the group averages out, and **the null reads exactly
1.0000 in both orders**. The corpus is repeated 24 times inside the timed
round, so that a round is about 26 ms and the quantization of
`performance.now()` is 0.4% of it instead of 1.5%.

| pair (four instances, two per build)   | quietest window | median of the per-round ratios |
| -------------------------------------- | --------------: | -----------------------------: |
| the copy of the short text (batch 10)  |           0.75% |                          0.37% |
| the same, with the groups swapped      |           1.14% |                          1.10% |
| the same module four times (the null)  |           0.00% |                          0.37% |
| the same null, with the groups swapped |           0.00% |                          0.34% |

Both orders put the new build ahead, by 0.75% and 1.14% on the quietest
window, and the Node paired A/B of the same change reads +2.18% and +1.92%.

**In Node the two builds have to be in two processes.** The two module
harness of Node has the same problem as the one of the browser — the null of
the harness that alternates the order inside the round reads 1.0000, but the
ratio of the pair depends on the order the modules are **listed** in, by up
to two and a half percent, and the readings of the two orders contradict each
other. The cause is the same: the code of the two modules is compiled into
one code space, at different addresses, and the two instances interfere with
each other. The measurement that has no such artifact is one build per
process: the process loads the module, warms it up, renders the corpus and
reports the CPU time of the process (`process.cpuUsage()`), and the driver
alternates the two builds over ten runs and takes the quietest window of each.
The null of that harness reads ±0.7%.

| pair (one build per process)           | quietest window |    p10 | median |
| -------------------------------------- | --------------: | -----: | -----: |
| the copy of the short text (batch 10)  |           2.18% |  3.45% |  3.65% |
| the same, with the builds swapped      |           1.92% |  2.66% |  5.05% |
| the same module twice (the null)       |          -0.67% | -0.63% |  0.21% |
| the same null, with the builds swapped |           0.31% | -0.35% |  0.61% |

Both orders put the new build ahead by two to three percent, against a null
control of ±0.7%. The profile agrees: the share of `__memcpy` in the
WebAssembly pipeline falls from 3.09% to 2.60% over the same change.

What was measured in the browser on the 2 core reference machine with
`tests/wasm/katex-browser-benchmark.mjs` (36 frozen formulas, 120 paired
rounds, the median of the per-round ratios):

| Quantity                      | KaTeX (JS) | VelociTex (C++ WASM) |
| ----------------------------- | ---------: | -------------------: |
| fastest round                 | 5.4–5.8 ms |           1.5–1.7 ms |
| median round                  | 6.2–7.4 ms |           1.7–2.0 ms |
| median of the per-round ratio |            |       **3.11–3.63x** |
| fastest round's ratio         |            |           3.18–3.73x |

The readings of the day are 2.96x, 3.00x, 3.04x, 3.08x, 3.14x and 3.17x —
one per release of the core; the current build reads 3.93x, 3.73x and 3.81x on
three runs (the median is 3.81x); the build before it read 3.75x, 3.81x and
3.80x on three runs and 3.73x, 3.88x and 3.76x on the three runs before those
(the median of the six is 3.78x), the one before that (the profile
guided build, with the profile collected from the source that is published)
read 3.71x,
3.73x, 3.75x, 3.75x, 3.76x and 3.78x on six runs (the median is 3.75x, the fastest
rounds read 3.67x to 3.93x), and the six runs before it read 3.65x, 3.67x,
3.75x, 3.75x, 3.81x and 3.83x (the fastest rounds read 3.67x to 4.00x); the
build before that
read 3.61x, 3.68x and 3.68x; the one before that
read 3.50x, 3.63x and 3.67x; the one before that
read 3.59x, 3.63x and 3.63x; the one before that read
3.44x, 3.44x and 3.42x; the one before that
read 3.41x, 3.39x and 3.33x; the one before that
read 3.38x, 3.39x and 3.39x; the one before that read 3.28x, 3.47x and 3.44x);
the one before that read 3.33x, 3.35x and 3.41x, and the one before that read
3.23x and
the one before it reads 3.26x, 3.33x and 3.33x on three runs (the best rounds
3.17x, 3.32x and 3.44x). The v33 build (which only touches the error path) read
**3.16x and 3.17x** on two runs of a loaded machine (the best round 3.24x); the
paired A/B against the build before it reads -0.84%, so the core itself did not
change. With the synchronous JavaScript glue of the package the five runs of
the day read 3.11x, 3.26x, 3.29x, 3.25x and 3.35x — **the gate script drifts by
±5% on this machine, so a change is judged with a paired A/B, not with these
readings** (the glue change is -1.9% against a null control of ±0.3%).
Under the old estimator ("the fastest round of each engine") the same machine
swung between 2.4x and 3.7x.

**The browser harness uses the paired estimator, not "the fastest round of
each engine".** On a loaded machine the two fastest rounds land in different
load windows, and the readings of the same pair differed by 6% between runs.
The script now warms both engines up for 25 rounds, measures 120 paired rounds
and reports the **median of the per-round ratios** (plus the fastest round of
each engine and the fastest round's ratio for comparison), and it asserts the
byte equality of the markup as well. Both estimators agree on this machine
(3.17x and 3.26x).

**The warmup is not the bottleneck.** The WebAssembly module needs many rounds
before it reaches its steady state (the first round is 10-20x slower than the
steady state), but KaTeX's JIT warms up at the same time: extending the warmup
from 25 to 120 rounds does not change the ratio (measured per window:
2.79x for rounds 0-24, 2.60x for 25-99, 2.59x for 100-199, 2.81x for 200-299,
2.77x for 300-399).

A per layer breakdown in the same browser session (us per formula) shows where
the browser time goes. Every pair is measured back to back inside every round
(A,B,B,A) and the result is the median of the per-round ratios; the corpus is
repeated six times inside the timed round, because `performance.now()` has a
resolution of 100 us (see the trap below). The absolute values drift by +-20%
on this loaded machine, so **the ratios are the result, the absolutes are not**.

| Call path                                | us/formula |  vs KaTeX |
| ---------------------------------------- | ---------: | --------: |
| `katex.renderToString`                   |    182-244 |     1.00x |
| `velocitex.renderToString` (npm package) |      50-62 | ~3.2-3.5x |
| the package's own `renderKatexMath`      |      48-57 | ~3.4-3.7x |
| `wasm.vt_render` (no JavaScript glue)    |      44-52 | ~3.6-4.1x |

**The JavaScript layer of the package costs 6-7 us per formula, not 13.**
Measured in one page with one WebAssembly instance, over the 36 frozen
workload formulas, with the corpus repeated inside the timed round (the
resolution of `performance.now()` is 100 us, so a round of 36 formulas of the
core — about 1.7 ms — resolves 2.8 us per formula; see the trap below). The
paired ratios are the trustworthy part:

| pair                                            |     ratio | reading                                                              |
| ----------------------------------------------- | --------: | -------------------------------------------------------------------- |
| public API / package fast path                  |     1.061 | the package layer (options JSON, promise, result object) is about 6% |
| package fast path / bare `vt_render`            |     1.080 | the encoding and the decoding are about 8%                           |
| **public API / bare `vt_render`**               | **1.146** | **the whole JavaScript layer is 14.6%, the core is about 87%**       |
| bare `vt_render` / bare `vt_render` (null test) |     1.016 | the noise floor of the protocol                                      |
| KaTeX / public API                              |      3.48 | the same order as the 3.11-3.63x of the gate                         |

The segments behind those ratios: the `TextDecoder.decode` of the 1162 byte
payload costs **0.42 us** on its own, the encoding of the source 1.2-1.4 us,
the options JSON of the package 0.46 us (it builds the JSON itself, it does
not call `JSON.stringify`), and the promise plus the result object make up the
rest. The package's fast path is not slower than a hand written bare call into
the module: it keeps the view over the module memory and encodes the source
itself, so it wins back what the naive version pays for the platform encoder
and for a fresh view. Copying the payload into a buffer of its own before it
is decoded costs 10.2 us, so the copy is not an alternative to the decode.

So the core is **~87%** of the browser path and the JavaScript layer 12-15%.
Even a glue that costs nothing would only reach ~3.5x, so the core is the
lever. Reaching 5x in the browser needs **31-38 us per formula** (KaTeX's
fastest round of ~153 us gives a budget of ~30.6 us, its median round of
~190 us gives ~38 us), which means the core has to reach ~25-31 us in the
browser and **16-20 us natively** instead of 27-29 (the native core measures
26.4 us per formula today).

**The public API does not build a promise on the hot path any more.** The
package's `compile` is split into a synchronous fast path (`compileFast`, the
module is loaded) and an asynchronous slow path (`compileSlow`, it waits for the
module and falls back to the .NET runtime). `renderToString` tries the fast path
first and, when it hits, never awaits: the caller gets the markup in the first
microtask instead of the third. The published type is still `Promise<string>`
and the slow path is semantically unchanged. Measured in the browser with two
glue layers sharing one WebAssembly instance (A,B,B,A, the corpus repeated
inside the timed round): **1.0194 / 1.0180 / 1.0198** — the new glue is
**1.8-1.9% faster**, against a null test of 0.9973-0.9999. A semantic diff of
31 cases in Node (the module is fed from disk) shows the fast path is
character for character identical; the five cases that fall back to .NET differ
only in the directory path inside the error message.

**A copy whose length is known at compile time is inlined; a copy whose length is
a run time value is a call into libc++, and in WebAssembly that call costs more
than the copy.** This was the third batch of core changes (2026-10-24) and it is
the clearest example of a change that the native profile cannot see: the native
paired A/B read +1.3% to +2.4% (two orders, null control 100.19%), the Node
WASM A/B read +4% to +6%, and the browser core direct test read **-2.9% on the
fastest round, -5.7% on the median round and -8.0% on the mean** — three
estimators, one direction. The change: `TagBuffer::Append` and
`MarkupBuffer::Append` copy the fragments with `detail::CopyBytes` (moves of 8,
4, 2 and 1 bytes, all compile time constants) instead of `std::memcpy` with a
run time length, and the delimiter functions take their `ClassList` by rvalue
reference instead of by value. The profile confirms it: `__memcpy` fell from
**4.59% to 2.25%** of the WebAssembly pipeline and `HtmlNode::WriteMarkup`
from 8.61% to 6.92%. The same reasoning removed the moves of the vlist children
in `MakeVList`: the child is now built in the vector itself instead of in a
local that is moved into it, because the move of a struct of that size is a
libc++ call in WebAssembly too.

**The lesson to keep: in WebAssembly, "is it a libc++ call" matters more than
"how many times do we copy".** A copy of a known size is a load and a store; a
copy of an unknown size is a cross module call. The class list rewrite of the
second batch made the class list bigger (64 bytes) and moved it through three
layers, and that showed up as a **3.18 point** jump of `__memcpy` in the
profile — the native side read +5.0% for the same change while the browser read
3-4% slower. The two sides of that change are in `docs/benchmarks.md`.

**A fragment must not be built twice.** The last change of the third batch
removed the buffer of the stack that the opening tag of a node was assembled
in: the tag now goes straight into the output buffer, so every byte of it
moves once instead of twice. It is the largest single win of the batch —
the native paired A/B reads **+3.1% / +3.5%** (two orders, null control
100.13%) and the browser core direct test reads **-7.2% on the median round**
and **-12.1% on the mean** while the fastest round barely moves. The gain is
in the tail, not in the average, which is what a removed copy in the middle
of the writer does to a loaded machine. It also **overturns an earlier
conclusion**: the first batch measured "write the opening tag in one piece"
as a win, but that was against a `std::string` output, where every `append`
called `memmove`; with the `MarkupBuffer` the merged write only saved a few
capacity checks and paid for a second copy of the whole tag.

**The lengths of the MathML output do not need a string of their own.** The
number formatter of the MathML writer built three or four `std::string` per
call (a raw text, a digit string that was built character by character, the
result and the `substr` temporaries) and the pool copied them again. It now
formats into a buffer of the stack and the pool takes the bytes from there:
**2.94 heap allocations per formula**, HTML and MathML together. The native
paired A/B reads +0.8% / +1.1% (two orders, null control 100.05%), the
WebAssembly build of Node reads +3.3% / +1.1% (the quietest windows), and the
browser core direct test reads -2.0% on the fastest round, -1.8% on the
median round and -0.7% on the mean (eleven pairs).

**A negative result from the same batch: do not split the short path of
`MarkupBuffer::Append` off.** A version with a sixteen byte fast path and a
call for the rest measured **+1.9% / +1.3% natively** (paired A/B, two
orders, null control 99.74%) and **+0.5% / +4.0% in the WebAssembly build
of Node**, but **-1.5% on the fastest round, -4.7% on the median round and
-4.8% on the mean in the browser** (six pairs of the core direct test). The
browser is where the gate runs, so it was reverted; the source is back to the
published state byte for byte. Native and Node agreed and the browser did
not — the rule "a measurement that only exists in Node is not a result" holds
here as well.

Four traps in this measurement, all of which produced wrong numbers first:

- **Do not load a second copy of the module in the same page.** The npm package
  instantiates the core, and instantiating it again for a hand written
  reference doubles the linear memory (16 MB each) and makes the two instances
  compete for cache and CPU. That page measured the core and the package at
  75 us each, where the single instance measures 51 against 60.
- **A loop variable that holds a length is not a loop variable that holds a
  content.** The first version of this breakdown stored the _number of bytes
  `encodeInto` wrote_ for every formula and then rendered the buffer with it,
  so the buffer always held the last formula while the length came from
  another one. The "rotate through the corpus" loop therefore rendered
  mismatched garbage, which unwound a WebAssembly exception and measured
  144 us per formula against 18.5 us for the same formula over and over —
  the exact opposite of the truth. The buffer and the length have to come from
  the same formula.
- **The options JSON is not a lever.** The core caches the parsed options by
  the JSON string, so it only re-parses them when the string changes. The
  benchmark alternates `displayMode`, so the cache thrashes — and the whole
  path still costs only 0.17 us per formula.
- **A round of 36 formulas resolves 2.8 us per formula, and the differences
  that were blamed on the JavaScript layer are of that order.** The core needs
  about 1.7 ms for the corpus and `performance.now()` in Chromium has a
  resolution of 100 us, so the per-formula value of a round is quantized in
  steps of 2.8 us (about 6% of our engine, but only 0.3% of KaTeX's). A
  measurement that reads "the glue costs 8.3 us and the package layer 6.5 us"
  was reading the quantization, not the glue: repeating the corpus inside the
  timed round (six times per formula, so the round is 11 ms) puts the whole
  JavaScript layer at 6-7 us. **Any difference below 3 us per formula in the
  browser has to be measured with the corpus repeated inside the round.**
- **KaTeX and VelociTex must not share a round when the segments are
  measured.** The allocation of the KaTeX segment pushes the garbage collector
  onto the segment that follows it, and the result was "the public API is
  faster than the fast path inside it", which cannot be. The KaTeX comparison
  runs in its own group of rounds.

**Where the native 32.2 us goes** (measured by switching the HTML
serialization off and comparing, `docs/benchmarks.md` has the method): parse
6.3 us (20%), HTML tree build 13 us (40%), HTML serialization 6 us (19%),
MathML 6.5 us (20%). The HTML tree build is KaTeX's own algorithm (sizes,
vlist, classes, styles) and cannot be skipped; emitting the markup directly
instead of building a DOM tree therefore saves at most the 19% of the
serialization, not a factor.

**The shape of the MathML writer is measured, not guessed.** A counting build
of `native/core/mathml.cpp` over the 36 workload formulas: **32.9 nodes, 35
`build` recursions, 23.6 interns into the value pool and 64 serialization steps
per formula**. The ablation method (build a variant with a stage switched off
by a compile-time flag, measure the two builds alternately in one process with
a rotating order, and take the median of the per-round ratios) splits the
MathML stage of the WebAssembly build into:

| part                                                     |     reading | share of the stage |
| -------------------------------------------------------- | ----------: | -----------------: |
| the walk over the semantic tree (styles, sizes, colours) |     ~1.4 us |                ~8% |
| **the graph (`Writer::build`)**                          | **~5.2 us** |           **~31%** |
| **the serialization (`Writer::serialize`)**              | **~3.0 us** |           **~18%** |
| the rest (the setup and the wrapper)                     |       ~2 us |               ~12% |

So "build while writing" has a ceiling of **~8.2 us, which is 17% of the whole
WebAssembly pipeline** — the old estimate of "60% of the stage" was optimistic.
One sub-item is already ruled out: the numeric attributes (`number()` and
`em()`) are called **0.86 times per formula**, so their `std::to_chars` and
their `std::string` surgery are not a hot spot at all.

**The ablation method is the only reliable one for the stage shares.** The
three output modes of `vt_render` give `parse = mode1 + mode2 - mode0`, but
mode 0 carries the HTML _and_ the MathML, so the subtraction underestimates
the parse by the cost of the extra copy. Switching a stage off with a
compile-time flag and comparing the two builds is exact. Two rules apply: the
builds are measured alternately **in one process**, and **the order of the
builds inside a round is rotated** — a fixed order makes the build that is
always measured first systematically slower (it turned a 5 us difference into a
3 us one, and even inverted the sign of a comparison).

**The native build pays 9% for dynamic TLS; the WebAssembly build does not.**
The native build is a `.so` that .NET loads with `dlopen`, so every access to
a `thread_local` goes through a `__tls_get_addr` PLT call: 7.26% + 1.80% (the
PLT entry) = **9.06%** of the native profile. The hot ones are on the hot
path — the node arena of `html/domtree.cpp` (about 300 bump allocations per
formula), the font metrics of `html/options.cpp` and the symbol info of
`html/build_common.cpp`. **Emscripten compiles `thread_local` into a plain
global when there are no threads** (the module has no TLS segment and no
`__tls_get_addr`), so the browser path does not pay it at all. Fixing it
means threading a per-render context through the whole HTML builder (or
`-ftls-model=initial-exec`, which makes `dlopen` depend on the static TLS
surplus — a deployment risk for a library that .NET loads).

**The WebAssembly penalty sits in the renderer, not in the parser.** The
stage split of the WebAssembly build (the three output modes of `vt_render`,
measured with the CPU time of the process) gives parse 5.7 us, HTML 26.3 us
and MathML 10.1 us against the native 5.2 / 16.3 / 5.9 us of the same clang
build, measured in the same window. The parser is barely slower in
WebAssembly; the HTML build costs 61% more and the MathML build 73% more,
which is 14 us of the 13 us the whole build is behind. The renderer is where
the WebAssembly boundary hurts.

**The per-function comparison says the same thing, and it ranks the work.** The
same source, the same 36 workload formulas, profiled twice — natively with
`perf` (clang 20, `-O3`, the same compiler as the WebAssembly build) and in
WebAssembly with the V8 profiler (a `-g` build with a name section) — and the
function names normalized on both sides:

| function                                     | native |   wasm | ratio |
| -------------------------------------------- | -----: | -----: | ----: |
| `ParserState::Run` (the parse loop)          |  9.53% | 11.09% |  1.16 |
| `HtmlNode::WriteMarkup` (HTML serialization) |  2.76% |  6.55% |  2.37 |
| `write_mathml` (the MathML entry)            |  2.76% |  5.54% |  2.01 |
| `Writer::build` (the MathML graph)           |  2.84% |  5.11% |  1.80 |
| `MakeVList`                                  |  2.73% |  3.65% |  1.34 |
| `AppendEm` (`makeEm`)                        |  2.70% |  3.50% |  1.30 |
| `Expand` (the macro expander)                |  2.79% |  3.42% |  1.23 |
| `memcpy` + `memmove`                         |  2.13% |  3.18% |  1.49 |
| `GetCharacterMetrics` (the font metrics)     |  0.41% |  1.83% |  4.45 |
| `MarkupBuffer::Append`                       |  5.05% |  2.43% |  0.48 |
| `Builder::BuildGroup` and friends (the tree) |  7.40% |  5.68% |  0.77 |

**The per-function ratios are contaminated by inlining, and the correction
matters.** The native build leaves `HtmlSpan::WriteBody` as its own symbol, so
the HTML serialization is 1.30% + 3.78% = 5.08% there against 5.70% for the
merged `HtmlNode::WriteMarkup` in WebAssembly - a ratio of 1.12x, not 2.37x.
The same effect makes the HTML tree build look cheaper in WebAssembly (0.68x).
The honest tax is per stage: **the parser +16%, the HTML stage +27%, the
MathML stage +37%**. Three readings: the parser pays almost nothing; the tax
in the HTML renderer is spread thin (every function a little slower, which is
what the memory accesses of the WebAssembly build predict); and the one
concentrated block is the MathML writer, where `write_mathml` and
`Writer::build` alone are worth +4 percentage points of the pipeline. The
**HTML renderer is still about 34% of the pipeline against the 16% of the
MathML writer**, so the flat HTML renderer is the first lever and the
streaming MathML writer the second.

**The native numbers are GCC numbers, and that hides half of the tax.** The
same source built with clang 20 and libstdc++ is 12% faster than the GCC 11.4
build: 28.8 us against 32.2 us per formula, same harness, same corpus, same
standard library. The difference sits in the parser (5.3 against 6.3 us) and
in the HTML stage (16.2 against 18.9 us); the MathML stage is the same (6.1
against 6.5 us). The WebAssembly build is a clang build, so a comparison
against the GCC numbers compares two different compilers. Measured in one
window against the like-for-like clang build, the WebAssembly build is
**+44%** behind (42.1 us against 29.3 us), and the gap is entirely in the
renderer: HTML +61%, MathML +73%, the parser at +9%. The tax is the engine
and libc++, and it is worst where the renderer walks the semantic tree and
writes the markup. Since the first measurement of it the MathML half of the
tax has shrunk (the value pool now uses the markup buffer, the WebAssembly
MathML stage went from 13.49 us to 12.09 us while the native side stayed the
same); the HTML half has not moved and is now the biggest block on the way to
5x in the browser.

**The MathML writer is the most WebAssembly-sensitive part of the core.**
Its 6.4 us (native) split into 15% for the walk that computes the styles and
the sizes, 50% for the build of the node graph and 35% for the serialization
of that graph. The graph is what the WebAssembly engine dislikes: 32 nodes
per formula, each with an attribute table and a value in a pool of strings.
Shrinking the node to one cache line and flattening the pool into one buffer
took the MathML stage down by 9% natively and 7% in WebAssembly, and the
whole WebAssembly build by 6% — the largest single step since the `_u16`
suffixes.

What has already been done to the core (all of it parity neutral, HTML and
MathML stay 5411/5411):

- The class list of a node (2026-10-24): the list is an array of the names
  themselves and not a joined text with a 16 bit mask of the known classes
  (`domtree.hpp`). The mask and `ClassFlagOf` are gone, the names are views
  of literals or of tables, and the size classes, the script fallback classes
  and the delimiter size classes are entries of a table. **Native paired A/B:
  +5.0%** against the previous release (null control 99.87%). The rewrite
  needs a name that lives as long as the render — five call sites handed a
  temporary string to the list before, and the parity gate caught them at
  5248/5411; the build with `-DVELOCITEX_CHECK_CLASS_NAMES` now proves that
  no name dangles.
- A leak in the SVG path data (2026-10-24): the path data was a `std::string`
  inside a node of the arena, and the nodes are never destructed, so the
  buffer of the string was never released. A page that rendered about thirty
  thousand formulas ran out of the fixed WebAssembly heap. The path data is
  an `ArenaText` now, `CopyShort` (which only covers the head and the tail of
  a text of up to forty-eight bytes) is replaced by `CopyText`, and the
  buffers that the arena retires when it grows are released with it. **The
  live heap is constant over 200 rounds of the corpus now** (1,192,747 bytes,
  0.00 blocks per render), and the browser survives the long run that the
  previous release failed.
- The bookkeeping diet (2026-10-24): the renderer options carried five
  `std::string` members and the builder copies the options for every group it
  enters, so a copy moved five strings; they are views now (`options.hpp`),
  and a copy is one memcpy. The style keys are the CSS names already, so the
  markup does not hyphenate them while it is written (`AppendHyphenated` is
  gone). The rare paths of `makeEm` moved into a function of their own, so the
  hot path is small enough to be inlined. The growth path of the markup buffer
  moved out of the hot path as well, and a span writes its opening tag in one
  piece. **Native paired A/B: +6.66%** (null control 100.02%); the same pair
  of WebAssembly builds reads +1% to +4% in Node, where the wall clock cannot
  resolve more; the browser gate reads 3.25-3.45x against 3.22-3.33x before
  (the gate script drifts by ±5%, so that column is only a reference).
- The string diet (2026-10-24): four places built a string on every render
  and do not any more. `RetrieveTextFontName` returns a view of a static
  table of the sixteen font names instead of concatenating two strings;
  the font of a delimiter is a view of the table of the four size fonts and
  its SVG label is a `const char*`; the root scope of the macro expander
  takes a shared empty string as its marker instead of copying the whole
  source twice (`LexToken(std::u16string, ...)` took the source by value and
  then copied it into the shared pointer); the UTF-8 decoder of the
  WebAssembly entry writes into the buffer directly and handles a run of
  ASCII on its own. **The deterministic effect: 22.53 heap allocations per
  formula became 13.78 (-38.8%).** The wall clock reads -1% to -4% (both
  orders of the group protocol point the same way) and the browser gate
  moved its median round from 2.0-2.2 ms to 1.9-2.0 ms.
- The value pool of the MathML writer is the `MarkupBuffer` instead of a
  `std::string` (`native/core/mathml.cpp`), and the text of a symbol is
  encoded straight into that pool (`MmlNode::set_content_code_point`). Every
  value used to be one `std::string::append`, which in WebAssembly is a call
  into libc++ — the native build does not notice, because libstdc++ inlines
  the short string operations. The MathML stage went from 13.49 us to 12.09 us
  in WebAssembly (-10.4%).
- The MathML annotation (the source of the formula) is encoded and escaped in
  one pass straight into the markup buffer. It used to become a `std::string`
  first, which asked the heap for the whole source.
- The name of a font is compared without a call into `memcmp`
  (`native/core/html/options.cpp`): the metrics of a font are looked up for
  every symbol, and in WebAssembly the call costs more than the comparison of
  a dozen bytes.
- The HTML builder does not build a `std::u16string` and a `std::string` for
  every symbol any more (`native/core/html/build_html.cpp`): the text of a
  symbol lives in a `char16_t[2]` on the stack (`ToScalar`), and the math class
  is one of the literals of the renderer (`AtomClass` returns a `const char*`
  and used to be wrapped in a `std::string` on every call). The class list of a
  space is a `ClassList` as well, so it does not need a `std::vector` of
  strings, and the length of an attribute is formatted into a buffer on the
  stack (`StackText`).
- The three steps together are worth **-2.50% / -2.33%** (v24 to v26) and
  **-2.01%** (v26 to v28, the median of three paired runs of 400 rounds, with a
  null test of +0.59% in the same window) on the whole WebAssembly pipeline.
  The browser moved from 3.00x to 3.17x.
- The HTML and the MathML markup are written into a dedicated `MarkupBuffer`
  (`native/core/html/markup.hpp`) instead of a `std::string`. A formula turns
  into a few hundred short fragments and `std::string::append` calls into
  `memmove` for every one of them; in WebAssembly that call costs more than
  the copy. The buffer copies the short fragments inline, which cut the
  WebAssembly HTML stage by 35% (54 -> 43 us per formula).
- `-flto` for the WebAssembly build; the release scripts use it.
- The macro expander dispatches through one open addressing table instead of
  running twelve linear scans per token.
- O(1) font metric lookup instead of a linear scan over the metric table.
- O(1) symbol, command and macro lookup instead of byte-wise `unordered_map`
  hashing.
- The per-token command chains (accents, extensible arrows, fonts) became
  sorted tables that are the single source of truth; the parser looks a name
  up once per token instead of walking a comparison chain.
- The tokens cache the characters of their source, so reading a token's text
  costs one load instead of two.
- The document, its node arena and the markup buffers are reused between
  renders instead of being reallocated.
- The MathML node shrank from ~1.3 KB to ~90 bytes (a compact attribute
  table plus a shared value pool), which cut the MathML stage by ~25%.
- The npm glue decodes the payload straight from the module memory, caches the
  module memory views and no longer copies the result a second time.
- The children of a DOM node, the child index lists of the builder, the style
  declarations and the attributes take their storage from the arena of the
  nodes (`ArenaAllocator` in `native/core/html/domtree.hpp`). The HTML stage
  went from 200 heap allocations per formula down to 31.
- The style declarations, the vlist elements and the `CssStyle` objects live
  in the same arena, and the MathML writer reuses its node table, its
  attribute tables and its value pool between renders. The MathML stage went
  from 20 heap allocations per formula down to 11.
- The style and attribute names are `string_view`s now: they are the literal
  names KaTeX uses, so a declaration never copies its name.
- The WebAssembly payload is written into the result buffer directly. The
  HTML and the MathML used to be copied out of the document's own buffers
  into the payload, which is ~7 KB per formula; in WebAssembly a `memcpy` is
  a byte loop, so that copy was not free.
- The `makeEm` formatting needs one 64 bit division instead of two; a
  division is a real instruction in WebAssembly.
- The `makeEm` formatting takes a 32 bit path when the scaled value fits into
  32 bits. WebAssembly has no instruction for the high half of a 64 bit
  multiplication, so a division by a constant expands into a dozen 32 bit
  instructions there. This one change cut the WebAssembly build by ~9%.
- The DOM nodes do not dispatch through a vtable any more: the kind of a node
  is a plain field and writing a node switches on it, so the compiler can
  inline the bodies of the common kinds into the recursion. A formula writes
  close to a hundred nodes.
- The lengths of a style declaration are kept as numbers and are formatted
  into KaTeX's `makeEm` form only when the declaration reaches the markup.
- A semantic node is built directly in the tree of the workspace. It is
  several hundred bytes large and used to be moved twice per node.
- The methods of the class list live in the header, so that the constant
  names at the call sites are compared without a call across the translation
  units.
- The child lists of a node are counted before they are filled, so the arena
  hands out their memory once instead of at every step of the growth.
- `IsWhiteSpace` is inline: the lexer, the macro expander and the parser ask
  for it for every character they read.
- The delimiters are `string_view`s now; the names are the literal names of
  the symbol table, so comparing a delimiter with a name is a memcmp and the
  parts of a stacked delimiter are not copied into strings of their own.
- The WebAssembly module parses the options JSON once per document instead of
  once per formula, and the npm glue keeps the encoded options.
- The MathML writer no longer builds a string per character and reuses the
  stack of its walk; the MathML stage went from 9.4 us to 5.5 us per formula.
- The class list compares the names character by character, so a name that
  differs in its first character ends the comparison right away, and the
  length of the first class is cached.
- The class list keeps a bit for every class name the renderer asks for
  (`mbin`, `mrel`, `mspace`, `mtight`, `nobreak`, `enclosing`, `katex-tag`,
  `allowbreak`, `mathnormal`, `katex-newline`), so the question whether a node
  carries a class is a test of a bit instead of a scan of the names. This cut
  1.3%. The count of the classes now matches KaTeX's, which counts the names
  and not the spaces.
- The whole DOM tree lives in the arena of the nodes: the nodes, their text,
  their class names, their style values and their attribute values all come
  from the bump arena, a node owns no heap memory and is never destructed
  (`ArenaDeleter` is an empty deleter, so the `unique_ptr` API stays). A
  formula went from 97 heap allocations for its nodes down to 0, which cut
  6.5%.
- The macro expander moves its tokens into the stack of the pending tokens
  instead of copying them, so the reference count of the source is not
  touched. This cut 1.2% (42.7 us).
- The command table, the macro table and the expansion command table only
  hold control sequences and single characters, so a longer name that does
  not start with a backslash is not in any of them. The expander and the
  parser skip the hash for such a name, and the single characters of the
  expansion table get a direct table. Most tokens of a formula are plain
  symbols, and this cut 4.1% off the parse stage.
- The parser asks for a semantic node at every step it takes, and the range
  check of `at()` cost more than the lookup itself. The index is one the
  parser handed out, so the check is gone; this cut 3.6% off the parse stage.
- KaTeX marks only a few characters with a script and the smallest of them is
  U+00B2, so an ASCII character never has one. The parser asks for the script
  of every plain symbol, and the answer was an `optional<u16string>` that the
  caller only tested for emptiness. The answer is a `bool` now and the ASCII
  range is rejected without a lookup.
- The end of an expression is only a control sequence or an ampersand, so the
  first character rejects the tokens of a formula that are neither.
- A matrix cell copies neither the name of its environment nor its task
  anymore: the name is read through a reference and the task is taken by
  reference.
- The MathML writer reuses the stack of the walk that turns the node tree
  into markup, so a render does not allocate it again. It shares the escape
  table of the HTML renderer, because KaTeX escapes the same characters with
  the same entities in both outputs. The text of a node is handed on as a
  view of the value pool instead of a string that is built for a comparison.
- Every comparison against a UTF-16 literal got a compile-time length through
  the `_u16` suffix (770 call sites, see the conventions above). This removed
  the runtime scan of the literal that GCC generated and cut the parse stage
  from 11.1 us to 6.3 us (-43%) and the whole native pipeline from 40.3 us to
  34 us (-16%). Clang folds the length of a literal on its own, so the
  WebAssembly build does not profit from the comparison sites; it does profit
  from the second batch (the ligature table of the typewriter font, the verb
  test of the lexer, the optional text of the accents and the color
  normalization), which is 2.4% in the WebAssembly build.
- The formatted numbers do not ask the heap for their bytes. `makeEm` and the
  attribute length reserved sixteen bytes, which is one byte more than the
  small buffer of a string holds, so every formatted length left the small
  buffer and asked the heap for the same few bytes: 3.8 allocations per
  formula. The allocation count of a formula went from 11.5 to 6.9, which is
  worth 0.5-1.5% in the WebAssembly build (glibc's `malloc` is cheap enough
  to hide it natively).
- The margins of a vlist are numbers, not strings. KaTeX writes them as
  lengths in `em`, and the style of the node already keeps a length as a
  number until the declaration reaches the markup. The vlist built a string
  for every margin of every element and copied it into the style; the number
  goes there directly now. The element shrinks by 48 bytes and the
  WebAssembly build is 4-5% faster.
- The MathML text is written byte by byte. `mml_utf8` asked the library for a
  copy of one to three bytes per character, and the call cost more than the
  copy.
- The node of the MathML tree fits into one cache line. The node carried an
  attribute table of eight slots and a vector for the attributes that did not
  fit into it; the busiest node of the corpus carries six attributes and the
  vector was never used, so the slots are six and the rare case lives in a
  table of its own. The node shrank from 96 to 64 bytes.
- The values of the MathML attributes and the texts of the elements live in
  one buffer with the offsets into it, instead of one string per value. A
  formula writes about 23 values; they are now neighbours in memory and the
  pool does not ask the heap for a string each.
- The MathML variant is a view of a literal instead of a string. The renderer
  asks for the variant of every node, and the answer is always one of a
  handful of fixed words.
- The markup buffer copies its fragments with wide stores. The compiler wrote
  the five bytes of `"<span"` as five single byte stores, and the markup of a
  formula is 1262 fragments. The buffer now copies a fragment with two
  overlapping wide stores, which cover the head and the tail of the fragment
  and neither read nor write outside it. In WebAssembly that is worth 2% of
  the whole build, because every byte store of a buffer that the allocator
  hands out costs more than a byte store of a static array.
- **v30: the text of a node grows in place, and the class names are compared
  without `memcmp`.** A formula builds 76 class lists with 204 names in
  total, and every name that was appended to a list copied the text of the
  list into a new block of the arena. The arena can now extend the block of
  the text when it is the last block it handed out, so the append writes the
  new name behind the old one and copies nothing. The names of the classes
  are compared with their own bytes instead of a call into `memcmp`, the way
  the font names already were. Together they are **-3.35%** in WebAssembly
  (the median of the per-round ratios over 800 rounds, against a null control
  of -0.46% and -0.40% in the same window): the arena about -1% and the
  comparison about -2.4%. Natively the change does not show up at all
  (29.3 us against 29.3 us), because libstdc++ inlines the comparison of a
  short name and the arena of the native build is cheap. The profile agrees:
  the share of `memcmp` falls from 1.61% to 0.39% and the share of the class
  list construction from 1.87% to 1.29%.

  The counts behind it come from an instrumented build of the core: 96.7
  nodes are written per formula, 75.9 class lists are built with 1.0 names
  each on average (the rest of the 204 names are appended one by one), 43
  lengths are formatted, 21.7 characters are measured and 583 fragments are
  written into the markup buffer. There is no redundant work in there: the
  work of a formula is proportional to the size of its output, which is why
  the remaining wins are one percent each.

- **v31: the WebAssembly build uses the non-trapping float to int
  conversion.** Emscripten switches the feature off by default
  (`-mno-nontrapping-fptoint`), and a conversion of a double into an integer
  is then a comparison with a branch into the trap handler instead of a
  single instruction. The build of the npm package now asks for
  `-mnontrapping-fptoint`, which is **-1.46%** of the whole build (the
  median of the per-round ratios over 800 rounds, against a null control of
  -0.34% and -0.15%). The feature needs a browser of 2021 or later (Chrome
  74, Firefox 62, Safari 15); the package falls back to the .NET runtime
  when the module cannot be instantiated.
  **`-mbulk-memory` was measured again on the current core (2026-10-24) and
  it is still not visible.** The test was a clean single flag A/B: only the
  flag differs, the core is the published one. The Node paired measurement
  (the "quietest windows" estimator) reads +1.98% in one order and +0.14% in
  the other, against a null control of +0.46% in the same window, so the
  corrected readings are +1.52% and -0.32%. The browser core direct test
  reads +1.6% on the fastest round (the estimator that is the most stable
  here) and +8.3% on the median round (the one that the outliers of a loaded
  machine contaminate), and the browser gate sees no difference at all (3.64x
  for both builds). The profile puts the upper bound at 2% (the `__memcpy` at
  1.25% and the `memmove` at 0.77% that it would replace), and the
  measurement lands under it. **The flag stays off**: the gain is not
  measurable, and it would add a feature requirement to the published
  module (Chrome 75, Firefox 79, Safari 15).
- **v32: the vertical list takes its wrapper classes and its wrapper style
  over instead of copying them, and the npm package encodes the source
  itself.** The children of a vertical list were collected in a vector that
  was never reserved, so the vector grew — and moved every child — while it
  was filled; the number of the children is known, so the list is reserved
  once now. The wrapper style of a child was copied twice, and a copy of a
  style copies every declaration of it into the arena again; the style is
  taken over now, and so are the wrapper classes. The first part is
  **-1.70%** of the whole WebAssembly build (the median of the per-round
  ratios over 800 rounds, against a null control of +0.02% in the same
  window). The second part replaces the `TextEncoder` of the platform — a
  call into the browser for every formula — with a loop over the characters
  of the source; the encoder is byte identical to `TextEncoder` for the
  lone surrogates and for the surrogate pairs (checked against 3000 random
  strings) and it is **33%** faster than the platform encoder (0.30 us
  against 0.45 us per formula, measured in the browser on the sources of the
  corpus). The browser reads **3.26x, 3.33x and 3.33x** against **3.23x**
  for the build before it (three runs of the benchmark, the best rounds
  3.17x, 3.32x and 3.44x).
- **v33: the diagnostics of the strict mode and of a formula KaTeX rejects
  follow KaTeX word for word, and the WebAssembly build stops mangling the
  characters above U+FFFF.** The strict violations carry KaTeX's text
  (`src/strict.ts`): the prefix `LaTeX-incompatible input and strict mode is
set to 'error': `, the `unknownSymbol` case
  (`Unrecognized Unicode character "X" (code)`), the `extraLatin` case
  (`Latin-1/Unicode text character "X" used in math mode`), the three cases of
  the spacing units, and **no position** for the violations KaTeX raises
  without a token (`mathVsTextUnits`, `mathVsSout`, `mathVsTextAccents`,
  `commentAtEnd`). A group that is not closed reports
  `Expected '}', got 'EOF'` when KaTeX parses it token by token (a primitive
  argument, a script argument, a plain group) and
  `Unexpected end of input in a macro argument, expected '}'` when KaTeX scans
  it as a whole (`src/MacroExpander.ts`, `consumeArg`). The delimiter of
  `\left`, `\right`, `\middle` and the `\big` family is a primitive group, so
  a brace opens a group instead of naming a delimiter
  (`src/functions/delimsizing.ts`). The error markup of a rejected formula is
  the source as a **single** symbol node, with the `<script>_fallback` class
  when the source starts with a character of a script KaTeX has no metrics
  for (`src/katex.ts`, `renderError`).
  **The WebAssembly build had a bug in its UTF-8 encoder: the four byte form
  of a character above U+FFFF shifted the code point twice, so every astral
  character (an emoji, a mathematical alphanumeric symbol) was written as an
  invalid byte sequence and reached the host as four replacement characters.**
  A lone surrogate now survives the JSON payload as the escape that keeps it
  in the string the host reads back, exactly like KaTeX, whose strings are
  UTF-16. The check over the whole command table of KaTeX — every command
  with an unclosed brace — reports **0 differences** over 582 cases, and the
  error documents of the fixtures are **49/49** identical (46/49 before).
- **`-mbulk-memory` is still not worth it.** Measured together with the
  feature above it made the result _worse_ (-1.24% against -1.46% for the
  feature alone), and the `memmove` calls of the profile (2.5%) are the
  copies of the source string and of the tokens, not the copies of the
  markup buffer (those are inline wide stores already).
- **A fixed linear memory is 56% faster in Node and not faster in the
  browser.** This is the largest effect of the whole session, and it is an
  artifact of the Node build of V8. A module whose memory may grow
  declares `(memory 256 32768)` — an initial size of 16 MB and a maximum
  of 2 GB — while a build with `-sALLOW_MEMORY_GROWTH=0` declares
  `(memory 256 256)`. With the fixed declaration the module is **-56%** in
  Node (the median of the per-round ratios over 400 rounds, confirmed with
  the order of the two builds reversed; the effect is in the core and not
  in the JavaScript around it — a harness that does not decode the payload
  reads the same -56%). **In the browser the same module reads 3.25x
  against 3.23x, that is, nothing** — Chrome does not take the path Node
  takes. The build was therefore not published. A fixed memory of 64 MB is
  even weaker: it is only -3.05% in Node, because V8 accounts the external
  memory of the module and collects more often.
- **`-sMALLOC=emmalloc` is not worth it.** The allocator of the module
  costs 1.3% of the profile, and the change is **-0.01%** of the build
  (the median of the per-round ratios over 800 rounds).
- **The class-name bit mask is already folded at compile time.** `ClassFlagOf`
  is a separate symbol in the WebAssembly profile (2.01%), which looks like
  the renderer's literal `HasClass("mbin")` queries are not folded. Making it
  `constexpr` reads **-0.15% and -0.45%** in the paired A/B, while the null
  test of the same window reads -0.28% — **inside the noise**. The reason is
  visible in the profile: the surviving out-of-line copy is the _runtime_ name
  path of `ClassList::Recount`; the literal call sites were folded by the
  compiler already. Reverted.
- **The JavaScript of the package is already at its floor.** The layers of
  the browser path were measured separately (one page, one instance, the corpus
  repeated inside the timed round, every pair measured back to back): the core
  44-52 us and the whole JavaScript layer **6-7 us** of the 50-62 us of the
  whole path — the public API against a bare `vt_render` reads 1.146, so the
  JavaScript layer is 14.6% of the core. Of the glue, the decode
  of the payload is **0.42 us** — the bytes have to become a string, and the
  package's fast path is not even slower than a hand written bare call into the
  module — the encoding of the source is 1.2-1.4 us and the JSON of the options
  is 0.46 us.
  **The earlier reading of "the glue 8.3 us and the package layer 6.5 us" was
  the quantization of `performance.now()`, not the glue**: a round of 36
  formulas of the core is about 1.7 ms and the resolution is 100 us, so the
  per-formula value of a round is quantized in steps of 2.8 us — the same order
  as the differences that were attributed to the JavaScript. Repeating the
  corpus inside the timed round (six times per formula, so the round is 11 ms)
  puts the whole layer at 6-7 us.
  **And the whole layer is not the lever anyway**: even a JavaScript layer that
  costs nothing only moves the browser ratio from 3.2x to about 3.5x.
  Three ways of making the path cheaper were measured and **none of them pays**:
  - **The promise of the package is free.** A build that renders the
    formula on the fast path without the inner promise of the compiler is
    **0.00%** against the build with it (a paired A/B of two variants of the
    entry point in one page, 150 rounds, A,B,B,A). A regular function that
    returns a promise instead of an `async function` is **0.00%** as well.
    An earlier measurement that attributed 2 us to the promise was an
    artifact: it compared an awaited asynchronous function with an awaited
    synchronous one, and measured the await.
  - **The decode does not care where the bytes live.** The payload is
    decoded from the WebAssembly memory in 0.19 us and from a buffer outside
    of it in 0.19 us; copying the payload into a buffer of its own first is
    **more** expensive. The `TextDecoder` of the platform is not slower on a
    resizable `ArrayBuffer` either (0.48 us against 0.38 us for 245 bytes
    in Node), so the copy is not worth it.
  - **The options of the package are cheap.** The JSON of the options is
    0.5 us and the checks around it are less.

Directions that were measured and did **not** pay off (do not retry them): a
direct mapped cache of the formatted `makeEm` lengths (the hit rate does not
pay for the lookup), `-mbulk-memory` (the module uses no bulk memory
instruction at all without it, so every `memcpy` is a byte loop — but the
copies are small and the call into libc++ is what costs, not the loop: two
runs of the alternating harness landed at -0.9% and +1.7% against a null test
of ±0.9%, and it would cost the browser support for the feature; re-measured
on 2026-10-24 with the "quietest window per group" estimator it reads
99.8-103.1% and the two orders disagree, so it is **neutral**; `-msimd128`
together with it is neutral as well), an offset cache for the class list
entries, a thread local pool allocator and dropping the `shared_ptr` of
`LexToken`. (Note: **replacing** the `std::string` members of `HtmlOptions`
with views _did_ pay off — see "The bookkeeping diet" above. What did not pay
off was making the strings shorter.)

**The wall clock cannot resolve a one percent change of the core on this
machine.** The paired A/B over whole rounds has a noise floor of +-0.3% for
the JavaScript glue, but of +-0.5% to +-2% for the core: the interference of
the machine (the load sits at 1.7-2.0) lasts much longer than one measurement
window, so it hits one build and not the other. The distribution of the per
round ratios of a core A/B is p25=92.9%, p75=109.0%, and the same pair of
builds read 99.0% and 103.4% on two runs of the same protocol. Two harder
quantities are used instead: the **minimum of every group** (the corpus is cut
into six groups, the builds alternate inside a group, and the minimum - the
window in which the machine did not interfere - is kept) and the **allocation
count** of the pool allocator (`native/core/pool_alloc.cpp` exports
`alloc_count_get()` in a measurement build; the number is deterministic).
Both orders of the group protocol have to point the same way before a core
change is published.

**The font name lookup is not a hotspot, even though the profile says it is.**
`GetCharacterMetrics` is 1.83% of the WebAssembly pipeline against 0.41%
natively (a ratio of 4.45x), which looks like the hash over the font name that
every symbol pays for. Replacing that hash — eight dependent multiplications
over the characters — with a single eight byte load and one mix changed
nothing: the paired A/B read **100.57% and 100.74%** against a null test of
**100.48%** in the same window, i.e. inside the noise and with the wrong sign.
The cost of `GetCharacterMetrics` is the metric table itself, not the name.
Reverted. The lesson generalizes: **read the call chain of a profile entry, not
its percentage** — the percentage says where the time is, the chain says why.

The wide store copy of the markup buffer was on this list as well: it had
been measured as "no difference in WebAssembly, ~1% slower natively". That
reading came from a harness whose input buffer was too small, so the
formulas failed and the copy never ran. With the valid harness and the
paired estimator it is **2% faster in WebAssembly and 3.2% faster in the
HTML stage natively**, so it is one of the things that were done.

Two more that were measured and reverted: building the whole class list in a
stack buffer and copying it into the arena once (the single class lists are
the majority, so the extra copy costs more than the saved allocations, 3.4%
slower) and comparing the style keys inline instead of through `memcmp`
(the keys are short, so the vectorized `memcmp` wins; no difference).

**The copies of the run time length are not a hotspot either.** The rule
"in WebAssembly a call into libc++ costs more than the copy" was applied to
the last copies that were left: the two run time length `std::memcpy` calls
of the `makeEm` fast path and the four of the MathML number formatter became
fixed width wide moves (`detail::CopyShort`), and the `memset` of the padding
became a loop. **86 cross module calls per formula** are gone (43 em values
times two, plus 0.9 MathML numbers). The time does not move: the native
paired A/B reads **99.91% / 99.92%** against a null control of +-0.6%, the
Node order of the WASM build contradicts itself (+0.38% against -0.94%), and
the browser core direct test reads a paired median of **1.005** over 14
pairs. It is published because the work is deterministically gone and the
direction is the one the project asks for, but it is published as a neutral
change: **the time is below the noise floor**.

**The initial size of the linear memory is not a lever.** The module is built
with `-sINITIAL_MEMORY=16777216` (16 MB) although the static data of the
module is under 2 MB. A build with 4 MB was compared against it with the
paired estimator: **-0.87%** (the median of the per-round ratios over 1000
rounds), against a null control of -0.14% and -0.38% in the same window. That
is inside the noise floor, so it is not a result — and the smaller memory has
a price: it grows twelve times over a session of 7200 renders, which detaches
the JavaScript views of the memory every time. Do not retry it.

Three more, measured on 2026-10-23:

- **The markup copy path is not a lever.** A formula makes 1262 appends: 728
  string fragments of 3890 bytes and 535 single characters, and the length
  distribution is dominated by fragments of one to eight bytes. Removing the
  copy altogether saves 4.6% natively, which is the ceiling. Replacing the
  inline byte loop of `MarkupBuffer::Append` with `memcpy` is 2.0% faster
  natively but **3.4% slower in WebAssembly** (the call into libc++ costs
  more than the few bytes it copies), and with `-mbulk-memory`, so that the
  `memcpy` becomes a `memory.copy` instruction, it is -0.2% (min) and +2.9%
  (median) against a null test of ±0.9% — no gain. The inline byte loop
  stays.
- **The layout of the 328 byte semantic node is not a lever either.** Moving
  the three rare text fields (`Alignment`, `Text2`, `Text3`) to the end of
  the node, so that the fields the renderers read stay in the first cache
  lines, is 1.4% slower.
- **`ArenaText` is not a lever either.** It copies 1325 bytes in 153 calls
  per formula; the quadratic re-copy of the class list does not show up. The
  _in place_ growth of the text is a different thing and does pay off in
  WebAssembly: see the entry of the v30 below.
- **The size of the semantic node does not matter.** The node is 328 bytes and
  four of its fields are `optional<u16string>`, which the roadmap wanted to
  turn into offsets into a string pool. A control experiment that only adds
  120 bytes of padding to the node (the change is a field, so the call sites
  do not move) changes nothing: the parse, the HTML stage, the MathML stage
  and the whole pipeline all stay inside the noise of the interleaved
  measurement. The walk over the semantic tree is not limited by the size of
  a node.
- **The shape of the DOM is not what WebAssembly pays for.** The HTML
  serialization was switched off in a WebAssembly build: the HTML stage goes
  from 28.1 us to 19.2 us, so the serialization is **32% of the stage in
  WebAssembly and 32% in the native build**. The WebAssembly tax is not in
  the tree of the DOM, it is in the build of the tree (19.2 us against 13 us
  natively), which is KaTeX's own algorithm.
- **Joining the class names in one go is slower.** The class list of a node is
  built by appending the names one by one, and every append copies the text
  of the list into a new block of the arena, so a list of k names copies its
  text k times. Joining the names into one text first (into a buffer of the
  stack, then into the arena) halves the copies and the blocks, and it is
  **9.5% slower** in the HTML stage: most lists carry a single name, and for
  those the old path was already one copy, while the new one pays for a
  length pass and a second copy. The number of arena blocks per formula
  (238) is therefore not what the HTML stage spends its time on.
- **A Latin-1 fast path for the payload of the browser package is not worth
  it.** `TextDecoder.decode` of the 1163 byte payload costs 3.6 us in the
  browser, while `String.fromCharCode.apply` costs 1.54 us. But only 7 of the
  36 benchmark formulas are pure ASCII — the others carry characters like
  `∑` — so the fast path would apply to a fifth of the payloads and save
  0.4 us per formula on average (0.6%).
- **The options JSON of the browser package is not a lever.** The core caches
  the parsed options by the JSON string and only re-parses them when the
  string changes; the benchmark alternates `displayMode`, so the cache
  thrashes, and the path still costs 0.17 us per formula.

Removing heap allocations buys almost nothing in the native build (glibc's
`malloc` is fast); it pays off in the WebAssembly and browser build. It is
therefore not the way to the native 5x — that needs the structural changes
below.

The work that would close the remaining gap is listed in `docs/benchmarks.md`.
The measured breakdown decides the order, and two of the earlier candidates are
now closed by measurement: the **size of the semantic node** does not change
the time (a control experiment with 120 bytes of extra padding changes
nothing), and the **shape of the DOM** is not what WebAssembly pays for (the
serialization is 32% of the HTML stage in WebAssembly and 32% natively, so the
tax is in the build of the tree, which is KaTeX's own algorithm).

What is left: the HTML tree build is 39% of the native time, the MathML stage
19% and the parser 19%. The MathML writer was the most WebAssembly-sensitive
part of the core (+80% against the native build, same compiler), so its value
pool was moved to the markup buffer and the text of a symbol now goes straight
into that pool; that took the tax down to about +70% and the WebAssembly
MathML stage from 13.49 us to 12.09 us. **The HTML stage now carries the
largest share of the tax** (+63%, unchanged so far): its markup buffer, its
arena and its per-symbol strings are the places where libc++ is still called
across the module boundary. The next steps, in order of what the measurement
supports:

1. the HTML renderer: the same treatment the MathML writer just got — no
   `std::string` on the path of a symbol, a value that is written into the
   markup buffer instead of into a string that is copied into it;
2. writing the MathML markup while it is built instead of building a node
   graph and serializing it afterwards;
3. the parser rewrite (no `shared_ptr` token indirection), which is the
   smallest win of the three for the browser, because the parser is not
   slower in WebAssembly; it shortens the native side.

**What the base algorithm costs, measured per formula.** An instrumented
build of the core counts the work of one formula of the workload corpus:

| quantity                       |      per formula | what it is                                                              |
| ------------------------------ | ---------------: | ----------------------------------------------------------------------- |
| semantic nodes                 |              ~30 | the tree the parser builds                                              |
| DOM nodes                      |             96.7 | the tree the HTML builder builds, 152 bytes per span and 200 per symbol |
| class lists                    |             75.9 | 1.0 names each on average, 204 names in total                           |
| markup fragments               |            582.6 | 4116 bytes of markup                                                    |
| arena used                     | **25 525 bytes** | the intermediate data of one formula                                    |
| lengths formatted              |             43.0 | `makeEm`                                                                |
| characters measured            |             21.7 | the font metrics                                                        |
| `IndexVector` materializations |              6.8 | the child lists of the builder                                          |

**The intermediate data is 6.2x the markup it produces.** A formula writes
25.5 KB into the arena of the nodes and reads them back to write 4.1 KB of
markup. That is the one place where the pipeline does clearly more work than
KaTeX's algorithm asks for, and it is what a rewrite of the base algorithm has
to attack: a renderer that writes the markup while it walks the semantic tree
and keeps only the _pending_ emissions on a stack, instead of a tree of 97
nodes, would cut the intermediate data to a small fraction of it.

The rewrite is not free, and the measurement says how much it can win. The
control experiment in this file (the HTML serialization switched off) puts the
serialization at 32% of the HTML stage in both the native and the WebAssembly
build; a renderer that writes the markup directly does not skip that work, it
moves it into the builder. The win is therefore the _tree_: its 25.5 KB, the
construction of its 97 nodes and the walk over it. The rest — the sizes, the
classes, the styles and the markup itself — is KaTeX's algorithm and has to
be paid either way. So the flat renderer is worth roughly 10-15% of the HTML
stage, not a factor, and the factor that decides 5x is still the WebAssembly
tax (x1.39) and the browser factor (x1.11-1.19).

**The order that the measurement supports** is therefore: the tax first (it
is the largest single factor and it is not in the algorithm), then the flat
renderer, then the MathML writer, then the parser. The first item of that
list is not a rewrite of the code but of the _build_: the WebAssembly
features that Emscripten switches off by default are worth about 1% of the
whole build, and the rest of the tax is the engine itself.

**The build that the measurement points at is the profile guided build, and
it is worth 5-8%.** The compiler lays the code of a program out with the
branches that a profile says are the hot ones next to each other. A renderer
has a few very hot paths (the token loop of the parser, the dispatch of the
builders) and a lot of cold ones (the diagnostics, the rare commands), and a
compiler that does not know which is which spreads them over the whole text
segment. The build of the standalone module now reads the profile of the
core (`native/wasm/katex.pgo`), and the module is **+5-8%** faster: the
Node paired measurement reads +7.2% in one order and +2.9% in the other (the
null control of the same window reads +0.40%), the browser core direct test
reads +3.3% on the fastest round, +5.2% on the mean and +7.9% on the median
(7 of 8 pairs), and the gate goes from 3.61-3.68x to **3.75-3.81x** (the
fastest round reads 3.93x). The output is byte identical — a profile cannot
change what a program computes.

Two things make it work, and both are worth knowing:

- **The profile does not have to come from the target.** The Emscripten SDK
  has no profile runtime for the wasm target (the `libclang_rt.profile` of
  the wasm target is not part of it, and the link fails with an undefined
  `__llvm_profile_runtime`), so the profile is collected from a **native**
  build of the core. The hash of a function is the hash of its name and of
  its structure, and both are the same for the two targets: the wasm build
  reads the native profile, every function of the core matches, and the
  profile is applied. Seven files of the wasm build have no data in the
  profile (the entry point and the files whose functions the corpus never
  calls), and the compiler says so with a warning per file.
- **The profile has to be collected from the source that is published.** The
  first version of the profile was collected from a copy of the core in which
  the one lambda that captured a structured binding was rewritten, because
  the compiler that was at hand could not compile that capture. The hash of
  the function changed with it, the profile did not match it any more, and
  the win was 1.7% smaller (Node: +5.9% instead of +7.2%). The source now
  unpacks the pair of the stack by hand (`native/core/mathml.cpp`), the
  comment there says why, and the profile matches every function of the core.
  The price is 0.3-0.5% in the native build, which is inside the noise of
  the paired A/B.

**The lengths in `em` cannot be cached: the value is needed anyway.** A formula
writes 43 lengths, and the text of a length looked like the obvious thing to
remember — the formatting of a number is the most expensive part of writing a
style declaration. The experiment says otherwise, and the reason is worth
keeping:

- **The values are nearly all different.** The rounded lengths of the 36
  benchmark formulas are 320 different numbers, about 20 per formula, so a
  table of the texts can answer at most 79% of the 43 calls of a formula. A
  table keyed by the _raw bits_ of the double answers 32% of them (the
  doubles differ in their last bits), and a table keyed by the _rounded_
  value answers 68.6% (256 entries) or 28.1% (64 entries, one generation per
  formula).
- **A hit still pays for the value.** The key of the table has to be the
  rounded value, and the rounded value is what the scaling computes — so the
  scaling is paid on a hit as well. The cache can only save the extraction of
  the digits, which is about twenty cycles for the small values that occur
  (the integer part is zero or one, so the loop over the digits runs once or
  twice). The lookup costs about as much as that.
- **The measurement agrees.** The native paired A/B reads 100.74% and 99.63%
  in the two orders (the null control of the same window reads 100.15%), the
  Node paired A/B reads -0.55% and +2.29%. Neutral, so it was not published.
- **What the experiment did find is worth keeping.** The rare path of
  `AppendEm` (the magnitudes that cannot be scaled exactly) dropped the
  suffix of the style declaration, so the declaration was written without its
  semicolon; and the copy of the digits warned under
  `-Wstringop-overflow`, because the compiler cannot see that the whole part
  is at most fifteen digits long. Both are fixed.

**The WebAssembly tax is not the bounds check.** The WebAssembly build was
believed to be slow because every memory access carries a bounds check. The
binary says otherwise:

- **The module has no bounds checks at all.** The disassembly of the
  published module (26 MB of text) holds exactly **one** `memory.size`, and
  the stores through a runtime address have no check at all:
  `(i32.store8 (i32.add (local.get $4) (local.get $6)) ...)`. The check is
  inserted by the **engine** when it compiles the module, not by the
  compiler that produced it, so it does not show up in the binary.
- **The check is real, but it is not the tax.** A micro-benchmark (one store
  and one load per iteration, a compiler barrier so that nothing is folded
  away) measures:

  | access                    |   native |  WebAssembly |
  | ------------------------- | -------: | -----------: |
  | a static array, any index | 0.285 ns | **0.319 ns** |
  | a runtime pointer         | 0.283 ns | **0.512 ns** |

  The native build does not see the difference at all. In WebAssembly an
  access through a pointer costs 61% more, because the engine has to check
  the address; an access to a static array does not, because the engine knows
  that the data segment is inside the memory.

- **But the tax does not add up.** The markup of a formula is a few hundred
  accesses, so the whole difference is worth about 0.2 us of the 47 us that
  a formula takes in the WebAssembly build - roughly 0.5%. The 1.8x tax is
  therefore **not** the checks; it is the instruction count of the engine's
  code generation. The way to pay it down is to do less work, not to touch
  fewer addresses.
- **The pattern is worth keeping for the rewrite.** A structure that is
  reached through a named static array is cheap in the WebAssembly build; a
  structure that is reached through a pointer is not. The flat HTML renderer
  should therefore address the nodes of its tree by **offset** into a static
  arena instead of by pointer.
- **What was tried with it.** The markup buffer of the WebAssembly build was
  given a static array (a megabyte; the largest markup of the corpus is 35
  kilobytes), with a storage of its own for the buffer of the values of the
  MathML output. The measured effect was 0.5% to 0.9% by the count (a few
  hundred engine checks less per formula) and **inside the noise of the
  measurement** (the Node paired A/B read -0.34% and +4.27% in the two
  orders, the browser core test leaned slightly to the new build). The
  change was **not published**: it is below the one percent that the project
  asks for, it costs a megabyte of the linear memory of the module (295
  pages against 279), and it puts a limit on the size of a formula that the
  native build does not have.

**The profile that can be trusted.** Two mistakes make a profile lie, and both
were made here before the numbers were worth anything:

- **A call graph built with DWARF fails to unwind.** `perf record
--call-graph=dwarf` put 132,055 of 132,234 samples of one rule on a single
  `callq HtmlNode::WriteMarkup` instruction, and the symbol level profile did
  not even list `WriteMarkup` in its first twelve. The address was attributed
  to `domtree.cpp:392` with 19.57%, which was an artefact of the failed
  unwinding. **A line level profile is only worth what its symbol level
  profile confirms.**
- **A flat profile is the honest one.** `perf record -F 4000` **without** `-g`
  records the instruction pointer only, and `perf report --no-children` then
  gives the self time of every symbol. There is nothing to unwind, so nothing
  can be attributed wrongly. The address of a position independent executable
  has to be reduced by the load base (the first `r--p` mapping of the binary in
  `perf script --show-mmap`) before `llvm-addr2line` can resolve it, and the
  `llvm-addr2line` of the Emscripten toolchain is the one that reads the DWARF
  5 of clang 20 (the `addr2line` of the system does not).

**The WebAssembly build can be profiled too.** The V8 profiler of Node names
the frames of a WebAssembly module by the index of the function, and the name
section of the released module is stripped. A **twin with `-g2`** is built for
the profiling; its code section is byte identical to the released one (804,487
bytes, the same bytes), so the function indices are the same and the names of
the twin are the names of the release. The callers of a symbol are read from
the tree of the profile, which is the only way to tell where a call into the C
library (`memcmp`, `memcpy`, `strlen`) comes from.

**Where the time goes in the WebAssembly build.** The profile of the 36 frozen
workload formulas, in the WebAssembly build of the core, with the names of the
twin:

| function                                 | share |
| ---------------------------------------- | ----: |
| `ParserState::Run`                       | 11.8% |
| `document_mathml_into`                   |  7.1% |
| `HtmlSpan::WriteBody`                    |  6.7% |
| `AppendEm`                               |  5.0% |
| `Writer::build`                          |  5.0% |
| `Expand`                                 |  4.8% |
| `Render` (the entry point of the module) |  3.9% |
| `MakeVList`                              |  2.6% |
| `Builder::BuildExpression` (three)       |  5.3% |
| `Builder::BuildGroup`                    |  2.5% |
| `__memcpy`                               |  2.3% |
| `Tokenize`                               |  2.3% |
| `MakeSymbol`                             |  2.1% |
| `HtmlSymbol::WriteBody`                  |  2.1% |
| `MakeSpan` (two)                         |  3.2% |
| `BuildArrayNode`                         |  1.2% |
| `AppendEscaped`                          |  1.2% |
| `CssStyle::SetEm`                        |  1.2% |
| `AllocateNodeArena`                      |  1.2% |
| `Builder::BuildExpression`               |  1.2% |
| `GetCharacterMetrics`                    |  1.0% |
| `memcmp`                                 |  0.3% |
| `strlen`                                 |  0.5% |

**The module does call into the C library, and the call sites are countable.**
The name section of the released module is stripped, so the calls are counted
on the twin (whose code section is byte identical to the released one): the
module of batch 10 has **301 `__memcpy`, 302 `memcmp`, 104 `memmove`, 482
`__memset` and 47 `strlen` call sites**. A module that was searched with a
stripped copy of itself reads zero, because the names are gone — the search
has to run on the twin. The profile puts the time of the calls at 2.3%
(`__memcpy`), 0.3% (`memcmp`), 0.5% (`strlen`), 0.3% (`__memset`) and 0.2%
(`memmove`), so the C library is about three and a half percent of the
pipeline, and every one of those call sites is a chance to save an
instruction.

The native build has the same shape: the parser is 19%, the HTML build 22%, the
HTML serialization 16% and the MathML 11%, and the `em` values of the style
declarations another 5.7%. **Nothing in the profile is above 14%**, which is
the reason why the percent items cannot add up to the gap (see "What 5x
actually costs" below).

**The comparisons of the renderer were calls into the C library.** The
`memcmp` of the profile is not the engine's business: it is the comparison of a
`string_view` with a literal. `basic_string_view::compare` compares the
**shorter** of the two lengths, and that value is only known at run time, so
`memcmp` cannot be inlined. The renderer asks the question for every atom of a
formula — `SpacingBetween`, `cancel_bin` and `IsMathClass` compare the class
of a node, and the writer of the MathML compares the tag of a node — and every
one of those comparisons was a call. A comparison against a literal whose
length is known at compile time is a load and a compare.

- **The published change** takes the literal as an array (`same_text(a,
"mord")`). The measured effect: the Node paired A/B reads **+1.31% and
  +1.48%** in the two orders, the browser core test reads the median round
  **9.800 ms against 9.450 ms** (-3.6%) and the fastest round 8.700 ms against
  8.650 ms, and the native paired A/B reads +1.37% and -0.93% against a null
  control of +0.10% — neutral, as it should be: the call that is removed is
  cheap in the native build.
- **The negative result**: the same change applied to the UTF-16 side as well
  (the command names of the parser, the hash tables of the symbols and of the
  macros) is **a regression**. The Node paired A/B reads -0.72% and -1.71%
  (the build is slower in both orders). A character by character loop is
  slower than the call for the names of five to fifteen characters, and the
  inlined literals make the module bigger. Only the side that compares short
  literals of the renderer was published.
- **The trap of the profile**: the first build of the published change was
  **1.5% and 4.5% slower**, and the profile was the reason: the profile of the
  core is a build input, and the functions that the change touched no longer
  matched it. **A measurement of a change of the core needs a profile that was
  collected from the source of the change** (`node
tools/reference/generate-pgo-profile.mjs`). With the fresh profile the same
  source reads 1.3% and 1.5% faster.

**A task of the parser is copied once, not three times.** The parser is a
stack of tasks, and every transition of the stack copied the task three
times: the copy that `changed()` made of it, the copy that the parameter of
`Push` made of it, and the copy into the stack itself. **A task is a plain
structure without a move constructor, so a "move" copies it as well** — the
comment that said otherwise was wrong. The copy of the task was the hottest
single line of the parser (`parser.cpp:1607`, 12 of the 130 samples of
`ParserState::Run`).

The edit is now applied to the copy in the stack (`PushEdited`,
`native/core/parser.cpp`): the task is copied into the stack once and the
edit is applied to the copy in place. The measured effect: the Node paired
A/B reads **+1.30% and +1.90%** in the two orders, the browser pair reads
**1.1% to 2.3% faster** in the two orders (see "A change of one percent is
measured in the browser too" above), and the native paired A/B reads +1.37%
and -0.93% against a null control of -0.48% — neutral, as it should be. The
module is **2,610 bytes smaller**, because the copies were instructions.

**A loop over the bytes is a call into the C library as well.** The rule that
a copy of a known length is inlined and a copy of an unknown length is a call
is about `memcpy` — but a loop that copies the bytes one by one is the same
thing after the optimiser has seen it: **LLVM recognises the pattern and
replaces it with a `memcpy` whose length is only known at run time.**
`detail::CopyShort` (`native/core/html/markup.hpp`) had such a loop for the
texts of one to three bytes, and the markup writer makes a few hundred calls
of that length per formula: the last digit of a fraction, a class name of
three letters, the digits of a number of the MathML.

The loop is now two explicit moves — a two byte copy, and for three bytes one
more byte — so nothing is left for the optimiser to turn into a call. The
measured effect:

| measurement                                        | reading                             |
| -------------------------------------------------- | ----------------------------------- |
| the call sites of `__memcpy` in the module         | **313 → 301** (12 fewer)            |
| the share of `__memcpy` in the WebAssembly profile | **3.09% → 2.60%**                   |
| Node, one build per process (the quietest window)  | **+2.18% / +1.92%**                 |
| Node, the p10 and the median of the same rounds    | +3.45% / +2.66% and +3.65% / +5.05% |
| the null control of the same window                | -0.67% / +0.31% (±0.7%)             |
| the browser, four instances (the quietest window)  | **+0.75% / +1.14%**                 |
| the null control of the browser                    | 0.00% / 0.00%                       |
| the native paired A/B                              | 100.33% / 99.26%, neutral           |
| the size of the module                             | 2,419 bytes smaller                 |

The native side is neutral, as it should be: libstdc++ inlines the copy of
three bytes, so there was never a call to remove there. The profile of the
WebAssembly build is the confirmation: the four functions that own the removed
call sites (`AppendEm`, `FormatNumber`, `SetAttributeEm` and `Writer::build`)
lose 36.6 ms of `__memcpy` time, and the share of `__memcpy` in the pipeline
falls from 3.09% to 2.60%.

**On WebAssembly, a `memcmp` of a known length is still a call.** The rule
above says that a copy of a known length is inlined. That is true for a
_copy_, and it is **false for a comparison on the WebAssembly target**: the
DataLayout of `wasm32-unknown-emscripten` does not allow an unaligned
access, so LLVM skips the expansion of `memcmp` into loads and keeps the
call. The renderer asks the question for every atom of a formula — the class
of a node, the tag of a MathML node, the name of a command — so the module
had **302 `memcmp` call sites, 197 of them with a length known at compile
time**.

`same_text` therefore compares the bytes itself, four at a time, with loads
and compares (`native/core/types.hpp`):

```cpp
template <size_t N>
inline bool same_text(std::string_view a, const char (&b)[N]) {
    constexpr size_t kLength = N - 1;
    if (a.size() != kLength) return false;
    const char* data = a.data();
    size_t i = 0;
    for (; i + 4 <= kLength; i += 4) {
        uint32_t left, right;
        std::memcpy(&left, data + i, 4);
        std::memcpy(&right, b + i, 4);
        if (left != right) return false;
    }
    for (; i < kLength; ++i)
        if (data[i] != b[i]) return false;
    return true;
}
```

The copy of four bytes reads exactly the four bytes of the text, so nothing
outside of the text is touched. Two things had to change with it:

- **The helper that compares a name against a list of names** (`oneof` in
  `native/core/mathml.cpp`) took an `initializer_list<std::string_view>`.
  A view hides the length of the literal, so the comparison was a call with
  a run time length. It is now a variadic template, and the literals are
  bare (without the `_u16` suffix), so each of them keeps its length.
- **The names of the MathML writer stayed views.** `mml_text(...).value_or("")`
  built a `std::string` — a call to `strlen` for the fallback and a copy of
  the optional's string — and then compared it with `find` and `ends_with`,
  which ask the C library for the length of the literal again. The names are
  views now, and the searches take a view as their needle.

| measurement                                       | reading                   |
| ------------------------------------------------- | ------------------------- |
| the `memcmp` call sites of the module             | **302 → 53**              |
| the share of `memcmp` in the WebAssembly profile  | **0.90% → 0.29%**         |
| Node, one build per process (the quietest window) | **+2.13% / +1.45%**       |
| Node, the p10 of the same rounds                  | +2.37% / +2.21%           |
| the null control of the same window               | +0.03%                    |
| the browser, four instances (the quietest window) | **+1.36% / +2.28%**       |
| the browser, the median of the per-round ratios   | +1.16% / +1.89%           |
| the null control of the browser                   | 0.00% / 0.00%             |
| the native paired A/B                             | 98.88% / 101.11%, neutral |
| the size of the module                            | 4,281 bytes bigger        |

All three measurements put the new build ahead, and the native side is
neutral, as it should be: libstdc++ inlines the comparison of a few bytes, so
there was never a call to remove there. The module is bigger, because every
comparison is now inline — the tax of the WebAssembly build is the number of
instructions the engine generates, and a load and a compare is cheaper than
a call.

- **The negative result: the digits of the `em` values.** The formatting of a
  length (`AppendEm`, `native/core/html/jsnum.hpp`) writes the four digits of
  the fraction with a chain of four divisions by ten, and the digits of the
  integer part with a chain of one division per digit. A table of the two
  digit numbers turns the fraction into two independent divisions by a
  hundred and two loads. The measured effect: the native paired A/B reads
  100.06% and 99.97% against a null control of 99.61%, the Node paired A/B
  reads +1.31% and -0.31% (the two orders contradict each other), and the
  browser pair cannot tell the two builds apart (the two orders put the
  better minimum on the second position, which is the position bias of the
  estimator, and the null control reads 1.0000). **The digit conversion is
  not the bottleneck** — the scaling of the value is, and that is a single
  64 bit multiplication. The change was **not published**: it is below the
  one percent that the project asks for, and the module grew by 793 bytes.

```
browser path = native core * (the WebAssembly tax) * (browser over Node) + the glue
```

| factor                                                |        reading | how it was measured                                                                                                                                                                                                                                                                                                                                            |
| ----------------------------------------------------- | -------------: | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| native core (clang 20)                                |        27.5 us | parse 5.3, HTML 16.5, MathML 5.8 (before the bookkeeping diet of 2026-10-24), measured in the same window as the row below                                                                                                                                                                                                                                     |
| the WebAssembly tax                                   |          x1.37 | the same source as WebAssembly: the tax was x1.44 at v28 (parse +9%, HTML +61%, MathML +73%); the WebAssembly builds of v30 and v31 are 3.35% and 1.46% faster while the native build is unchanged                                                                                                                                                             |
| browser over Node                                     | x1.11 to x1.19 | 50.9 us in the browser against 46.0 us of WebAssembly in Node (medians); 42.6 against 35.7 (fastest rounds)                                                                                                                                                                                                                                                    |
| the JavaScript layer (the glue and the package layer) |      **+6 us** | the layer breakdown above: the public API against a bare `vt_render` is 1.146, so the JavaScript layer is 14.6% of the core; the decode 0.42 us, the encoding of the source 1.2-1.4 us, the options JSON 0.46 us, the rest is the promise and the result object. The earlier reading of 13 us was the quantization of `performance.now()` (see the trap above) |

29.3 x 1.37 x 1.15 + 6 = **52 us**, which is 3.0x to 3.6x against KaTeX's
156-189 us in Chrome. The benchmark harness itself reads **3.16x to 3.33x**
(the readings of the day are 3.16x for v33, which only touches the
error path, 3.26x and 3.33x and 3.33x for v32, 3.23x for v31, 3.12x and
3.14x for v31 earlier, 3.06x to 3.16x for v30, and 2.96x to 3.17x for the
earlier releases of the core): its corpus alternates
`displayMode`, which invalidates the options cache of the core and makes the
KaTeX formulas more expensive. **5x therefore needs a native core of about
16-20 us** (KaTeX's fastest round of ~153 us gives a budget of ~30.6 us and
its median round of ~190 us gives ~38 us; minus the 6 us of JavaScript,
divided by 1.37 x 1.15 = 1.5755), that is a third to a half below the current
one, and that is more than a rewrite of all three stages delivers:

| path                                                 | native core | browser path |     ratio |
| ---------------------------------------------------- | ----------: | -----------: | --------: |
| now                                                  |     27.5 us |       ~59 us |  3.3-3.5x |
| all three stages rewritten                           |    ~18.5 us |       ~35 us | ~4.5-5.4x |
| and the rewrite also brings the tax down to +35%     |    ~18.5 us |       ~33 us | ~4.7-5.7x |
| and the JavaScript layer is down to the decode alone |    ~18.5 us |       ~29 us | ~5.4-6.5x |

So 5x in the browser needs **the rewrite of the pipeline and one of the other
two**: the rewrite alone lands at about 4.5x, and the remaining gap is closed
by a smaller WebAssembly tax or by a JavaScript layer that is only the decode.
That is why the remaining one percent items in the profile cannot add up to
5x: they move the terms of the product by one percent, and the gap is 32-45%.

Even with all of that the optimistic estimate is 2-3x; 5x needs a rewrite-level
pipeline (a one-pass parser plus a flat renderer that emits KaTeX's box model
directly).

Keep the HTML/MathML path free of the drawing layout
(`try_compile(..., layout = false)`) — that is where most of the advantage over
KaTeX comes from.
