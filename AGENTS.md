# VelociTex Working Conventions

English | [中文](AGENTS.zh-CN.md)

Confirmed requirements: [docs/requirements.md](docs/requirements.md). Implementation order and phase exits: [docs/roadmap.md](docs/roadmap.md). Read the [Agent handover](docs/agent-handoff.md) first.

- The locked KaTeX 0.19.0 is the only compatibility baseline; do not track `main` or upgrade to another version.
- Build a redesigned C# data pipeline that preserves upstream semantics and typesetting rules; do not transliterate the JavaScript object tree, DOM, or CSS implementation.
- The shipped native runtime depends on no browser, WebView, JavaScript engine, Node.js, or Typst. The separately packaged WASM / JS web adapter was explicitly requested on 2026-10-19; it may use the browser and bundled .NET WASM runtime without adding these dependencies to native packages. Node.js, browsers, and scripting tools are fine for development comparison and build-time generation.
- The first version delivers typesetting quality and usability first. Never report later ≥10× goals as achieved; allocation of result objects, export data, and native resources must be measured honestly.
- Formulas may use the KaTeX fonts. Code and font licenses stay separate; the fonts are not MIT.
- The library is centered on a single formula. Body-text mixed layout and automatic line breaking are not first-version core duties; the host adaptations needed for a full downstream replacement live in the migration document.
- Preserve Native AOT and trimming compatibility, and follow standard .NET project conventions with centrally managed dependency versions and lock files. New low-level optimizations require comparison evidence.
- Any WinUI interface change requires running the real app and reviewing automated screenshots; compiling and unit tests do not substitute for visual acceptance.
- Re-read that repository's AGENTS.md before touching `../oh-my-study/`. This file grants no extra permission for publishing, uploading, or modifying downstream projects.
- Each phase delivers implementation, applicable verification evidence, and remaining work. Checks that were not executed must be explicitly marked unverified.
