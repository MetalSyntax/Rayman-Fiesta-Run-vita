<h1 align="center">
  <img src="extras/livearea/pic0.png" alt="Rayman Fiesta Run PS Vita Banner" width="700"><br>
  Rayman Fiesta Run · PS Vita Port
</h1>

<p align="center">
  <a href="#-status--playability">Status & Playability</a> •
  <a href="#setup-instructions-for-players">How to install</a> •
  <a href="#controls">Controls</a> •
  <a href="#known-issues">Known Issues</a> •
  <a href="#build-instructions-for-developers">How to compile</a> •
  <a href="#credits--acknowledgments">Credits</a>
</p>

An unofficial, native **iOS-to-PS Vita wrapper port** of *Rayman Fiesta Run* (originally developed by **Ubisoft Montpellier** and **Pastagames**). There is no Android `.so` and no JNI here: the port ships its own **Mach-O loader** and **Objective-C runtime** that map the official iOS ARMv7 executable (`rfr_dev`, from the 1.2.3 IPA) into PS Vita RAM and replace the Apple frameworks it uses (Foundation, UIKit, GLKit, CoreGraphics, OpenAL/AudioToolbox, GCD/Blocks, sqlite) with native implementations on top of **vitaGL**, **openal-soft**, **FreeType** and the Vita system libraries.

---

## 🎮 Status & Playability

**Status: PLAYABLE (BETA v0.1) (v01.00)**

The game boots, reaches the main menu and the world map, and plays levels on physical PlayStation Vita hardware with music, touch and working saves. The world map runs at about 19 FPS and the first load before the menu is long (see [Known Issues](#known-issues)).

- [x] **Mach-O loader & ObjC runtime**: maps the armv7 PIE image at `0x98000000`, applies 62150 rebase words, binds 6388 import slots (862 symbols, 1 import left unimplemented), registers 231 game classes / 6 categories / 2870 selector refs, and runs 97 static initializers. *Verified on console.*
- [x] **Graphics**: GLES1 (plus some GLES2 calls) → vitaGL. The game runs as an iPhone 5 (`GameViewController_iPhone5`, 16:9): its GLKView renders at 1136×640 and is scaled to the Vita's 960×544, no rotation, no black bars (rendering at 960×540 was tried: no FPS gain, and touch drifted). *Verified on console.*
- [x] **Performance**: double presents from the GLKViewController timer were removed; levels run at ~45–50 FPS. *Verified on console.*
- [ ] **World map performance**: ~19 FPS, CPU-bound (the GPU is idle). VBO uploads were the biggest GL cost and were sped up; more work pending.
- [x] **Touch**: the front touchscreen is forwarded as iOS touches; a 4 pt slop keeps panel jitter from being read as a drag (one tap enters a world on the map). *Verified on console.*
- [ ] **Physical buttons**: buttons are mapped to touch points through `controls.txt`; the default layout has not been calibrated on console yet.
- [x] **Audio**: OpenAL SFX and music through `AVAudioPlayer` (tracks converted to `.ogg` when staging). *Verified on console.*
- [x] **Saves**: `slot0.dat`, `Activities` and `Achievements` in `ux0:raymanfiesta/Documents/` are written and loaded back (`UIDocument`, `NSFileHandle`, `-[NSData getBytes:]`). *Verified on console.*
- [ ] **Loading screen**: the "Loading.." screen after the Ubisoft logos stayed black because `gettimeofday`/`time` used the Vita's 64-bit `time_t` layout instead of iOS's 32-bit one; fixed in this build, pending confirmation on console.
- [x] **Offline bypasses**: GameKit, StoreKit, Social and PLCrashReporter are stubbed; there are no videos to skip.

---

## Disclaimer

**Rayman** and **Rayman Fiesta Run** are registered trademarks of **Ubisoft Entertainment**. The work presented in this repository is unofficial and is not produced, endorsed, or sanctioned by Ubisoft or any affiliated entities.

This software does NOT contain original game code, binaries, copyrighted graphics, or game assets. The authors do not promote or condone piracy in any way. To play, you must provide your own legally obtained, **decrypted** copy of the iOS `.ipa`.

---

## Setup Instructions (For Players)

1. **CFW & Plugins**: PS Vita on Custom Firmware (**Enso 3.60 / 3.65** recommended) with `kubridge` under `*KERNEL` in `ur0:tai/config.txt`:
   ```ini
   *KERNEL
   ur0:tai/kubridge.skprx
   ```
2. **Shader Compiler**: `libshacccg.suprx` must be in `ur0:data/` (extract it with **ShaRKBR33D**).
3. **Game Files**: a decrypted **Rayman Fiesta Run 1.2.3** iOS `.ipa` (FairPlay `cryptid 0`). Open it as a ZIP and extract `Payload/<App>.app/` (the folder that contains the `rfr_dev` executable). Then run `tools/stage_data.sh` on a PC (see [Build Instructions](#build-instructions-for-developers)) to generate the loader tables and convert the music. The game data is about 1.1 GB.
4. **Copy to the Vita** (`ux0:raymanfiesta/`, **not** `ux0:data/`: the game's `casa_gamedata` tree is 9 folders deep under `ux0:data/raymanfiesta/game_app/`, and `ux0:` supports at most 8 nested folders):
   ```
   ux0:raymanfiesta/
     game_app/      the .app bundle from the IPA (music converted to .ogg by stage_data.sh)
     rebase.bin     rebase table generated from rfr_dev by tools/ios_prep.py
     binds.bin      import table generated from rfr_dev by tools/ios_prep.py
     nibs/          precompiled view-controller nibs (tools/nib_prep.py)
     controls.txt   button -> touch mapping, editable without recompiling
     Documents/     created automatically — your save games
     logs/          created automatically — raymanfiesta_NNN.log, one per run
   ```
5. **Install VPK**: install `raymanfiesta.vpk` with VitaShell.

---

## Controls

| Input | Action | Status |
| :---: | :--- | :---: |
| **Front Touchscreen** | Full touch input: jump, punch, menus | ✅ Working |
| **Cross (✖)** | Touch at (430, 270) | ⚠️ Not calibrated |
| **Circle (●)** | Touch at (50, 270) | ⚠️ Not calibrated |
| **Square (◼)** | Touch at (430, 50) | ⚠️ Not calibrated |
| **Start** | Touch at screen center (240, 160) | ⚠️ Not calibrated |
| **Left Stick** | Accelerometer (tilt) | ⚠️ Untested |
| **Select + Start (hold 2 s)** | Cycle image rotation (saved to `rotation.txt`) | ⚠️ Untested |

> [!TIP]
> Buttons are touch points defined in `ux0:raymanfiesta/controls.txt` (one `BUTTON X Y` per line, up to 6 lines, in 480×320 on-screen coordinates, origin top-left). Edit it over FTP to remap buttons without recompiling.

---

## Known Issues

- **Long first load**: about 30 s from the Ubisoft logos to the main menu. Image decoding was sped up in this build (not yet measured on console).
- **Black loading screen** before the menu: fixed in this build (time functions), pending console confirmation.
- **World map at ~19 FPS**: levels run at ~45–50 FPS.
- **Teensy counter not drawn**: on the world map, the teensy unlock counter is skipped because its texture atlas is not ready when the game draws it (this used to crash; now it is just hidden).
- **Online features disabled**: Game Center, in-app purchases, social sharing and crash reporting are stubbed by design.
- **Crash dumps** are written by the system to `ux0:data/` (not to the game folder).

---

## Build Instructions (For Developers)

### Prerequisites

- [VitaSDK](https://vitasdk.org/) (`$VITASDK`) with `vitashark`, `openal`, `vorbis`/`ogg`, `freetype`, `libpng`, `zlib`, `bzip2` and `kubridge`. vitaGL is vendored and built with `SOFTFP_ABI=1`.
- CMake 3.14+, Python 3 with `capstone` (for `tools/ios_prep.py`), and `ffmpeg` (music conversion to `.ogg`).
- The decrypted `.app` folder, linked as `game_app` in the repo root (never committed).

### Compilation Commands

```bash
git clone https://github.com/MetalSyntax/Rayman-Fiesta-Run-vita.git
cd Rayman-Fiesta-Run-vita
ln -s /path/to/Payload/<App>.app game_app

# Stage the console data into ux0_data/raymanfiesta/
# (copies the bundle, generates rebase.bin / binds.bin / nibs, converts music to .ogg)
tools/stage_data.sh

# Build eboot.bin / raymanfiesta.vpk
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

### Deploy & Debug (over VitaShell FTP)

```bash
python3 -I tools/deploy_data.py          # incremental upload of ux0_data/raymanfiesta/ (never touches Documents/ or logs/)
python3 -I tools/deploy_data.py --logs   # fetch logs/raymanfiesta_NNN.log and crash dumps into ./logs/
```

A crash PC in `0x98xxxxxx` minus `0x98000000` is the address in the Mach-O (`rfr_dev`). A data abort at an address below `0x02000000` usually means a missing rebase (see `ux0_data/raymanfiesta/report.txt`). The `source/` runtime is shared with the other iOS ports and synced with `tools/sync_runtime.sh`; only `source/port_config.h` and `source/patch.c` are game-specific. Architecture and progress notes: [`PORTING_PLAN.md`](PORTING_PLAN.md).

---

## ☕ Support My Work

This project is **free** and will stay free. If you enjoy it, a tip from **$1** helps me keep porting games to the PS Vita:

<p align="center">
  <a href="https://ko-fi.com/metalsyntax"><img src="https://ko-fi.com/img/githubbutton_sm.svg" alt="Support me on Ko-fi"></a>
</p>

- **Ko-fi:** [ko-fi.com/metalsyntax](https://ko-fi.com/metalsyntax)
- **PayPal:** [paypal.me/MetalSyntax](https://paypal.me/MetalSyntax)
- **Bitcoin (BTC):** `133HdCsaWxdjTfmwpgGVykByhNBV338uRc`

**Ko-fi supporters get:**
- **Early access** to new ports before their public release (test builds of upcoming projects).
- **Data preparation scripts and step-by-step guides** to set up your own game files for each new port.
- A say in which games I port next.

> Game files are never distributed. You always need your own legally obtained copy of the game.

---

## Credits & Acknowledgments

- **Rayman Fiesta Run** is intellectual property of **Ubisoft**, developed by Ubisoft Montpellier and Pastagames.
- **Rayman Jungle Run PS Vita Port** by [MetalSyntax](https://github.com/MetalSyntax/Rayman-Jungle-Run-vita), the reference for the same Pastagames engine family.
- **Inotia 2 PS Vita Port** by [MetalSyntax](https://github.com/MetalSyntax/Inotia2-vita), the origin of the Mach-O loader.
- **vitaGL** graphics library by [Rinnegatamante](https://github.com/Rinnegatamante/vitaGL).
- **kubridge** plugin by [Bythos](https://github.com/bythos14/kubridge).
- **openal-soft**, **FreeType**, **libpng**, **zlib**, **libvorbis** and **capstone** projects.
- Special thanks to the PS Vita homebrew and porting community.
