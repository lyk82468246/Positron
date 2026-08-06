# positron_script upstream notes

## Duktape

- Source: Duktape 2.7.0, as bundled by NetSurf 3.11.
- Repository path: `netsurf-all-3.11/netsurf/content/handlers/javascript/duktape/`.
- The single-file distributable keeps its upstream MIT license and copyright
  notice in `duktape.c` and `duktape.h`.
- `duk_custom.h` is NetSurf's GPLv2 configuration layer. It supplies the
  NetSurf build configuration and the WM6 interrupt callback declaration, so
  the combined `positron_script.dll` build is subject to the applicable GPLv2
  distribution obligations; see `THIRD_PARTY.md` and the NetSurf COPYING file.

## Positron changes

- `positron_script.c` wraps the engine behind a small opaque C ABI and keeps
  contexts, allocations, result strings and errors inside the DLL.
- The ABI minor version adds `PScript_CreateEx` with a hard Duktape heap
  limit, plus current/peak allocation telemetry and a dedicated memory-limit
  error. The legacy `PScript_Create` entry selects the 512 KiB default.
- ABI minor 1.2 adds a small CommonJS-style module boundary:
  `PScript_EvaluateModule` executes a named source once, `require()` reads
  already loaded exports, failed loads are removed, and
  `PScript_ClearModules` drops the module registry. This is host-supplied
  source only; it does not add URL/file loading or browser bindings.
- ABI minor 1.3 adds `PScript_SetModuleSourceProvider` and
  `PScript_LoadModule`: a synchronous WM host callback can provide root and
  dependency source on demand. The DLL releases each returned buffer after
  evaluation, caches successful exports, and removes failed entries. TEST84
  is the pending device gate for this provider path; it does not enable
  browser JavaScript.
- The WM6 build selects Duktape's no-DST Windows date provider because the
  Windows Mobile SDK does not provide `SystemTimeToTzSpecificLocalTime`.
- No DOM, `window`, network, fetch, or browser-core binding is added by this
  standalone layer. TEST80/81 cover evaluation and timeout/source limits;
  TEST82 covers the DLL heap limit and recovery path; TEST83 covers the
  module cache and lifecycle boundary; TEST84 covers the host source provider
  and on-demand dependency path. next122 build/staging passed; device
  verification of TEST84 is pending.
