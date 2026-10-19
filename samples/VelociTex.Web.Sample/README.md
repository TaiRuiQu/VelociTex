# Browser sample

Build from the repository root with `./build-wasm.ps1 -Locked`, then serve the
repository over HTTP (for example `python -m http.server 8080 --bind 127.0.0.1`).
Open `http://127.0.0.1:8080/samples/VelociTex.Web.Sample/`.

The sample serves the unbundled package module and discovers adjacent runtime
assets automatically. For a bundled npm application, see
[`npm/velocitex/README.md`](../../npm/velocitex/README.md).
