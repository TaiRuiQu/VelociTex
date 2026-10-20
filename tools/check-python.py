"""Parse Python sources without importing or executing them."""
import ast
import pathlib
import sys

failed = False
for name in sys.argv[1:]:
    path = pathlib.Path(name)
    try:
        ast.parse(path.read_text(encoding="utf-8-sig"), filename=str(path))
    except SyntaxError as error:
        failed = True
        print(f"{name}: {error}", file=sys.stderr)
if failed:
    raise SystemExit(1)
