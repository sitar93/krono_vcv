# KRONO VCV Rack Plugin

**Brand:** Jolin · **Author:** Federico Intrisano  

VCV Rack port of KRONO, aligned with the firmware behavior of the physical module.  
**Current release: `v2.1.0`** — same version in the Git tag, [`CHANGELOG.md`](CHANGELOG.md), and **`plugin.json`** (Rack 2 requires manifest major **2**; see [VCV manifest](https://vcvrack.com/manual/Manifest.html)). `make dist` produces names such as **`Krono-2.1.0-win-x64.vcvplugin`** (middle segment = manifest `version`, suffix = platform, e.g. `lin-x64`, `mac-x64`, `mac-arm64`). Paired with **KRONO Eurorack firmware v1.4.0** (30 modes, Gamma 21–30).

| Link | URL |
|------|-----|
| Krono (product) | [jolin.tech/krono](https://jolin.tech/krono) |
| This plugin (source) | [github.com/sitar93/krono_vcv](https://github.com/sitar93/krono_vcv) |
| Eurorack firmware (hardware) | [github.com/sitar93/krono](https://github.com/sitar93/krono) |

Release history: [`CHANGELOG.md`](CHANGELOG.md).

## Features

| Aspect | Detail |
|--------|--------|
| Module | Single module `Krono` |
| Modes | 30 operational modes (Alpha 1-10, Beta 11-20, Gamma 21-30) |
| Clock | Tap tempo + external clock |
| I/O | MOD/SWAP aligned to firmware; 12 gate outputs `1A..6A`, `1B..6B` |
| State | Patch persistence (tempo, mode, calc, fixed bank, rhythm, Gamma params) |

## Module operation

This VCV module mirrors KRONO firmware from the hardware project.

### Controls

| Role | VCV / HW | Behavior |
|------|----------|----------|
| Tap | Button (PA0) | Sets tempo from valid tap intervals; long hold enters mode-change UI; short tap confirms mode |
| Mode | Button (PA1) | Short press: calc swap in base modes; in modes 12–30, short press runs the mode gesture; in mode-change UI, each release increments target index |
| External clock | Input (PB3) | When stable, overrides tap tempo; on timeout, falls back to last valid internal or external timing |
| Swap / MOD CV | Input (PB4) | Swap or bank advance when allowed; in modes 12–30, same gesture as MOD short press |

### Behavior notes

| Topic | Detail |
|-------|--------|
| Tap tempo | Updates only after valid measured intervals to avoid accidental BPM jumps |
| Clock priority | External clock wins when stable; loss of clock returns to a valid internal reference |
| Swap / MOD | Classic modes: mainly A/B dataset roles; modes 12–30: drives each mode’s performance function |
| State recall | Tempo, mode, swap state, and mode-specific parameters restore from the patch |

### Outputs

| Output | Role |
|--------|------|
| `1A`, `1B` | **Main beat** at the current **base tempo** (same idea as the hardware doc: clock on **1A**/**1B**). In Gamma modes 21–30 they are **not** auto-pulsed every beat; the internal **base-tempo** clock still advances the mode |
| `2A..6A`, `2B..6B` | Mode-dependent gates |
| All | 12 gate outputs total; many modes pair **A** and **B** datasets; **Swap** exchanges roles where applicable |

### Mode families

| Range | Family |
|-------|--------|
| 1–10 | Clock and rhythm transforms with A/B swap |
| 11 | Fixed 16-step pattern banks (MOD/CV cycles bank) |
| 12–20 | Rhythm modes: MOD drives mode-specific actions |
| 21–30 | Gamma modes: TAP hold past ~3 s (Aux double-pulse), then MOD count + TAP confirm |

## Operational modes

Mode **names** and **summaries** match the [KRONO Eurorack firmware user reference](https://github.com/sitar93/krono/blob/main/README.md#operational-modes-reference) (hardware docs). Enum order is the same as `operational_mode_t` in [`src/modes/modes.h`](https://github.com/sitar93/krono/blob/main/src/modes/modes.h). On the module, **Swap** is the short **Mode** button (or calc swap) where applicable; **MOD** is the same short press in rhythm/Gamma ranges — in VCV use the **Swap / MOD CV** input as the firmware uses **PB4**. **Base tempo** is the main musical period (hardware README: **main beat** on **1A**/**1B** at the active interval); the summaries below say **beat** where they mean one tick of that clock.

| # | Mode | Summary |
|---|------|---------|
| 1 | **RATIOS** | Group A: clocks at **multiples** ×2…×6 of base on 2A–6A. Group B: **divisions** /2…/6 on 2B–6B. **Swap:** inverts A/B roles (A divisions, B multiplications). |
| 2 | **EUCLIDEAN** | Euclidean rhythms with distinct K/N sets per group on outputs 2–6. **Swap:** swaps K/N sets between A and B. |
| 3 | **MUSICAL** | Rhythmic ratios vs base tempo on 2–6 per group. **Swap:** swaps ratio sets A/B. |
| 4 | **PROBABILISTIC** | Per-output trigger probabilities on each **beat**; A rising, B decreasing curves. **Swap:** inverts curves between groups. |
| 5 | **SEQUENTIAL** | Fibonacci-style vs primes-style sequences on A/B. **Swap:** alternate sequence sets (e.g. Lucas / composites). |
| 6 | **SWING** | Per-output swing on even beats; multiple profiles. **Swap:** swaps swing sets. Profiles persist in saved state. |
| 7 | **POLYRHYTHM** | X:Y polyrhythms on 2–5; output 6 = logical OR of 2–5 in that group. **Swap:** swaps X:Y sets. |
| 8 | **LOGIC** | Combines **mode 1 (RATIOS)** derived signals: Group A **XOR** between paired A/B outputs; Group B **NOR**. **Swap:** swaps gate types (A↔B roles in that scheme). |
| 9 | **PHASING** | Group B at slightly detuned rate vs A; derived clocks on 3–6. **Swap:** cycles deviation amount. |
| 10 | **CHAOS** | Lorenz attractor threshold crossings; shared divisor across outputs 2–6. **Swap:** steps divisor (wrapped). Divisor persisted. |
| 11 | **FIXED** | 16-step fixed patterns at **4×** main clock; drum-style mapping on 2–6; **10 banks** (0–9), **MOD** advances bank; banks persisted. |
| 12 | **DRIFT** | Fixed base pattern with stochastic mutation at bar boundaries. **MOD:** elastic loop on drift probability (`10→…→100→…→0`), with stronger unpredictability and occasional larger jumps at higher values. MOD state persisted. |
| 13 | **FILL** | Fill-focused groove shaping with sparse low-end behavior at low values. **MOD:** **drastic loop** on fill (`0→10→…→50→0`), no gradual descent. Low values stay very empty with kick emphasis; each step is intentionally more audible. MOD state persisted. |
| 14 | **SKIP** | Base pattern; probabilistic skipping of hits. **MOD:** elastic loop on skip probability: `10→…→100→…→0→…`. MOD state persisted. |
| 15 | **STUTTER** | Base pattern with stutter lengths. **MOD:** **drastic loop** `2→4→8→2…` (no descending/off cycle). After each full stutter cycle, the base rhythm is slightly randomized to keep motion alive. MOD state persisted. |
| 16 | **MORPH** | Fully generative morph stream (A→B→C→D… continuously evolving pseudo-chaotically while staying musically coherent). **MOD:** freeze current state; next press resumes and advances to the next generated state. Freeze state persisted. |
| 17 | **MUTE** | Random additive/subtractive mute flow by output. **MOD:** each press mutes or unmutes one random channel depending on phase; each unmute also applies a slight random pattern variation to that channel. Mute state persisted. |
| 18 | **DENSITY** | Density-sculpted variation layer. **MOD:** **drastic loop** `0→10→…→200→0`; low values are highly sparse; each increase regenerates rhythmic variation (not just density). MOD state persisted. |
| 19 | **SONG** | Sectioned form with generated loops: bars **1–6** = generated base loop, bars **7–8** = generated variation loop. **MOD:** schedules a brand-new random base loop for the next cycle; without MOD, the current base loop repeats. Variation state persisted. |
| 20 | **ACCUMULATE** | Automatic accumulation loop with random output activation and random phase offsets on each newly activated output. Each activation also applies a slight random loop variation on the activated output. At max accumulation it resets **drastically** to minimum and repeats. **MOD:** freeze/unfreeze automatic accumulation. Accumulation state persisted. |
| 21 | **SEQUENTIAL RESET** | **One output active per beat**, full sweep **1A…6A** then **1B…6B** (12 steps). **MOD:** restart beat phase. **Swap:** play the 12-step cycle backward. **1A/1B** follow this pattern, not a separate copy of the main beat. |
| 22 | **SEQUENTIAL FREEZE** | Same sweep as 21. **MOD:** toggle freeze (hold step). Persisted. |
| 23 | **SEQUENTIAL TRIP** | Six scripted trip patterns. **MOD:** next pattern, reset step. **Swap:** reverse step order. Persisted. |
| 24 | **SEQUENTIAL FIRE** | **MOD:** **1A** and **6B** together; then on **each following beat** one **pair** steps **up** on A and **down** on B (**2A+5B**, **3A+4B**, … **6A+1B**). Normal trigger length. |
| 25 | **SEQUENTIAL BOUNCE** | **MOD:** all **12** jacks fire **once** together, then each **row** runs a **mini burst**: A side **speeds up** (gaps shrink; top A row finishes first), B side **slows down** (bottom B row finishes last). **Six** extra pulses per jack after the unison; timing in **fixed seconds**, not locked to current BPM. |
| 26 | **PORTALS** | **Held levels** (not short triggers): each column is a “door” — one side **high**, one **low**, for the whole beat. **Divide** (default): row **1** swaps every beat, row **2** every two beats, row **3** every three, and so on. **MOD:** **multiply** — higher rows **alternate faster within the beat**. Persisted (multiply vs divide). |
| 27 | **COIN TOSS** | On **each beat**, per column random **A** vs **B** with fixed odds (85/75/65/50/25/15 % for **A**). **MOD:** flip to **mirrored** odds. Persisted. |
| 28 | **RATCHET** | **1A–6A** multiplication, **1B–6B** division (same base routing as mode 1). **MOD:** **double** the effective “fast” side **without** restarting the bar—subdivisions **stretch** to match; the underlying beat grid **does not** jump. Persisted. |
| 29 | **ANTI-RATCHET** | Same routing as 28. **MOD:** **halve** that effective speed the same way—**no** extra pause, **no** forced reset of the divided side. Persisted. |
| 30 | **STARTnSTOP** | Same routing as 28. **MOD:** **mute** all gates; **internal** timing **keeps running** so you return in phase when unmuted. Persisted. |

**Modes 12–20:** only **short MOD** (no MOD+TAP). **Gamma 21–30:** same short **MOD** gesture; **Swap** still selects **calculation swap** where the firmware uses it (e.g. backward sweep in **SEQUENTIAL RESET**).

## Mode change and save

| Step | Action |
|------|--------|
| Enter | Hold **Tap** to enter mode-change state |
| Alpha bank | On entry, at the **first Aux blink** (~1 s), release Tap and start pressing **Mode (MOD) immediately** to select modes **1–10** |
| Beta bank | Keep holding ~2 s before release to select modes **11–20** (Aux short blink) |
| Gamma bank | Keep holding ~3 s before release to select modes **21–30** (Aux longer double-pulse style) |
| After release | Wait without Mode to dismiss, or press Mode N times then **Tap** to confirm mode N in the armed bank |
| Save timeout | If you only released Tap, waiting without Mode triggers save-dismiss (firmware-aligned timeout) |
| Persisted | Tempo, mode, calc per mode, fixed bank, rhythm state, Gamma parameters |

## Repository layout

| Path | Contents |
|------|----------|
| `src/` | Plugin DSP, engine, UI |
| `res/` | Panel and assets |
| `scripts/` | Windows PowerShell and MSYS2 helpers |
| `tools/` | Dev utilities (e.g. LED tuner HTML) |
| `Makefile` | Rack build entry |
| `plugin.json` | Manifest |

## Quick start (Windows)

### Prerequisites

| Requirement | Notes |
|-------------|-------|
| MSYS2 | Default `C:\msys64` |
| VCV Rack SDK | e.g. `D:\Files\VCV\Rack-SDK` |

### Commands

| Goal | Command |
|------|---------|
| Build | `.\scripts\build_windows.ps1 -RackDir "D:\Files\VCV\Rack-SDK"` |
| Build + install | `.\scripts\build_windows.ps1 -RackDir "D:\Files\VCV\Rack-SDK" -InstallToRack` |
| Run Rack | `.\scripts\run_rack.ps1` |
| One-click | Double-click `BUILD_AND_RUN_KRONO.cmd` (build, install, verify, launch) |

## Manual build (MSYS2)

```bash
export RACK_DIR="/d/Files/VCV/Rack-SDK"
cd "/d/path/to/krono_vcv"
make
```

| Target | Result |
|--------|--------|
| `make` | Build plugin |
| `make dist` | Package into `dist/` |
| `make install` | Install to Rack user plugin folder |

### Files for GitHub Releases (`.vcvplugin`, `.sha256`)

After **`make dist`** (with `RACK_DIR` set to your [Rack SDK](https://vcvrack.com/manual/Building.html)), packages appear under **`dist/`** in the project root (folder is gitignored until you build):

| Artifact | Where it comes from |
|----------|-------------------|
| **`Krono-2.1.0-win-x64.vcvplugin`** (pattern) | Written by the SDK `plugin.mk` into **`dist/`** — `Krono-` + `plugin.json` version + `-` + arch (`win-x64`, `lin-x64`, `mac-x64`, `mac-arm64`, …). No separate `.zip` from stock `make dist`; the bundle **is** the `.vcvplugin`. |
| **`.sha256`** | **Not** generated by `make dist`. Optional sidecar: hash the `.vcvplugin` from `dist\` (e.g. `Get-FileHash -Algorithm SHA256 dist\Krono-*-win-x64.vcvplugin` in PowerShell) and save the hex digest. |

To ship **all** platforms from one environment, use the official **[rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain)** (or build on each OS and collect every `dist/*.vcvplugin`).

GitHub layout (see [Releases](https://github.com/sitar93/krono_vcv/releases)): template text in **`release/release_notes.txt`**. After `make dist`, run **`scripts/prepare_assets_from_dist.ps1 -UpdateReleaseNotes`** to copy **`dist/Krono-*-*.vcvplugin`** and matching **`Krono-*-*.sha256`** into **`release/`** for upload. **`BUILD_AND_RUN_KRONO.cmd`** runs that step automatically after a successful build (install path runs `make dist`, so `dist/` exists). Binaries stay gitignored (`*.vcvplugin`).

## Release notes flow

| Step | Task |
|------|------|
| 1 | Bump `plugin.json` version (Rack 2 line: `2.x.y`) |
| 2 | Update [`CHANGELOG.md`](CHANGELOG.md) |
| 3 | Clean build and smoke test (Rack 2) |
| 4 | `make dist` — attach **`dist/*.vcvplugin`** (per platform) to the GitHub release; optional `.sha256` sidecars |
| 5 | Git tag **`v2.x.y`** (match `plugin.json`; Rack 2 uses major **2**) |
| 6 | GitHub Release **`v2.x.y`** + VCV Library thread (version + commit hash) |

## VCV Library submission

| Resource | URL |
|----------|-----|
| Library workflow | [VCV Library README](https://raw.githubusercontent.com/VCVRack/library/master/README.md) |
| Manifest | [VCV Plugin Manifest](https://vcvrack.com/manual/Manifest.html) |
| Tutorial | [Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial.html) |

Create or reuse one issue thread per plugin slug; post metadata and, for each update, version + commit hash.

## License

| Field | Value |
|-------|-------|
| Name | Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International |
| SPDX / manifest | `CC-BY-NC-SA-4.0` |
| Full text | `LICENSE`, `LICENSE.txt` |

You may share and adapt; attribution required; no commercial use; derivatives keep the same license.
