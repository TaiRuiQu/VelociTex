import ctypes, json, sys, pathlib
root = pathlib.Path(__file__).resolve().parents[1]
lib = ctypes.CDLL(str(root / "artifacts/native/VelociTex.NativeMath.dll"))
lib.vt_mathml.argtypes = [ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
lib.vt_mathml.restype = ctypes.c_int
fixtures = json.loads((root / "benchmarks/corpus.normal.json").read_text(encoding="utf-8"))["fixtures"]
bad = []
for fixture in fixtures:
    data = fixture["latex"].encode("utf-8")
    buf = ctypes.create_string_buffer(65536)
    size = lib.vt_mathml(data, len(data), buf, len(buf))
    if size < 0:
        bad.append((fixture["id"], fixture["latex"], size))
    else:
        text = buf.raw[:size].decode("utf-8")
        print(f'{fixture["id"]:28} {size:6} {text[:90]}')
print("unsupported", len(bad), "of", len(fixtures))
for item in bad[:10]:
    print("BAD", item)
