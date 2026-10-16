#!/usr/bin/env bash
# Compila o gsplay contra o GS do runtime já compilado (PS2Recomp/out/${BUILD:-fast}).
set -euo pipefail
cd "$(dirname "$0")/../.."
P=PS2Recomp; B=$P/out/${BUILD:-fast}
g++ -std=gnu++20 -O2 -march=native -DNDEBUG -DPLATFORM_DESKTOP -DGRAPHICS_API_OPENGL_33 \
  -I$P/ps2xRuntime/include -I$P/ps2xRuntime/src/lib/Kernel -I$P/ps2xIOP/include -I$B/_deps/raylib-src/src \
  tools/gsplay/gsplay.cpp -o tools/gsplay/gsplay \
  -Wl,--start-group $B/ps2xRuntime/libps2_runtime.a $B/ps2xIOP/libps2_iop.a $B/_deps/raylib-build/raylib/libraylib.a -Wl,--end-group \
  $(pkg-config --libs libavcodec libavformat libavutil libswresample libswscale 2>/dev/null) -lzstd -lGL -lm -lpthread -ldl
echo "ok: tools/gsplay/gsplay"
