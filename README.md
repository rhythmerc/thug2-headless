# thug2-headless

[thug2-decomp](https://github.com/CanuckAussieKev/thug2-decomp)'s game code (Tony Hawk's Underground 2) built as a
library a host steps one game frame at a time, with no window and no rendering or sound of its own: the skater's physics,
tricks and scripts, running on the host's own collision and rails. It is the THUG2 engine of
[skate3-quest-ar](https://github.com/rhythmerc/skate3-quest-ar) (a Quest 3 AR skateboarding app), which links it through
its `thug2-sys` crate.

It builds on Linux (x86-64) and Android (arm64, NDK r26) with clang.

**It runs on your own copy of THUG2 (PC).** No game files are here, and none are needed to build it.

## The API

`app/thug2_engine.h` (C):

- `thug2_boot(game_dir, argc, argv)`: starts the game on a thread of its own (`-level Default` starts it in the
  developer-only empty level, with no stock level loaded) and returns when its first frame is ready.
- `thug2_step(pad, skater)`: one frame of the game's main loop with a pad. It returns the skater as the frame left
  it: position, velocity, orientation and state (ground, air, wall, lip, rail, wallplant). The game and its host take
  turns, through the decomp's loop-pacing hook; nothing of the game runs between steps.
- `thug2_set_world(triangles, count, spawn, heading)`: the host's world as the game's collision. Each triangle carries
  THUG2's face flags (skatable, wallride, vert), and the collision file is written by `world/col_writer.cpp`. A new
  world replaces the last.
- `thug2_set_rails(points, counts, rails)`: the host's rails, in place of the level's.
- `thug2_teleport(spawn, heading)`, `thug2_set_cache_dir(folder)`, `thug2_quit()`.
- The skater's look, for a host that draws it: `thug2_skater_meshes`, `thug2_skater_mesh_data`, `thug2_skater_texture`,
  `thug2_skater_pose`.
- The game's sound effects, for a host that plays them: `thug2_sound_voices` (each voice playing: its sound, once or
  looping, volume, pan and frequency) and `thug2_sound` (a sound's 16-bit PCM). The sound output
  (`backends/capture_audio.cpp`) plays nothing itself; it keeps what the game asks of it. Streams (music, speech)
  aren't played.
- The game's 2D (its HUD), for a host that draws it: `thug2_hud_enable`, then after each step `thug2_hud` (the
  frame's 2D draws in the game's order: triangle lists of 640x480 screen vertices, each with a texture, a colour op
  and a blend) and `thug2_hud_texture` (a draw's texture as RGBA8). Asked for, the game's 2D list is drawn once more at
  the end of each frame with the in-memory GPU taking the draws down (`backends/capture_hud.cpp`). `thug2-drive`'s
  `THUG2_HUD_SHOT=<file.png>` (at `THUG2_HUD_FRAME`) rasterises a frame's 2D to a picture.

The units are the game's: inches, y up. Steering left while facing +z turns towards +x, as in a right-handed frame.

## Layout

- `app/`: the API (`engine.cpp`: the decomp's start-up with headless backends, and the lockstep turns), the skater
  probe, and `thug2-drive`, a command-line host for testing.
- `backends/`: what the Windows build installs (files, GPU, sound, movies, network, input), in headless POSIX forms.
  The GPU backend keeps textures and buffers in memory under the decomp's real PC renderer. Files are found whatever
  their case, and the empty Default level gets the sky's scene and nodes as stand-ins.
- `compat/`: what MSVC and Win32 give the decomp's code. `msvc_crt.h` is forced into every file; `windows.h` is the
  subset the core uses, with POSIX underneath.
- `world/`: the collision file writer.
- `build_sources.txt`: the decomp sources built here (from `tools/sources.py` and `tools/syntax.sh`).

## Building

You need clang 18 (or newer), CMake 3.27 and Ninja, and a checkout of the decomp with the Linux/Android patches:
[rhythmerc/thug2-decomp](https://github.com/rhythmerc/thug2-decomp), branch `linux-headless`. Put it beside this one,
or point `THUG2_DECOMP_DIR` at it.

```sh
git clone -b linux-headless https://github.com/rhythmerc/thug2-decomp ../thug2-decomp
cmake -S . -B out/build -G Ninja -DCMAKE_CXX_COMPILER=clang++-18 -DCMAKE_C_COMPILER=clang-18
cmake --build out/build
```

Third-party code (Dear ImGui, GSL, LZHAM, stb, and SDL3's headers) is downloaded at configure time, at the versions
and checksums the decomp pins. A copy in `deps/` is used instead when there is one.

For Android (arm64, API 32):

```sh
cmake -S . -B out/android -G Ninja -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-32 -DANDROID_STL=c++_shared
cmake --build out/android
```

## Trying it

```sh
THUG2_GAME_DIR=<your THUG2 folder, holding Data> THUG2_WORLD=test THUG2_FRAMES=300 \
    THUG2_INPUT="140-300:up,160-164:cross,166-240:triangle" out/build/thug2-drive -level Default
```

This gives a small test park (a floor, a quarter pipe with a deck, a wall and a rail) at frame 120. The skater
pushes, ollies and grinds the rail; every 30 frames it logs where it is and what it is doing. `app/drive_main.cpp`
lists the options (`THUG2_SOUNDS=<folder>` logs the sounds the game plays and writes each to a .wav).
`tools/quest-run.sh` runs the same test on a Quest over adb.
`tools/default-level-files.txt` lists the game files such a run reads.

## Licence

The decomp's game code belongs to the game's owners, and no licence is granted for it (see the decomp's README). This
repository's own code (everything here) is MIT-licensed; see `LICENSE`.
