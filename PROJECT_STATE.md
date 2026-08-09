# Project State

- Updated: 2026-08-09 (Asia/Shanghai)
- Branch: `main`, tracking `origin/main`
- Commit: `141cb18` (`修复高DPI交互重排并增加后退历史`), pushed to `origin/main`
- Working tree after push: tracked files are clean; only local `PROJECT_STATE.md` and local-only `tmp/` are untracked. `tmp/` must remain outside Git.
- Current milestone: next168 is the accepted automated device baseline for bounded successful-GET Browse history and Left-key back reload; its real interaction check is queued for a later accumulated manual batch.

## Project mission

Positron targets Windows Mobile 6 Professional / Windows CE 5.02 / ARMV4I. It is both a collection of reusable modern native DLLs for other WM applications and a browser/Electron-like runtime built from those DLLs, with NetSurf-derived HTML/CSS/DOM/layout, modern TLS/HTTP/JSON/image services, and an opt-in JavaScript runtime.

## Goal hierarchy

### Long-term

- Provide a reusable WM6 modern-infrastructure DLL ecosystem with stable C ABIs.
- Build a usable browser and Positron application runtime on top of those DLLs.
- Preserve source self-containment, license compliance, and compatibility with VS2008/WM6 ARMV4I.

### Mid-term

- Move from rendering a few fixtures to browsing a useful set of lightweight real pages.
- Expand the explicitly enabled browser JavaScript/DOM/event bridge incrementally without coupling `positron_script.dll` to the browser core.
- Close major capability gaps before spending large effort on visual polish or micro-optimisation.

### Short-term / current milestone

- Continue from next168 while accumulating a short list of changes with plausible visual/interaction regressions for one combined manual session.
- Do not expand the accepted history slice into page caching, forward UI, persistent history, redirects, or JavaScript History API without a new bounded milestone.

## Verified facts

### Repository and build graph

- `git status --short --branch` was `## main...origin/main` before this file was created. `HEAD` and `origin/main` both pointed to `fe8773c`.
- `Positron.sln` contains 15 VS2008 projects: six public DLL projects (`positron_tls`, `positron_json`, `positron_http`, `positron_core`, `positron_image`, `positron_script`), eight internal/test support projects, and `positron_image_demo`.
- `scripts/build.bat` invokes VS2008 `devenv.com` with `Debug|Windows Mobile 6 Professional SDK (ARMV4I)` by default. It does not invoke the desktop x86 compiler/linker directly.
- `scripts/stage.bat` performs an incremental ARMV4I build before copying six DLLs, `test_host.exe`, `test_host.ini`, bundled fonts, and font notices.
- `python scripts/audit_repo.py` was rerun during this freeze and passed: `15 projects`, `609 project inputs`, tracked mode; `28` documentation files and `28` local links; pinned Mbed TLS `2.16.12` and cJSON `1.7.18`.
- `THIRD_PARTY.md` states that all source dependencies needed by the solution are vendored. VS2008 SP1, the WM6 Professional SDK, emulator, and device images remain external and are not redistributable with this repository.

### Implemented product surfaces

- `positron_tls.dll` exposes initialisation, CA loading, verified TLS connections, read/write, close, and error APIs. `positron_json.dll` exposes opaque cJSON parsing/query/serialization APIs. `positron_http.dll` exposes HTTP/HTTPS GET/POST APIs. Phase documents record WM device verification, but next167 did not rerun TEST1-5.
- `positron_image.dll` has ABI 1.5 (`positron_image/positron_image.h`): retained bitmap/SVG objects, encoded-memory and raw-pixel bitmap creation, draw/info/free, PNG/JPEG/BMP/GIF encoding, and SVG creation telemetry. `positron_core` retains compatibility image forwarding.
- `positron_script.dll` has ABI 1.6 (`positron_script/positron_script.h`): persistent Duktape 2.7.0 contexts, execution/source/memory budgets, CommonJS-style modules/provider, JSON globals/calls, and synchronous JSON native callbacks. It remains independent of `positron_core.dll`.
- `PSCRIPT_MAX_NATIVE_FUNCTIONS` is intentionally 16. The current browser bootstrap in `test_host/main.c` registers 13 native functions; six form operations are multiplexed through `__pcoreFormProperty`.
- `positron_core.dll` exposes opaque C APIs for HTML/CSS/DOM, stylesheet/resource handling, NetSurf layout/redraw, device viewport/DPI, hit testing/navigation, forms, overflow, event dispatch, and the current DOM property bridge. The formal layout path uses NetSurf layout/redraw rather than the retired hand-written browser layout.
- Browser JavaScript is opt-in. Repository `test_host/test_host.ini` contains `javascript=0` and `auto=1`; classic script execution and the minimal DOM/event/form bridge are not enabled on the default Browse path.
- The current form bridge is implemented in `positron_core/pcore_select.c` and declared in `positron_core/positron_core.h`: `defaultValue`, `defaultChecked`, and `selectedIndex` read/write operations. TEST133-135 exercise the JavaScript-facing bridge and boundary behaviour.

### next167 device and manual evidence

- Evidence file: `C:\WMShare\Positron-next167\test_host.log`.
- Device metrics: `screen=480x640 dpi=192`.
- Configured tests: `13,20,27,43,44,56,58-77,80-135`, automated mode, browser JavaScript disabled by default.
- The log contains 83 `TEST ... OK` records, zero `[ERROR]` records, and one final `TESTBENCH PASS`. There are 82 configured test numbers because TEST13 emits separate overview and box-detail OK records.
- TEST13 completed all three network navigations: `example.com`, IANA Example Domains, and IANA Reserved Domains.
- TEST65, TEST76, TEST110, TEST123, and TEST133-135 all passed. TEST76 now includes a fixed 640x480/192 DPI interaction-restyle fixture whose main box remains `x=50,width=540` before and after active/clear relayout.
- TEST13 telemetry recorded example.com `3998/3856/42 ms`, IANA Example Domains `15573/14024/708 ms`, and Reserved Domains `10176/9798/136 ms` for total/network/max UI slice. These are observations, not thresholds.
- Targeted manual evidence: on 640x480/192 DPI, the user confirmed that clicking Learn More no longer collapses the outgoing example.com page to the left and its centered margins remain correct.
- Real input evidence: the user confirmed that clicking a real SIP candidate now enters the complete candidate, closing the reported TEST65 manual acceptance issue for this package. This does not claim compatibility with every OEM IME.
- Source and staged artifacts match:
  - `positron_core.dll` SHA-256: `80311BBA704CA0D18CCDB14570CCFF7C0117B5DC6C5CBC3B7C023383F7909E4A`
  - `test_host.exe` SHA-256: `0D45130FBEADD5A797DBD65A63D86EBE177A9E9C35619B77FAAC67DF26B904C1`
  - `test_host.ini` SHA-256: `9E65A976F5B208ABA42498B25A69F0704E35693420591DAD9C8938F88881544A`

### Important online-source record

| Date used/recorded | Source | Verified conclusion | Decision affected |
|---|---|---|---|
| 2026-08-09 | https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms906001%28v%3Dmsdn.10%29 | Windows CE exposes `ImmGetCompositionString` through the CE IME surface. | next161 uses WM `imm.h`/`coredll` APIs, not desktop `imm32.lib`. |
| 2026-08-09 | https://learn.microsoft.com/en-us/previous-versions/windows/embedded/ms921476%28v%3Dmsdn.10%29 | `WM_IME_COMPOSITION` is the WM/CE composition message path. | Native EDIT composition events feed the explicit browser script context. |
| 2026-08-09 | https://www.w3.org/TR/uievents/ and https://www.w3.org/TR/input-events-2/ | Composition, keyboard, and `beforeinput` metadata/order require distinct event semantics. | TEST123-125 and the size-tagged `isComposing` Ex ABI. |
| 2026-08-07 | https://github.com/kokke/tiny-regex-c | A small C regex engine can cover a conservative ASCII subset. | Vendored at commit `f2632c6...` for `pattern` validity; unsupported syntax is rejected by the adapter. |
| 2026-06-25 release | https://github.com/libexpat/libexpat/releases/tag/R_2_8_2 | Expat 2.8.2 provides the XML parser used below libdom/libsvgtiny. | Vendored and built with a constrained WM profile; public API remains C. |
| UNKNOWN retrieval date | https://github.com/memononen/nanosvg | NanoSVG supplies a mature CPU rasterizer. | libsvgtiny remains the SVG parser; NanoSVG is used only for anti-aliased rasterisation. |
| UNKNOWN retrieval date | https://github.com/libjpeg-turbo/libjpeg-turbo | The pinned compressor can emit explicit 4:4:4 JPEG and avoid WM Imaging's severe small-image chroma artefacts. | Vendored libjpeg-turbo 1.5.3 compressor subset behind `positron_image.dll`. |
| 2026-06-10 snapshot | https://curl.se/ca/cacert.pem | Mozilla CA trust data can be generated into a repository source header. | `positron_tls/ca_bundle.h` contains the pinned 121-certificate snapshot. |

## Decisions

- **Platform constraint:** Product and device builds target WM6 Professional / CE 5.02 / ARMV4I through VS2008. Do not use desktop x86 VC tools for validation.
- **Language constraint:** Production port code is C89 unless a narrowly isolated C++ adapter is required by a WM/upstream API. Run the C89 expert script first and inspect every non-zero rewrite.
- **Reuse before reimplementation:** Prefer vendored or newly researched license-compatible upstream implementations for parsers, protocol stacks, crypto, codecs, and JavaScript. Custom code should be limited to ABI wrappers, platform glue, and small missing behaviour.
- **Public ABI boundary:** Public DLLs use stable C ABIs, UTF-8, opaque handles, and matching DLL-owned release functions. Do not expose C++ ABI, NetSurf internals, Duktape types, COM ownership, or cross-CRT frees.
- **Architecture boundary:** `positron_core` remains transport-agnostic; the embedder supplies fetch and URL-resolution callbacks. `positron_script` remains independently consumable and does not automatically gain DOM/window/network.
- **JavaScript gate:** Browser JavaScript stays disabled by default (`javascript=0`). New bindings are exposed only in an explicitly created classic-script context.
- **Dynamic device metrics:** Resolution and DPI are runtime inputs. 96 DPI is only the CSS reference conversion and must not become a fixed device DPI or fixed resolution. Browse/rotation uses `PCore_SetDeviceViewport`.
- **Test policy:** Accumulate related features, run an automated device gate, then periodically perform manual visual/interaction checks. Automated assertions do not override visible layout regressions.
- **Manual batching:** Do not request manual interaction after every automated batch. Add plausible visual/interaction risks to a named checklist and run them together after several batches or immediately for crashes, data loss, severe layout corruption, or blocked core interaction.
- **Git cadence:** After an automated device baseline passes, commit and push its scoped tracked changes unless the user asks to batch Git work. Never add local screenshot evidence under `tmp/`.
- **Frozen risky directions:** The next38-43 navigation experiments and next115/116 float experiments must not be merged or resumed wholesale. Restart only as isolated, attributable changes with TEST13 deep navigation and rotation gates.
- **Font scope:** Bundled fallback is limited to symbols and monochrome emoji; ordinary multilingual font coverage is not a current requirement.
- **Resource-budget meaning:** The 2 MiB limit described in old discussions is a `test_host` pending-resource budget, not a product-wide device-memory limit.
- **Native callback limit:** Keep the 16-slot script callback bound unless a separate ABI/design decision is made; next166 solved pressure by multiplexing related operations rather than weakening TEST110 or increasing the limit.

## Current implementation status

- **DONE:** Source dependency vendoring and license inventory; repository audit is green.
- **DONE:** Phase 1-3 public TLS/JSON/HTTP surfaces are implemented and historically device-verified.
- **DONE:** NetSurf-based HTML/CSS/DOM layout/redraw path, GDI plotting, dynamic viewport/DPI, navigation transaction, image/SVG cache paths, common table/list/form/event subsets, and automated TEST13 deep navigation.
- **DONE:** Independent `positron_image.dll` and `positron_script.dll` public surfaces described above.
- **DONE:** Opt-in classic inline/external script order, page context, minimal DOM/event/form bridge through TEST135.
- **DONE:** next166 bridge-slot regression, including TEST110 and TEST133-135.
- **DONE:** next167 high-DPI interaction-restyle fix, TEST76 regression, 480x640/192 DPI automated gate, Learn More visual interaction, and real SIP candidate-input acceptance.
- **DONE (automated baseline):** next168 host-only URL history, Left-key back reload, and TEST136 are implemented; the 640x480/192 DPI device log has 84 OK, zero ERROR, and final PASS.
- **IN_PROGRESS:** A genuinely interactive modern browser. JavaScript, DOM/window APIs, resource loading, forms, CSS/layout coverage, and interaction remain partial.
- **IN_PROGRESS / BATCHED:** Manual validation across rotating resolution/DPI profiles. The current checklist includes next168 real Left-key back reload, failure-position retention, and page-state/layout observation.
- **DONE for the reported scenario:** Real SIP candidate selection enters the complete candidate on the accepted package. TEST123 remains a shared/synthetic probe and does not prove every OEM IME, preedit UI, or composition edge case.
- **TODO after next168:** Select the next bounded capability batch according to the existing “availability before polish” priority.
- **BLOCKED / SUSPENDED:** Float support. next115/116 caused TEST79 and real TEST13 regressions; no complete box-construction/normalisation design has replaced them.
- **BLOCKED:** No external blocker currently prevents ARMV4I build or automated test execution.

## Changed files

- **Uncommitted next167 product change:** `test_host/main.c` adds a device-aware style/layout helper and the TEST76 high-DPI repeated-restyle regression.
- **Uncommitted gate configuration:** `test_host/test_host.ini` uses `auto=1`, `javascript=0`, and tests `13,20,27,43,44,56,58-77,80-136` for the next168 candidate.
- **Uncommitted next168 addition:** the same files contain bounded successful-GET history, Left-key back reload, TEST136, and the automated range through TEST136.
- **Status reconciliation:** `README.md`, relevant `.agents/*.md`, and `positron_script/UPSTREAM.md` record the accepted next168 automated baseline; this `PROJECT_STATE.md` remains a local handoff file.
- **Local-only evidence:** `tmp/` is untracked by design and contains user screenshots; do not add, delete, or rewrite it.
- **HEAD `fe8773c` changed:** `test_host/main.c` plus `README.md` and five `.agents` status/debugging files. Its product change multiplexes six form-property operations into one native callback registration.
- **Previous related commit `dc8a4ab` changed:** `positron_core/pcore_select.c`, `positron_core/positron_core.h`, `test_host/main.c`, `test_host/test_host.ini`, and status documents for the form-default APIs.
- **Files to read first for this milestone:** `test_host/main.c`, `test_host/test_host.ini`, and accepted `C:\WMShare\Positron-next168\test_host.log`.

## Tests and verification

### Run and passed

- `git status --short --branch` before report creation: `## main...origin/main`.
- `python scripts/audit_repo.py` during freeze: repository audit OK (15 projects, 609 inputs); documentation audit OK (28 files/28 links).
- Current `vs2008-build.log`: `成功 0 个，失败 0 个，最新 15 个，跳过 0 个`, meaning all 15 projects were already current at the last incremental invocation.
- Pre-commit next167 verification recorded in the current session:
  - `python scripts/c89ize.py test_host/main.c`: 0 changes.
  - `python scripts/test_c89ize.py`: 4 tests OK.
  - `scripts\build.bat Debug build`: ARMV4I Debug build completed with 0 errors; only existing libcss fixed-point conversion warnings were reported.
  - `scripts\stage.bat Debug C:\WMShare\Positron-next167`: completed; key source/stage hashes match as listed above.
- Device `C:\WMShare\Positron-next167\test_host.log`: 83 OK records, no ERROR, final TESTBENCH PASS.
- Manual next167: Learn More preserves the centered outgoing-page margins; real SIP candidate selection enters the complete candidate.
- next168 pre-device checks: C89 rewrite 0 changes; C89 script tests 4 OK; repository/document audit passed; VS2008 ARMV4I Debug build succeeded with 0 errors and 3 existing libcss warnings; staging completed.
- next168 source/stage hashes match: `test_host.exe` `7FDB95BCC62A50E6C265A78AB718E11E581D4EDBCFB687275D3561BE3E8ED42F`, `test_host.ini` `221F9F2DE20A22F347373CD1EF6BF1CB24C64AD20146F7162AB3CB24ADCEE7AD`, unchanged core DLL `80311BBA704CA0D18CCDB14570CCFF7C0117B5DC6C5CBC3B7C023383F7909E4A`.
- Device `C:\WMShare\Positron-next168\test_host.log`: `screen=640x480 dpi=192`; TEST13 completed all three navigations; TEST136 OK; 84 OK records, zero ERROR, final TESTBENCH PASS.

### Not run or not proven by next168

- Release configuration was not rerun for HEAD `fe8773c` during this freeze.
- TEST1-12, 14-19, 21-26, 28-42, 45-55, 57, and 78-79 were not selected by next168's automated configuration. Some have older device evidence, but the next168 log does not re-prove them.
- next168 has no separate manual back-navigation session; that interaction is explicitly queued for the accumulated manual batch.
- The accepted SIP result does not prove every hardware/OEM IME, all preedit/candidate-window visuals, selection replacement, or every composition event ordering edge case.
- Automated TEST13 proves the configured navigation/control flow and assertions, not all visual layout, touch behaviour, rotation behaviour, or public-network reliability.

## Open questions and risks

### Facts

- Current status documents have been reconciled to the next168 automated baseline; older chronological entries may still describe what was pending at their historical point.
- `positron_script/UPSTREAM.md` now records TEST84/85-99 as device-verified.
- Historical `PHASE*.md` files and much of the chronological roadmap contain “current” wording that was true only when written.
- The current browser can still spend a substantial uninterrupted UI-thread slice; next168 observed a 70 ms maximum slice in this run, but prior accepted runs were slower and no performance threshold is claimed.
- Browser JavaScript remains deliberately partial and default-off. Passing TEST110-135 is not evidence of full DOM, `window`, fetch, timers, promises, modules, async/defer, CSP, or web compatibility.
- Known layout/rendering gaps include suspended floats, single-column Grid fallback rather than track layout, incomplete advanced SVG/background/table cases, and partial form/event semantics.

### Assumptions / unknowns

- **UNKNOWN / MANUAL CHECKLIST:** Whether next168 Left-key back preserves expected page state/layout across network delay and rotation.
- **VERIFIED FOR THIS PACKAGE/SCENARIO:** Real SIP candidate selection enters the full candidate. **UNKNOWN:** whether every OEM IME and all composition/preedit edge cases behave identically.
- **UNKNOWN:** Whether all historical Communication and rendering groups still pass with next168, because the current config intentionally runs a selected regression set.

### User decisions still needed

- next168 automatic acceptance is closed. Manual checks are intentionally batched; no user action is needed until the accumulated checklist is scheduled.
- Full browser JavaScript compatibility is not an approved immediate target; only incremental, explicitly gated vertical slices are established.

### Discussion-only or obsolete statements

- “next164 is the current baseline”, “next166 awaits device validation”, and “next167 awaits SIP acceptance” are obsolete.
- next165's native-slot failure is historical and superseded by next166; do not treat it as a current blocker.
- next37 is a frozen navigation recovery reference, not the current repository version.
- Fixed 96 DPI, fixed resolution, and a product-wide 2 MiB device limit were rejected or corrected; do not reintroduce them.
- TEST23/78/79 float claims and next115/116 float implementations were withdrawn.
- Early statements that JavaScript might be deferred are superseded only by the independently consumable, opt-in implementation now present. They do not authorize enabling browser JavaScript by default.
- `.agents/CLAUDE_LAST_CONTEXT.md` and the phase documents are historical context, not a current task list.

## Next safe step

**Select one bounded next capability while preserving the accumulated manual checklist.**

- **Result:** one falsifiable capability slice with an automated regression, unchanged default JavaScript gate, and no dependency on suspended float work.
- **Completion standard:** C89/audit/ARMV4I build and automated device gate pass; then commit and push the scoped tracked changes.
- **Manual backlog:** next168 real Left-key back reload, reload-failure history retention, and layout/page-state observation; add later risks rather than scheduling a run immediately.
- **Do not:** add `tmp/`, enable JavaScript by default, or broaden the batch into unrelated visual polish.

## Instructions for the next session

1. Read this file first, then `.agents/ARCHITECTURE.md`, `.agents/FAILED_EXPERIMENTS.md`, `.agents/KNOWN_LIMITATIONS.md`, and the top/current sections of `.agents/HANDOFF.md` and `.agents/ROADMAP.md`.
2. Run `git status --short --branch` and `git rev-parse --short HEAD`. Expect `main` at pushed `141cb18` with only local `PROJECT_STATE.md` and `tmp/` untracked; do not overwrite or track `tmp/`.
3. Read `C:\WMShare\Positron-next168\test_host.log` and confirm its final `TESTBENCH PASS` before relying on next168.
4. Run `python scripts/audit_repo.py` before dependency/project changes.
5. Use only `scripts\build.bat` / `scripts\stage.bat` for VS2008 WM6 ARMV4I builds. Never validate with desktop x86 `cl/link/dumpbin`.
6. Run `scripts/test_c89ize.py` before port rewrites; run `c89ize.py` on touched C files first, and inspect any non-zero changes before manual edits.
7. Treat the top/current sections of README and `.agents` documents plus this report as authoritative; old chronological entries describe their historical point, not a current pending gate.
8. Do not infer visual correctness from automated assertions, full browser compatibility from TEST110-135, or real IME behaviour from TEST123.
9. Do not merge `codex/post-next37-experiments` wholesale or restore withdrawn float/scrollbar experiments.
10. Continue automated batches while maintaining the accumulated manual checklist; do not ask for a manual run after every batch.
