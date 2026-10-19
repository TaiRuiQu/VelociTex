"""Record only the P3 delta relative to the preserved downstream dirty snapshot."""
from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import subprocess
from pathlib import Path


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def canonical(data: bytes) -> bytes:
    return data.decode("utf-8-sig").replace("\r\n", "\n").encode("utf-8")


parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--downstream", type=Path, required=True)
parser.add_argument("--baseline", type=Path, required=True)
parser.add_argument("--output", type=Path, default=Path("integration/oh-my-study"))
parser.add_argument("--rehearsal", type=Path, default=Path("artifacts/p3/patch-rehearsal"))
args = parser.parse_args()
repo, baseline, output = args.downstream.resolve(), args.baseline.resolve(), args.output.resolve()
output.mkdir(parents=True, exist_ok=True)

existing = [
    "AGENTS.md", "Directory.Build.props", "Directory.Packages.props", "NuGet.Config",
    "src/OhMyStudy.App/OhMyStudy.App.csproj",
    "src/OhMyStudy.App/Services/NativeReadingSession.cs",
    "src/OhMyStudy.App/Controls/QuestionPreview.cs",
    "src/OhMyStudy.App/MainWindow.PreviewSmoke.cs",
    "src/OhMyStudy.App/packages.lock.json",
    "src/OhMyStudy.App/packages.self-contained.lock.json",
    "src/OhMyStudy.App/packages.framework-dependent.lock.json",
] + [f"src/OhMyStudy.{name}/packages.lock.json" for name in ("Application", "Documents", "Domain", "Infrastructure")]
new = [
    "src/OhMyStudy.App/Controls/NativeFormulaParagraph.cs",
    "src/OhMyStudy.App/Services/NativeFormulaReadingSession.cs",
    "src/OhMyStudy.App/MainWindow.NativeFormulaAcceptance.cs",
] + [f"src/OhMyStudy.{name}/packages.aot.lock.json" for name in ("Application", "Documents", "Domain", "Infrastructure")]

patch: list[str] = []
records = []
rehearsal = args.rehearsal.resolve()
if rehearsal.exists():
    # Never remove a user directory. Each rehearsal gets a fresh owned child.
    import uuid
    rehearsal = rehearsal / uuid.uuid4().hex
rehearsal.mkdir(parents=True, exist_ok=False)
subprocess.run(["git", "-C", str(rehearsal), "init", "--quiet"], check=True)
for relative in sorted(existing + new):
    previous_path, current_path = baseline / relative, repo / relative
    if relative in existing and not previous_path.is_file():
        raise RuntimeError(f"Missing preserved dirty baseline: {relative}")
    if relative in new and previous_path.exists():
        raise RuntimeError(f"A new migration file already existed in the snapshot: {relative}")
    before = previous_path.read_bytes() if relative in existing else None
    after = current_path.read_bytes()
    before_lf, after_lf = canonical(before) if before is not None else b"", canonical(after)
    changed = before_lf != after_lf or before is None
    records.append({
        "path": relative,
        "existedBefore": before is not None,
        "changedByPatch": changed,
        "beforeRawSha256": sha(before) if before is not None else None,
        "beforeCanonicalLfSha256": sha(before_lf) if before is not None else None,
        "afterRawSha256": sha(after),
        "afterCanonicalLfSha256": sha(after_lf),
        "beforeBytes": len(before) if before is not None else 0,
        "afterBytes": len(after),
    })
    if before is not None:
        target = rehearsal / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(before_lf)
    if not changed:
        continue
    patch.append(f"diff --git a/{relative} b/{relative}\n")
    if before is None:
        patch.append("new file mode 100644\n")
    lines = difflib.unified_diff(
        before_lf.decode().splitlines(keepends=True),
        after_lf.decode().splitlines(keepends=True),
        fromfile=f"a/{relative}" if before is not None else "/dev/null",
        tofile=f"b/{relative}",
        n=3,
    )
    for line in lines:
        if line.endswith("\n"):
            patch.append(line)
        else:
            patch.append(line + "\n\\ No newline at end of file\n")

patch_path = output / "p3.patch"
patch_bytes = "".join(patch).encode("utf-8")
patch_path.write_bytes(patch_bytes)
subprocess.run(["git", "-C", str(rehearsal), "apply", "--check", str(patch_path)], check=True)
subprocess.run(["git", "-C", str(rehearsal), "apply", "--whitespace=nowarn", str(patch_path)], check=True)
for record in records:
    actual = canonical((rehearsal / record["path"]).read_bytes())
    if sha(actual) != record["afterCanonicalLfSha256"]:
        raise RuntimeError(f"Applied result differs from current migration source: {record['path']}")

manifest = {
    "schemaVersion": 1,
    "scope": "P3 migration delta only; the downstream Git index and commits were not modified.",
    "downstream": str(repo),
    "baselineCommit": (baseline / "git-head.txt").read_text(encoding="utf-8-sig").strip(),
    "baselinePolicy": "Existing files use the preserved working-tree bytes, including prior user changes. The four initially clean non-App default locks and Directory.Build.props use verified original Git HEAD bytes.",
    "initialDirtyStatus": (baseline / "git-status.txt").read_text(encoding="utf-8-sig"),
    "patchEncoding": "UTF-8 without BOM, canonical LF; exact raw before/after hashes are listed separately. Unchanged source is excluded from the patch.",
    "patchSha256": sha(patch_bytes),
    "rehearsal": {"path": str(rehearsal), "gitApplyCheckPassed": True, "allAppliedCanonicalHashesMatch": True},
    "files": records,
}
(output / "p3.manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
print(f"PASS: {sum(item['changedByPatch'] for item in records)} changed files, {len(records)} guarded paths, exact dirty-baseline patch rehearsed.")
