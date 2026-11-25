# Rayman Fiesta Run (PS Vita) — Beta Release v0.1

First public build of the native iOS-to-PS Vita wrapper port of *Rayman Fiesta Run* (iOS 1.2.3). On real hardware the game boots, reaches the main menu and the world map, and plays levels with music, touch and working saves. Levels run at about 45–50 FPS; the world map runs at about 19 FPS and the first load before the menu is long.

## 📦 Release Package Contents

- **`raymanfiesta.vpk`** — the loader (Title ID `PSVRFR001`, version `01.00`), with the LiveArea. No game code or assets.
- **`rebase.bin`** — relocation table for the 1.2.3 `rfr_dev` executable (62,150 words).
- **`binds.bin`** — import table for the Apple framework symbols the executable uses (6,388 import slots).
- **`controls.txt`** — default button → touch mapping (editable without recompiling).

## 🛠️ What are rebase.bin and binds.bin?

The loader maps the armv7 PIE executable at a fixed slide (`0x98000000`). `rebase.bin` lists every word holding an absolute address that must be moved by that slide, and `binds.bin` lists the slots the executable imports from Apple frameworks. Both are generated from your own executable by `tools/ios_prep.py` (run by `tools/stage_data.sh`); the attached copies match the 1.2.3 IPA.

## 🚀 Player Installation Guide

1. **Requirements**: Enso 3.60/3.65, **kubridge** in `ur0:tai/config.txt` (`*KERNEL` section), and `libshacccg.suprx` in `ur0:data/`.
2. **Game files**: a decrypted *Rayman Fiesta Run* **1.2.3** iOS `.ipa` (`cryptid 0`). Clone the repo, extract `Payload/<App>.app/` (the folder with the `rfr_dev` executable), link it as `game_app` and run:
   ```bash
   tools/stage_data.sh
   ```
   This stages `ux0_data/raymanfiesta/` (`game_app/`, `rebase.bin`, `binds.bin`, `nibs/`, `controls.txt`) and converts the music to `.ogg` (needs `ffmpeg`). The game data is about 1.1 GB.
3. **Copy** `ux0_data/raymanfiesta/` to `ux0:raymanfiesta/` on the Vita (not `ux0:data/`: the game's folder tree is too deep for it). `Documents/` and `logs/` are created automatically.
4. **Install** `raymanfiesta.vpk` with VitaShell.

## 🎮 Controls

| Input | Action | Status |
| :---: | :--- | :---: |
| **Front Touchscreen** | Full touch input: jump, punch, menus | ✅ Working |
| **Cross (✖)** | Touch at (430, 270) | ⚠️ Not calibrated |
| **Circle (●)** | Touch at (50, 270) | ⚠️ Not calibrated |
| **Square (◼)** | Touch at (430, 50) | ⚠️ Not calibrated |
| **Start** | Touch at screen center (240, 160) | ⚠️ Not calibrated |
| **Left Stick** | Accelerometer (tilt) | ⚠️ Untested |
| **Select + Start (hold 2 s)** | Cycle image rotation (saved to `rotation.txt`) | ⚠️ Untested |

## 📋 Changelog (v0.1)

- **Crash on the world map**: `TeensyUnlockManager` drew a table of UVs that was still empty, because the texture atlas was not ready yet → the table is filled on the first draws, and the teensy counter is hidden until the atlas exists.
- **Frame repeats**: the GLKViewController timer presented on every tick, repeating frames already presented by the game → the swap is skipped when the game has presented its own frame.
- **Saves**: `UIDocument` was an empty class and `NSFileHandle` was missing, so the save slot never loaded → implemented on top of the Vita file API; `-[NSData getBytes:]` was missing, which crashed the `Activities` parser → implemented. Saves are written and loaded back on console.
- **Touch on the world map**: a resting finger was reported as a drag every poll, cancelling the tap that enters a world → drags start only after a 4 pt slop.
- **Music**: `AVAudioPlayer` is implemented; the tracks are converted to `.ogg` during staging.
- **Loading speed**: `NSData` file reads use a single read, `fopen` uses a 64 KB buffer, and resource path lookups are cached.
- **World-map buffers**: the VBO copy uses a hash lookup instead of a linear search (buffer upload time on the map fell from 2.2 s to 1.5 s per 10 s).
- **Loading screen**: `gettimeofday` and `time` used the Vita's 64-bit `time_t`, while the game expects the 32-bit iOS layout → Darwin-typed wrappers. Pending confirmation on console.
- **Touch controls**: physical buttons are mapped to touch points through `controls.txt`.

## ⚠️ Known Issues

- **Long first load**: about 30 s from the Ubisoft logos to the main menu.
- **Black loading screen** before the menu: fixed in this build, pending console confirmation.
- **World map at ~19 FPS**: CPU-bound; levels run at ~45–50 FPS.
- **Teensy counter not drawn** on the world map while its texture atlas is not ready.
- **Physical buttons not calibrated**: the default touch points have not been tuned on console.
- **Online features disabled**: Game Center, in-app purchases, social sharing and crash reporting are stubbed.

Installation and build instructions: see [README.md](README.md).
