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
  has passed the ARMV4I device gate; it does not enable browser JavaScript.
- ABI minor 1.4 adds persistent primitive global setters,
  `PScript_GetGlobalJson`, and `PScript_CallGlobalJson`. These operations
  keep the opaque-handle boundary, use JSON only at the host call boundary,
  reject undefined/non-JSON values and results over the DLL's 255-byte result
  payload instead of truncating, and classify malformed arguments or missing
  functions as recoverable errors. TEST85-89 cover the bridge, persistence,
  error recovery, and limits; this still does not enable browser JavaScript.
- ABI minor 1.5 adds `PScript_RegisterGlobalJsonFunction`,
  `PScript_UnregisterGlobalJsonFunction`, and a native-function count query.
  A fixed 16-slot table exposes synchronous host callbacks as JavaScript
  globals; arguments and one return value cross the boundary as compact JSON.
  Callbacks must not re-enter or destroy the context, and the DLL rejects
  callback output at or above its 256-byte buffer instead of truncating.
  TEST90-94 cover basic calls, structured JSON, callback failure recovery,
  replacement/unregister, and the slot limit; this still does not enable
  browser JavaScript.
- ABI minor 1.6 adds `PScript_SetGlobalJson`. The host can inject a copied
  JSON object, array, string, number, boolean, or null without including
  Duktape headers; input uses the existing 64 KiB source limit and malformed
  or oversized input leaves the previous global untouched. TEST95-99 cover
  structured reads, mutation across public calls, malformed/null recovery,
  input-limit preservation, and type replacement; this still does not enable
  browser JavaScript.
- The WM6 build selects Duktape's no-DST Windows date provider because the
  Windows Mobile SDK does not provide `SystemTimeToTzSpecificLocalTime`.
- No DOM, `window`, network, fetch, or browser-core binding is added by this
  standalone layer. TEST80/81 cover evaluation and timeout/source limits;
  TEST82 covers the DLL heap limit and recovery path; TEST83 covers the
  module cache and lifecycle boundary; TEST84 covers the host source provider
  and on-demand dependency path; TEST85-89 cover persistent global values,
  JSON calls and their limits; TEST90-94 cover the synchronous native JSON
  callback bridge; TEST95-99 cover structured JSON global injection. next123
  ARMV4I build/staging passed; next124/125/126 ARMV4I Debug builds passed;
  TEST84/85-99 have since passed the selected ARMV4I device regression,
  including the next167 `screen=480x640 dpi=192` high-DPI Browse gate.
