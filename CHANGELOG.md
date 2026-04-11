# Changelog

All notable changes to this project are documented here.  
Release tags on GitHub use **v1.x.y** (product line). The VCV Rack manifest in `plugin.json` uses **2.x.y** where major **2** denotes the Rack 2 plugin API ([manifest rules](https://vcvrack.com/manual/Manifest.html)).

## [1.1.0] - 2026-04-10

### Highlights

- **Release `v1.1.0`** — this is the version name for GitHub / users. Rack 2 build uses `plugin.json` **2.1.0** only as the required **2.*.*** manifest (and as the prefix of `make dist` filenames); it is not a separate “2.1.0 product release.”
- **KRONO Eurorack firmware** — Behavior aligned with hardware firmware **v1.4.0** (release **v1.0.0** matched **v1.3.2**).
- **30 modes** — Full Omega (1–20) and Gamma (21–30) families, aligned with [KRONO firmware](https://github.com/sitar93/krono) at that revision.
- **Correct multi-instance behavior** — Rhythm/Gamma engine state lives in a per-module runtime (`EngineRuntime`); no shared static state between Krono instances.
- **Thread safety (Rack 2)** — Engine pointer for firmware-style dispatch uses `thread_local`, so parallel engine threads do not cross-wire instances.
- **LED feedback** — On mode confirm (final tap) and on save-dismiss timeout, **status** and **aux** flash together with a **single** normal-length pulse (same duration as a standard aux blink), not the Gamma double-pulse pattern.
- **Patch persistence** — Extended state recall (tempo, mode, calc, fixed bank, rhythm, Gamma parameters) consistent with firmware-aligned storage.

### Build / tooling

- Windows scripts and MSYS2 helpers updated for local build, install check, and Rack launch paths (see README).

## [1.0.0] - 2026-04-09

- Initial public release; manifest **1.0.0**; aligned with firmware **v1.3.2** per release commit message.
