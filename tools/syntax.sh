#!/bin/bash
# Syntax-checks one source with clang on Linux; writes its diagnostics to out/syntax/<n>.log
f="$1"; D=~/projects/thug2-decomp; H=~/projects/thug2-headless
n=$(echo "$f" | sed "s#$D/src/##; s#$H/##; s#/#__#g")
clang++-18 -std=c++20 -fsyntax-only -ferror-limit=50 -w \
  -I$D/src -I$H/compat -I$H/compat/generated -I$H/out/generated -I$H/out/sdl3-install/include -I$H/deps/imgui-1.91.9b-docking -I$H/deps/GSL-4.2.0/include -I$H/deps/lzham_codec-d379b1f9121e2197881c61cfc4713c78848bdfe7/include \
  -include $H/compat/msvc_crt.h "$f" > $H/out/${OUT:-syntax}/$n.log 2>&1
echo "$? $f"
