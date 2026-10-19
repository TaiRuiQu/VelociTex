# npm publishing with the shared release tag

The existing `.github/workflows/publish.yml` now publishes both NuGet and npm on
the same `v*` tag. npm's version comes directly from that tag: `v1.0.0-rc.2`
publishes `velocitex@1.0.0-rc.2` to `next`; `v1.0.0` publishes to `latest`.
Build and lock metadata are versioned in a temporary distribution directory;
tracked npm metadata is not modified and CI does not create a version commit.

## One-time account setup

1. Own the npm package `velocitex`. For a new package, first publish one verified
   preview tarball interactively with npm authentication/2FA. A manual run of
   the **WASM and npm** build workflow supplies the tested tarball, or build it
   locally with `build-wasm.ps1` and `verify-wasm.ps1`. For example:

   ```sh
   npm login
   npm publish ./velocitex-0.1.0-preview.1.tgz --access public --tag next --ignore-scripts
   ```

   This command is a maintainer action; adding CI does not execute it. Use a new
   release version afterwards. See the
   [first-publish issue in npm's documentation repository](https://github.com/npm/documentation/issues/1926).
2. Create the GitHub environment **`npm`**, permitting your intended `v*` tags.
   Keep the existing `nuget` environment. Reviewers are optional; this workflow
   adds no approval gate of its own.
3. Configure a GitHub Actions Trusted Publisher in the npm package's settings:

   | Field | Value |
   | --- | --- |
   | Organization/user | `TaiRuiQu` |
   | Repository | `VelociTex` |
   | Workflow filename | **`publish.yml`**, filename only |
   | Environment | **`npm`** |
   | Allowed actions | Explicitly allow **`npm publish`** |

   `wasm.yml` only builds/verifies; do not use that filename in the trust policy.
   New trust configurations currently allow staged publishing by default;
   automatic direct publication requires the separate publish permission.
   Complete the first successful OIDC publication within two days of creating
   the trust configuration, or recreate it. No npm password or `NPM_TOKEN` is
   needed in GitHub. See [npm's setup guidance](https://docs.npmjs.com/trusted-publishers/).

The publishing job uses a GitHub-hosted Ubuntu runner, Node 22.23.2 and pinned
npm 11.16.0 with OIDC and provenance. Development/reference npm remains 10.9.8.
The publishing runner installs the CLI independently; it does not check out
source, build the package or execute package lifecycle scripts.

## Release

Commit/push the reviewed source and workflows, then push a new tag:

```sh
git tag v1.0.0-rc.2
git push origin v1.0.0-rc.2
```

Lint and both native/web verification suites must succeed before either
publication starts. The web pipeline installs and browser-tests the actual
tarball, including strict TypeScript consumption. The npm publishing job checks
the immutable artifact's SHA-256, version, source revision, clean-source status,
package identity and AOT metadata, then publishes those exact bytes. The GitHub
Release waits for both registries to succeed. Branch/PR builds never publish;
creating/editing a GitHub Release does not trigger another upload.

The registries cannot participate in a cross-registry transaction. If one fails
after the other succeeds, fix the issue and use **Re-run failed jobs**. Duplicate
versions fail explicitly rather than silently overwriting an existing package.

The GitHub environment, initial npm package and trust relationship are external
account settings, not resources created by the YAML. Hosted execution, OIDC,
provenance and real registry writes remain unverified until the first live run.
See [local verification](verification/npm-ci-2026-10-19.md).
