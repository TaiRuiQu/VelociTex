# Downstream migration snapshots

## P5 candidate integration

The current downstream display path consumes the frozen local `1.0.0-rc.1`
packages in `artifacts/packages/p5`. `p5.patch` upgrades the existing P3 working
tree: it changes the three central package versions, the local feed and three
application lock variants, for five changed paths. It contains no UI source
changes. Apply it only after the exact captured P3 state, not directly to an
arbitrary clean checkout.

`p5.manifest.json` guards 23 paths, including all 22 P3 paths and the unchanged
math smoke source. An isolated `git apply --check` and actual application of the
patch both passed, with every resulting canonical hash verified. Existing
downstream user changes remain in their working tree; neither its index nor
commits were modified.

The locked Debug build, locked Native AOT publish, actual math/preview runs and
33 reviewed screenshots passed with the candidate. A separate actual run passed
while the owned Typst directory was absent, and the tool was restored. Printing
retains its existing implementation. Exact candidate identities, process IDs,
hashes and limitations are in `artifacts/p5/downstream-aot/downstream-runtime.json`
and the [P5 verification record](../../docs/verification/p5-2026-10-19.md).

The P3 commands below also apply to the migrated current workspace. Do not
overwrite frozen package bytes or replay either patch against an unmatched
baseline. Physical monitor DPI changes, old hardware and screen-reader audio
remain unverified; this local candidate is not a published stable release.

## Historical P3 migration delta

`p3.patch` contains only this migration's edits. Its baseline is the preserved
**dirty working tree** of `../oh-my-study` at commit
`08da66b5b90bdafe2aa6ba235b7c0eaf465f9ae8`, not that commit's clean tree.
Several host files already existed as user-owned untracked work. Applying this
patch to an unrelated clean checkout is therefore not supported.

`p3.manifest.json` records exact raw before/after SHA-256 values for 22 guarded
paths. Eighteen paths change; the four ordinary library locks are also recorded
as unchanged. All three existing application variant locks include the pinned
local preview packages. Patch
text uses UTF-8 and LF as required by the downstream Git attributes. Canonical
hashes are included separately from the raw working-tree hashes.

The patch was applied in a fresh isolated rehearsal containing the captured
baseline files; `git apply --check` passed and every resulting canonical hash
matched the actual downstream files. The downstream repository's index and
commits were never changed. Existing unrelated changes, including the deleted
`migration-progress.md`, are absent from this patch.

Native display is the default. The internal environment variable
`OHMYSTUDY_LEGACY_FORMULA_DISPLAY=1` retains the previous display route during
migration acceptance. It is a temporary rollback mechanism, not a product
setting. Printing and PDF generation retain their existing Typst path.

Package versions remain pinned to `0.2.0-preview.1` and the frozen local feed
`../VelociTex/artifacts/packages/p2`; source mapping restricts `VelociTex.*` to
that feed. The package bytes must match the committed P2 package-consumer locks.
Do not repack a different payload with the same version. Ordinary library locks
remain unchanged; global Native AOT publishes use separate
`packages.aot.lock.json` files so they do not invalidate normal builds.

Reproduction from the migrated downstream workspace:

```powershell
dotnet build src/OhMyStudy.App/OhMyStudy.App.csproj -c Debug -p:Platform=x64 -p:RestoreLockedMode=true
dotnet publish src/OhMyStudy.App/OhMyStudy.App.csproj -c Release -r win-x64 -p:Platform=x64 -p:PublishAot=true -p:RestoreLockedMode=true -o <owned-output>
```

The actual application supports `--math-smoke <report>` and
`--preview-smoke <report>`. Both create isolated sample libraries and automatic
screenshots. Preview smoke additionally exercises keyboard copying, whole-field
and per-formula UIA values, live theme changes, line baselines, surface density,
scrolling, cancellation, virtualization and 1/10/50/100 formula pages.
Composition density overrides do not prove a physical monitor DPI transition.
Screen-reader audio and old-CPU acceptance remain separate unverified items.

Regenerate this delta only while the exact captured baseline remains available:

```powershell
python tools/Write-P3MigrationPatch.py --downstream ../oh-my-study --baseline artifacts/p3/baseline
```

This snapshot does not claim 1.0 or full core compatibility. In particular, the
P2 package preserves unsupported prime expressions as original source; broader
compatibility work must ship under a new package identity before downstream
acceptance is refreshed.
