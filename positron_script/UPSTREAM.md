# `positron_script` upstream and port notes

## Upstream

- Engine: Duktape 2.7.0, from the NetSurf 3.11 source snapshot.
- Source path:
  `netsurf-all-3.11/netsurf/content/handlers/javascript/duktape/`.
- `duktape.c` and `duktape.h` retain the upstream MIT license and copyright.
- `duk_custom.h` comes from NetSurf's GPLv2 JavaScript integration layer and
  supplies the build configuration used by this DLL.

Because the build combines these files, redistribution must follow the
applicable notices described in [`../THIRD_PARTY.md`](../THIRD_PARTY.md) and
the NetSurf `COPYING` file.

## Positron boundary

`positron_script.c` wraps Duktape behind an opaque C ABI. Callers do not include
Duktape headers and do not free Duktape or DLL-owned memory directly.

The current public ABI provides:

- isolated persistent contexts with execution and heap budgets;
- UTF-8 evaluation, result and error retrieval;
- CommonJS-style named modules and synchronous host source providers;
- persistent primitive and JSON-compatible globals;
- JSON-based global function calls;
- a fixed table of synchronous native JSON callbacks;
- current/peak memory and resource-count diagnostics.

The exact ABI version, limits, ownership and callback restrictions are defined
in [`positron_script.h`](positron_script.h). In particular, a context is not
safe for concurrent calls and a host callback must not re-enter or destroy the
context executing it.

The WM6 port selects Duktape's no-DST Windows date provider because the Windows
Mobile SDK does not provide `SystemTimeToTzSpecificLocalTime`.

## Browser relationship

This DLL is a standalone JavaScript service. It does not create a window,
fetch resources or own DOM objects.

The experimental browser binding lives in the consumer layer: the host
combines `positron_script.dll` with `positron_core.dll`, networking and WM
input controls behind an explicit `javascript=1` switch. That binding uses the
same Duktape engine; it is not a second JavaScript implementation.

See [`../docs/ARCHITECTURE.md`](../docs/ARCHITECTURE.md) for the stable
architecture and [`.agents/KNOWN_LIMITATIONS.md`](../.agents/KNOWN_LIMITATIONS.md)
for current browser-binding limits.

## Updating

An upstream update must:

1. record the new official version and source;
2. retain all upstream notices;
3. review `duk_custom.h` and the WM6 date/interrupt configuration;
4. preserve the public C ABI or make an explicit ABI-major decision;
5. pass C89 checks, the formal ARMV4I build and script/device regressions.

Do not replace the vendored engine only to gain a browser API; browser objects
belong to the host integration boundary.
