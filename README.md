# Super A'Can Emulator

An emulator for the Funtech Super A'Can: a native macOS app, a standalone build for Windows and
Linux, and a RetroArch core for Windows, Linux, macOS and Android (see
[Windows, Linux, Android and RetroArch](#windows-linux-android-and-retroarch)). The macOS app is
written in AppKit with CoreAudio and GameController and has no SDL dependency. The design uses the
console's own colours: shell gray and A'Can green.

![Best of: twelve Super A'Can games running in the emulator, all with English translation patches: Super Dragon Force, The Son of Evil, Rebel Star, Sango Fighter, Speedy Dragon, Journey to the Laugh, Boom Zoo, Formosa Duel, Monopoly: Adventure in Africa, Super Taiwanese Baseball League, Magical Pool and Gambling Lord](screenshots/bestof.png)

The whole Super A'Can library, running in this emulator with the English translation patches from
[ACR-GD](https://github.com/ACR-GD): Super Dragon Force, The Son of Evil, Rebel Star, Sango Fighter,
Speedy Dragon, Journey to the Laugh, Boom Zoo, Formosa Duel, Monopoly: Adventure in Africa, Super
Taiwanese Baseball League, Magical Pool and Gambling Lord.

## Build

```bash
make app      # → "Super ACan Emulator.app"
make run-app  # build and launch
make dmg      # → SuperACan-Emulator-3.0.dmg (drag-to-Applications installer)
make test     # save-state determinism test (needs ../build/Super-Dragon-Force_*.bin)
make          # → superacan-emu, the SDL command-line build (headless runs, scripted screenshots)
```

## Using the app

- **Library:** use **+ Add ROMs** (⇧⌘O), or drag `.bin` files or whole folders onto the window.
  Data ROMs (`*_DAT*`, `08002…`) are skipped automatically, and Super Dragon Force's data ROM is
  picked up from the same folder.
- **Play:** double-click a card, select one and press Return, or use the **Play** button.
  **File → Open ROM…** (⌘O) adds a ROM and plays it in one step. You can also use Finder's
  *Open With*.
- **Cards:** each card shows the screen you were on when you last quit the game.
- **Esc** opens the pause menu (Resume / Save State / Load State / Reset / Quit to Library, plus a
  slot picker).

| Keyboard | Super A'Can | | Menu shortcut | Action |
|---|---|---|---|---|
| Arrow keys | D-Pad | | ⌘S / ⌘L | Save / load state |
| Z / X | A / B | | ⌘1 … ⌘5 | Choose state slot |
| A / S | X / Y | | ⌘R | Reset |
| Q / W | L / R | | ⌘P | Pause |
| Return | Start | | ⌘W | Quit to library |
| Space | Select | | ⌥⌘2…4 | Window size 2×–4× |
| Tab (hold) | Fast-forward | | ⌃⌘F | Full screen |

Gamepads (Xbox, PlayStation, MFi) work automatically. Right trigger is fast-forward.

### Sound style (Emulation → Sound Style)

- **Accurate (hardware envelopes)**, the default: notes fade in and out the way the console's
  UM6619 sound chip shapes them, following the MiSTer FPGA core.
- **Sustained (fuller, like MAME)**: notes hold full volume until released, as in MAME, which
  doesn't implement the chip's envelope registers. Louder and bass-heavier, less accurate.

The command-line build takes `--sound accurate|sustained`.

## Data

Everything lives in `~/Library/Application Support/Super ACan Emulator/`:
- `States/` holds per-game save states, 5 slots each.
- `Covers/` holds last-session screenshots.

The library list itself is stored in the app's preferences (`com.superacan.emulator`).

Save states (format v2) capture the whole machine: the 68000 context, work RAM, PPU, the 6502
sound CPU and sampler, timers and DMA. Loading a state reproduces the following video and audio
bit-for-bit (`make test`). Each state is tagged with a fingerprint of the ROM, so it can only be
loaded into the game it came from.

## Windows, Linux, Android and RetroArch

Prebuilt files are on the [Releases](https://github.com/ACR-GD/superacan-emulator/releases) page:
a standalone emulator for Windows and Linux, and a libretro core for RetroArch on Windows, Linux,
macOS and Android.

- **Standalone (Windows / Linux):** `superacan-emu game.bin`, or drop a ROM onto the program or its
  window. Keys: arrows = D-pad, Z/X = A/B, A/S = X/Y, Q/W = L/R, Enter = Start, Space = Select.
- **RetroArch:** put the core in RetroArch's `cores` folder and `superacan_libretro.info` in `info`.
  On Android use *Load Core → Install or Restore a Core*. Save states, `.srm` battery saves and two
  controllers are supported; the *Sound style* core option switches accurate/sustained envelopes.
  Optional console ROMs go in `system/superacan/`. Super Dragon Force's data ROM (`08002`) must sit
  next to the game.

Build them yourself:

```bash
make                                  # standalone SDL2 build (macOS, Linux, Windows/MSYS2 MinGW64)
make -f Makefile.libretro             # libretro core for this OS (.so / .dylib / .dll)
make -f Makefile.libretro deps/musashi/m68kops.c && ndk-build -C jni   # Android cores
c++ -std=c++17 -Ideps/libretro tests/libretro_test.cpp -o lrtest && ./lrtest ./superacan_libretro.so game.bin 1500 out.ppm
```

The last line checks a core without RetroArch: it runs a game, writes the last frame and checks
that a save state replays identically. `.github/workflows/build.yml` builds everything on GitHub
Actions and publishes a release for each `v*` tag.

## Building

```
git clone --recurse-submodules https://github.com/ACR-GD/superacan-emulator.git
cd superacan-emulator && make app
```

No game or console ROMs are included. Bring your own game dumps. The console's internal
ROMs are optional: put `internal_68k.bin`, `umc6650.bin` and the 6502 ROMs in a folder and pass
`--bios DIR` to the command-line build; without them the emulator boots games directly.
`make test TEST_PRG=path/to/game.bin` runs the save-state round-trip test on a game of yours.

## Layout

```
src/main.cpp        SDL2 frontend (Windows, Linux, command line)
src/libretro/       libretro core (RetroArch)
src/mac/app.mm      native frontend (library, game view, pause menu, audio, input, menus)
src/emu.*           machine (68000 via Musashi + bus + PPU + APU)
src/video/ppu.*     UM6618 video: 4 tile layers, ROZ, sprites
src/sound/apu.*     6502 sound CPU + sampler
src/savestate.*     save states; src/state_io.h is the serializer
src/main.cpp        SDL command-line build
tools/              make_icon.py, make_app_bundle.sh
deps/               Musashi (68000 core) and the MiSTer Super A'Can core (submodules)
```


## License

The emulator source in this repository is MIT-licensed (see `LICENSE`). The submodules keep
their own licenses: `deps/musashi` (Musashi, its own license) and `deps/mister` (the MiSTer
Super A'Can core, GPLv2). No console or game ROMs are included.
