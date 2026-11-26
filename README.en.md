# Positron — Modern Web Infrastructure & Lightweight Browser Engine for Windows Mobile / WinCE

**English** | [简体中文](README.md)

The built-in Internet Explorer Mobile and networking stack on Windows Mobile 6 / 6.5 (Windows CE 5.2, ARMV4I) are long obsolete, while Visual Studio 2008, C89, and tight device memory budgets make porting modern desktop browser engines like Chromium/Blink or Gecko impractical. **Positron** takes a pragmatic approach: it ports and wraps **Mbed TLS**, **NetSurf** (HTML/CSS/DOM/layout), **Duktape** (JavaScript), **SQLite**, **cJSON**, **miniz**, **libjpeg-turbo**, **libsvgtiny/NanoSVG**, and **FFmpeg** into 11 modular, C-ABI DLLs that legacy Win32/WinCE applications can consume independently or combine into a lightweight web browser and pure-resource `app://` runtime (`positron.exe`).

Positron does **not** aim to replicate a full modern desktop browser. Its goal is to provide composable, resource-bounded, device-tested public infrastructure and a constrained web runtime for Windows Mobile / WinCE. `test_host.exe` is a regression harness and example consumer, not the owner of public product APIs.

## Scope & Positioning

- **Target platform:** Windows Mobile 6 / 6.5 Professional (Windows CE 5.2 / WinCE), ARMV4I.
- **Toolchain:** Visual Studio 2008 SP1 and Windows Mobile 6 Professional SDK (C89-compatible).
- **Ported core stack:** Mbed TLS (TLS/HTTPS), NetSurf (HTML/CSS/DOM/layout), Duktape (JavaScript), SQLite, cJSON, miniz (ZIP), libjpeg-turbo + libsvgtiny/NanoSVG, and FFmpeg (bounded software media decoding).
- **Public interface:** Stable C ABI, UTF-8 strings, opaque handles, explicit memory ownership, and fixed resource budgets.
- **Usage model:** Each DLL can be consumed standalone by Win32/WinCE applications, or combined by `positron.exe` or a custom host into a constrained browser and resource-app experience.
- **Maturity:** Active compatibility layer and infrastructure stack with real device and emulator regression gates, not a full modern Web platform or general-purpose security browser.

Browser JavaScript is disabled by default. Standalone `positron_script.dll` and browser scripting share the same Duktape engine; the former is a general embedded scripting service, while the latter is a bounded Web binding coordinated by `positron_browser.dll` and the host.

## Public DLLs

| DLL | Purpose | Typical Entry Points |
|---|---|---|
| `positron_tls.dll` | Mbed TLS client, mutual-auth peer, and listener | `PTls_Init`, `PTls_ConnectVerified`, `PTls_ServerListen` |
| `positron_json.dll` | Bounded cJSON opaque-handle wrapper | `PJson_Parse`, accessors, `PJson_Free` |
| `positron_db.dll` | SQLite local database, versioned migrations, outbox, and REST row-level sync state | `PDb_OpenUtf8`, `PDb_ApplyMigration`, `PDb_SyncBuildRequest`, `PDb_SyncApplyResponse` |
| `positron_archive.dll` | Bounded miniz ZIP inspection, CRC verification, STORE/DEFLATE reading, and archive creation | `PArchive_Open/Read`, `PArchive_WriterCreate/Add/Finish` ([README](positron_archive/README.md)) |
| `positron_runtime.dll` | Pure-resource (`app://`) manifest metadata, dev-directory / isolated SQL, and read-only installed resource verification | `PRuntime_ManifestCreate`, `PRuntime_PrepareRevision/OpenInstalled` ([README](positron_runtime/README.md)) |
| `positron_media.dll` | Bounded FFmpeg software media decoding, device probing, and host-clock pumping | `pm_probe`, `pm_open`, `pm_pump`, `pm_close` |
| `positron_http.dll` | TLS-backed HTTP/1.1 GET/POST, streaming download preflight, and URL reference resolution | `PHttp_Init`, `PHttp_GetEx`, `PHttp_PostEx` |
| `positron_image.dll` | Bitmap and SVG decoding, drawing, and encoding (libjpeg-turbo, libsvgtiny/NanoSVG, WM Imaging) | `PImage_CreateBitmapFromMemory`, `PImage_DrawBitmap` |
| `positron_script.dll` | Budgeted standalone Duktape JavaScript context, JSON bridge, and module loading | `PScript_CreateEx`, `PScript_Evaluate`, `PScript_Destroy` |
| `positron_core.dll` | NetSurf-based HTML/CSS/DOM, styling, layout, GDI painting, forms, and interaction model | `PCore_Init`, `PCore_ParseHTML`, `PCore_LayoutDocument`, `PCore_DocumentWidth/Height`, paint API |
| `positron_browser.dll` | History/session, viewport snapshots, script bootstrap/scroll/resize, scroll restoration, dynamic `matchMedia()`/`visualViewport`, bounded layout geometry sync, DOM/Event bindings, navigation candidate lifecycle, and platform callback coordination | `PBrowser_HistoryCreate`, `PBrowser_ScriptSessionCreate`, `PBrowser_NavigationCandidateCreate`, `PBrowser_NavigationCommitGetInfo`, `PBrowser_NavigationCleanupGetInfo` |

Exact signatures, error codes, and ownership rules are defined in each component's public headers. See component READMEs for usage and [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for overall ownership and data flow.

## Quick Start

### Prerequisites

Build hosts require:

- Windows;
- Visual Studio 2008 SP1;
- Windows Mobile 6 Professional SDK;
- Python 3 (for build helper and audit scripts);
- An active WMDC session (established via GUI) when running real-device or emulator verification.

Third-party source dependencies required by `Positron.sln` are vendored in this repository; a normal clone does not need to download source packages at build time.

### Build

From the repository root:

```bat
scripts\build.bat
```

This runs an incremental `Debug` build by default. You can also specify a configuration or clean rebuild:

```bat
scripts\build.bat Release
scripts\build.bat Debug rebuild
```

Always build through the official solution scripts rather than invoking ARM compiler commands manually. See [docs/BUILDING.md](docs/BUILDING.md) for toolchain details and troubleshooting.

### Stage & Device Gate

`stage.bat` builds the chosen configuration and copies the matching EXE, DLLs, fonts, and configuration files into a runnable directory:

```bat
scripts\stage.bat Debug C:\WMShare\Positron
```

Run `test_host.exe` (or `positron.exe`) from the staged directory on the device. To run the automated device gate against an already-connected WMDC / Device Emulator session:

```bat
scripts\device_gate.bat -Candidate local-check
```

For INI modes, automated vs. manual verification, and nightly ZIP / CAB packaging, see [docs/TESTING.md](docs/TESTING.md), [docs/NIGHTLY_RELEASE.md](docs/NIGHTLY_RELEASE.md), and [docs/NIGHTLY_CAB_RELEASE.md](docs/NIGHTLY_CAB_RELEASE.md).

## Repository Layout

| Path | Contents |
|---|---|
| `positron_*` | Public DLLs, standalone `positron.exe` app, internal ported static libraries, and public headers |
| `positron_cab/` | VS2008 Smart Device CAB deployment project |
| `test_host/` | Regression harness, device fixtures, and example consumer |
| `samples/` | Standalone DLL consumer examples |
| `compat/` | WinCE / VS2008 missing CRT and C99 compatibility shims |
| `netsurf-all-3.11/` | Pinned NetSurf and supporting library sources |
| `third_party/` | Other pinned third-party sources, fonts, and port notes |
| `scripts/` | Build, C89 conversion, repository audit, device gate, and packaging scripts |
| `docs/` | Stable engineering documentation (currently maintained in Chinese) |
| `.agents/` | Current agent handoff, known limitations, roadmap, and failed-experiment index |
| `tmp/` | Local screenshots, logs, and diagnostics (untracked) |

## Documentation

Most in-depth engineering documents under `docs/` and `.agents/` are maintained in Chinese:

- [Documentation Index](docs/README.md): Entry points by task and role.
- [Architecture & Public Boundaries](docs/ARCHITECTURE.md): DLL responsibilities, data flow, ABI, and platform rules.
- [Capabilities Matrix](docs/CAPABILITIES.md): Trunk capability status and limits for all 11 public DLLs.
- [Building](docs/BUILDING.md): Official VS2008 / WM6 build and staging flow.
- [Testing & Acceptance](docs/TESTING.md): INI options, automated device gates, and manual checks.
- [Troubleshooting](docs/TROUBLESHOOTING.md): Build, deployment, WMDC/RAPI, networking, and input issues.
- [Third-Party Inventory](THIRD_PARTY.md): Pinned versions, upstream sources, licenses, and redistribution notes (English).
- [Current Development Handoff](.agents/HANDOFF.md): Dynamic engineering snapshot, not a long-term product specification.

## Compatibility & Security Boundaries

Several vendored dependencies are pinned to older versions for VS2008/C89 compatibility, including components past upstream end-of-life. Distributors must evaluate certificates, protocols, parsers, and upstream security advisories against their own threat model. Positron does not promise TLS 1.3, full modern Web APIs, complete CSS, or arbitrary website compatibility.

Verified TLS/HTTP paths should be used by default; insecure connection entry points exist only for explicit diagnostics or controlled environments. Browser JavaScript remains opt-in and does not constitute a full browser security sandbox.

## License

Original Positron code is licensed under the root [LICENSE](LICENSE). Vendored dependencies keep their own licenses; binaries compiled with NetSurf browser sources are subject to GPLv2 obligations. Read [THIRD_PARTY.md](THIRD_PARTY.md) and each dependency's original license before redistributing. This note is not legal advice.
