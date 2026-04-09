# KRONO VCV Rack Plugin

VCV Rack port of KRONO, aligned with the firmware behavior of the physical module.

## Features

- One module: `Krono`
- 20 operational modes
- Tap tempo + external clock support
- MOD/SWAP behaviors aligned to firmware logic
- 12 gate outputs (`1A..6A`, `1B..6B`)
- Patch persistence (tempo, mode, calc/fixed/rhythm state)

## Repository Layout

- `src/` plugin DSP/engine/UI code
- `res/` panel and visual assets
- `scripts/` Windows PowerShell and MSYS2 helpers
- `tools/` local utility files used during development
- `Makefile` Rack build entrypoint
- `plugin.json` manifest metadata

## Quick Start (Windows)

### Prerequisites

- MSYS2 installed (default `C:\msys64`)
- VCV Rack SDK installed (example `D:\Files\VCV\Rack-SDK`)

### Build

From PowerShell, in repo root:

```powershell
.\scripts\build_windows.ps1 -RackDir "D:\Files\VCV\Rack-SDK"
```

### Build + Install to Rack

```powershell
.\scripts\build_windows.ps1 -RackDir "D:\Files\VCV\Rack-SDK" -InstallToRack
```

### Run Rack

```powershell
.\scripts\run_rack.ps1
```

### One-click Build/Install/Run

You can also double-click:

- `BUILD_AND_RUN_KRONO.cmd`

It performs:

1. build in MSYS2
2. install to Rack user plugins folder
3. install verification
4. Rack launch

## Manual Build (MSYS2)

```bash
export RACK_DIR="/d/Files/VCV/Rack-SDK"
cd "/d/path/to/krono_vcv"
make
```

Useful targets:

- `make` build plugin
- `make dist` create distributable package in `dist/`
- `make install` install to Rack user plugin folder

## Release Notes Flow

Recommended release flow:

1. update `plugin.json` version
2. run clean build and smoke test
3. create package with `make dist`
4. tag release (`vX.Y.Z`) in git
5. publish GitHub release with changelog
6. request/update listing in VCV Library thread

## VCV Library Submission (Open-source path)

Use the VCV Library GitHub workflow:

- create (or reuse) one issue thread for your plugin slug
- post plugin metadata (name, license, URLs)
- for each update, post new version + commit hash

References:

- [VCV Library repository workflow](https://raw.githubusercontent.com/VCVRack/library/master/README.md)
- [VCV Plugin Manifest docs](https://vcvrack.com/manual/Manifest.html)
- [VCV Plugin Development Tutorial](https://vcvrack.com/manual/PluginDevelopmentTutorial.html)

## License

Same license as KRONO firmware:

- **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International**
- Identifier used in manifest: `CC-BY-NC-SA-4.0`
- Full text: `LICENSE` and `LICENSE.txt`

In short:

- You may share and adapt.
- Attribution is required.
- Commercial use is not allowed.
- Derivatives must keep the same license.
