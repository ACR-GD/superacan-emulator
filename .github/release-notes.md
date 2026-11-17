## Downloads

| File | What it is |
|---|---|
| `superacan-emu-windows-x64.zip` | Windows standalone emulator (SDL2; `SDL2.dll` and `libwinpthread-1.dll` included) |
| `superacan-emu-linux-x86_64.tar.gz` | Linux standalone emulator (needs SDL2: `sudo apt install libsdl2-2.0-0`) |
| `superacan_libretro-windows-x64.zip` | RetroArch core for Windows |
| `superacan_libretro-linux-x86_64.zip` | RetroArch core for Linux |
| `superacan_libretro-macos.zip` | RetroArch core for macOS (Apple Silicon + Intel) |
| `superacan_libretro-android.zip` | RetroArch core for Android (arm64-v8a, armeabi-v7a, x86_64, x86) |

**Standalone:** run `superacan-emu` with a ROM (`superacan-emu game.bin`), drop a ROM on the program, or start it and drop a ROM on its window. Keys: arrows = D-pad, Z/X = A/B, A/S = X/Y, Q/W = L/R, Enter = Start, Space = Select.

**RetroArch:** copy the core file to RetroArch's `cores` folder and `superacan_libretro.info` to its `info` folder, then *Load Core → Super A'Can Emulator* and *Load Content*. On Android, use *Load Core → Install or Restore a Core* and pick `superacan_libretro_android.so` from the folder for your device (`arm64-v8a` on almost all phones). Save states, battery saves (`.srm`) and two controllers work. The console's own ROMs are optional (`system/superacan/`).

No game or console ROMs are included.
