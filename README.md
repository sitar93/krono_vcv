# KRONO VCV Rack Plugin

VCV Rack port of KRONO, aligned with the firmware behavior of the physical module.  
GitHub release **`v1.1.0`** — manifest version **`2.1.0`** in `plugin.json` (MAJOR **2** = Rack 2 line, required by the [VCV manifest](https://vcvrack.com/manual/Manifest.html)). Paired with **KRONO Eurorack firmware v1.4.0** (30 modes, Gamma 21–30).

Release history: [`CHANGELOG.md`](CHANGELOG.md).

## Features

| Aspect | Detail |
|--------|--------|
| Module | Single module `Krono` |
| Modes | 30 operational modes (Omega: TAP ~2 s + MOD; Gamma: TAP ~3 s + MOD) |
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
| `1A`, `1B` | Base clock (F1). In Gamma modes 21–30 they are **not** auto-pulsed every beat; internal F1 still advances the mode |
| `2A..6A`, `2B..6B` | Mode-dependent gates |
| All | 12 gate outputs total; many modes pair **A** and **B** datasets; **Swap** exchanges roles where applicable |

### Mode families

| Range | Family |
|-------|--------|
| 1–10 | Clock and rhythm transforms with A/B swap |
| 11 | Fixed 16-step pattern banks (MOD/CV cycles bank) |
| 12–20 | Rhythm modes: MOD drives mode-specific actions |
| 21–30 | Gamma modes: TAP hold past ~3 s (Aux double-pulse), then MOD count + TAP confirm |

## Modes overview

| # | Slug | Summary |
|---|------|---------|
| 1 | `DEFAULT` | Mult on A, div on B; Swap exchanges roles |
| 2 | `EUCLIDEAN` | Euclidean sets on A/B; Swap exchanges sets |
| 3 | `MUSICAL` | Musical ratio sets; Swap flips A/B assignment |
| 4 | `PROBABILISTIC` | Probability-weighted triggers; Swap inverts tendencies |
| 5 | `SEQUENTIAL` | Sequence-style clocks; Swap swaps families |
| 6 | `SWING` | Swing on alternating beats; Swap exchanges profiles |
| 7 | `POLYRHYTHM` | X:Y relationships; Swap exchanges assignments |
| 8 | `LOGIC` | Logic of internal clocks; Swap swaps logic styles |
| 9 | `PHASING` | Detuned phase drift; Swap swaps deviation side |
| 10 | `CHAOS` | Chaotic thresholds; Swap toggles scaling behavior |
| 11 | `FIXED` | 16-step banks; MOD/CV advances bank |
| 12 | `DRIFT` | Mutating groove; MOD elastic drift probability |
| 13 | `FILL` | Fill contrast; MOD steps fill amount |
| 14 | `SKIP` | Probabilistic skips; MOD elastic skip probability |
| 15 | `STUTTER` | Stutter lengths; MOD cycles length |
| 16 | `MORPH` | Evolving patterns; MOD freeze / unfreeze |
| 17 | `MUTE` | Mute choreography; MOD varies mute phases |
| 18 | `DENSITY` | Sparse ↔ dense; MOD steps density |
| 19 | `SONG` | Base vs variation sections; MOD schedules new base |
| 20 | `ACCUMULATE` | Layering + reset; MOD freeze / unfreeze engine |
| 21 | `GAMMA_SEQUENTIAL_RESET` | 12-step sweep 1A…6A then 1B…6B; MOD/CV resets phase |
| 22 | `GAMMA_SEQUENTIAL_FREEZE` | Same sweep; MOD toggles freeze on step |
| 23 | `GAMMA_SEQUENTIAL_TRIP` | Six trip patterns; MOD cycles pattern |
| 24 | `GAMMA_SEQUENTIAL_FIRE` | MOD arms 1A+6B; following F1s fire paired outputs |
| 25 | `GAMMA_SEQUENTIAL_BOUNCE` | MOD one-shot accel (A) / decel (B) burst |
| 26 | `GAMMA_PORTALS` | Held “door” pairs; MOD toggles multiply vs divide cadence |
| 27 | `GAMMA_COIN_TOSS` | Random A vs B per pair; MOD inverts probability weights |
| 28 | `GAMMA_RATCHET` | Mult A, div B; MOD doubles effective tempo (×2) |
| 29 | `GAMMA_ANTI_RATCHET` | Same routing; MOD halves effective tempo (÷2) |
| 30 | `GAMMA_START_STOP` | Same routing; MOD toggles muted latch on outputs |

## Modes in detail

| # | Mode | Description |
|---|------|-------------|
| 1 | **DEFAULT** | Group A multiplies the base clock, group B divides it. Swap exchanges the two roles. |
| 2 | **EUCLIDEAN** | Euclidean distributions with different pulse/step sets for A and B. Swap exchanges those sets. |
| 3 | **MUSICAL** | Musically oriented timing ratios vs base tempo. Swap flips which ratio set drives each group. |
| 4 | **PROBABILISTIC** | Triggers from probability curves per output. A and B use opposing tendencies; swap inverts them. |
| 5 | **SEQUENTIAL** | Sequence-style clocks (contrasting numerical families) on A/B. Swap swaps which family drives which group. |
| 6 | **SWING** | Swing on alternating beats with per-output feel. Swap exchanges swing datasets / profile mapping. |
| 7 | **POLYRHYTHM** | X:Y-style relationships; composite behavior on the top output per group. Swap exchanges ratio assignments. |
| 8 | **LOGIC** | Gates from logical combinations of internal clock streams. Swap flips which group gets each logic style. |
| 9 | **PHASING** | Slight rate detuning for phase drift over time. Swap changes deviation assignment. |
| 10 | **CHAOS** | Chaotic thresholding for rhythm triggers. Swap steps alternate chaos scaling / division. |
| 11 | **FIXED** | 16-step fixed patterns, multiple banks. MOD or PB4 cycles banks; bank is persistent. |
| 12 | **DRIFT** | Base pattern with controlled random mutation over bars. MOD walks drift probability up/down. |
| 13 | **FILL** | Strong sparse vs active contrast. MOD steps through a pronounced fill amount loop. |
| 14 | **SKIP** | Probabilistic skips on a base groove. MOD changes skip probability over an elastic range. |
| 15 | **STUTTER** | Repeats with stutter-length tiers; base pattern gets subtle refresh. MOD cycles stutter length. |
| 16 | **MORPH** | Continuously evolving coherent rhythms. MOD freezes or resumes evolution. |
| 17 | **MUTE** | Additive/subtractive mute choreography. MOD toggles random mute phases and reintroductions. |
| 18 | **DENSITY** | Event density from sparse to busy. MOD steps density with audible regeneration. |
| 19 | **SONG** | Alternates base and variation sections. MOD schedules a fresh base for the next cycle. |
| 20 | **ACCUMULATE** | Layers outputs over time with offsets and variation, then hard reset. MOD freezes or unfreezes the engine. |
| 21 | **GAMMA_SEQUENTIAL_RESET** | One active jack per F1 in order 1A…6A, then 1B…6B (12 steps). Calc swap plays the cycle backward. MOD/CV restarts beat phase (no 1A/1B catch-up burst). |
| 22 | **GAMMA_SEQUENTIAL_FREEZE** | Same 12-step sweep as 21. MOD toggles freeze so the step index stops advancing on F1. |
| 23 | **GAMMA_SEQUENTIAL_TRIP** | Six different multi-output “trip” patterns per F1. MOD cycles pattern index; calc swap reverses step order within the pattern. |
| 24 | **GAMMA_SEQUENTIAL_FIRE** | Idle until MOD: fires 1A and 6B together, then on subsequent F1 edges fires paired outputs (2A+5B … 6A+1B) until the burst ends. |
| 25 | **GAMMA_SEQUENTIAL_BOUNCE** | MOD arms a one-shot scene: per-output pulse trains with accel timing on A rows and decel on B rows (firmware-timed gaps). |
| 26 | **GAMMA_PORTALS** | Each pair holds one side high (“open door”). Divide mode: toggle pairs on F1 counts. Multiply mode: toggle each pair on T/k ms. MOD switches divide vs multiply and resets cadence. |
| 27 | **GAMMA_COIN_TOSS** | On each F1, each pair randomly fires A or B with tiered odds. MOD inverts those odds (complement). |
| 28 | **GAMMA_RATCHET** | Classic mult-on-A, div-on-B per factor row. MOD toggles “double-speed” effective period for mult scheduling (firmware ratchet). |
| 29 | **GAMMA_ANTI_RATCHET** | Same routing as 28. MOD toggles half-speed effective period for mult scheduling. |
| 30 | **GAMMA_START_STOP** | Same routing as 28–29. MOD toggles a mute latch so mult/div pulses are suppressed while latched. |

## Mode change and save

| Step | Action |
|------|--------|
| Enter | Hold **Tap** to enter mode-change state |
| Omega bank | Keep holding ~2 s before release to select modes **11–20** (Aux short blink) |
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
| **`Krono-<version>-<arch>.vcvplugin`** | Written by the SDK `plugin.mk` into **`dist/`** — one file per build machine/arch (e.g. `win-x64`, `lin-x64`, `mac-x64`, `mac-arm64`). There is no separate `.zip` from stock `make dist`; the installable bundle **is** the `.vcvplugin`. |
| **`.sha256`** | **Not** generated by `make dist`. If you want checksum sidecars for the release page, create them yourself, e.g. `Get-FileHash -Algorithm SHA256 dist\Krono-2.1.0-win-x64.vcvplugin` in PowerShell and save the hex digest to a text file, or use `sha256sum` on Linux/macOS. |

To ship **all** platforms from one environment, use the official **[rack-plugin-toolchain](https://github.com/VCVRack/rack-plugin-toolchain)** (or build on each OS and collect every `dist/*.vcvplugin`).

GitHub layout (same idea as [v1.0.0 on Releases](https://github.com/sitar93/krono_vcv/releases)): template text in **`release/release_notes.txt`**. After `make dist`, run **`scripts/prepare_assets_from_dist.ps1 -UpdateReleaseNotes`** to copy **`Krono-2.1.0-*.vcvplugin`** and **`Krono-2.1.0-*.sha256`** into **`release/`** for upload. **`BUILD_AND_RUN_KRONO.cmd`** runs that step automatically after a successful build (install path runs `make dist`, so `dist/` exists). Binaries stay gitignored (`*.vcvplugin`).

## Release notes flow

| Step | Task |
|------|------|
| 1 | Bump `plugin.json` version (Rack 2 line: `2.x.y`) |
| 2 | Update [`CHANGELOG.md`](CHANGELOG.md) |
| 3 | Clean build and smoke test (Rack 2) |
| 4 | `make dist` — attach **`dist/*.vcvplugin`** (per platform) to the GitHub release; optional `.sha256` sidecars |
| 5 | Git tag **`v1.x.y`** (product tag; manifest stays `2.x.y`) |
| 6 | GitHub Release **`v1.x.y`** + VCV Library thread (version + commit hash) |

## VCV Library submission

| Resource | URL |
|----------|-----|
| Library workflow | [VCV Library README](https://raw.githubusercontent.com/VCVRack/library/master/README.md) |
| Manifest | [VCV Plugin Manifest](https://vcvrack.com/manual/Manifest.html) |
| Tutorial | [Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial.html) |

Create or reuse one issue thread per plugin slug; post metadata and, for each update, version + commit hash.

## License

Same license as KRONO firmware:

| Field | Value |
|-------|-------|
| Name | Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International |
| SPDX / manifest | `CC-BY-NC-SA-4.0` |
| Full text | `LICENSE`, `LICENSE.txt` |

You may share and adapt; attribution required; no commercial use; derivatives keep the same license.
