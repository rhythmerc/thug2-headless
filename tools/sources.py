"""The sources of the decomp's core layers, from their CMakeLists (add_library / target_sources lists)."""
import re, sys, pathlib
root = pathlib.Path(sys.argv[1])
for layer in sys.argv[2:]:
    d = root / "src" / layer
    text = (d / "CMakeLists.txt").read_text()
    text = re.sub(r"#[^\n]*", "", text)
    for m in re.finditer(r"(?:add_library|target_sources)\s*\(([^)]*)\)", text, re.S):
        for tok in m.group(1).split():
            if tok.endswith((".cpp", ".c")) and "$" not in tok:
                p = d / tok
                if p.exists():
                    print(p)
