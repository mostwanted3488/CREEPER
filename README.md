# CREEPER

One-knob build-up riser by **@buyorbealoser**. VST3 / AU / Standalone for macOS and Windows.

Turn the TNT knob up: the creeper swells and blinks faster, the redstone lights up, and your track tightens
into a build-up. At 100 % the creeper explodes and everything shakes.

## What the knob does (0 → 100 %)

- **Resonant low-cut sweep** (20 Hz → ~900 Hz)
- **Light drive**
- **Tempo-synced stutter** following your project tempo: 1/4 → 1/8 → 1/16 → 1/32, soft edges (no clicks)
- **Stereo widening**, back to the centre at the drop (low end stays mono)
- **Reverb wash** and a **noise riser** that blinks with the creeper
- **97 → 100 %: pre-drop cut** — the track drops out and only the reverb tail rings out into the drop
- **Brickwall limiter** at -2 dBFS
- At **0 %** the plugin is fully transparent (bit-identical once the tails have died out)

The explosion **sound** is only played when you push the knob to 100 % by hand in the plugin window.
When the knob is moved by **automation**, the explosion is shown but stays silent.

### OPTIONS

| Setting      | Values                    |
|--------------|---------------------------|
| IMPACT       | ON / OFF                  |
| TAIL         | SHORT / NORMAL / LONG     |
| INTENSITY    | SUBTLE / NORMAL / HARD    |
| MAX STUTTER  | 1/8 / 1/16 / 1/32         |
| STEREO       | ON / OFF                  |

Knob: drag up/down (Shift = fine), mouse wheel, double-click = reset.
Automate it in FL Studio: right-click the knob → *Create automation clip* (parameter **Charge**).

## Install

Download the zip for your system from the **Releases** page (or the latest build in **Actions**).

**macOS**
- `CREEPER.vst3` → `~/Library/Audio/Plug-Ins/VST3/`
- `CREEPER.component` (AU) → `~/Library/Audio/Plug-Ins/Components/`
- The plugin is not notarized yet. If macOS blocks it, run in Terminal:
  `xattr -cr ~/Library/Audio/Plug-Ins/VST3/CREEPER.vst3 ~/Library/Audio/Plug-Ins/Components/CREEPER.component`

**macOS — build it yourself (if no Mac download is available)**
1. Download this repo: green **Code** button → **Download ZIP**, unzip it.
2. Open **Terminal**, type `bash ` (with a space), drag `build-mac.sh` into the window, press Enter.
3. First time only: if asked, install the Xcode Command Line Tools, then run step 2 again.
It builds and installs the VST3 and AU automatically (about 5–10 minutes the first time).

**Windows**
- `CREEPER.vst3` → `C:\Program Files\Common Files\VST3\`

Then rescan plugins in your DAW (FL Studio: *Options → Manage plugins → Find plugins*).

## Build from source

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

JUCE 8.0.9 is downloaded automatically. Offline DSP test: add `-DCREEPER_BUILD_TESTS=ON` and run `CreeperTest`.
