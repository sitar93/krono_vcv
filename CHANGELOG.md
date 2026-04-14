# Changelog

All notable changes to this project are documented here.  
From **v2.1.0** onward, the **Git tag** matches **`plugin.json`**: both use **2.1.0** style versioning, with major **2** required for the Rack 2 plugin API ([manifest rules](https://vcvrack.com/manual/Manifest.html)). Earlier **v1.0.0** used manifest **1.0.0** (Rack 1 line).

## [2.1.0] - 2026-04-10

### Highlights

- **v2.1.0** — Git tag and `plugin.json` **2.1.0** aligned; Rack 2 build (successor to **v1.0.0** / manifest 1.0.0).
- **KRONO Eurorack firmware** — Behavior aligned with hardware firmware **v1.4.0** (**v1.0.0** of this plugin matched firmware **v1.3.2**).
- **30 modes** — Full Alpha (1-10), Beta (11-20), and Gamma (21-30) families, aligned with [KRONO firmware](https://github.com/sitar93/krono) at that revision.
- **Correct multi-instance behavior** — Rhythm/Gamma engine state lives in a per-module runtime (`EngineRuntime`); no shared static state between Krono instances.
- **Thread safety (Rack 2)** — Engine pointer for firmware-style dispatch uses `thread_local`, so parallel engine threads do not cross-wire instances.
- **LED feedback** — On mode confirm (final tap) and on save-dismiss timeout, **status** and **aux** flash together with a **single** normal-length pulse (same duration as a standard aux blink), not the Gamma double-pulse pattern.
- **Patch persistence** — Extended state recall (tempo, mode, calc, fixed bank, rhythm, Gamma parameters) consistent with firmware-aligned storage.

### Build / tooling

- Windows scripts and MSYS2 helpers updated for local build, install check, and Rack launch paths (see README).

## [1.0.0] - 2026-04-09

- Initial public release; manifest **1.0.0**; aligned with firmware **v1.3.2** per release commit message.
