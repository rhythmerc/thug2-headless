# thug2-headless

Go/no-go spike: the THUG2 decomp's core (base, formats, wire, engine, script, game, render_pc) built on Linux with
clang and run headless, to see whether its skater can become a third skating engine in skate3-quest-ar.

- `compat/`: what MSVC and Win32 give the decomp's code (`msvc_crt.h` is forced into every file; `windows.h` is the
  subset the core uses, POSIX underneath).
- `build_sources.txt`: the decomp sources that compile here (from `tools/sources.py` + `tools/syntax.sh`).
- `deps/` (not committed): GSL 4.2.0 and LZHAM, as the decomp's CMake pins them.

Needs `~/projects/thug2-decomp` (branch `linux-headless`) and the game's data in `~/Games/THUG2-extract/Game`.
