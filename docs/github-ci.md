# GitHub CI and NuGet Trusted Publishing

English | [中文](github-ci.zh-CN.md)

## Workflows

- `.github/workflows/lint.yml` runs on branch pushes, pull requests and manual
  dispatch. It is also called by the release workflow. `Workflow lint` uses
  actionlint 1.7.7. `Source lint and JIT assertions` parses tracked PowerShell,
  JavaScript, Python, JSON and XML sources, then builds the solution and package
  inspector with the pinned SDK, locked dependencies and compiler/analyzer
  warnings treated as errors. It runs the P4 executable assertions under JIT.
  This is syntax and compiler/analyzer lint, not a formatting migration or a
  PSScriptAnalyzer/Ruff/ESLint policy.
- `.github/workflows/publish.yml` runs when a `v*` tag is pushed to
  `TaiRuiQu/VelociTex`. It requires a `v`-prefixed version tag (for example,
  `v1.0.0-rc.2`). The version suffix determines prerelease status. It checks the source
  revision, reruns lint, executes P4 in JIT and Native AOT, compares their outputs,
  and packs all three libraries and portable symbols with payload determinism
  and SourceLink checks. Fresh isolated Core/Direct2D package consumers run in
  both JIT and AOT before artifacts can reach the publish job. After NuGet
  publication succeeds, a separate job creates the GitHub Release with generated
  release notes. A suffix such as `-rc.2` creates a prerelease; a stable tag such
  as `v1.0.0` creates a normal release.

The Windows build job has no OIDC permission. The separate Ubuntu publish job
downloads only this run's immutable artifact, checks all six package hashes and
the source revision, and requests a temporary NuGet credential immediately before
pushing. It does not check out or execute repository build scripts. Third-party
actions are pinned to commit hashes.
Only the GitHub Release job receives `contents: write`; it does not receive OIDC
permission. Publication and Release creation run in the same workflow because
events created with `GITHUB_TOKEN` do not generally start another workflow. See
[GitHub's token event rules](https://docs.github.com/en/actions/concepts/security/github_token).
Concurrency is scoped to the tag so different version tags do not displace each
other's pending runs.

## One-time account configuration

1. In GitHub repository **Settings → Environments**, create `nuget`. Limit its
   deployment refs to the intended `v*` tags. Configure required reviewers if
   release approval is desired; the workflow itself does not require a reviewer.
2. Add an environment secret named `NUGET_USER` containing your **nuget.org
   profile username**, not an email address or API key.
3. In nuget.org, sign in as that user and create a Trusted Publishing policy:

   | Field | Value |
   | --- | --- |
   | Repository owner | `TaiRuiQu` |
   | Repository | `VelociTex` |
   | Workflow file | `publish.yml` (filename only) |
   | Environment | `nuget` |
   | Package scope | `VelociTex.*`, or the three exact package IDs |

   Allow new package creation for the first publication and new versions for
   subsequent publications. Select the actual package owner (your user or
   organization) and ensure the login user has the required package rights.
   Check name availability before the first release. No long-lived NuGet API key
   is stored in GitHub. See Microsoft's
   [Trusted Publishing instructions](https://learn.microsoft.com/en-us/nuget/nuget-org/trusted-publishing).
4. Optionally require the two Lint checks in branch protection. Protect release
   tags with a repository ruleset so only intended maintainers can create them.

These account settings are outside the repository and are not created by adding
the YAML files. Configuring CI does not itself publish any package.
The checks actually executed for this configuration are recorded in
[CI verification](verification/ci-2026-10-19.md).

## Releasing

Commit and push the reviewed source and workflows. Use a new version; the local
`1.0.0-rc.1` candidate and its locks remain frozen. Create and push a version tag:

```powershell
git tag v1.0.0-rc.2
git push origin v1.0.0-rc.2
```

Pushing the tag triggers Lint, verification, NuGet Trusted Publishing and automatic
GitHub Release creation in that order. Lightweight and annotated tags are both
supported. Tag names must follow `vMAJOR.MINOR.PATCH[-prerelease]`; other `v*` names
fail validation and tags without a `v` prefix do not trigger this workflow.
Manually creating or editing a Release does not trigger another publication.
The tag must include these workflow files. Push version tags individually;
[GitHub does not emit tag push events when more than three tags are pushed at once](https://docs.github.com/en/actions/reference/workflows-and-actions/events-that-trigger-workflows#push).

The Release job verifies the remote tag still points to the checked source
commit, and preserves an existing published Release with matching prerelease
status. Drafts and conflicting prerelease states fail explicitly. Generated notes
use `gh release create --verify-tag --generate-notes`; no personal access token is
needed. If only Release creation fails after successful NuGet publication, use
**Re-run failed jobs** rather than rerunning the successful package pushes.

The workflow stores the exact packages, symbol packages, package report and
SourceLink report as a publish artifact. A separate verification artifact stores
runtime reports, consumer logs and generated consumer locks. Download these
artifacts if a job fails before retrying. NuGet publication is not transactional:
earlier packages may have been uploaded when a later push fails. Duplicate
versions deliberately fail rather than silently accepting unrelated bytes;
resolve partial publication before retrying or choose a new version.

Consumer locks are generated under ignored `artifacts/release-consumer/locks`
using `Verify-PackageConsumer.ps1 -LockDirectory ... -UpdateLocks`. Every
generated restore is immediately followed by `--locked-mode`; actual package and
DLL hashes are verified by the consumer. These are **per-run generated locks**,
not independently reviewed source lock templates. Package ZIP timestamps and
SourceLink revisions can change hashes, so CI never overwrites historical
`samples/**/package-locks` or represents rebuilt packages as frozen RC1 bytes.
SDK and library build dependencies still use committed lock files throughout.

## Local reproduction and boundaries

```powershell
./tools/Test-CILint.ps1
./build.ps1 -Target P4 -Locked
node tools/reference/p1-runtime-parity.mjs --phase=p4
./pack-local.ps1 -Version 1.0.0-rc.2 -OutputDirectory artifacts/packages/ci -Locked -VerifyDeterminism
./tools/Verify-PackageConsumer.ps1 -Version 1.0.0-rc.2 -Feed artifacts/packages/ci -Output artifacts/ci-consumer -LockDirectory artifacts/ci-consumer/locks -UpdateLocks
```

Use PowerShell 7, the SDK in `global.json`, Windows x64 with C++ Native AOT build
tools, Node 22.23.2 and Python 3. GitHub jobs use `windows-2025`; workflow lint and
publication use `ubuntu-24.04`.

CI assertions and JIT/AOT parity do not replace the independent browser reference
suite, real WinUI screenshot review, or the physical hardware/platform/audio
gates in [release-checklist.md](release-checklist.md). This workflow does not
claim those gates passed. Complete the outstanding formal acceptance and XML
documentation cleanup before publishing stable `1.0.0`; creating a stable
Release does not establish those results automatically.
